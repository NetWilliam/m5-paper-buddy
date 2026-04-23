#!/usr/bin/env python3
"""Bridge Claude Code ↔ M5Paper buddy.

Stands in for the Claude Desktop app. Hook flow:

  Claude Code hook  ──POST──▶  this daemon  ──serial/BLE──▶  M5Paper
                                     ▲                           │
                                     └───── permission ack ──────┘

Two transports:
  - USB serial: zero-setup, autodetects /dev/cu.usbserial-*.
  - BLE (Nordic UART Service via bleak): wireless. First connect triggers
    macOS's system pairing dialog — enter the 6-digit passkey shown on
    the Paper. After that, the daemon auto-reconnects whenever both sides
    are alive.

Heartbeat extensions vs the stock desktop protocol (firmware ignores
unknown fields, so this stays backward compatible):
  project / branch / dirty   — session's git context
  budget                      — daily token budget bar
  model                       — current Claude model
  assistant_msg               — last prose reply pulled from transcript
  prompt.body                 — full approval content (diff / full command)
  prompt.kind                 — "permission" or "question"
  prompt.options              — AskUserQuestion options (rendered as buttons)

Usage:
    python3 tools/claude_code_bridge.py                    # auto: serial first, else BLE
    python3 tools/claude_code_bridge.py --transport ble    # force BLE
    python3 tools/claude_code_bridge.py --transport serial # force serial
    python3 tools/claude_code_bridge.py --budget 1000000
"""

import argparse
import os
import sys
import threading
import time
from http.server import HTTPServer

from bridge.state import DaemonState, log
from bridge.transport import SerialTransport
from bridge.protocol import LineProtocol
from bridge.heartbeat import build_heartbeat, heartbeat_loop
from bridge.hooks import HookHandler
from bridge.cli import pick_transport, ble_diag, tz_offset_seconds

state = DaemonState()
protocol = LineProtocol(state)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="explicit serial port (implies --transport serial)")
    ap.add_argument("--transport", choices=("auto", "serial", "ble"), default="auto")
    ap.add_argument("--ble-name", default="Paper Buddy",
                    help="BLE device name to scan for (default: Paper Buddy)")
    ap.add_argument("--ble-addr", default=None,
                    help="BLE device address for fast reconnect (e.g. 44:1B:F6:CC:44:2A)")
    ap.add_argument("--ble-diag", action="store_true",
                    help="run BLE diagnostic scan and exit")
    ap.add_argument("--http-port", type=int, default=9876)
    ap.add_argument("--owner", default=os.environ.get("USER", ""))
    ap.add_argument(
        "--budget",
        type=int,
        default=200000,
        help="context-window limit for the budget bar (default 200K = "
        "Claude 4.6 standard context; set 1000000 for 1M-context "
        "beta; set 0 to hide the bar)",
    )
    args = ap.parse_args()

    if args.ble_diag:
        ble_diag(args.ble_name)
        return

    state.budget_limit = max(0, args.budget)

    if args.port:
        state.transport = SerialTransport(args.port)
    else:
        state.transport = pick_transport(args.transport, args.ble_name, args.ble_addr)

    def _handshake():
        if args.owner:
            protocol.send_line({"cmd": "owner", "name": args.owner})
        protocol.send_line({"time": [int(time.time()), tz_offset_seconds()]})
        protocol.send_line(build_heartbeat(state))

    state.transport.start(protocol.on_byte, on_connect=_handshake)
    threading.Thread(target=heartbeat_loop, args=(state, protocol), daemon=True).start()

    HookHandler.state = state
    HookHandler.protocol = protocol
    srv = HTTPServer(("127.0.0.1", args.http_port), HookHandler)
    log(f"[http] listening on 127.0.0.1:{args.http_port}  budget={state.budget_limit}")
    log("[ready] start a Claude Code session with the hooks installed")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        log("\n[exit] bye")


if __name__ == "__main__":
    main()
