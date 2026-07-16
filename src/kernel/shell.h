#pragma once

void shell_init();
void shell_run();

// Tokenizes `line` and dispatches it through the exact same commands[]
// table shell_run()'s console loop uses, but captures whatever the command
// handler vga_print()s into out_buf (NUL-terminated, out_max bytes) instead
// of drawing it to the fullscreen text console - see vga_capture_begin()
// in vga.h. This is what the Terminal app (app.c) calls per Enter press.
//
// KNOWN GAP: a couple of commands touch the console/compositor directly
// instead of going through vga_print (e.g. `clear` calls vga_clear(),
// `desktop` calls desktop() itself) - those still act on the real screen
// even when invoked from inside a Terminal window. Fine for tonight;
// cosmetic only for `clear`, and `desktop`/`poweroff` are edge cases
// nobody will hit from inside a Terminal window in practice.
void shell_exec_line(const char* line, char* out_buf, int out_max);
