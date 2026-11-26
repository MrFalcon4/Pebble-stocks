# SAMPLE Stocks fixture (emulator only)

A plugin-only pbw that serves fixed, made-up quotes on `finance/stock`, so the
Stocks Viewer can be screenshotted in the emulator, where the real Stocks plugin
has no phone to run on. The viewer shows a **SAMPLE** badge whenever this
fixture (uuid `5a3f9c2e-…8f13`) is the plugin answering. Never publish it.

    cd tools/sample-plugin && pebble build && pebble install --emulator emery
