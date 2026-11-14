#pragma once
#include "Config.h"
#if FEATURE_WIFI_SYNC
#include <Arduino.h>
#include <mbedtls/md.h>
// OTA only during explicit Wi-Fi Sync, with a physical-screen session PIN.
// No HTTP endpoint is exposed in normal playback mode.
class WiFiOtaService {
public:
  void newSession();
  void stopSession();
  bool supported() const;
  uint32_t maxImageBytes() const;
  String info() const;
  String displayLine() const;
  bool start(const String& pin, const String& size, const String& sha256, bool transferBusy);
  bool write(const uint8_t* data, size_t length);
  bool finish();
  void abort(const char* reason="ABORTED");
  bool busy() const { return uploading || rebootQueued; }
  bool inProgress() const { return uploading || rebootQueued; }
  bool restartPending() const { return rebootQueued; }
  const String& error() const { return lastError; }
  uint8_t percent() const;
  void loop();
private:
  void fail(const char* reason);
  String pinCode;
  String expectedSha;
  String lastError;
  mbedtls_md_context_t shaContext{};
  bool shaInitialized=false,uploading=false,rebootQueued=false;
  uint32_t expectedSize=0,received=0,restartAt=0,lastChunkAt=0;
  uint8_t badPinAttempts=0;
  uint8_t imageHeader[24]{};size_t headerBytes=0;bool headerValidated=false;
};
extern WiFiOtaService wifiOta;
#endif
