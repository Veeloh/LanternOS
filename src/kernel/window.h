#pragma once
#include <stdint.h>

#define MAX_WINDOWS 8
#define TITLE_H     20

typedef struct {
	int x, y, w, h;
	const char* title;
} window_t;

void desktop();

// --- read-only accessors for taskbar.c (compositor internals stay in window.c) ---
int       window_count(void);
int       window_z_order(int slot);   // slot 0 = backmost .. window_count()-1 = frontmost
window_t* window_at(int idx);         // raw window by index (not z-order)
int       window_top_index(void);     // raw index of the frontmost window, -1 if none
