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
			//allocate a stack
			processes[i].stack = (uint8_t*)kmalloc(STACK_SIZE);

			//set up registers
			processes[i].regs.esp = (uint32_t)(processes[i].stack + STACK_SIZE);
			uint32_t* stack = (uint32_t*)(processes[i].stack + STACK_SIZE);
			*--stack = 0x202;
			*--stack = 0x08;
			*--stack = (uint32_t)entry;
			*--stack = 0;
			*--stack = 0;
			*--stack = 0;
			*--stack = 0;
			*--stack = (uint32_t)(processes[i].stack + STACK_SIZE); //esp
			*--stack = 0;
			*--stack = 0;
			*--stack = 0;
			processes[i].regs.esp = (uint32_t)stack;
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

process_t* process_get_by_id(int id) {
	return &processes[id];
}
