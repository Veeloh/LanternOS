#include "process.h"
#include "heap.h"
#include "vga.h"

extern void context_switch(registers_t* old, registers_t* new);

static process_t processes[MAX_PROCESSES];
static int current_process = 0;
static int process_count = 0;

// This is where a freshly spawned process actually starts running.
// It's never called directly - context_switch's final `ret` lands
// here the first time a process is scheduled, because process_spawn()
// makes it look like this is where that process last "left off".
//
// We land here with interrupts still disabled: process_schedule() is
// only ever called from timer_handler(), an interrupt-gate ISR, which
// has IF cleared for its whole duration. A resumed (previously-run)
// process doesn't need to worry about this - it eventually unwinds
// back through its own original interrupt frame and IF gets restored
// by that iret. But a brand new process never goes through iret at
// all, so without this it would run forever with interrupts masked
// and could never be preempted again.
static void process_bootstrap(void) {
	__asm__ volatile ("sti");

	process_t* self = process_get_current();
	if (self->entry) self->entry();

	// entry() returned. There's no real caller to return to - this
	// stack frame is synthetic - so just retire the process and idle
	// until the scheduler switches to someone else.
	self->state = PROCESS_DEAD;
	while (1) {
		__asm__ volatile ("hlt");
	}
}

void process_init() {
	for (int i = 0; i < MAX_PROCESSES; i++) {
		processes[i].state = PROCESS_DEAD;
		processes[i].pid = i;
		processes[i].stack = 0;
		processes[i].entry = 0;
	}

	//register current execution
	processes[0].state = PROCESS_RUNNING;
	process_count = 1;
	current_process = 0;
}

void process_spawn(void (*entry)()) {
	//find a free stack
	for (int i = 0; i < MAX_PROCESSES; i++) {
		if (processes[i].state == PROCESS_DEAD) {
			processes[i].pid = i;
			processes[i].entry = entry;
			//allocate a stack
			processes[i].stack = (uint8_t*)kmalloc(STACK_SIZE);

			// Build a stack that looks like this process already
			// called context_switch once and is just about to
			// `ret` into process_bootstrap - so the very first
			// real context_switch into this process just works,
			// no special-casing needed for "never run before".
			uint32_t* sp = (uint32_t*)(processes[i].stack + STACK_SIZE);
			*--sp = (uint32_t)process_bootstrap; // fake return address
			*--sp = 0; // ebp
			*--sp = 0; // ebx
			*--sp = 0; // esi
			*--sp = 0; // edi

			processes[i].regs.esp = (uint32_t)sp;
			processes[i].state = PROCESS_READY;
			process_count++;
			return;
		}
	}
}

void process_schedule() {
	if (process_count == 0) return;

	//find next ready process
	int next = current_process;
	for (int i = 0; i < MAX_PROCESSES; i++) {
		next = (next + 1) % MAX_PROCESSES;
		if (processes[next].state == PROCESS_READY || processes[next].state == PROCESS_RUNNING) {
			break;
		}
	}

	if (next == current_process) return;

	int prev = current_process;
	processes[prev].state = PROCESS_READY;
	processes[next].state = PROCESS_RUNNING;
	current_process = next;

	context_switch(&processes[prev].regs, &processes[next].regs);
}

process_t* process_get_current() {
	return &processes[current_process];
}

process_t* process_get_by_id(int id) {
	return &processes[id];
}
