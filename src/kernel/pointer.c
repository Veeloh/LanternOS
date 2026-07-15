#include "pointer.h"
#include "mouse.h"
#include "vga.h"

void pointer_init(void) {
	vga_print("\npointer: using PS/2 mouse");
	mouse_init();
}

void pointer_poll(void) {
	// PS/2 is interrupt-driven (mouse_handler() via IRQ12) - nothing to do here.
}

int pointer_get_x(void) {
	return mouse_get_x();
}

int pointer_get_y(void) {
	return mouse_get_y();
}

int pointer_left_pressed(void) {
	return mouse_left_pressed();
}

int pointer_right_pressed(void) {
	return mouse_right_pressed();
}
