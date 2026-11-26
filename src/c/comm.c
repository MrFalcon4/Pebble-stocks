#include "comm.h"
#include "model.h"
#include <string.h>

// Worst case snapshot: 12 tickers x (5 tuples x 7 header bytes + 88 data bytes)
// plus 6 small tuples, about 1.6 KB. 2 KB leaves headroom.
#define INBOX_SIZE 2048
#define OUTBOX_SIZE 64
#define HELLO_RETRY_MS 2000
#define HELLO_MAX_ATTEMPTS 3

static CommChangedHandler s_on_changed;
static AppTimer *s_hello_timer;
static int s_hello_attempts;
// Static scratch space: too large for the app stack.
static Stock s_incoming[STOCKS_MAX];

static bool read_int(DictionaryIterator *iter, uint32_t key, int32_t *out) {
  Tuple *t = dict_find(iter, key);
  if (!t) {
    return false;
  }
  if (t->type == TUPLE_INT || t->type == TUPLE_UINT) {
    switch (t->length) {
      case 1: *out = (t->type == TUPLE_INT) ? t->value->int8 : (int32_t)t->value->uint8; return true;
      case 2: *out = (t->type == TUPLE_INT) ? t->value->int16 : (int32_t)t->value->uint16; return true;
      case 4: *out = (t->type == TUPLE_INT) ? t->value->int32 : (int32_t)t->value->uint32; return true;
      default: return false;
    }
  }
  return false;
}

// Copies a CSTRING tuple into dst. The tuple's length bounds the read, so a
// missing NUL from the phone can't run past the message buffer.
static bool read_text(DictionaryIterator *iter, uint32_t key, char *dst, size_t dst_size) {
  dst[0] = '\0';
  Tuple *t = dict_find(iter, key);
  if (!t || t->type != TUPLE_CSTRING) {
    return false;
  }
  return model_copy_text(dst, dst_size, t->value->cstring, t->length);
}

static bool parse_snapshot(DictionaryIterator *iter, int32_t count, int32_t updated, bool sample) {
  if (count < 0 || count > STOCKS_MAX) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Rejected snapshot: count %d", (int)count);
    return false;
  }
  int kept = 0;
  for (int32_t i = 0; i < count; i++) {
    Stock *s = &s_incoming[kept];
    memset(s, 0, sizeof(*s));
    bool ok = read_text(iter, MESSAGE_KEY_sym + (uint32_t)i, s->symbol, sizeof(s->symbol));
    ok = read_text(iter, MESSAGE_KEY_price + (uint32_t)i, s->price, sizeof(s->price)) && ok;
    if (!ok) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Dropped ticker %d: missing symbol or price", (int)i);
      continue;
    }
    if (!read_text(iter, MESSAGE_KEY_name + (uint32_t)i, s->name, sizeof(s->name))) {
      model_copy_text(s->name, sizeof(s->name), s->symbol, sizeof(s->symbol));
    }
    read_text(iter, MESSAGE_KEY_chg + (uint32_t)i, s->change, sizeof(s->change));
    read_text(iter, MESSAGE_KEY_pct + (uint32_t)i, s->percent, sizeof(s->percent));
    kept++;
  }
  model_set_snapshot(s_incoming, kept, (time_t)updated, sample);
  return true;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  int32_t status = -1;
  int32_t updated = 0;
  int32_t count = 0;
  int32_t sample = 0;
  bool has_status = read_int(iter, MESSAGE_KEY_status, &status);
  bool has_updated = read_int(iter, MESSAGE_KEY_updated, &updated);
  bool changed = false;

  if (has_status) {
    // Any message carrying a status proves the phone side is up.
    if (s_hello_timer) {
      app_timer_cancel(s_hello_timer);
      s_hello_timer = NULL;
    }
    if (status == STOCKS_STATUS_OK) {
      if (read_int(iter, MESSAGE_KEY_count, &count)) {
        read_int(iter, MESSAGE_KEY_sample, &sample);
        changed = parse_snapshot(iter, count, has_updated ? updated : (int32_t)time(NULL), sample != 0);
      }
    } else if (status > STOCKS_STATUS_OK && status < STOCKS_STATUS_COUNT &&
               status != STOCKS_STATUS_WAITING) {
      char err[STOCK_ERR_SIZE];
      read_text(iter, MESSAGE_KEY_err, err, sizeof(err));
      model_set_status((StocksStatus)status, err);
      changed = true;
    } else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Ignored unknown status %d", (int)status);
    }
  } else if (has_updated && updated > 0) {
    // Heartbeat: same data, confirmed fresh.
    model_set_updated((time_t)updated);
    changed = true;
  }

  if (changed && s_on_changed) {
    s_on_changed();
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox dropped: %d", (int)reason);
}

static void send_hello(void);

static void hello_timer_fired(void *data) {
  s_hello_timer = NULL;
  send_hello();
}

static void send_hello(void) {
  if (s_hello_attempts >= HELLO_MAX_ATTEMPTS) {
    return;
  }
  s_hello_attempts++;
  DictionaryIterator *out = NULL;
  if (app_message_outbox_begin(&out) == APP_MSG_OK && out) {
    dict_write_uint8(out, MESSAGE_KEY_hello, 1);
    app_message_outbox_send();
  }
  // Retry until the phone answers with a status (cancelled in inbox_received).
  s_hello_timer = app_timer_register(HELLO_RETRY_MS, hello_timer_fired, NULL);
}

void comm_init(CommChangedHandler on_changed) {
  s_on_changed = on_changed;
  s_hello_attempts = 0;
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  uint32_t inbox = app_message_inbox_size_maximum();
  if (inbox > INBOX_SIZE) {
    inbox = INBOX_SIZE;
  }
  app_message_open(inbox, OUTBOX_SIZE);
  send_hello();
}

void comm_deinit(void) {
  if (s_hello_timer) {
    app_timer_cancel(s_hello_timer);
    s_hello_timer = NULL;
  }
  app_message_deregister_callbacks();
  s_on_changed = NULL;
}
