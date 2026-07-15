#pragma once
#include "window.h"

// The plug-in interface every base app implements. A window's app_type
// (window.h) picks which vtable window.c dispatches through - window.c
// itself never knows anything about individual apps.
//
// All four hooks are optional (NULL is fine and just means "app doesn't
// care about this event"). window.c already handles window drag and
// bring-to-front on its own; these hooks only cover an app's own content.
//
// KNOWN GAP (not solved tonight): there's no window-close UX yet (no close
// button, no removal from the windows[]/z_order[] arrays), so on_close is
// wired up and will run correctly whenever that lands, but nothing calls
// it yet. Fine for now - app_state just lives for the process's lifetime.
typedef struct {
	// Called once, right after the window is spawned (from spawn_window()
	// in window.c). Use it to kmalloc win->app_state and set up initial
	// state.
	void (*on_open)(window_t* win);

	// Called once, right before the window is destroyed - see KNOWN GAP
	// above. If on_open allocated win->app_state, free it here.
	void (*on_close)(window_t* win);

	// Called every repaint to draw the window's content area (the body,
	// below the title bar - window.c already draws the title bar/border/
	// background fill before calling this). This is the one hook every
	// real app needs.
	void (*draw)(window_t* win);

	// Called when this window is focused (the frontmost window,
	// window_top_index()) and a key comes in from keyboard_getchar().
	// window.c only calls this when a key was actually pressed (c != 0).
	void (*on_key)(window_t* win, char c);

	// Called on a left-click that lands inside the content area (below
	// the title bar). lx/ly are local coordinates relative to the body's
	// top-left corner, i.e. (0,0) is (win->x, win->y + TITLE_H).
	void (*on_click)(window_t* win, int lx, int ly);
} app_vtable_t;

// Returns the vtable for a given app type, or NULL for APP_NONE / any
// type window.c doesn't recognise. window.c always NULL-checks both the
// vtable pointer and the individual hook before calling it.
const app_vtable_t* app_get_vtable(app_type_t type);
