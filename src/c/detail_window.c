#include "detail_window.h"
#include "model.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define PAD 8

static Window *s_window;
static Layer *s_canvas;
static int s_index;

// Splits "$1,234.56" into a currency prefix ("$") and a numeric part
// ("1,234.56") the LECO number font can draw. Returns false if the numeric
// part has characters LECO lacks, in which case the caller draws it in Gothic.
static bool split_price(const char *price, char *prefix, size_t prefix_size, const char **number) {
  size_t i = 0;
  while (price[i] != '\0' && !(price[i] >= '0' && price[i] <= '9')) {
    i++;
  }
  if (price[i] == '\0' || i >= prefix_size) {
    return false;
  }
  for (size_t j = i; price[j] != '\0'; j++) {
    const char c = price[j];
    if (!((c >= '0' && c <= '9') || c == '.' || c == ',')) {
      return false;
    }
  }
  memcpy(prefix, price, i);
  prefix[i] = '\0';
  *number = price + i;
  return true;
}

static void draw_price(GContext *ctx, const char *price, GRect box) {
  const UiFonts *f = ui_fonts();
  char prefix[8];
  const char *number = NULL;
  graphics_context_set_text_color(ctx, GColorBlack);

  if (split_price(price, prefix, sizeof(prefix), &number)) {
    const GFont number_fonts[] = { f->big_number, f->mid_number };
    const int16_t number_y[] = { 0, 6 };
    for (size_t k = 0; k < ARRAY_LENGTH(number_fonts); k++) {
      const int16_t pw = prefix[0] ? ui_text_width_raw(prefix, f->title) + 2 : 0;
      const int16_t nw = ui_text_width_raw(number, number_fonts[k]);
      if (pw + nw <= box.size.w) {
        if (pw) {
          graphics_draw_text(ctx, prefix, f->title, GRect(box.origin.x, box.origin.y + 2, pw, 34),
                             GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        }
        graphics_draw_text(ctx, number, number_fonts[k],
                           GRect(box.origin.x + pw, box.origin.y + number_y[k], nw + 2, 42),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
        return;
      }
    }
  }
  graphics_draw_text(ctx, price, f->title, GRect(box.origin.x, box.origin.y + 2, box.size.w, 34),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void format_updated(char *buf, size_t size, time_t now) {
  const time_t updated = model_updated();
  if (updated <= 0) {
    snprintf(buf, size, "Not updated yet");
    return;
  }
  const int minutes = model_age_minutes(now);
  if (model_is_stale(now)) {
    if (minutes < 60) {
      snprintf(buf, size, "Stale - %d min ago", minutes);
    } else {
      snprintf(buf, size, "Stale - %d h ago", minutes / 60);
    }
    return;
  }
  struct tm *t = localtime(&updated);
  char clock[8] = "";
  if (t) {
    strftime(clock, sizeof(clock), clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  }
  const char *shown = (!clock_is_24h_style() && clock[0] == '0') ? clock + 1 : clock;
  snprintf(buf, size, "Updated %s", shown);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  const UiFonts *f = ui_fonts();
  const GRect area = layer_get_unobstructed_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  const Stock *stock = model_get(s_index);
  char position[24];
  snprintf(position, sizeof(position), "%d/%d", s_index + 1, model_count());
  ui_draw_header(ctx, area, position);
  if (!stock) {
    return;
  }

  const int16_t w = area.size.w - 2 * PAD;
  int16_t y = area.origin.y + UI_HEADER_HEIGHT + 2;

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, stock->symbol, f->title, GRect(PAD, y, w, 32),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 30;
  graphics_context_set_text_color(ctx, UI_COLOR_MUTED);
  graphics_draw_text(ctx, stock->name, f->body, GRect(PAD, y, w, 42),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  y += 44;

  draw_price(ctx, stock->price, GRect(PAD, y, w, 44));
  y += 46;

  const GColor trend = ui_trend_color(model_trend(stock));
  char movement[STOCK_CHANGE_SIZE + STOCK_PERCENT_SIZE + 4];
  if (stock->change[0] && stock->percent[0]) {
    snprintf(movement, sizeof(movement), "%s  %s", stock->change, stock->percent);
  } else {
    snprintf(movement, sizeof(movement), "%s", stock->change[0] ? stock->change
                                                : (stock->percent[0] ? stock->percent : "--"));
  }
  ui_draw_pill(ctx, GRect(PAD, y, w, 30), trend, movement, f->row_bold);
  y += 36;

  // Anchor the timestamp to the bottom edge; skip it if the area is too short.
  const int16_t bottom = area.origin.y + area.size.h;
  if (bottom - 26 >= y) {
    y = bottom - 26;
    const time_t now = time(NULL);
    char updated[32];
    format_updated(updated, sizeof(updated), now);
    graphics_context_set_text_color(ctx, model_is_stale(now) ? UI_COLOR_DOWN : UI_COLOR_MUTED);
    graphics_draw_text(ctx, updated, model_is_stale(now) ? f->body_bold : f->body,
                       GRect(PAD, y, w, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void cycle(int delta) {
  const int count = model_count();
  if (count <= 0) {
    return;
  }
  s_index = ((s_index + delta) % count + count) % count;
  layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  cycle(-1);
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  cycle(1);
}

static void click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 200, up_click);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 200, down_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

void detail_window_push(int index) {
  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, GColorWhite);
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load,
      .unload = window_unload,
    });
  }
  s_index = index;
  window_stack_push(s_window, true);
}

void detail_window_refresh(void) {
  if (!s_canvas) {
    return;
  }
  const int count = model_count();
  if (count == 0) {
    // Nothing left to show: back to the list's setup screen.
    window_stack_remove(s_window, true);
    return;
  }
  if (s_index >= count) {
    s_index = count - 1;
  }
  layer_mark_dirty(s_canvas);
}

void detail_window_relayout(void) {
  detail_window_tick();
}

void detail_window_tick(void) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

void detail_window_destroy(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
