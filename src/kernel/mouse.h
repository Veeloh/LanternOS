#pragma once
#include <stdint.h>

void mouse_init();
void mouse_handler();
int mouse_get_x();
int mouse_get_y();
int mouse_left_pressed();
int mouse_right_pressed();
int mouse_middle_pressed();
