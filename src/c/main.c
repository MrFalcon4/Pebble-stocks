// Stocks Viewer: shows the quotes served by the Pebble "Stocks" plugin
// (category finance / item stock). PKJS subscribes on the phone and forwards a
// validated snapshot; this side renders it and keeps the last one in persist.
#include <pebble.h>
#include "comm.h"
#include "detail_window.h"
#include "list_window.h"
#include "model.h"

static void on_model_changed(void) {
  list_window_refresh();
  detail_window_refresh();
}

static void minute_tick(struct tm *tick_time, TimeUnits changed) {
  list_window_tick();
  detail_window_tick();
}

static void unobstructed_change(AnimationProgress progress, void *context) {
  list_window_relayout();
  detail_window_relayout();
}

static void log_heap(const char *when) {
  APP_LOG(APP_LOG_LEVEL_INFO, "heap %s: used=%u free=%u", when,
          (unsigned)heap_bytes_used(), (unsigned)heap_bytes_free());
}

static void init(void) {
  model_init();
  list_window_push();
  comm_init(on_model_changed);
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){ .change = unobstructed_change }, NULL);
  log_heap("after init");
}

static void deinit(void) {
  log_heap("before deinit");
  unobstructed_area_service_unsubscribe();
  tick_timer_service_unsubscribe();
  comm_deinit();
  detail_window_destroy();
  list_window_destroy();
  model_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
