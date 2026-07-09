// hello.c - minimal SolOS userland test program.
// No libc, no crt0 - _start runs directly on the kernel's stack
// (elf_exec calls the entry point like a normal function), so we
// don't need to set up our own stack frame here.

static void sys_write(const char* str) {
	__asm__ volatile (
		"mov $1, %%eax\n"   // SYS_WRITE
		"mov %0, %%ebx\n"
		"int $0x80\n"
		:: "r"(str)
		: "eax", "ebx"
	);
}

static void sys_exit(int code) {
	__asm__ volatile (
		"mov $0, %%eax\n"   // SYS_EXIT
		"mov %0, %%ebx\n"
		"int $0x80\n"
		:: "r"(code)
		: "eax", "ebx"
	);
}

void _start() {
	sys_write("Hello from userland!\n");
	sys_exit(0);

	// sys_exit halts the process (see sys_exit() in syscall.c - it
	// marks the process dead and spins forever), so we never get
	// here. This is just a safety net in case that ever changes.
//	while (1);
}
