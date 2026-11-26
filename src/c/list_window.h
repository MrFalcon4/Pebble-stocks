#pragma once
#include <pebble.h>

void list_window_push(void);
void list_window_destroy(void);
// Rebuild rows / empty state after the model changed.
void list_window_refresh(void);
// Minute tick: redraw the header clock and stale badge.
void list_window_tick(void);
// Unobstructed area changed: re-frame layers.
void list_window_relayout(void);
