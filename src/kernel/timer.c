#include "timer.h"
#include "idt.h"
#include "vga.h"
#include "clock.h"
#include "process.h"

static uint32_t ticks = 0;

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

void timer_handler() {
	ticks++;
	clock_tick();
	process_schedule();
	//send eoi
	outb(0x20, 0x20);
}

uint32_t timer_get_ticks() {
	return ticks;
}

extern void timer_isr();

void timer_init(uint32_t frequency) {
	//register handler for IRQ0 = int 32
	idt_set_handler(32, (uint32_t)timer_isr);

	//program pit to fire at requested freq
	uint32_t divisor = 1193180 / frequency;

	outb(0x43, 0x36); //command: channel 0, lobyte/hibyte, square wave
	outb(0x40, divisor & 0xFF); //low byte
	outb(0x40, (divisor >> 8) & 0xFF); //high byte
}
