#include "list_window.h"
#include "detail_window.h"
#include "model.h"
#include "ui.h"
#include <stdio.h>

#define ROW_HEIGHT 50
#define ROW_PAD 6

static Window *s_window;
static Layer *s_header_layer;
static Layer *s_empty_layer;
static MenuLayer *s_menu_layer;

// ---------------------------------------------------------------- header

static void header_update(Layer *layer, GContext *ctx) {
  ui_draw_header(ctx, layer_get_bounds(layer), "Stocks");
}

// ---------------------------------------------------------------- rows

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  return (uint16_t)model_count();
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return ROW_HEIGHT;
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  const Stock *stock = model_get(index->row);
  if (!stock) {
    return;
  }
  const UiFonts *f = ui_fonts();
  const GRect b = layer_get_bounds(cell);
  const bool selected = menu_cell_layer_is_highlighted(cell);
  const GColor fg = selected ? GColorWhite : GColorBlack;
  const GColor muted = selected ? GColorLightGray : UI_COLOR_MUTED;
  const int16_t inner_w = b.size.w - 2 * ROW_PAD;

  // Line 1: symbol (left) and price (right). Price shrinks to 18pt if it won't fit.
  GFont price_font = f->row_bold;
  int16_t price_w = ui_text_width_raw(stock->price, price_font);
  if (price_w > inner_w * 3 / 5) {
    price_font = f->body_bold;
    price_w = ui_text_width(stock->price, price_font, inner_w * 4 / 5);
  }
  const int16_t symbol_w = inner_w - price_w - 4;
  const int16_t price_y = (price_font == f->row_bold) ? -3 : 2;
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, stock->price, price_font,
                     GRect(b.size.w - ROW_PAD - price_w - 2, price_y, price_w + 2, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  graphics_draw_text(ctx, stock->symbol, f->row_bold,
                     GRect(ROW_PAD, -3, symbol_w > 0 ? symbol_w : 0, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Line 2: name (left) and the % change pill (right).
  const char *pill_text = stock->percent[0] ? stock->percent : (stock->change[0] ? stock->change : "--");
  int16_t pill_w = ui_text_width(pill_text, f->small_bold, inner_w / 2) + 14;
  if (pill_w < 58) {
    pill_w = 58;
  }
  const GRect pill = GRect(b.size.w - ROW_PAD - pill_w, 27, pill_w, 18);
  ui_draw_pill(ctx, pill, ui_trend_color(model_trend(stock)), pill_text, f->small_bold);

  graphics_context_set_text_color(ctx, muted);
  const int16_t name_w = inner_w - pill_w - 6;
  graphics_draw_text(ctx, stock->name, f->small,
                     GRect(ROW_PAD, 26, name_w > 0 ? name_w : 0, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_separator(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  const GRect b = layer_get_bounds(cell);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_line(ctx, GPoint(ROW_PAD, 0), GPoint(b.size.w - ROW_PAD, 0));
}

static int16_t get_separator_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return 1;
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  if (model_get(index->row)) {
    detail_window_push(index->row);
  }
}

// ---------------------------------------------------------------- empty / setup state

static void draw_glyph(GContext *ctx, GPoint origin) {
  // A tiny rising chart: axis plus a three-segment line.
  graphics_context_set_stroke_color(ctx, UI_COLOR_MUTED);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, origin, GPoint(origin.x + 56, origin.y));
  graphics_draw_line(ctx, origin, GPoint(origin.x, origin.y - 34));
  graphics_context_set_stroke_color(ctx, UI_COLOR_UP);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, GPoint(origin.x + 6, origin.y - 8), GPoint(origin.x + 22, origin.y - 20));
  graphics_draw_line(ctx, GPoint(origin.x + 22, origin.y - 20), GPoint(origin.x + 34, origin.y - 13));
  graphics_draw_line(ctx, GPoint(origin.x + 34, origin.y - 13), GPoint(origin.x + 52, origin.y - 30));
  graphics_context_set_stroke_width(ctx, 1);
}

static const char *status_line(char *buf, size_t size) {
  switch (model_status()) {
    case STOCKS_STATUS_WAITING: return "Connecting to phone...";
    case STOCKS_STATUS_NO_PLUGIN: return "Stocks plugin not found";
    case STOCKS_STATUS_EMPTY: return "No tickers yet";
    case STOCKS_STATUS_NO_API: return "Update the Pebble app";
    case STOCKS_STATUS_ERROR:
      snprintf(buf, size, "Error: %s", model_error()[0] ? model_error() : "unknown");
      return buf;
    default: return "";
  }
}

static void empty_update(Layer *layer, GContext *ctx) {
  const UiFonts *f = ui_fonts();
  const GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  const int16_t pad = 14;
  const int16_t w = b.size.w - 2 * pad;
  int16_t y = 14;
  draw_glyph(ctx, GPoint((b.size.w - 56) / 2, y + 38));
  y += 48;

  char buf[40];
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, status_line(buf, sizeof(buf)), f->row_bold, GRect(pad, y, w, 60),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  GSize used = graphics_text_layout_get_content_size(status_line(buf, sizeof(buf)), f->row_bold,
                                                     GRect(pad, y, w, 60), GTextOverflowModeWordWrap,
                                                     GTextAlignmentCenter);
  y += used.h + 8;
  graphics_context_set_text_color(ctx, UI_COLOR_MUTED);
  graphics_draw_text(ctx, "Install the Stocks plugin and add tickers in its settings", f->body,
                     GRect(pad, y, w, b.size.h - y - 4), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

// ---------------------------------------------------------------- layout

static void layout(void) {
  if (!s_window) {
    return;
  }
  Layer *root = window_get_root_layer(s_window);
  const GRect area = layer_get_unobstructed_bounds(root);
  const GRect header = GRect(area.origin.x, area.origin.y, area.size.w, UI_HEADER_HEIGHT);
  const GRect body = GRect(area.origin.x, area.origin.y + UI_HEADER_HEIGHT, area.size.w,
                           area.size.h - UI_HEADER_HEIGHT);
  layer_set_frame(s_header_layer, header);
  layer_set_frame(menu_layer_get_layer(s_menu_layer), body);
  layer_set_frame(s_empty_layer, body);
}

void list_window_relayout(void) {
  layout();
}

void list_window_refresh(void) {
  if (!s_window) {
    return;
  }
  const bool empty = model_count() == 0;
  layer_set_hidden(menu_layer_get_layer(s_menu_layer), empty);
  layer_set_hidden(s_empty_layer, !empty);
  menu_layer_reload_data(s_menu_layer);
  if (!empty) {
    MenuIndex sel = menu_layer_get_selected_index(s_menu_layer);
    if (sel.row >= model_count()) {
      sel.row = (uint16_t)(model_count() - 1);
      menu_layer_set_selected_index(s_menu_layer, sel, MenuRowAlignCenter, false);
    }
  }
  layer_mark_dirty(s_header_layer);
  layer_mark_dirty(s_empty_layer);
}

void list_window_tick(void) {
  if (s_header_layer) {
    layer_mark_dirty(s_header_layer);
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  s_header_layer = layer_create(GRect(0, 0, bounds.size.w, UI_HEADER_HEIGHT));
  layer_set_update_proc(s_header_layer, header_update);

  const GRect body = GRect(0, UI_HEADER_HEIGHT, bounds.size.w, bounds.size.h - UI_HEADER_HEIGHT);
  s_menu_layer = menu_layer_create(body);
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .draw_separator = draw_separator,
    .get_separator_height = get_separator_height,
    .select_click = select_click,
  });
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorBlack, GColorWhite);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);

  s_empty_layer = layer_create(body);
  layer_set_update_proc(s_empty_layer, empty_update);

  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
  layer_add_child(root, s_empty_layer);
  layer_add_child(root, s_header_layer);

  layout();
  list_window_refresh();
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
  layer_destroy(s_empty_layer);
  s_empty_layer = NULL;
  layer_destroy(s_header_layer);
  s_header_layer = NULL;
}

void list_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, GColorWhite);
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}

void list_window_destroy(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}
