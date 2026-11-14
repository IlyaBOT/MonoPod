#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline size_t& mock_psram_free(){static size_t value=8u*1024u*1024u;return value;}
inline size_t heap_caps_get_free_size(int){return mock_psram_free();}
inline void* heap_caps_malloc(size_t n,int){return malloc(n);}
