#pragma once
#include <stdint.h>

#define MENUBAR_H 20

// Draws the menu bar strip across the top of the screen (from the taskbar's
// right edge to fb_w), plus any open dropdown. app_name is whatever should
// be shown top-right - window.c passes the frontmost window's title, or 0
// for "no app" (nothing is drawn on the right in that case).
void menubar_draw(int fb_w, const char* app_name);

// Hit-tests a click against the menu bar / an open dropdown. Returns 1 if
// the click was consumed (window.c should skip its normal window-hit-test
// for this click), 0 if the click missed the bar/dropdown entirely.
int menubar_handle_click(int mx, int my);

// Total rect currently owned by the menu bar (the strip, plus whichever
// dropdown panel is open) - so window.c can fold it into dirty-rect repaint
// bookkeeping the same way it does for the taskbar's start menu.
void menubar_full_rect(int fb_w, int* x, int* y, int* w, int* h);

int  menubar_is_open(void);
void menubar_close(void);

// --- app-supplied menu items ---
//
// Apps register their own File/Edit items instead of menubar.c hardcoding
// them. A cmd is just a plain "do this" callback, no args - if an item
// needs to know which window it's acting on, have on_open() give the cmd
// a way to find that window itself (e.g. a file-scoped pointer/id it
// closes over), since menubar.c doesn't track windows.
//
// KNOWN GAP (not solved tonight): menubar.c has no idea which window is
// frontmost, so items registered here are global, not per-app - if two
// windows both register a "Save" item they'll pile up side by side rather
// than one replacing the other. Call menubar_clear_menu() first (e.g. from
// on_open, or from window.c right before/after a focus change) if you want
// a clean slate before adding your own items.
typedef void (*menubar_cmd_t)(void);

typedef enum { MENUBAR_FILE, MENUBAR_EDIT } menubar_menu_t;

#define MENUBAR_MAX_ITEMS 4

// Adds one row to the given dropdown. title should outlive the item (a
// string literal is fine). Silently no-ops once MENUBAR_MAX_ITEMS is hit.
void menubar_add_item(menubar_menu_t menu, const char* title, menubar_cmd_t cmd);

// Removes every item from both dropdowns (also closes the dropdown if
// one's open, since its row count is about to change).
void menubar_clear_menu(void);
