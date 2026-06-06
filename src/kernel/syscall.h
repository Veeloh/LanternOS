#pragma once
#include <stdint.h>

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2

void syscall_init();
void syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);
