// MonoPod ESP32-S3 N16R8 diagnostic, standalone from production firmware.
// Arduino-ESP32 3.3.x; dependencies Adafruit GFX + Adafruit ST7735 and ST7789 Library.
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <esp_heap_caps.h>
#include <driver/i2s_std.h>
#include <math.h>
#include "YModemRx.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error This standalone diagnostic targets ESP32-S3 only.
#endif

namespace Pins {
constexpr int SCK=12, MOSI=11, MISO=13, TFT_CS=10, TFT_DC=39, TFT_RST=47;
constexpr int SD_CS=14, I2S_BCLK=16, I2S_LRCK=17, I2S_DATA=18, PIEZO=42;
constexpr int TFT_BL=15;
}
constexpr uint32_t UART_BAUD=115200; // default safe value for USB-UART bridges
constexpr uint32_t FRAME_INTERVAL_MS=45;
// Do NOT power the 30mA backlight directly from GPIO15. Use external key if installed.
constexpr bool HAS_P_CHANNEL_BACKLIGHT_KEY=false;

Adafruit_ST7735 tft(Pins::TFT_CS,Pins::TFT_DC,Pins::TFT_RST);
GFXcanvas16* framebuffer=nullptr;
i2s_chan_handle_t i2sTx=nullptr;
volatile bool playing=false;
bool uploadReady=false, uploadFailed=false;
uint32_t lastFrame=0, frames=0, fpsWindow=0;
float fps=0, angle=0;
uint8_t* pcm=nullptr;
uint32_t pcmSize=0, pcmWritten=0;
uint32_t audioRate=16000;
uint8_t audioChannels=1;
char cmdline[88];size_t cmdlen=0;
uint32_t lastPct=0;

static bool receiveBegin(void*,const char* name,uint32_t length){
  // Header has explicit size, so allocate in PSRAM rather than internal RAM.
  const size_t freeBig=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  const size_t safety=128*1024;
  size_t len=strlen(name);
  if(len<4 || strcmp(name+len-4,".pcm")!=0 || length>safety+(8*1024*1024))return false;
  if(length==0 || length> (freeBig>safety?freeBig-safety:0) || length % (2*audioChannels))return false;
  pcm=(uint8_t*)heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!pcm)return false;
  pcmSize=length;pcmWritten=0;
  return true;
}
static bool receiveWrite(void*,const uint8_t* data,size_t n){
  if(!pcm || pcmWritten+n>pcmSize)return false;
  memcpy(pcm+pcmWritten,data,n);pcmWritten+=n;
  return true;
}
static void receiveDone(void*){ uploadReady=true; }
static void receiveAbort(void*){
  if(pcm){heap_caps_free(pcm);pcm=nullptr;}
  pcmSize=pcmWritten=0;uploadFailed=true;
}
YModem::Receiver receiver({nullptr,receiveBegin,receiveWrite,receiveDone,receiveAbort});
void logYModemStats(const char* label) {
  const YModem::Stats s=receiver.stats();
  Serial0.printf("[YMODEM] %s OK=%lu CRC=%lu HEADER=%lu SEQ=%lu TIMEOUT=%lu\r\n",
     label,(unsigned long)s.packetsOk,(unsigned long)s.crcErrors,
     (unsigned long)s.frameErrors,(unsigned long)s.seqErrors,(unsigned long)s.timeouts);
}




void beep896(){
  pinMode(Pins::PIEZO,OUTPUT);
  if(ledcAttach(Pins::PIEZO,896,10)){
    ledcWriteTone(Pins::PIEZO,896);
    delay(110);                       // one short beep, exactly once at boot
    ledcWriteTone(Pins::PIEZO,0);
    ledcDetach(Pins::PIEZO);
  }
  digitalWrite(Pins::PIEZO,LOW);
}
void setupI2S(){
  i2s_chan_config_t channels=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,I2S_ROLE_MASTER);
  esp_err_t e=i2s_new_channel(&channels,&i2sTx,nullptr);
  if(e!=ESP_OK){Serial0.printf("I2S create failed %d\r\n",(int)e);i2sTx=nullptr;return;}
  i2s_std_config_t conf={
    .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
    .gpio_cfg={
      .mclk=I2S_GPIO_UNUSED,
      .bclk=(gpio_num_t)Pins::I2S_BCLK,
      .ws=(gpio_num_t)Pins::I2S_LRCK,
      .dout=(gpio_num_t)Pins::I2S_DATA,
      .din=I2S_GPIO_UNUSED,
      .invert_flags={.mclk_inv=false,.bclk_inv=false,.ws_inv=false}
    }
  };
  e=i2s_channel_init_std_mode(i2sTx,&conf);
  if(e!=ESP_OK){Serial0.printf("I2S init failed %d\r\n",(int)e);i2s_del_channel(i2sTx);i2sTx=nullptr;return;}
  Serial0.println("I2S ready: GPIO16 BCK / GPIO17 LRCK / GPIO18 DATA, 16-bit stereo TX");
}

void audioWorker(void*){
  esp_err_t e=ESP_FAIL;
  uint32_t totalFrames=0;
  if(i2sTx && pcm && pcmSize){
    i2s_std_clk_config_t clk=I2S_STD_CLK_DEFAULT_CONFIG(audioRate);
    e=i2s_channel_reconfig_std_clock(i2sTx,&clk);
    if(e==ESP_OK)e=i2s_channel_enable(i2sTx);
    if(e==ESP_OK){
      const size_t sourceFrameBytes=2*audioChannels;
      const size_t count=pcmSize/sourceFrameBytes;
      int16_t stereo[512*2];
      for(size_t at=0;at<count;){
        size_t n=count-at;if(n>512)n=512;
        for(size_t f=0;f<n;f++){
          const size_t pos=(at+f)*sourceFrameBytes;
          int16_t left=(int16_t)((uint16_t)pcm[pos] | (uint16_t)pcm[pos+1]<<8);
          int16_t right=left;
          if(audioChannels==2)right=(int16_t)((uint16_t)pcm[pos+2] | (uint16_t)pcm[pos+3]<<8);
          stereo[f*2]=left/2;stereo[f*2+1]=right/2; // fixed -6dB for safe diagnostics
        }
        size_t written=0;
        esp_err_t status=i2s_channel_write(i2sTx,stereo,n*4,&written,pdMS_TO_TICKS(1000));
        if(status!=ESP_OK || written!=n*4){e=status==ESP_OK?ESP_FAIL:status;break;}
        at+=n;totalFrames+=(uint32_t)n;
      }
      // Explicit zero tail, then drain DMA and disable I2S TX. No repeat, no mixer.
      memset(stereo,0,sizeof(stereo));
      size_t ignored=0;
      i2s_channel_write(i2sTx,stereo,sizeof(stereo),&ignored,pdMS_TO_TICKS(1000));
      vTaskDelay(pdMS_TO_TICKS(250));
      i2s_channel_disable(i2sTx);
    }
  }
  if(pcm){heap_caps_free(pcm);pcm=nullptr;}
  pcmSize=pcmWritten=0;
  Serial0.printf("AUDIO_DONE frames=%lu error=%d (played once)\r\n",(unsigned long)totalFrames,(int)e);
  playing=false;
  vTaskDelete(nullptr);
}

void startPlayback(){
  if(!i2sTx || !pcm || pcmWritten!=pcmSize){
    Serial0.println("ERR AUDIO_UNAVAILABLE");
    if(pcm){heap_caps_free(pcm);pcm=nullptr;}pcmSize=0;return;
  }
  playing=true;
  Serial0.printf("AUDIO_START bytes=%lu rate=%lu ch=%u PSRAM\r\n",
      (unsigned long)pcmSize,(unsigned long)audioRate,audioChannels);
  if(xTaskCreatePinnedToCore(audioWorker,"pcm_once",7168,nullptr,4,nullptr,0)!=pdPASS){
    playing=false;Serial0.println("ERR AUDIO_TASK");
    if(pcm){heap_caps_free(pcm);pcm=nullptr;}pcmSize=0;
  }
}

void drawReceive(){
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_RED,ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10,12);tft.print("RECEIVING");
  tft.setCursor(11,12);tft.print("RECEIVING"); // visually bolder bitmap text
  tft.setCursor(29,38);tft.print("CONTENT");
  tft.setCursor(30,38);tft.print("CONTENT");
  tft.setTextSize(1);
  tft.setCursor(39,70);tft.print("UART YMODEM");
  tft.setCursor(18,95);tft.print("DO NOT DISCONNECT");
  tft.drawRect(8,112,144,8,ST77XX_RED);
  lastPct=0;
}
void progressReceive(){
  if(!receiver.fileSize())return;
  uint32_t pct=(uint32_t)((uint64_t)receiver.bytes()*100/receiver.fileSize());
  if(pct==lastPct)return;
  lastPct=pct;
  tft.fillRect(9,113,142,6,ST77XX_BLACK);
  tft.fillRect(9,113,(142*pct)/100,6,ST77XX_RED);
}
void cubeFrame(){
  if(!framebuffer)return;
  framebuffer->fillScreen(ST77XX_BLACK);
  framebuffer->setTextColor(ST77XX_WHITE);framebuffer->setTextSize(1);
  framebuffer->setCursor(3,3);framebuffer->print("MONOPOD S3 DEVTEST");
  framebuffer->setCursor(3,14);framebuffer->printf("FPS: %.1f",fps);
  framebuffer->setCursor(3,115);
  framebuffer->print("I2S / YMODEM TEST");
  if(playing){framebuffer->setCursor(102,14);framebuffer->print("PLAY");}
  float cx=cosf(angle),sx=sinf(angle),cy=cosf(angle*.71f),sy=sinf(angle*.71f);
  int16_t px[8],py[8];float depth[8];
  for(int i=0;i<8;i++){
    float x=(i&1)?1.f:-1.f, y=(i&2)?1.f:-1.f, z=(i&4)?1.f:-1.f;
    float x1=x*cy+z*sy,z1=-x*sy+z*cy;
    float y2=y*cx-z1*sx,z2=y*sx+z1*cx;
    float perspective=46.f/(z2+3.5f);
    px[i]=(int16_t)(80 + x1*perspective);
    py[i]=(int16_t)(69 + y2*perspective);
    depth[i]=z2;
  }
  for(int i=0;i<8;i++)for(int axis=0;axis<3;axis++){
    int other=i^(1<<axis);
    if(other<=i)continue;
    uint16_t c=(depth[i]+depth[other]>0)?ST77XX_CYAN:0x39E7;
    framebuffer->drawLine(px[i],py[i],px[other],py[other],c);
  }
  tft.drawRGBBitmap(0,0,framebuffer->getBuffer(),160,128);
  frames++;
  uint32_t now=millis();
  if(now-fpsWindow>=1000){fps=1000.0f*frames/(now-fpsWindow);frames=0;fpsWindow=now;}
  angle+=0.045f;
}
void command(const char* s){
  if(strcmp(s,"HELP")==0){Serial0.println("HELP | INFO | UPLOAD=<sample_rate>,<channels> (YMODEM CRC16, .pcm)");return;}
  if(strcmp(s,"INFO")==0){
    size_t big=heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    size_t limit=big>128*1024?big-128*1024:0;
    Serial0.printf("INFO PSRAM_MAX=%lu AUDIO=%s UART=%lu\r\n",
      (unsigned long)limit,playing?"PLAYING":"IDLE",(unsigned long)UART_BAUD);
    return;
  }
  unsigned long rate;unsigned channels;
  if(sscanf(s,"UPLOAD=%lu,%u",&rate,&channels)==2){
    if(playing || receiver.active() || !i2sTx){Serial0.println("ERR BUSY_OR_I2S");return;}
    if(rate<8000 || rate>48000 || (channels!=1 && channels!=2)){
      Serial0.println("ERR PCM_FORMAT (use 8000..48000 Hz and 1 or 2 channels)");return;
    }
    audioRate=(uint32_t)rate; audioChannels=(uint8_t)channels;
    uploadReady=uploadFailed=false;
    if(pcm){heap_caps_free(pcm);pcm=nullptr;pcmSize=pcmWritten=0;}
    drawReceive();
    Serial0.println("READY YMODEM (send one .pcm file)");
    receiver.start(millis());
    auto a=receiver.greeting();Serial0.write(a.response,a.length);
    return;
  }
  Serial0.println("ERR UNKNOWN_COMMAND; type HELP");
}
void processUART(){
  while(Serial0.available()){
    uint8_t b=(uint8_t)Serial0.read();
    if(receiver.active()){
      auto a=receiver.input(b,millis());
      if(a.length)Serial0.write(a.response,a.length);
      if(a.progress)progressReceive();
      if(a.error)uploadFailed=true;
      continue;
    }
    if(b=='\r'||b=='\n'){
      if(cmdlen){cmdline[cmdlen]=0;command(cmdline);cmdlen=0;}
    }else if(b>=32 && b<=126){
      if(cmdlen<sizeof(cmdline)-1)cmdline[cmdlen++]=(char)b;
      else cmdlen=0;
    }
  }
  if(receiver.active()){
    auto a=receiver.tick(millis());
    if(a.length)Serial0.write(a.response,a.length);
    if(a.error)uploadFailed=true;
  }
}
void setup(){
  // Arduino-ESP32 uses a small RX ring by default (typically 256 B).
  // A 1K YMODEM frame can overflow it while the sketch is busy.
  Serial0.setRxBufferSize(8192); // must be called BEFORE begin()
  Serial0.begin(UART_BAUD);
  delay(300);
  Serial0.println("\r\n=== MonoPod S3 standalone I2S / YMODEM dev test v1.2 ===");
  Serial0.printf("Chip=%s Flash=%lu PSRAM=%lu\r\n",ESP.getChipModel(),
    (unsigned long)ESP.getFlashChipSize(),(unsigned long)ESP.getPsramSize());
  beep896();
  Serial0.println("PIEZO startup: 896 Hz x 110 ms (once)");
  pinMode(Pins::TFT_CS,OUTPUT);digitalWrite(Pins::TFT_CS,HIGH);
  pinMode(Pins::SD_CS,OUTPUT);digitalWrite(Pins::SD_CS,HIGH);
  SPI.begin(Pins::SCK,Pins::MISO,Pins::MOSI,Pins::SD_CS);
  tft.initR(INITR_BLACKTAB);tft.setRotation(1);tft.setSPISpeed(16000000);
  tft.fillScreen(ST77XX_BLACK);
  if(HAS_P_CHANNEL_BACKLIGHT_KEY){
    ledcAttach(Pins::TFT_BL,16000,10);ledcWrite(Pins::TFT_BL,0);
  }
  framebuffer=new GFXcanvas16(160,128);
  if(!framebuffer || !framebuffer->getBuffer())Serial0.println("ERR NO_FRAMEBUFFER");
  setupI2S();
  lastFrame=0;fpsWindow=millis();
  Serial0.printf("UART=%lu RX_BUFFER=8192 YMODEM=128-or-1024-byte-blocks\r\n",(unsigned long)UART_BAUD);
  Serial0.println("READY. Commands: HELP, INFO, UPLOAD=16000,1");
}
void loop(){
  processUART();
  if(receiver.active())return; // no animation or audio starts while binary RX
  if(uploadFailed){uploadFailed=false;logYModemStats("FAILED");Serial0.println("UPLOAD_FAILED / ABORTED");receiver.reset();tft.fillScreen(ST77XX_BLACK);}
  if(uploadReady){
    uploadReady=false;logYModemStats("COMPLETE");receiver.reset();
    tft.fillScreen(ST77XX_BLACK);
    Serial0.printf("UPLOAD_OK bytes=%lu; playback starts once\r\n",(unsigned long)pcmWritten);
    startPlayback();
  }
  uint32_t now=millis();
  if(now-lastFrame>=FRAME_INTERVAL_MS){lastFrame=now;cubeFrame();}
  delay(1);
}
