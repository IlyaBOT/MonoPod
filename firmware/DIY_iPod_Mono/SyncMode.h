#pragma once
#include <Arduino.h>
// Shared player/SD ownership gate for all transports.
// USB MSC/iTunes backend is NOT implemented by this release.
enum class SyncTransport : uint8_t { None=0, UsbCdc=1, WiFi=2, UsbMsc=3 };
class SyncController {
public:
  bool enter(SyncTransport wanted);
  bool leave(SyncTransport expected);
  bool active() const { return current != SyncTransport::None; }
  bool is(SyncTransport wanted) const { return current == wanted; }
  SyncTransport transport() const { return current; }
  bool canWrite(SyncTransport who) const { return current == who; }
private:
  SyncTransport current=SyncTransport::None;
};
extern SyncController syncMode;
