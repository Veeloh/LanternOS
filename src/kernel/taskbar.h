#pragma once
#include <stdint.h>

#define TASKBAR_W 48

typedef enum {
	TB_NONE = 0,
	TB_POWER,             // power off the machine (acpi_poweroff)
	TB_OPEN_TERMINAL,     // launch (or focus) a terminal window
	TB_SELECT_WINDOW,     // focus an existing window; out_window_idx is set
	TB_RETURN_TO_SHELL,   // start menu "back" button -> exit desktop() to the text shell
	TB_OPEN_FILES,        // start menu file browser icon (stub, no app yet)
	TB_OPEN_SETTINGS,     // start menu settings icon (stub, no app yet)
	TB_OPEN_TEXTEDIT,     // start menu text editor icon - opens a blank Text Edit window
} tb_action_t;

// Draws the taskbar strip, and the start menu popup if it's open, into the
// current draw target (desktop() points this at its backbuffer).
// battery_pct/charging/weekday/day are supplied by the caller since SolOS
// doesn't have a battery or RTC-calendar driver yet - see the TODOs in
// window.c where these are set.
void taskbar_draw(int fb_h, int battery_pct, int charging,
                   const char* weekday, int day,
                   uint8_t hh, uint8_t mm, uint8_t ss);

// Hit-tests a click against the taskbar / start menu popup. Returns the
// action taken, or TB_NONE if the click missed everything the taskbar owns
// (including "missed" if outside the taskbar column and the menu is closed).
tb_action_t taskbar_handle_click(int mx, int my, int* out_window_idx);

// Toggles / queries the start menu popup.
int  taskbar_start_menu_open(void);
void taskbar_close_start_menu(void);

// Total rect currently owned by the taskbar (the strip, plus the start
// menu popup when it's open) - so window.c can fold it into its dirty-rect
// bookkeeping instead of repainting the whole screen every frame.
void taskbar_full_rect(int fb_h, int* x, int* y, int* w, int* h);
