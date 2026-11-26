// Pure helpers: turn a plugin envelope for finance/stock into a bounded, clean
// snapshot, and a snapshot into an AppMessage dictionary. No Pebble globals
// here so this file runs under plain Node for tests.

'use strict';

var MAX_TICKERS = 12;

// Byte budgets exclude the NUL terminator; they mirror the C buffer sizes
// in src/c/model.h (size - 1).
var LIMITS = {
  symbol: 11,
  name: 31,
  price: 15,
  change: 15,
  percent: 11,
  err: 23
};

var STATUS = {
  OK: 0,
  NO_PLUGIN: 1,
  EMPTY: 2,
  ERROR: 3,
  NO_API: 5
};

// C0/C1 controls, zero-width and bidi-override characters, BOM. Removing
// these keeps a hostile or garbled name from breaking the watch layout.
var UNSAFE_CHARS = /[\u0000-\u001F\u007F-\u009F\u00AD\u200B-\u200F\u2028-\u202E\u2060-\u206F\uFEFF]/g;

function utf8Length(codePoint) {
  if (codePoint < 0x80) return 1;
  if (codePoint < 0x800) return 2;
  if (codePoint < 0x10000) return 3;
  return 4;
}

/** Cuts `text` to at most `maxBytes` UTF-8 bytes without splitting a character. */
function truncateUtf8(text, maxBytes) {
  var out = '';
  var used = 0;
  for (var i = 0; i < text.length; i++) {
    var code = text.charCodeAt(i);
    var chunk = text.charAt(i);
    // Keep surrogate pairs together.
    if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.length) {
      var low = text.charCodeAt(i + 1);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        code = ((code - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
        chunk = text.substr(i, 2);
        i++;
      } else {
        continue;  // lone high surrogate: drop it
      }
    } else if (code >= 0xD800 && code <= 0xDFFF) {
      continue;  // lone surrogate: drop it
    }
    var size = utf8Length(code);
    if (used + size > maxBytes) break;
    out += chunk;
    used += size;
  }
  return out;
}

/** Returns a clean, length-capped string, or '' if `value` isn't usable text. */
function cleanText(value, maxBytes) {
  if (typeof value !== 'string') return '';
  var text = value.replace(UNSAFE_CHARS, ' ').replace(/\s+/g, ' ').trim();
  return truncateUtf8(text, maxBytes);
}

/** First text of the listed shapes on a plugin property, e.g. shortText. */
function readText(property, shapes) {
  if (!property || typeof property !== 'object') return '';
  for (var i = 0; i < shapes.length; i++) {
    var payload = property[shapes[i]];
    if (payload && typeof payload === 'object' && typeof payload.text === 'string') {
      return payload.text;
    }
  }
  return '';
}

/** One plugin instance -> { symbol, name, price, change, percent } or null. */
function stockFromInstance(instance) {
  if (!instance || typeof instance !== 'object') return null;
  var p = instance.properties;
  if (!p || typeof p !== 'object') return null;

  var symbol = cleanText(readText(p.ticker, ['shortText']), LIMITS.symbol) ||
    cleanText(typeof instance.instanceId === 'string' ? instance.instanceId : '', LIMITS.symbol);
  // Prefer shortText: the plugin's price longText also carries the % change.
  var price = cleanText(readText(p.price, ['shortText', 'longText']), LIMITS.price);
  if (!symbol || !price) return null;

  return {
    symbol: symbol,
    name: cleanText(readText(p.name, ['shortText', 'longText']), LIMITS.name) || symbol,
    price: price,
    change: cleanText(readText(p.day_change, ['shortText', 'longText']), LIMITS.change),
    percent: cleanText(readText(p.day_change_percent, ['shortText', 'longText']), LIMITS.percent)
  };
}

/** Envelope -> array of at most MAX_TICKERS stocks, in the plugin's order, no duplicate symbols. */
function stocksFromEnvelope(envelope) {
  var instances = envelope && Array.isArray(envelope.instances) ? envelope.instances : [];
  var seen = {};
  var stocks = [];
  for (var i = 0; i < instances.length && stocks.length < MAX_TICKERS; i++) {
    var stock = stockFromInstance(instances[i]);
    if (!stock || seen[stock.symbol]) continue;
    seen[stock.symbol] = true;
    stocks.push(stock);
  }
  return stocks;
}

/** Stable string for change detection: same text means nothing to resend. */
function fingerprint(stocks, sample) {
  return JSON.stringify({ s: sample ? 1 : 0, d: stocks });
}

/** Snapshot -> AppMessage dictionary keyed by numeric message key ids. */
function snapshotMessage(keys, stocks, updatedSec, sample) {
  var msg = {};
  msg[keys.status] = STATUS.OK;
  msg[keys.count] = stocks.length;
  msg[keys.updated] = updatedSec;
  msg[keys.sample] = sample ? 1 : 0;
  for (var i = 0; i < stocks.length; i++) {
    msg[keys.sym + i] = stocks[i].symbol;
    msg[keys.name + i] = stocks[i].name;
    msg[keys.price + i] = stocks[i].price;
    if (stocks[i].change) msg[keys.chg + i] = stocks[i].change;
    if (stocks[i].percent) msg[keys.pct + i] = stocks[i].percent;
  }
  return msg;
}

function statusMessage(keys, status, err) {
  var msg = {};
  msg[keys.status] = status;
  var text = cleanText(err || '', LIMITS.err);
  if (text) msg[keys.err] = text;
  return msg;
}

function heartbeatMessage(keys, updatedSec) {
  var msg = {};
  msg[keys.updated] = updatedSec;
  return msg;
}

/** Maps a plugin error code to a watch status. */
function statusForError(code) {
  return code === 'PLUGIN_UNAVAILABLE' ? STATUS.NO_PLUGIN : STATUS.ERROR;
}

module.exports = {
  MAX_TICKERS: MAX_TICKERS,
  LIMITS: LIMITS,
  STATUS: STATUS,
  cleanText: cleanText,
  truncateUtf8: truncateUtf8,
  stockFromInstance: stockFromInstance,
  stocksFromEnvelope: stocksFromEnvelope,
  fingerprint: fingerprint,
  snapshotMessage: snapshotMessage,
  statusMessage: statusMessage,
  heartbeatMessage: heartbeatMessage,
  statusForError: statusForError
};
