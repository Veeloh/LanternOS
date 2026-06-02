#pragma once
#include <stdint.h>

#define MAX_PROCESSES 16
#define STACK_SIZE 4096

typedef enum {
	PROCESS_DEAD,
	PROCESS_READY,
	PROCESS_RUNNING,
} process_state_t;

typedef struct {
	uint32_t eax, ebx, ecx, edx;
	uint32_t esi, edi, ebp, esp;
	uint32_t eip, eflags;
} registers_t;

typedef struct {
	uint32_t pid;
	process_state_t state;
	registers_t regs;
	uint8_t stack;
} process_t;

void process_init();
void process_spawn(void (*entry)());
void process_schedule();
