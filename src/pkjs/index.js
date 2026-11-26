// Stocks Viewer, phone side. Subscribes to finance/stock (served by the Pebble
// "Stocks" plugin, or whichever plugin the user prefers for that item) and
// forwards a validated snapshot to the watch. Makes no network requests of its
// own and collects nothing.

'use strict';

var keys = require('message_keys');
var snap = require('./snapshot');
var createQueue = require('./queue').createQueue;

// Our own emulator-only fixture plugin (tools/sample-plugin). If it is the one
// answering, the watch labels everything SAMPLE. It never ships with this app.
var SAMPLE_PLUGIN_UUID = '5a3f9c2e-7b1d-4e8a-9c6f-0d2b4a6e8f13';
// Send a "still fresh" heartbeat at most this often when prices haven't changed.
var HEARTBEAT_MIN_MS = 50 * 1000;
// If the plugin host says nothing at all for this long, tell the watch.
var FIRST_DATA_TIMEOUT_MS = 30 * 1000;

var queue = createQueue(function (message, ack, nack) {
  Pebble.sendAppMessage(message, ack, nack);
}, log);

var lastFingerprint = null;
var lastSentAt = 0;
var lastState = null;     // the last state message, resent when the watch says hello
var subscription = null;
var firstDataTimer = null;

function log(text) {
  console.log('[StocksViewer] ' + text);
}

function nowSec() {
  return Math.floor(Date.now() / 1000);
}

function sendState(message, fingerprint) {
  lastState = message;
  lastFingerprint = fingerprint;
  lastSentAt = Date.now();
  queue.enqueue('state', message);
}

function onData(envelope) {
  clearFirstDataTimer();
  var stocks = snap.stocksFromEnvelope(envelope);
  var sample = !!envelope && envelope.pluginUuid === SAMPLE_PLUGIN_UUID;
  if (stocks.length === 0) {
    var emptyPrint = 'empty';
    if (lastFingerprint !== emptyPrint) {
      sendState(snap.statusMessage(keys, snap.STATUS.EMPTY, ''), emptyPrint);
    }
    return;
  }
  var print = snap.fingerprint(stocks, sample);
  if (print !== lastFingerprint) {
    log('sending ' + stocks.length + ' tickers');
    sendState(snap.snapshotMessage(keys, stocks, nowSec(), sample), print);
  } else if (Date.now() - lastSentAt >= HEARTBEAT_MIN_MS) {
    lastSentAt = Date.now();
    queue.enqueue('heartbeat', snap.heartbeatMessage(keys, nowSec()));
  }
}

function onError(err) {
  clearFirstDataTimer();
  var code = err && typeof err.code === 'string' ? err.code : 'UNKNOWN';
  log('finance/stock error: ' + code);
  var status = snap.statusForError(code);
  var print = 'status:' + status + ':' + code;
  if (print !== lastFingerprint) {
    sendState(snap.statusMessage(keys, status, code), print);
  }
}

function clearFirstDataTimer() {
  if (firstDataTimer) {
    clearTimeout(firstDataTimer);
    firstDataTimer = null;
  }
}

function subscribe() {
  if (subscription) return;
  if (typeof Pebble.subscribeToSource !== 'function') {
    log('this Pebble app has no plugin API');
    sendState(snap.statusMessage(keys, snap.STATUS.NO_API, ''), 'no-api');
    return;
  }
  try {
    subscription = Pebble.subscribeToSource({
      category: 'finance',
      item: 'stock',
      properties: ['ticker', 'name', 'price', 'day_change', 'day_change_percent'],
      limit: snap.MAX_TICKERS,
      onData: onData,
      onError: onError
    });
  } catch (e) {
    log('subscribe failed: ' + e);
    sendState(snap.statusMessage(keys, snap.STATUS.ERROR, 'SUBSCRIBE'), 'subscribe-failed');
    return;
  }
  firstDataTimer = setTimeout(function () {
    firstDataTimer = null;
    if (lastFingerprint === null) onError({ code: 'TIMEOUT' });
  }, FIRST_DATA_TIMEOUT_MS);
}

Pebble.addEventListener('ready', function () {
  log('ready');
  subscribe();
});

// The watch says hello when it starts (it may have restarted while we kept
// running): resend the latest state so it doesn't wait for the next change.
Pebble.addEventListener('appmessage', function (e) {
  var payload = (e && e.payload) || {};
  if (payload.hello === undefined && payload[keys.hello] === undefined) return;
  if (lastState) {
    lastSentAt = Date.now();
    queue.enqueue('state', lastState);
  }
});
