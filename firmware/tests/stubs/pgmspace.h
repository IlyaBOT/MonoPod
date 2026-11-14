#pragma once
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(ptr) (*(ptr))
#endif
