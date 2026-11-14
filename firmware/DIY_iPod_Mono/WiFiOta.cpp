#include "WiFiOta.h"
#if FEATURE_WIFI_SYNC
#include "IpodIdentity.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <stdlib.h>
#include <string.h>
WiFiOtaService wifiOta;
namespace {
const char* boardName(){
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  return "esp32s3";
#else
  return "esp32";
#endif
}
// Use the IDF image's declared chip ID, not the first-byte magic alone.
bool correctChip(const uint8_t* bytes,size_t len){
  if(len<sizeof(esp_image_header_t))return false;
  esp_image_header_t header;
  memcpy(&header,bytes,sizeof(header));
#if MONOPOD_BOARD == MONOPOD_BOARD_ESP32_S3
  return header.magic==ESP_IMAGE_HEADER_MAGIC && header.chip_id==ESP_CHIP_ID_ESP32S3;
#else
  return header.magic==ESP_IMAGE_HEADER_MAGIC && header.chip_id==ESP_CHIP_ID_ESP32;
#endif
}
bool decimal32(const String& s,uint32_t& val){
  if(s.isEmpty()||s.length()>10)return false;
  uint64_t n=0;
  for(size_t i=0;i<s.length();i++){
    if(s[i]<'0'||s[i]>'9')return false;
    n=n*10u+(s[i]-'0');
    if(n>0xffffffffULL)return false;
  }
  val=(uint32_t)n;
  return true;
}
bool validSha(const String& digest){
  if(digest.length()!=64)return false;
  for(size_t i=0;i<digest.length();i++){
    char c=digest[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
  }
  return true;
}
String hexSha(const uint8_t bytes[32]){
  char out[65];
  static const char hex[]="0123456789abcdef";
  for(size_t i=0;i<32;i++){out[i*2]=hex[bytes[i]>>4];out[i*2+1]=hex[bytes[i]&15];}
  out[64]=0;return String(out);
}
}
bool WiFiOtaService::supported() const {
  const esp_partition_t* partition=esp_ota_get_next_update_partition(nullptr);
  return partition && partition->type==ESP_PARTITION_TYPE_APP &&
    esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,nullptr) &&
    esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,nullptr) &&
    esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,nullptr) &&
    partition!=esp_ota_get_running_partition();
}
uint32_t WiFiOtaService::maxImageBytes()const{
  const esp_partition_t* p=esp_ota_get_next_update_partition(nullptr);
  return p?p->size:0;
}
void WiFiOtaService::newSession(){
  abort();
  lastError="";badPinAttempts=0;
  // The code is intentionally shown ONLY on the physical screen, never via HTTP.
  pinCode=String(100000u+(esp_random()%900000u));
}
void WiFiOtaService::stopSession(){abort();pinCode="";}
String WiFiOtaService::info()const{
  return String("{\"protocol\":1,\"firmware\":\"0.10.6.8\",\"board\":\"")+boardName()+
    "\",\"device_id\":\""+IpodIdentity::guid()+
    "\",\"supported\":"+(supported()?"true":"false")+
    ",\"max_image_bytes\":"+String(maxImageBytes())+
    ",\"pin_required\":true,\"pin_location\":\"device_screen\",\"upload_active\":"+
    (uploading?"true":"false")+",\"progress_percent\":"+String(percent())+
    ",\"reboot_pending\":"+(rebootQueued?"true":"false")+"}";
}
uint8_t WiFiOtaService::percent() const{
  if(!expectedSize)return 0;
  return (uint8_t)min(100u,(unsigned)((uint64_t)received*100ULL/expectedSize));
}
String WiFiOtaService::displayLine()const{
  if(rebootQueued)return "OTA OK - REBOOTING";
  if(uploading)return String("OTA UPDATE ")+String(percent())+"%";
  if(!supported())return "OTA: NO DUAL SLOT";
  if(pinCode.length()==6)return "OTA PIN: "+pinCode;
  return "Wi-Fi sync active";
}
void WiFiOtaService::fail(const char* reason){
  if(!lastError.length())lastError=reason;
  if(uploading)Update.abort();
  uploading=false;expectedSize=0;received=0;
  if(shaInitialized){mbedtls_md_free(&shaContext);shaInitialized=false;}
  headerBytes=0;headerValidated=false;
}
void WiFiOtaService::abort(const char* reason){
  if(uploading)Update.abort();
  uploading=false;expectedSize=0;received=0;
  if(shaInitialized){mbedtls_md_free(&shaContext);shaInitialized=false;}
  headerBytes=0;headerValidated=false;
  if(reason && *reason)lastError=reason;
}
bool WiFiOtaService::start(const String& pin,const String& size,const String& digest,bool transferBusy){
  lastError="";
  if(rebootQueued){lastError="REBOOT_PENDING";return false;}
  if(uploading||transferBusy){lastError="TRANSFER_ACTIVE";return false;}
  if(!supported()){lastError="NO_OTA_PARTITION";return false;}
  if(badPinAttempts>=5){lastError="PIN_LOCKED_RESTART_SYNC";return false;}
  // Constant-time comparison over 6 positions; never send/log the secret.
  uint8_t diff=(uint8_t)(pin.length()!=6 || pinCode.length()!=6);
  for(size_t i=0;i<6;i++){
    diff |= (uint8_t)((i<pin.length()?pin[i]:0) ^ (i<pinCode.length()?pinCode[i]:0));
  }
  if(diff){++badPinAttempts;lastError="INVALID_PIN";return false;}
  uint32_t bytes=0;
  if(!decimal32(size,bytes)||bytes<1024||bytes>maxImageBytes()){
    lastError="INVALID_IMAGE_SIZE";return false;
  }
  if(!validSha(digest)){lastError="INVALID_SHA256";return false;}
  expectedSize=bytes;received=0;headerBytes=0;headerValidated=false;expectedSha=digest;expectedSha.toLowerCase();
  mbedtls_md_init(&shaContext);
  const mbedtls_md_info_t* md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if(!md || mbedtls_md_setup(&shaContext,md,0)!=0 || mbedtls_md_starts(&shaContext)!=0){
    shaInitialized=true;fail("HASH_INIT_FAILED");return false;
  }
  shaInitialized=true;
  if(!Update.begin(expectedSize,U_FLASH)){
    fail("FLASH_BEGIN_FAILED");return false;
  }
  uploading=true;lastChunkAt=millis();return true;
}
bool WiFiOtaService::write(const uint8_t* data,size_t length){
  if(!uploading)return false;
  if(!length)return true;
  lastChunkAt=millis();
  if(length>expectedSize-received-headerBytes){fail("IMAGE_TOO_LARGE");return false;}
  // Buffer the complete 24-byte ESP application header BEFORE writing to flash.
  // The network stack may split the header across multiple HTTPUpload blocks.
  if(!headerValidated){
    const size_t need=sizeof(imageHeader)-headerBytes;
    const size_t n=min(length,need);
    memcpy(imageHeader+headerBytes,data,n);
    headerBytes+=n;data+=n;length-=n;
    if(headerBytes<sizeof(imageHeader))return true;
    if(!correctChip(imageHeader,sizeof(imageHeader))){
      fail("WRONG_CHIP_OR_NOT_APP_IMAGE");return false;
    }
    headerValidated=true;
    if(mbedtls_md_update(&shaContext,imageHeader,headerBytes)!=0 ||
       Update.write(imageHeader,headerBytes)!=headerBytes){
      fail("FLASH_WRITE_FAILED");return false;
    }
    received+=(uint32_t)headerBytes;
    headerBytes=0;
  }
  if(length){
    if(mbedtls_md_update(&shaContext,data,length)!=0){fail("HASH_FAILED");return false;}
    if(Update.write((uint8_t*)data,length)!=length){fail("FLASH_WRITE_FAILED");return false;}
    received+=(uint32_t)length;
  }
  return true;
}
bool WiFiOtaService::finish(){
  if(!uploading)return false;
  if(!headerValidated||received!=expectedSize){fail("INCOMPLETE_IMAGE");return false;}
  uint8_t actual[32];
  if(mbedtls_md_finish(&shaContext,actual)!=0){fail("HASH_FAILED");return false;}
  mbedtls_md_free(&shaContext);shaInitialized=false;
  if(hexSha(actual)!=expectedSha){fail("SHA256_MISMATCH");return false;}
  if(!Update.end(false)){fail("IMAGE_VALIDATION_FAILED");return false;}
  uploading=false;
  // Wait for HTTP response to be flushed before restarting. Do not exit Sync.
  rebootQueued=true;restartAt=millis()+1800;
  pinCode="";lastError="";
  return true;
}
void WiFiOtaService::loop(){
  if(uploading && (uint32_t)(millis()-lastChunkAt)>20000u)abort("TRANSFER_TIMEOUT");
  if(rebootQueued && (int32_t)(millis()-restartAt)>=0){
    ESP.restart();
  }
}
#endif
