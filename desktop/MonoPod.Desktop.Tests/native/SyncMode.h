
#pragma once
enum class SyncTransport {None,UsbCdc,WiFi};struct SyncController {SyncTransport current=SyncTransport::None;bool canWrite(SyncTransport t)const{return current==t;}bool is(SyncTransport t)const{return current==t;}bool enter(SyncTransport t){if(current!=SyncTransport::None&&current!=t)return false;current=t;return true;}bool leave(SyncTransport){current=SyncTransport::None;return true;}};extern SyncController syncMode;
