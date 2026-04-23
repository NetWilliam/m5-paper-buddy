"""Transport abstraction for device I/O: USB serial and BLE (Nordic UART Service)."""

import asyncio
import subprocess
import sys
import threading
import time
from datetime import datetime


def log(*a, **kw):
    ts = datetime.now().isoformat(timespec="milliseconds")
    print(f"[{ts}]", *a, file=sys.stderr, flush=True, **kw)


# Nordic UART Service UUIDs — match the firmware's ble_bridge.cpp.
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # central → device (write)
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # device → central (notify)


class Transport:
    def start(self, on_byte, on_connect=None):
        raise NotImplementedError

    def write(self, data: bytes):
        raise NotImplementedError

    def connected(self) -> bool:
        raise NotImplementedError


class SerialTransport(Transport):
    def __init__(self, port):
        import serial

        self._port_name = port
        self.ser = serial.Serial(port, 115200, timeout=0.2)
        self._write_lock = threading.Lock()
        time.sleep(0.2)  # let the port settle before talking
        log(f"[serial] opened {port}")

    def start(self, on_byte, on_connect=None):
        if on_connect:
            on_connect()  # serial is "connected" as soon as the port opens
        threading.Thread(target=self._reader, args=(on_byte,), daemon=True).start()

    def _reader(self, on_byte):
        while True:
            try:
                chunk = self.ser.read(256)
            except Exception as e:
                log(f"[serial] read fail: {e}")
                time.sleep(1)
                continue
            for b in chunk:
                on_byte(b)

    def write(self, data: bytes):
        with self._write_lock:
            try:
                self.ser.write(data)
            except Exception as e:
                log(f"[serial] write fail: {e}")

    def connected(self):
        return True


class BLETransport(Transport):
    """BLE Central via bleak.

    Runs an asyncio event loop on a dedicated thread. Scans for a device
    advertising a name starting with the given prefix (default "Paper Buddy"),
    connects, subscribes to the Nordic UART TX characteristic for
    notifications, and exposes a thread-safe write() that marshals back onto
    the asyncio loop.

    Reconnects automatically on disconnect or scan failure.
    """

    def __init__(self, name_prefix="Paper Buddy", address=None):
        self._name_prefix = name_prefix
        self._address = address
        self._loop = None
        self._client = None
        self._thread = None
        self._on_byte = None
        self._on_connect = None
        self._connected_evt = threading.Event()
        self._write_lock = threading.Lock()
        self._attempt = 0

    def start(self, on_byte, on_connect=None):
        self._on_byte = on_byte
        self._on_connect = on_connect
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        try:
            self._loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self._loop)
            self._loop.run_until_complete(self._main())
        except Exception as e:
            log(f"[ble] thread crashed: {e!r}")

    @staticmethod
    async def _btctl(args: list[str], timeout: int = 10) -> str:
        """Run a bluetoothctl command and return its combined output."""
        loop = asyncio.get_event_loop()
        out = await loop.run_in_executor(None, lambda: subprocess.run(
            ["bluetoothctl", *args],
            capture_output=True, text=True, timeout=timeout,
        ))
        return out.stdout + out.stderr

    async def _scan(self, timeout: float = 12.0):
        """Scan and return (target_device | None, all_devices_dict)."""
        from bleak import BleakScanner

        all_devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
        target = None
        for _addr, (dev, _adv) in all_devs.items():
            if dev.name and dev.name.startswith(self._name_prefix):
                target = dev
                break

        return target, all_devs

    async def _main(self):
        try:
            from bleak import BleakScanner, BleakClient
            import importlib.metadata
            log(f"[ble] bleak {importlib.metadata.version('bleak')}")
        except ImportError:
            log("[ble] bleak not installed. run: pip install bleak")
            return

        while True:
            self._attempt += 1
            device = None

            # Remove stale BlueZ entry if we have a cached address
            if self._address:
                log(f"[ble] removing stale BlueZ entry for {self._address}")
                out = await self._btctl(["remove", self._address])
                if "not available" not in out.lower() and "not found" not in out.lower():
                    for line in out.strip().splitlines():
                        if line.strip():
                            log(f"[ble]   btctl: {line.strip()}")
                self._address = None

            # Scan with full visibility into results
            scan_timeout = 12.0
            log(f"[ble] scan #{self._attempt} (timeout={scan_timeout}s, filter='name starts with {self._name_prefix}')")
            try:
                device, all_devs = await self._scan(scan_timeout)
            except Exception as e:
                log(f"[ble] scan error: {e}")
                all_devs = {}

            # Log all discovered devices
            if all_devs:
                log(f"[ble] scan found {len(all_devs)} device(s):")
                for addr, (dev, adv) in all_devs.items():
                    name = dev.name or "(none)"
                    rssi = getattr(adv, "rssi", "?")
                    svcs = []
                    if hasattr(adv, "service_uuids") and adv.service_uuids:
                        svcs = [str(u) for u in adv.service_uuids[:3]]
                    svc_str = f" svc={svcs}" if svcs else ""
                    log(f"[ble]   {addr}  {name:30s}  RSSI={rssi}{svc_str}")
            else:
                log("[ble] scan found 0 devices")

            if not device:
                if self._attempt % 5 == 0:
                    log(f"[ble] {self._attempt} consecutive scan failures. Is the device powered on and advertising?")
                await asyncio.sleep(5)
                continue

            log(f"[ble] found target: {device.name} @ {device.address}")
            self._address = device.address

            # Connect
            try:
                client = BleakClient(device)
                log(f"[ble] connecting ...")
                await client.connect(timeout=10.0)

                # Log connection details
                mtu = client.mtu_size or "?"
                svcs = [str(s.uuid) for s in client.services]
                log(f"[ble] connected ({client.address}, MTU={mtu}, services={svcs})")

                self._client = client

                def _on_notify(_sender, data: bytearray):
                    for b in data:
                        self._on_byte(b)

                await client.start_notify(NUS_TX_UUID, _on_notify)

                self._connected_evt.set()
                self._attempt = 0  # reset on successful connect
                if self._on_connect:
                    threading.Thread(
                        target=self._on_connect,
                        daemon=True,
                        name="ble-handshake",
                    ).start()

                while client.is_connected:
                    await asyncio.sleep(1.0)
                log("[ble] link lost")
            except Exception as e:
                log(f"[ble] connect/notify error: {e!r}")
            finally:
                try:
                    await client.disconnect()
                except Exception:
                    pass
                self._client = None
                self._connected_evt.clear()

            await asyncio.sleep(2)

    def write(self, data: bytes):
        client = self._client
        if client is None or not client.is_connected:
            return
        with self._write_lock:
            # BLE GATT writes are limited to (MTU - 3) bytes per operation.
            chunk_size = max(20, (client.mtu_size or 23) - 3)
            offset = 0
            while offset < len(data):
                end = min(offset + chunk_size, len(data))
                chunk = data[offset:end]
                try:
                    fut = asyncio.run_coroutine_threadsafe(
                        client.write_gatt_char(NUS_RX_UUID, chunk, response=False),
                        self._loop,
                    )
                    fut.result(timeout=3)
                except Exception as e:
                    log(f"[ble] write fail ({len(data)}B chunk@{offset}): {e!r}")
                    return
                offset = end

    def connected(self):
        return self._connected_evt.is_set()
