#include "model.h"
#include <string.h>

// Persist layout. Bump PERSIST_VERSION whenever Stock or PersistMeta change shape;
// a mismatch discards the stored snapshot instead of misreading it.
#define PERSIST_KEY_VERSION 1
#define PERSIST_KEY_META 2
#define PERSIST_KEY_STOCK_BASE 100
#define PERSIST_VERSION 1

typedef struct {
  uint8_t count;
  uint8_t status;
  uint8_t sample;
  uint8_t reserved;
  int32_t updated;
  char err[STOCK_ERR_SIZE];
} PersistMeta;

_Static_assert(sizeof(Stock) <= PERSIST_DATA_MAX_LENGTH, "Stock too large for one persist key");
_Static_assert(sizeof(PersistMeta) <= PERSIST_DATA_MAX_LENGTH, "Meta too large for one persist key");

// Prices change every minute in market hours; don't rewrite 14 flash keys each
// time. Snapshots are saved at most this often, and always on exit.
#define PERSIST_MIN_INTERVAL_S (10 * 60)

static Stock s_stocks[STOCKS_MAX];
static PersistMeta s_meta;
static bool s_meta_dirty;
static bool s_stocks_dirty;
static time_t s_last_save;

static void terminate_stock(Stock *stock) {
  stock->symbol[STOCK_SYMBOL_SIZE - 1] = '\0';
  stock->name[STOCK_NAME_SIZE - 1] = '\0';
  stock->price[STOCK_PRICE_SIZE - 1] = '\0';
  stock->change[STOCK_CHANGE_SIZE - 1] = '\0';
  stock->percent[STOCK_PERCENT_SIZE - 1] = '\0';
}

static size_t utf8_sequence_length(unsigned char lead) {
  if (lead >= 0xF0) return 4;
  if (lead >= 0xE0) return 3;
  if (lead >= 0xC0) return 2;
  return 1;
}

bool model_copy_text(char *dst, size_t dst_size, const char *src, size_t src_max) {
  if (!dst || dst_size == 0) {
    return false;
  }
  dst[0] = '\0';
  if (!src) {
    return false;
  }
  const size_t limit = dst_size - 1;
  size_t end = 0;
  while (end < limit && end < src_max && src[end] != '\0') {
    const unsigned char c = (unsigned char)src[end];
    dst[end] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    end++;
  }
  // If the last character is incomplete (cut by the limit, or malformed input),
  // drop it so the font renderer never sees a partial UTF-8 sequence.
  size_t lead = end;
  size_t back = 0;
  while (lead > 0 && back < 4 && (((unsigned char)dst[lead - 1]) & 0xC0) == 0x80) {
    lead--;
    back++;
  }
  if (lead > 0) {
    const unsigned char lead_byte = (unsigned char)dst[lead - 1];
    if (lead_byte >= 0x80 && (lead - 1) + utf8_sequence_length(lead_byte) > end) {
      end = lead - 1;
    }
  } else if (back > 0) {
    end = 0;  // only stray continuation bytes
  }
  dst[end] = '\0';
  return end > 0;
}

static void clear_stocks(void) {
  memset(s_stocks, 0, sizeof(s_stocks));
  s_meta.count = 0;
}

static void save_meta(void) {
  persist_write_int(PERSIST_KEY_VERSION, PERSIST_VERSION);
  persist_write_data(PERSIST_KEY_META, &s_meta, sizeof(s_meta));
  s_meta_dirty = false;
}

static void save_all(void) {
  s_stocks_dirty = false;
  s_last_save = time(NULL);
  save_meta();
  for (int i = 0; i < STOCKS_MAX; i++) {
    uint32_t key = PERSIST_KEY_STOCK_BASE + (uint32_t)i;
    if (i < s_meta.count) {
      persist_write_data(key, &s_stocks[i], sizeof(Stock));
    } else if (persist_exists(key)) {
      persist_delete(key);
    }
  }
}

static bool load_all(void) {
  if (!persist_exists(PERSIST_KEY_VERSION) ||
      persist_read_int(PERSIST_KEY_VERSION) != PERSIST_VERSION) {
    return false;
  }
  if (persist_get_size(PERSIST_KEY_META) != (int)sizeof(PersistMeta)) {
    return false;
  }
  PersistMeta meta;
  if (persist_read_data(PERSIST_KEY_META, &meta, sizeof(meta)) != (int)sizeof(meta)) {
    return false;
  }
  if (meta.count > STOCKS_MAX || meta.status >= STOCKS_STATUS_COUNT) {
    return false;
  }
  meta.err[STOCK_ERR_SIZE - 1] = '\0';
  for (int i = 0; i < meta.count; i++) {
    uint32_t key = PERSIST_KEY_STOCK_BASE + (uint32_t)i;
    if (persist_get_size(key) != (int)sizeof(Stock) ||
        persist_read_data(key, &s_stocks[i], sizeof(Stock)) != (int)sizeof(Stock)) {
      return false;
    }
    terminate_stock(&s_stocks[i]);
  }
  s_meta = meta;
  return true;
}

void model_init(void) {
  memset(&s_meta, 0, sizeof(s_meta));
  clear_stocks();
  if (!load_all()) {
    memset(&s_meta, 0, sizeof(s_meta));
    clear_stocks();
    s_meta.status = STOCKS_STATUS_WAITING;
  }
  s_meta_dirty = false;
  s_stocks_dirty = false;
  s_last_save = 0;
}

void model_deinit(void) {
  if (s_stocks_dirty) {
    save_all();
  } else if (s_meta_dirty) {
    save_meta();
  }
}

static void save_soon(void) {
  if (s_last_save == 0 || time(NULL) - s_last_save >= PERSIST_MIN_INTERVAL_S) {
    save_all();
  } else {
    s_stocks_dirty = true;
  }
}

int model_count(void) {
  return s_meta.count;
}

const Stock *model_get(int index) {
  if (index < 0 || index >= s_meta.count) {
    return NULL;
  }
  return &s_stocks[index];
}

StocksStatus model_status(void) {
  return (StocksStatus)s_meta.status;
}

const char *model_error(void) {
  return s_meta.err;
}

bool model_is_sample(void) {
  return s_meta.sample != 0;
}

time_t model_updated(void) {
  return (time_t)s_meta.updated;
}

int model_age_minutes(time_t now) {
  if (s_meta.updated <= 0) {
    return -1;
  }
  time_t age = now - (time_t)s_meta.updated;
  if (age < 0) {
    return 0;
  }
  return (int)(age / 60);
}

bool model_is_stale(time_t now) {
  if (s_meta.count == 0) {
    return false;
  }
  if (s_meta.updated <= 0) {
    return true;
  }
  return (now - (time_t)s_meta.updated) > STOCKS_STALE_SECONDS;
}

StockTrend model_trend(const Stock *stock) {
  if (!stock) {
    return STOCK_TREND_FLAT;
  }
  const char *text = stock->percent[0] ? stock->percent : stock->change;
  // "+0.00%" is no movement: only a non-zero digit earns a colour.
  bool nonzero = false;
  for (const char *c = text; *c; c++) {
    if (*c >= '1' && *c <= '9') {
      nonzero = true;
      break;
    }
  }
  if (!nonzero) {
    return STOCK_TREND_FLAT;
  }
  unsigned char c0 = (unsigned char)text[0];
  if (c0 == '+') {
    return STOCK_TREND_UP;
  }
  // ASCII hyphen, or U+2212 MINUS SIGN (E2 88 92).
  if (c0 == '-' || (c0 == 0xE2 && (unsigned char)text[1] == 0x88 && (unsigned char)text[2] == 0x92)) {
    return STOCK_TREND_DOWN;
  }
  return STOCK_TREND_FLAT;
}

void model_set_snapshot(const Stock *stocks, int count, time_t updated, bool sample) {
  if (count < 0) {
    count = 0;
  }
  if (count > STOCKS_MAX) {
    count = STOCKS_MAX;
  }
  clear_stocks();
  for (int i = 0; i < count; i++) {
    s_stocks[i] = stocks[i];
    terminate_stock(&s_stocks[i]);
  }
  s_meta.count = (uint8_t)count;
  s_meta.status = (count > 0) ? STOCKS_STATUS_OK : STOCKS_STATUS_EMPTY;
  s_meta.sample = sample ? 1 : 0;
  s_meta.updated = (int32_t)updated;
  s_meta.err[0] = '\0';
  save_soon();
}

void model_set_updated(time_t updated) {
  if ((int32_t)updated == s_meta.updated) {
    return;
  }
  s_meta.updated = (int32_t)updated;
  // Heartbeats arrive about once a minute; write them to flash on exit only.
  s_meta_dirty = true;
}

void model_set_status(StocksStatus status, const char *err) {
  if (status >= STOCKS_STATUS_COUNT || status == STOCKS_STATUS_OK) {
    return;
  }
  s_meta.status = (uint8_t)status;
  model_copy_text(s_meta.err, sizeof(s_meta.err), err, STOCK_ERR_SIZE * 4);
  // ERROR and EMPTY keep the last good tickers (shown as stale): the Stocks
  // plugin drops tickers whose fetch failed, so a phone that's offline looks
  // exactly like an empty list. Only a missing plugin / API clears them.
  const bool keep = (status == STOCKS_STATUS_ERROR || status == STOCKS_STATUS_EMPTY) && s_meta.count > 0;
  if (!keep) {
    clear_stocks();
    s_meta.sample = 0;
    s_meta.updated = 0;
  }
  save_all();
}
