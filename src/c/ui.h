#pragma once
#include <pebble.h>
#include "model.h"

#define UI_HEADER_HEIGHT 26

// Palette: classic Pebble black chrome, white body, green/red movement pills.
#define UI_COLOR_UP GColorIslamicGreen
#define UI_COLOR_DOWN GColorRed
#define UI_COLOR_FLAT GColorDarkGray
#define UI_COLOR_STALE GColorChromeYellow
#define UI_COLOR_SAMPLE GColorPictonBlue
#define UI_COLOR_MUTED GColorDarkGray

typedef struct {
  GFont header;      // GOTHIC_18_BOLD
  GFont small;       // GOTHIC_14
  GFont small_bold;  // GOTHIC_14_BOLD
  GFont body;        // GOTHIC_18
  GFont body_bold;   // GOTHIC_18_BOLD
  GFont row_bold;    // GOTHIC_24_BOLD
  GFont title;       // GOTHIC_28_BOLD
  GFont big_number;  // LECO_36_BOLD_NUMBERS
  GFont mid_number;  // LECO_28_LIGHT_NUMBERS
} UiFonts;

// System fonts: loaded once, never unloaded (system fonts need no destroy).
const UiFonts *ui_fonts(void);

GColor ui_trend_color(StockTrend trend);

// Draws the black header bar across `bounds.size.w`: title on the left, the
// current time on the right, and a STALE / SAMPLE badge in the middle.
void ui_draw_header(GContext *ctx, GRect bounds, const char *title);

// Draws a filled rounded pill with centred white text.
void ui_draw_pill(GContext *ctx, GRect rect, GColor fill, const char *text, GFont font);

// Width of `text` in `font` on one unwrapped line, capped at max_w.
int16_t ui_text_width(const char *text, GFont font, int16_t max_w);
// Same, uncapped: use for "does it fit" decisions.
int16_t ui_text_width_raw(const char *text, GFont font);
