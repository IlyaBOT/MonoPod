#include "SyncMode.h"
#include "Library.h"
#include "AudioEngine.h"
#include "Feedback.h"
#include "UI.h"
#include "SyncFiles.h"
SyncController syncMode;
bool SyncController::enter(SyncTransport wanted) {
  if(wanted==SyncTransport::None || !media.sdOK()) return false;
  if(current!=SyncTransport::None) return current==wanted;
  player.stop();             // decoder/I2S SD file handle must be released FIRST
  feedback.quiet();          // no click/beep while synchronized
  current=wanted;            // lock normal UI BEFORE exposing storage/network
  ui.redraw();
  return true;
}
bool SyncController::leave(SyncTransport expected) {
  if(SyncFiles::busy())return false;
  if(current!=expected || expected==SyncTransport::None) return false;
  current=SyncTransport::None;
  ui.redraw();
  return true;
}
