#pragma once
#include <pebble.h>

// Hard cap on tickers shown; matches the messageKeys array sizes in package.json.
#define STOCKS_MAX 12

// Buffer sizes include the terminating NUL. PKJS truncates to (size - 1) bytes.
#define STOCK_SYMBOL_SIZE 12
#define STOCK_NAME_SIZE 32
#define STOCK_PRICE_SIZE 16
#define STOCK_CHANGE_SIZE 16
#define STOCK_PERCENT_SIZE 12
#define STOCK_ERR_SIZE 24

// Data older than this is flagged as stale.
#define STOCKS_STALE_SECONDS (3 * 60)

typedef enum {
  STOCKS_STATUS_OK = 0,         // snapshot holds live tickers
  STOCKS_STATUS_NO_PLUGIN = 1,  // no installed plugin serves finance/stock
  STOCKS_STATUS_EMPTY = 2,      // plugin answered with zero tickers
  STOCKS_STATUS_ERROR = 3,      // subscription error; err holds the code
  STOCKS_STATUS_WAITING = 4,    // nothing heard from the phone yet (local only)
  STOCKS_STATUS_NO_API = 5,     // Pebble app too old for the plugin API
  STOCKS_STATUS_COUNT
} StocksStatus;

typedef enum {
  STOCK_TREND_FLAT = 0,
  STOCK_TREND_UP = 1,
  STOCK_TREND_DOWN = -1,
} StockTrend;

typedef struct {
  char symbol[STOCK_SYMBOL_SIZE];
  char name[STOCK_NAME_SIZE];
  char price[STOCK_PRICE_SIZE];
  char change[STOCK_CHANGE_SIZE];
  char percent[STOCK_PERCENT_SIZE];
} Stock;

void model_init(void);
void model_deinit(void);

int model_count(void);
// Returns NULL when index is out of range.
const Stock *model_get(int index);
StocksStatus model_status(void);
const char *model_error(void);
bool model_is_sample(void);
time_t model_updated(void);

bool model_is_stale(time_t now);
// Whole minutes since the last update, or -1 if never updated.
int model_age_minutes(time_t now);

StockTrend model_trend(const Stock *stock);

// Replaces the snapshot (count clamped to STOCKS_MAX) and persists it.
void model_set_snapshot(const Stock *stocks, int count, time_t updated, bool sample);
// Marks the existing snapshot as confirmed fresh at `updated`.
void model_set_updated(time_t updated);
// Records a non-OK status. NO_PLUGIN / NO_API clear the tickers; ERROR and
// EMPTY keep the last good tickers (if any) so they show as stale.
void model_set_status(StocksStatus status, const char *err);

// Copies src into dst (dst_size bytes incl. NUL): never overflows, always
// NUL-terminates, never splits a UTF-8 sequence, replaces control bytes with
// spaces. Returns false if src was NULL or empty after copying.
bool model_copy_text(char *dst, size_t dst_size, const char *src, size_t src_max);
