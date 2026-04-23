"""Line-based RX parsing and TX encoding for the device wire protocol."""

import json

from bridge.state import log


class LineProtocol:
    def __init__(self, state):
        self._state = state
        self._rx_buf = bytearray()

    def on_byte(self, b: int):
        if b in (0x0A, 0x0D):  # \n or \r
            if self._rx_buf:
                raw = bytes(self._rx_buf)
                self._rx_buf = bytearray()
                try:
                    line = raw.decode("utf-8", errors="replace")
                except Exception:
                    return
                log(f"[dev<] {line}")
                if not line.startswith("{"):
                    return
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    return
                cmd = obj.get("cmd")
                if cmd == "permission":
                    pid = obj.get("id")
                    h = self._state.pending.get(pid)
                    if h:
                        h["decision"] = obj.get("decision")
                        h["event"].set()
                elif cmd == "focus_session":
                    self._state.focused_sid = obj.get("sid") or None
                    self._state.bump.set()
        else:
            if len(self._rx_buf) < 4096:
                self._rx_buf.append(b)

    def send_line(self, obj: dict):
        if self._state.transport is None:
            return
        data = (json.dumps(obj, separators=(",", ":"), ensure_ascii=False) + "\n").encode()
        self._state.transport.write(data)
