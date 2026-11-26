#include "ui.h"
#include <stdio.h>
#include <string.h>

static UiFonts s_fonts;
static bool s_fonts_loaded;

const UiFonts *ui_fonts(void) {
  if (!s_fonts_loaded) {
    s_fonts.header = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    s_fonts.small = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    s_fonts.small_bold = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    s_fonts.body = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    s_fonts.body_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    s_fonts.row_bold = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    s_fonts.title = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    s_fonts.big_number = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
    s_fonts.mid_number = fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS);
    s_fonts_loaded = true;
  }
  return &s_fonts;
}

GColor ui_trend_color(StockTrend trend) {
  switch (trend) {
    case STOCK_TREND_UP: return UI_COLOR_UP;
    case STOCK_TREND_DOWN: return UI_COLOR_DOWN;
    default: return UI_COLOR_FLAT;
  }
}

int16_t ui_text_width(const char *text, GFont font, int16_t max_w) {
  // Measure in a box far wider than the screen so the text never wraps, then
  // clamp: a wrapped measurement would under-report and pass a fit check.
  GSize size = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 2000, 80), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  return size.w > max_w ? max_w : size.w;
}

int16_t ui_text_width_raw(const char *text, GFont font) {
  GSize size = graphics_text_layout_get_content_size(
      text, font, GRect(0, 0, 2000, 80), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  return size.w;
}

static void format_time(char *buf, size_t size, time_t now) {
  struct tm *t = localtime(&now);
  if (!t) {
    buf[0] = '\0';
    return;
  }
  strftime(buf, size, clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
  // 12h clock: drop the leading zero ("09:41" -> "9:41").
  if (!clock_is_24h_style() && buf[0] == '0') {
    memmove(buf, buf + 1, strlen(buf));
  }
}

static void format_age(char *buf, size_t size, int minutes) {
  if (minutes < 0) {
    snprintf(buf, size, "STALE");
  } else if (minutes < 60) {
    snprintf(buf, size, "STALE %dm", minutes);
  } else if (minutes < 48 * 60) {
    snprintf(buf, size, "STALE %dh", minutes / 60);
  } else {
    snprintf(buf, size, "STALE %dd", minutes / (24 * 60));
  }
}

void ui_draw_header(GContext *ctx, GRect bounds, const char *title) {
  const UiFonts *f = ui_fonts();
  GRect bar = GRect(bounds.origin.x, bounds.origin.y, bounds.size.w, UI_HEADER_HEIGHT);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bar, 0, GCornerNone);

  time_t now = time(NULL);
  char clock[8];
  format_time(clock, sizeof(clock), now);

  const int16_t pad = 6;
  const int16_t text_y = bar.origin.y + 1;
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, title ? title : "", f->header,
                     GRect(bar.origin.x + pad, text_y, bar.size.w / 2, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, clock, f->header,
                     GRect(bar.origin.x + bar.size.w / 2, text_y, bar.size.w / 2 - pad, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

  char badge[16];
  GColor badge_color = GColorClear;
  if (model_is_stale(now)) {
    format_age(badge, sizeof(badge), model_age_minutes(now));
    badge_color = UI_COLOR_STALE;
  } else if (model_is_sample() && model_count() > 0) {
    snprintf(badge, sizeof(badge), "SAMPLE");
    badge_color = UI_COLOR_SAMPLE;
  }
  if (!gcolor_equal(badge_color, GColorClear)) {
    const int16_t w = ui_text_width(badge, f->small_bold, 90) + 10;
    GRect pill = GRect(bar.origin.x + (bar.size.w - w) / 2, bar.origin.y + 5, w, 16);
    graphics_context_set_fill_color(ctx, badge_color);
    graphics_fill_rect(ctx, pill, 3, GCornersAll);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, badge, f->small_bold, GRect(pill.origin.x, pill.origin.y - 2, pill.size.w, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

void ui_draw_pill(GContext *ctx, GRect rect, GColor fill, const char *text, GFont font) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, rect, rect.size.h / 3, GCornersAll);
  graphics_context_set_text_color(ctx, GColorWhite);
  GSize size = graphics_text_layout_get_content_size(text, font, rect, GTextOverflowModeTrailingEllipsis,
                                                     GTextAlignmentCenter);
  // Gothic glyphs sit low in their line box; nudge up to look centred.
  int16_t y = rect.origin.y + (rect.size.h - size.h) / 2 - size.h / 6;
  graphics_draw_text(ctx, text, font, GRect(rect.origin.x + 2, y, rect.size.w - 4, size.h + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
