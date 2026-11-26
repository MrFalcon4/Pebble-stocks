// A small AppMessage queue: one message in flight, retry with backoff on NACK,
// and coalescing so a burst of updates never floods the watch.

'use strict';

var MAX_RETRIES = 4;
var BASE_DELAY_MS = 400;

/**
 * kinds: 'state' (a full snapshot or a status; a newer one replaces any queued
 * one) and 'heartbeat' (only the latest is kept; dropped if a state is queued,
 * since a state message carries its own timestamp).
 */
function createQueue(send, log, schedule) {
  var pending = [];
  var inFlight = false;
  var attempts = 0;
  schedule = schedule || setTimeout;

  function pump() {
    if (inFlight || pending.length === 0) return;
    inFlight = true;
    var item = pending[0];
    send(item.message, function onAck() {
      inFlight = false;
      attempts = 0;
      if (pending[0] === item) pending.shift();
      pump();
    }, function onNack(error) {
      inFlight = false;
      attempts++;
      if (attempts > MAX_RETRIES) {
        log('dropping ' + item.kind + ' after ' + MAX_RETRIES + ' retries: ' + describe(error));
        attempts = 0;
        if (pending[0] === item) pending.shift();
        pump();
        return;
      }
      var delay = BASE_DELAY_MS * Math.pow(2, attempts - 1);
      log('send failed (' + describe(error) + '), retry ' + attempts + ' in ' + delay + 'ms');
      schedule(pump, delay);
    });
  }

  function describe(error) {
    try {
      return error && error.error && error.error.message ? error.error.message : JSON.stringify(error);
    } catch (e) {
      return String(error);
    }
  }

  function enqueue(kind, message) {
    // Never replace the item currently being sent (index 0 while in flight).
    var start = inFlight ? 1 : 0;
    if (kind === 'state') {
      pending = pending.slice(0, start).concat(pending.slice(start).filter(function (item) {
        return item.kind !== 'state' && item.kind !== 'heartbeat';
      }));
    } else if (kind === 'heartbeat') {
      for (var i = start; i < pending.length; i++) {
        if (pending[i].kind === 'state') return;  // the queued state is newer anyway
      }
      pending = pending.slice(0, start).concat(pending.slice(start).filter(function (item) {
        return item.kind !== 'heartbeat';
      }));
    }
    pending.push({ kind: kind, message: message });
    pump();
  }

  return { enqueue: enqueue, size: function () { return pending.length; } };
}

module.exports = { createQueue: createQueue };
