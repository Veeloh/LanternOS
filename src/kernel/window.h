#pragma once
#include <stdint.h>

#define MAX_WINDOWS 8
#define TITLE_H     20

// Every window is tagged with the app it hosts. APP_NONE means "plain
// frame, no content hooks" (e.g. the startup "SolOS" window) - window.c
// falls back to drawing just the empty body colour for those. Any other
// value gets its draw/key/click calls dispatched through app_get_vtable()
// in app.h/app.c, so adding a new app never requires touching window.c.
typedef enum {
	APP_NONE = 0,
	APP_TERMINAL,
	APP_FILES,
	APP_SETTINGS,
	APP_ABOUT,
	APP_TEXTEDIT,
	APP_CALCULATOR,
} app_type_t;

typedef struct {
	int x, y, w, h;
	const char* title;
	app_type_t app_type;
	void* app_state;   // opaque, owned by whichever app_vtable_t (app.h) is in use
} window_t;

void desktop();

// --- read-only accessors for taskbar.c (compositor internals stay in window.c) ---
int       window_count(void);
int       window_z_order(int slot);   // slot 0 = backmost .. window_count()-1 = frontmost
window_t* window_at(int idx);         // raw window by index (not z-order)
int       window_top_index(void);     // raw index of the frontmost window, -1 if none
