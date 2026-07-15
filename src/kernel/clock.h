#pragma once
#include <stdint.h>

void clock_init(uint8_t h, uint8_t m, uint8_t s);
void clock_tick();
void clock_draw();
void clock_set(uint8_t h, uint8_t m, uint8_t s);
void clock_get(uint8_t* h, uint8_t* m, uint8_t* s);
