#include "process.h"
#include "heap.h"
#include "vga.h"

static process_t processes[MAX_PROCESSES];
static int current_process = 0;
static int process_count = 0;

void process_init() {
	for (int i = 0; i < MAX_PROCESSES; i++) {
		processes[i].state = PROCESS_DEAD;
		processes[i].pid = i;
		processes[i].stack = 0;
	}
}

void process_spawn(void (*entry)()) {
	//find a free stack
	for (int i = 0; i < MAX_PROCESSES; i++) {
		if (processes[i].state == PROCESS_DEAD) {
			//allocate a stack
			processes[i].stack = (uint8_t*)kmalloc(STACK_SIZE);

			//set up registers
			processes[i].regs.esp = (uint32_t)(processes[i].stack + STACK_SIZE);
			processes[i].regs.eip = (uint32_t)entry;
			processes[i].regs.eflags = 0x202; // interupts enable lmao me when she ints my cpu lol
			processes[i].regs.eax = 0;
			processes[i].regs.ebx = 0;
			processes[i].regs.ecx = 0;
			processes[i].regs.edx = 0;
			processes[i].regs.esi = 0;
			processes[i].regs.edi = 0;
			processes[i].regs.ebp = 0;
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

	processes[current_process].state = PROCESS_READY;
	processes[next].state = PROCESS_RUNNING;
	current_process = next;
}

process_t* process_get_current() {
	return &processes[current_process];
}
