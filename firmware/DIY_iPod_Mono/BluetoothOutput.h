#pragma once
#include "Config.h"
#include "Types.h"
#if FEATURE_BLUETOOTH
#include <BluetoothA2DPSource.h>
#include <AudioOutput.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

struct BTHeadset {
  char name[48] = {};
  uint8_t address[6] = {};
  int rssi = -127;
  uint32_t seenMs = 0;
};
enum class BTPairKind : uint8_t { None, LegacyPin, ConfirmNumber, Passkey };
struct BTPairRequest {
  BTPairKind kind = BTPairKind::None;
  uint8_t address[6] = {};
  uint32_t number = 0;
  uint8_t minDigits = 4;
  uint8_t maxDigits = 16;
  uint32_t receivedMs = 0;
};

class BluetoothAudioOutput: public AudioOutput {
public:
  BluetoothAudioOutput();
  bool begin() override;
  bool SetRate(int hz) override;
  bool SetChannels(int channels) override;
  bool ConsumeSample(int16_t sample[2]) override;
  bool stop() override;
  static int32_t callback(uint8_t *data, int32_t bytes);
  static bool ensureStarted();
  static void service();
  static bool startScan();
  static uint8_t deviceCount();
  static BTHeadset discovered(uint8_t i);
  static uint8_t bondCount();
  static BTHeadset bonded(uint8_t i);
  static bool connectAddress(const uint8_t address[6]);
  static bool disconnect();
  static bool removeBond(const uint8_t address[6]);
  static bool connected();
  static bool connectedTo(const uint8_t address[6]);
  static bool scanning();
  static bool discoverCallback(const char* name, esp_bd_addr_t address, int rssi);
  static void onAvrc(uint8_t key, bool isReleased);
  static void onConnection(esp_a2d_connection_state_t state, void *ctx);
  static Key pollRemote();
  // Called by the A2DP-derived GAP dispatcher; true means consumed (don't auto-accept).
  static bool onGap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  static BTPairRequest pairingRequest();
  static bool answerPairing(bool accept, const String &digits=String());
  static void cancelPairing();
  static String addressString(const uint8_t addr[6]);
private:
  static constexpr uint16_t CAPACITY = 4096;
  static int16_t pcm[CAPACITY*2];
  static volatile uint16_t rd, wr;
  static portMUX_TYPE mux;
  static bool initialized;
  static uint8_t foundCount;
  static BTHeadset found[16];
  static BTPairRequest request;
};
#endif
