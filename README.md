# Stocks Viewer for Pebble Time 2

A classic-Pebble-style watchapp that **displays the quotes served by the Pebble
"Stocks" plugin** ([store](https://apps.repebble.com/730a1d95229d42e9b66098cb)).
The plugin fetches prices on the phone but has no UI of its own; this app is
that UI.

Target: `emery` (Pebble Time 2, 200×228, 64 colours). SDK 4.44, pebble-tool ≥ 5.1.

## How it works

```
Stocks plugin (phone) --finance/stock--> PKJS (src/pkjs) --AppMessage--> watch (src/c)
```

- **PKJS** subscribes with `Pebble.subscribeToSource({ category: 'finance', item: 'stock' })`.
  It makes no network requests itself (no `Internet` permission) and collects nothing.
  Each envelope is validated (`src/pkjs/snapshot.js`): text shapes only, control /
  zero-width / bidi characters stripped, UTF-8 byte caps matching the C buffers,
  max 12 tickers, duplicates dropped, symbol + price required.
- Messages go through a queue (`src/pkjs/queue.js`): one in flight, retry with
  backoff on NACK, newer state replaces queued state. A snapshot is sent only
  when the content changes; otherwise a tiny "still fresh" heartbeat at most once
  every 50 s.
- **Watch** (`src/c`): MenuLayer list (symbol, price, name, % change pill: green up,
  red down, gray flat), detail window (big LECO price, change and %, UP/DOWN cycles
  tickers), black header with the time (minute tick), **STALE** badge when the last
  update is older than 3 minutes, and a setup screen when there is no data.
- The last snapshot is kept in persistent storage (versioned keys, size-checked on
  read) so the app opens instantly, even without the phone.

Ticker selection lives in the **Stocks plugin's own settings** in the Pebble app.

## Build and run

```sh
uv tool install pebble-tool && pebble sdk install latest
pebble build
pebble install --emulator emery
npm test            # PKJS unit tests (Node ≥ 18)
```

### Emulator sample data

The real plugin needs a phone, so `tools/sample-plugin` is a separate plugin-only
pbw that serves **fixed, made-up quotes** (every name ends in "(sample)"). When it
is the plugin answering, the watch shows a **SAMPLE** badge. It is never bundled
into the app pbw.

```sh
(cd tools/sample-plugin && pebble build && pebble install --emulator emery)
pebble install --emulator emery
```

## Messages (phone → watch)

| Kind | Keys |
| --- | --- |
| Snapshot | `status`=0, `count`, `updated` (epoch s), `sample`, `sym[i]`, `name[i]`, `price[i]`, `chg[i]`, `pct[i]` |
| Status | `status` = 1 no plugin, 2 empty, 3 error (+ `err` code), 5 no plugin API |
| Heartbeat | `updated` only |

The watch sends `hello` on launch; PKJS answers with its latest state.

## Known limits

- Prices are the plugin's preformatted strings (no numeric shape), so colour comes
  from the leading `+` / `-` of the % change.
- If every quote fails (phone offline), the plugin reports zero tickers. The watch
  then keeps the last prices with a STALE badge instead of clearing them, which also
  means removing every ticker shows old data until a new ticker is added.
- Prices come from Yahoo Finance through the Stocks plugin's unofficial connection.
