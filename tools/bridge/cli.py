"""CLI utilities: transport selection, BLE diagnostics, timezone."""

import asyncio
import glob
import subprocess
import sys
import time
from datetime import datetime

from bridge.transport import BLETransport, SerialTransport, Transport, log


def tz_offset_seconds() -> int:
    now = time.time()
    local = datetime.fromtimestamp(now)
    utc_dt = datetime(*datetime.fromtimestamp(now, tz=None).utctimetuple()[:6])
    return int((local - utc_dt).total_seconds())


def _find_known_ble_addr(name_prefix: str) -> str | None:
    """Look up BlueZ's paired/known device list for a matching name.
    Returns the address if found, so we can skip scanning."""
    try:
        import subprocess
        out = subprocess.check_output(
            ["bluetoothctl", "devices"],
            timeout=5, text=True, stderr=subprocess.DEVNULL,
        )
        for line in out.strip().splitlines():
            parts = line.split("Device ", 1)
            if len(parts) < 2:
                continue
            rest = parts[1]
            addr, _, name = rest.partition(" ")
            if name.startswith(name_prefix):
                return addr
    except Exception:
        pass
    return None


def pick_transport(kind: str, ble_name: str = "Paper Buddy", ble_addr: str = None) -> Transport:
    """Resolve --transport flag to a concrete Transport. 'auto' tries
    serial first (zero-setup, no BLE permission dance) and falls back
    to BLE if no USB device is found."""
    candidates = sorted(
        glob.glob("/dev/cu.usbserial-*")
        + glob.glob("/dev/ttyUSB*")
        + glob.glob("/dev/ttyACM*")
    )

    if kind == "serial":
        if not candidates:
            sys.exit(
                "--transport serial requested but no serial device found (/dev/cu.usbserial-*, /dev/ttyUSB*, /dev/ttyACM*)"
            )
        return SerialTransport(candidates[0])

    if kind == "ble":
        return BLETransport(name_prefix=ble_name, address=ble_addr)

    # auto
    if candidates:
        log("[transport] serial device found, using USB")
        return SerialTransport(candidates[0])
    log("[transport] no serial device, falling back to BLE")
    return BLETransport(name_prefix=ble_name, address=ble_addr)


def ble_diag(name_prefix: str = "Paper Buddy"):
    """Run a one-shot BLE diagnostic scan and exit."""
    import subprocess

    try:
        import importlib.metadata
        log(f"[diag] bleak version: {importlib.metadata.version('bleak')}")
    except ImportError:
        log("[diag] bleak not installed")
        return

    try:
        out = subprocess.run(["bluetoothctl", "--version"],
                             capture_output=True, text=True, timeout=5)
        log(f"[diag] BlueZ version: {out.stdout.strip()}")
    except Exception:
        log("[diag] BlueZ version: unknown")

    try:
        out = subprocess.run(["hciconfig", "hci0"],
                             capture_output=True, text=True, timeout=5)
        for line in out.stdout.strip().splitlines():
            if "BD Address" in line or "HCI Version" in line:
                log(f"[diag] adapter: {line.strip()}")
    except Exception:
        pass

    from bleak import BleakScanner

    async def _scan():
        log("[diag] scanning 15s ...")
        devs = await BleakScanner.discover(timeout=15.0, return_adv=True)
        return devs

    all_devs = asyncio.run(_scan())
    target_found = False

    log(f"[diag] found {len(all_devs)} device(s):")
    for addr, (dev, adv) in sorted(all_devs.items(), key=lambda x: getattr(x[1][1], "rssi", -999), reverse=True):
        name = dev.name or "(none)"
        rssi = getattr(adv, "rssi", "?")
        svcs = []
        if hasattr(adv, "service_uuids") and adv.service_uuids:
            svcs = [str(u)[:8] + "..." for u in adv.service_uuids]
        is_target = name.startswith(name_prefix)
        marker = " <-- MATCH" if is_target else ""
        if is_target:
            target_found = True
        svc_str = f"  svc={svcs}" if svcs else ""
        log(f"[diag]   {addr}  RSSI={rssi:>4}  {name:30s}{svc_str}{marker}")

    if not target_found:
        log(f"[diag] no device matching '{name_prefix}*' found in scan")

    log("[diag] --- bluetoothctl known devices ---")
    try:
        out = subprocess.run(["bluetoothctl", "devices"],
                             capture_output=True, text=True, timeout=5)
        for line in out.stdout.strip().splitlines():
            if name_prefix in line or "Paper" in line:
                log(f"[diag]   {line.strip()}")
                parts = line.split("Device ", 1)
                if len(parts) >= 2:
                    addr = parts[1].split()[0]
                    info = subprocess.run(
                        ["bluetoothctl", "info", addr],
                        capture_output=True, text=True, timeout=5,
                    )
                    for iline in info.stdout.strip().splitlines():
                        il = iline.strip()
                        if any(k in il for k in ["Name:", "Alias:", "Paired:", "Connected:", "UUID:"]):
                            log(f"[diag]     {il}")
    except Exception as e:
        log(f"[diag] bluetoothctl error: {e}")
