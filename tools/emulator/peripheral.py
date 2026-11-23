"""Bite Cube BLE peripheral emulator.

Advertises like a cube and exposes FFF0 / FFF1 (notify) / FFF2 (write).
Streams the receiver, alarm snapshot and drift messages every 15 s, answers
data requests (DATA_REQ), applies settings (COMMAND_INT) and acknowledges
them (COMMAND_ACK); bites and drop-backs are injected from the console.
Frames and CRC follow docs/PROTOCOL.md.

Implementation: the `bless` BLE GATT server (BlueZ on Linux, CoreBluetooth on
macOS, WinRT on Windows).

Usage:
    pip install bless
    python3 peripheral.py
"""
from __future__ import annotations

import asyncio
import logging
import sys

try:
    from bless import (
        BlessGATTCharacteristic,
        BlessServer,
        GATTAttributePermissions,
        GATTCharacteristicProperties,
    )
except ImportError:  # pragma: no cover
    print("pip install bless")
    raise

from crc import check
from packets import (
    MSG_COMMAND,
    MSG_DATA_REQ,
    REQ_ALARMS,
    REQ_ALL_ALARMS,
    REQ_RECEIVER,
    REQ_REGION,
    EmulatorState,
    apply_command,
    build_ack,
    build_alarm_event,
    build_receiver,
    build_snapshot,
    build_wave,
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("cube-emu")

SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
FFF1_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
FFF2_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"

DEVICE_NAME = "Bite CubeR-EMU00001"


class CubeEmulator:
    def __init__(self):
        self.state = EmulatorState()
        self.server: BlessServer | None = None
        self.loop: asyncio.AbstractEventLoop | None = None

    async def start(self):
        self.loop = asyncio.get_event_loop()
        self.server = BlessServer(name=DEVICE_NAME, loop=self.loop)
        self.server.read_request_func = self._on_read
        self.server.write_request_func = self._on_write
        await self.server.add_new_service(SERVICE_UUID)
        await self.server.add_new_characteristic(
            SERVICE_UUID, FFF1_UUID,
            GATTCharacteristicProperties.notify | GATTCharacteristicProperties.read,
            None, GATTAttributePermissions.readable,
        )
        await self.server.add_new_characteristic(
            SERVICE_UUID, FFF2_UUID,
            GATTCharacteristicProperties.write | GATTCharacteristicProperties.write_without_response,
            None, GATTAttributePermissions.writeable,
        )
        await self.server.start()
        log.info("advertising as %s", DEVICE_NAME)
        try:
            await asyncio.gather(self._periodic_loop(), self._console_loop())
        except KeyboardInterrupt:
            pass
        finally:
            await self.server.stop()

    # ── periodic emission ─────────────────────────────────────────────
    async def _periodic_loop(self):
        while True:
            try:
                await self._notify(build_receiver(self.state))
                await asyncio.sleep(0.05)
                await self._send_snapshot()
                await self._notify(build_wave(self.state))
            except Exception as e:  # noqa: BLE001
                log.warning("periodic emit failed: %s", e)
            await asyncio.sleep(15)

    async def _send_snapshot(self):
        """Both halves of the alarm snapshot share one sequence number."""
        seq = self.state.next_seq()
        await self._notify(build_snapshot(self.state, 0, seq))
        await asyncio.sleep(0.05)
        await self._notify(build_snapshot(self.state, 1, seq))

    # ── BLE callbacks ────────────────────────────────────────────────
    def _on_read(self, characteristic: BlessGATTCharacteristic, **kw) -> bytearray:
        return characteristic.value or bytearray()

    def _on_write(self, characteristic: BlessGATTCharacteristic, value: bytearray, **kw):
        data = bytes(value)
        if characteristic.uuid.lower() != FFF2_UUID.lower():
            return
        if not check(data):
            log.warning("WRITE with a bad frame / CRC: %s", data.hex(" "))
            return
        msgid, p = data[5], data[6:-2]
        if msgid == MSG_DATA_REQ and p:
            log.info("DATA_REQ %u", p[0])
            asyncio.run_coroutine_threadsafe(self._answer_request(p[0]), self.loop)
        elif msgid == MSG_COMMAND and len(p) >= 8:
            cmd, alarm, p1, p2, p3, p4 = p[0], p[1], p[4], p[5], p[6], p[7]
            status = apply_command(self.state, cmd, alarm, p1, p2, p3, p4)
            log.info("COMMAND %u alarm %u params %u %u %u %u -> status %u", cmd, alarm, p1, p2, p3, p4, status)
            asyncio.run_coroutine_threadsafe(self._notify(build_ack(self.state, cmd, status)), self.loop)
            asyncio.run_coroutine_threadsafe(self._send_snapshot(), self.loop)
        else:
            log.info("WRITE msg %u: %s", msgid, data.hex(" "))

    async def _answer_request(self, what: int):
        if what == REQ_RECEIVER:
            await self._notify(build_receiver(self.state))
        elif what in (REQ_ALARMS, REQ_ALL_ALARMS):
            await self._send_snapshot()
        elif what == REQ_REGION:
            await self._notify(build_wave(self.state))

    async def _notify(self, frame: bytes):
        if not self.server:
            return
        log.debug("NOTIFY %s", frame.hex(" "))
        char = self.server.get_characteristic(FFF1_UUID)
        char.value = bytearray(frame)
        res = self.server.update_value(SERVICE_UUID, FFF1_UUID)
        if asyncio.iscoroutine(res):  # older bless versions
            await res

    # ── console: inject events / change the state ────────────────────
    async def _console_loop(self):
        """'bite N' / 'drop N' / 'drift N CM' / 'vol N' / 'mode N' / 'pair N' / 'unpair N' / 'quit'."""
        loop = asyncio.get_event_loop()
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                await asyncio.sleep(1)
                continue
            parts = line.strip().lower().split()
            if not parts:
                continue
            cmd, args = parts[0], [int(x) for x in parts[1:] if x.isdigit()]
            try:
                if cmd in ("q", "quit", "exit"):
                    return
                if cmd == "bite":
                    for _ in range(10):  # a running line repeats
                        await self._notify(build_alarm_event(self.state, args[0] - 1, 0))
                        await asyncio.sleep(0.125)
                elif cmd == "drop":
                    await self._notify(build_alarm_event(self.state, args[0] - 1, 1))
                elif cmd == "drift":
                    a = self.state.alarms[args[0] - 1]
                    a.drift_cm, a.drift_status = args[1], 1 if args[1] else 0
                    await self._notify(build_wave(self.state))
                elif cmd == "vol":
                    self.state.receiver.vol = args[0]
                    await self._notify(build_receiver(self.state))
                elif cmd == "mode":
                    self.state.receiver.mode = args[0]
                    await self._notify(build_receiver(self.state))
                elif cmd in ("pair", "unpair"):
                    self.state.alarms[args[0] - 1].paired = cmd == "pair"
                    await self._send_snapshot()
                else:
                    print("commands: bite N | drop N | drift N CM | vol N | mode N | pair N | unpair N | quit")
            except (IndexError, ValueError) as e:
                print("err:", e)


def main():
    asyncio.run(CubeEmulator().start())


if __name__ == "__main__":
    main()
