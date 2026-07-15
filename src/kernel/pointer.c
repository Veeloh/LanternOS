#include "pointer.h"
#include "elan_touchpad.h"
#include "mouse.h"
#include "vga.h"

static int use_elan = 0;

void pointer_init(void) {
	if (elan_init() == 0) {
		use_elan = 1;
		vga_print("\npointer: using Elan I2C touchpad");
	} else {
		use_elan = 0;
		vga_print("\npointer: no Elan touchpad found, falling back to PS/2");
		mouse_init();
	}
}

void pointer_poll(void) {
	// PS/2 is interrupt-driven (mouse_handler() via IRQ12) - nothing to do
	// here for that path. Elan has no interrupt wired up yet, so it has
	// to be polled for a fresh report every loop iteration.
	if (use_elan) elan_poll(0);
}

int pointer_get_x(void) {
	return use_elan ? elan_get_x() : mouse_get_x();
}

int pointer_get_y(void) {
	return use_elan ? elan_get_y() : mouse_get_y();
}

int pointer_left_pressed(void) {
	return use_elan ? elan_left_pressed() : mouse_left_pressed();
}

int pointer_right_pressed(void) {
	return use_elan ? elan_right_pressed() : mouse_right_pressed();
}
