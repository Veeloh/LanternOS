#pragma once
#include <stdint.h>

#define KEY_UP      0x11
#define KEY_DOWN    0x12
#define KEY_LEFT    0x13
#define KEY_RIGHT   0x14
#define KEY_CTRL_C  0x03   // conventional ETX code for Ctrl-C

extern char character_map[];

void keyboard_init();
char keyboard_getchar();
