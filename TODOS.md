# TODO

## Implement full NimBLE NUS BLE transport
- **Why:** Enables wireless operation without USB cable. ble_transport.cc is currently a stub.
- **What:** Port m5-paper-buddy's ble_bridge.cpp to ESP-IDF NimBLE stack with Nordic UART Service (UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`), LE Secure Connections, bonding, passkey display.
- **Pros:** Full mobility, works from another machine, dual transport redundancy.
- **Cons:** ~200 lines of NimBLE boilerplate, BLE pairing UX complexity.
- **Context:** The rest of the firmware already does dual-write to USB+BLE (sendLine in main.cc). Only the BLE rx/tx paths need filling in. The firmware was designed for this from the start.
- **Depends on:** None. ble_transport.h/.cc interface is already defined.

## Add multi-option support for AskUserQuestion prompts
- **Why:** Claude Code's AskUserQuestion is a core UX pattern. Currently dead on the device. The bridge sends option_labels, the firmware ignores them.
- **What:** Parse `promptKind=='question'` and option labels from heartbeat JSON. Render a scrollable option list on the RLCD. Map KEY/BOOT to scroll/select. Extend state_parser.cc to parse options array. Add option selection UI to buddy_display.cc.
- **Pros:** Full Claude Code prompt support. Device can handle all prompt types.
- **Cons:** 2-button UI makes option selection clunky (scroll + select state machine). ~100 lines of display code + protocol changes.
- **Context:** buddy_display.cc hardcodes "KEY=approve BOOT=deny". The bridge's claude_code_bridge.py sends `option_labels` in the heartbeat JSON. TamaState needs an options array field.
- **Depends on:** None. Can be done independently.

## Make bridge daemon multi-threaded
- **Why:** A pending approval blocks ALL hooks for up to 30s. SessionStart, PostToolUse, Stop hooks queue behind it in the single-threaded HTTPServer.
- **What:** Switch from BaseHTTPRequestServer to ThreadingHTTPServer (one line change). Add threading.Lock around shared state (PENDING dict).
- **Pros:** Non-blocking hooks. Claude Code never waits on the daemon.
- **Cons:** Shared state needs thread safety. Minimal risk with a simple lock.
- **Context:** claude_code_bridge.py line ~815 does `event.wait(timeout=30)` inside the POST handler. The fix is `from http.server import ThreadingHTTPServer` instead of `HTTPServer`.
- **Depends on:** None. One-file change in the bridge daemon.
