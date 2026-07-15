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
