#pragma once
#include <pebble.h>

typedef void (*CommChangedHandler)(void);

// Opens AppMessage and asks the phone for a fresh snapshot.
void comm_init(CommChangedHandler on_changed);
void comm_deinit(void);
