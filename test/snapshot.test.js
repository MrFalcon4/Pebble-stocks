'use strict';
var test = require('node:test');
var assert = require('node:assert');
var snap = require('../src/pkjs/snapshot');
var createQueue = require('../src/pkjs/queue').createQueue;

function bytes(s) { return Buffer.byteLength(s, 'utf8'); }

var KEYS = { status: 1, count: 2, updated: 3, sample: 4, err: 5, sym: 100, name: 200, price: 300, chg: 400, pct: 500 };

function instance(sym, extra) {
  var p = {
    ticker: { shortText: { text: sym } },
    name: { shortText: { text: sym + ' Inc' } },
    price: { shortText: { text: '$1.00' }, longText: { text: '$1.00  +1.00%' } },
    day_change: { shortText: { text: '+$0.01' } },
    day_change_percent: { shortText: { text: '+1.00%' } }
  };
  Object.keys(extra || {}).forEach(function (k) { p[k] = extra[k]; });
  return { instanceId: sym, properties: p };
}

test('reads the plugin shapes and prefers shortText price', function () {
  var s = snap.stocksFromEnvelope({ instances: [instance('AAPL')] });
  assert.deepStrictEqual(s, [{ symbol: 'AAPL', name: 'AAPL Inc', price: '$1.00', change: '+$0.01', percent: '+1.00%' }]);
});

test('caps the ticker count and drops duplicates and junk', function () {
  var list = [null, 7, { properties: null }, instance('DUP'), instance('DUP')];
  for (var i = 0; i < 30; i++) list.push(instance('T' + i));
  var s = snap.stocksFromEnvelope({ instances: list });
  assert.strictEqual(s.length, snap.MAX_TICKERS);
  assert.strictEqual(s[0].symbol, 'DUP');
  assert.strictEqual(s[1].symbol, 'T0');
});

test('requires symbol and price, falls back to instanceId and symbol', function () {
  assert.strictEqual(snap.stockFromInstance(instance('X', { price: undefined })), null);
  var noTicker = instance('Y', { ticker: { shortText: { text: 42 } }, name: undefined });
  var s = snap.stockFromInstance(noTicker);
  assert.strictEqual(s.symbol, 'Y');
  assert.strictEqual(s.name, 'Y');
});

test('strips control and bidi characters and caps UTF-8 bytes without splitting', function () {
  var evil = 'A\u0000B\u202EC\n\tD';
  assert.strictEqual(snap.cleanText(evil, 31), 'A B C D');
  var euro = snap.cleanText('€€€€€€', 11);  // 3 bytes each
  assert.strictEqual(euro, '€€€');
  assert.ok(bytes(euro) <= 11);
  var emoji = snap.cleanText('ab😀😀😀', 7);
  assert.strictEqual(emoji, 'ab😀');
  assert.strictEqual(snap.cleanText('\uD800lone', 10), 'lone');
  assert.strictEqual(snap.cleanText({ text: 'x' }, 10), '');
});

test('every field respects its C buffer budget', function () {
  var long = new Array(200).join('Ω');
  var s = snap.stockFromInstance(instance(long, {
    name: { shortText: { text: long } }, price: { shortText: { text: long } },
    day_change: { shortText: { text: long } }, day_change_percent: { shortText: { text: long } }
  }));
  assert.ok(bytes(s.symbol) <= 11 && bytes(s.name) <= 31 && bytes(s.price) <= 15);
  assert.ok(bytes(s.change) <= 15 && bytes(s.percent) <= 11);
});

test('snapshot message uses array key offsets', function () {
  var stocks = snap.stocksFromEnvelope({ instances: [instance('A'), instance('B')] });
  var m = snap.snapshotMessage(KEYS, stocks, 1000, true);
  assert.strictEqual(m[1], 0);
  assert.strictEqual(m[2], 2);
  assert.strictEqual(m[4], 1);
  assert.strictEqual(m[101], 'B');
  assert.strictEqual(m[301], '$1.00');
});

test('error codes map to statuses', function () {
  assert.strictEqual(snap.statusForError('PLUGIN_UNAVAILABLE'), snap.STATUS.NO_PLUGIN);
  assert.strictEqual(snap.statusForError('PERMISSION_DENIED'), snap.STATUS.ERROR);
});

test('queue retries on nack, then moves on; coalesces states and heartbeats', function () {
  var sent = [];
  var timers = [];
  var replies = [];
  var q = createQueue(function (m, ack, nack) { sent.push(m); replies.push({ ack: ack, nack: nack }); },
    function () {}, function (fn) { timers.push(fn); });
  q.enqueue('state', 's1');
  q.enqueue('state', 's2');       // replaces nothing in flight, queues behind s1
  q.enqueue('state', 's3');       // supersedes s2
  q.enqueue('heartbeat', 'h1');   // dropped: a state is queued
  assert.deepStrictEqual(sent, ['s1']);
  replies[0].nack({});
  timers.shift()();
  assert.deepStrictEqual(sent, ['s1', 's1']);
  replies[1].ack();
  assert.deepStrictEqual(sent, ['s1', 's1', 's3']);
  replies[2].ack();
  assert.strictEqual(q.size(), 0);
  q.enqueue('heartbeat', 'h2');
  replies[3].ack();
  // Five nacks in a row: give up on h3 and continue with what follows.
  q.enqueue('heartbeat', 'h3');
  q.enqueue('state', 's4');
  for (var i = 0; i < 4; i++) { replies[replies.length - 1].nack({}); timers.shift()(); }
  replies[replies.length - 1].nack({});
  assert.strictEqual(sent[sent.length - 1], 's4');
});
