
#pragma once
struct MediaLibrary {bool ready=true;bool sdOK()const{return ready;}uint64_t totalBytes()const{return 64000000000ULL;}uint64_t usedBytes()const{return 1000000000ULL;}size_t size()const{return 12;}void rescan(){}static bool isAudio(String p){p.toLowerCase();return p.endsWith(".mp3")||p.endsWith(".wav")||p.endsWith(".flac")||p.endsWith(".aac");}};extern MediaLibrary media;
