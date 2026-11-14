#pragma once
#include <cstdint>
#include <cstdio>
inline bool psramFound(){return true;}
class AudioFileSource{
public:
virtual ~AudioFileSource()=default;
virtual uint32_t read(void*,uint32_t){return 0;}
virtual uint32_t readNonBlock(void* p,uint32_t n){return read(p,n);}
virtual bool seek(int32_t,int){return false;}
virtual bool close(){return true;}
virtual bool isOpen(){return true;}
virtual uint32_t getSize(){return 0;}
virtual uint32_t getPos(){return 0;}
virtual bool loop(){return true;}
};
