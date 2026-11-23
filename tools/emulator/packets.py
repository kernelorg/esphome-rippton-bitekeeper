"""State and frame builders of the Bite Cube emulator (see docs/PROTOCOL.md).

Frames: FE LEN SEQ SYSID COMPID MSGID PAYLOAD CRC_L CRC_H. The builders
return complete frames (CRC included).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import List

from crc import wrap

CUBE_SYSID, CUBE_COMPID = 0x02, 0x02

MSG_TX_DATA, MSG_RX_DATA, MSG_ALARM, MSG_DATA_REQ = 0, 1, 2, 3
MSG_COMMAND, MSG_ACK, MSG_WAVE = 4, 5, 10

REQ_ALARMS, REQ_RECEIVER, REQ_REGION, REQ_ALL_ALARMS = 0, 1, 2, 3

# COMMAND_INT ids
CMD_ALARM_VOLUME, CMD_ALARM_TONE, CMD_ALARM_SENSITIVITY, CMD_ALARM_COLOR = 0, 1, 2, 3
CMD_ALARM_MUTE, CMD_RECEIVER_VOLUME, CMD_RECEIVER_VIBRATION, CMD_MUTE_ALL = 4, 5, 7, 8
CMD_DRIFT, CMD_DROP_BACK = 10, 11

ACK_OK, ACK_FAIL, ACK_BAD_PARAM = 0, 1, 4

DEFAULT_COLORS = [(0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00), (0x00, 0x00, 0xFF),
                  (0xFF, 0xFF, 0x00), (0xFF, 0xFF, 0xFF), (0x80, 0x00, 0xFF)]


@dataclass
class Alarm:
    paired: bool = False
    vol: int = 5
    tone: int = 0
    sens: int = 5
    color: int = 0
    charge: int = 0
    drop_back: bool = False
    mute: int = 0
    drift: bool = False
    online: int = 1
    voltage_mv: int = 3900
    version: int = 0x279E
    name: int = 0x0042
    sound_crc: int = 0
    colors: List[tuple] = field(default_factory=lambda: list(DEFAULT_COLORS))
    drift_cm: int = 0
    drift_status: int = 0

    def record(self, index: int) -> bytes:
        """40-byte record of the alarm snapshot (empty position = zeros + id)."""
        if not self.paired:
            return bytes([index]) + bytes(39)
        flags1 = (self.charge & 0x0F) | (0x10 if self.drop_back else 0)
        flags2 = (self.mute & 0x0F) | (0x10 if self.drift else 0)
        rec = bytes([index, self.vol, self.tone, self.sens, self.color, flags1, flags2, self.online])
        rec += struct.pack("<HHHH", self.voltage_mv, self.version, self.name, self.sound_crc)
        for r, g, b in self.colors[:6]:
            rec += bytes([b, g, r, 0xFF])
        assert len(rec) == 40
        return rec


@dataclass
class Receiver:
    vol: int = 5
    charge: int = 0
    tone_status: int = 0
    mute: int = 0
    mode: int = 0
    voltage_mv: int = 3950
    version: int = 0x0101
    vibration: bool = False


@dataclass
class EmulatorState:
    seq: int = 0
    receiver: Receiver = field(default_factory=Receiver)
    alarms: List[Alarm] = field(default_factory=lambda: [
        Alarm(paired=True, vol=3, tone=2, sens=4, color=0, name=0x0042),
        Alarm(paired=True, vol=1, tone=8, sens=2, color=1, online=1, voltage_mv=3850, name=0x026D),
    ] + [Alarm() for _ in range(6)])

    def next_seq(self) -> int:
        s = self.seq
        self.seq = (self.seq + 1) & 0xFF
        return s


def frame(state: EmulatorState, msgid: int, payload: bytes, seq: int | None = None) -> bytes:
    s = state.next_seq() if seq is None else seq
    return wrap(bytes([0xFE, len(payload), s, CUBE_SYSID, CUBE_COMPID, msgid]) + payload)


def build_snapshot(state: EmulatorState, half: int, seq: int | None = None) -> bytes:
    """Alarm snapshot: half 0 → alarms 1-4, half 1 → alarms 5-8 (160 bytes)."""
    base = 4 * half
    payload = b"".join(state.alarms[base + i].record(base + i) for i in range(4))
    return frame(state, MSG_TX_DATA, payload, seq)


def build_receiver(state: EmulatorState) -> bytes:
    r = state.receiver
    payload = bytes([r.vol, r.charge & 0x0F, r.tone_status, r.mute, r.mode, 0])
    payload += struct.pack("<HH", r.voltage_mv, r.version)
    payload += b"".join(struct.pack("<H", a.sound_crc) for a in state.alarms)
    return frame(state, MSG_RX_DATA, payload)


def build_alarm_event(state: EmulatorState, index: int, direction: int) -> bytes:
    """Bite (direction 0) or drop-back (1) of the alarm at index 0..7."""
    a = state.alarms[index]
    r, g, b = a.colors[a.color % 6]
    payload = bytes([index, a.tone, a.color, direction & 1, b, g, r, 0xFF]) + struct.pack("<H", a.sound_crc) + bytes(2)
    return frame(state, MSG_ALARM, payload)


def build_wave(state: EmulatorState) -> bytes:
    payload = b"".join(bytes([a.drift_cm, a.drift_status]) for a in state.alarms)
    return frame(state, MSG_WAVE, payload)


def build_ack(state: EmulatorState, command: int, status: int) -> bytes:
    return frame(state, MSG_ACK, bytes([command, status]))


def apply_command(state: EmulatorState, cmd: int, alarm: int, p1: int, p2: int, p3: int, p4: int) -> int:
    """Apply a COMMAND_INT to the state; returns the ACK status."""
    rcv = state.receiver
    if cmd == CMD_RECEIVER_VOLUME:
        rcv.vol = p1
    elif cmd == CMD_RECEIVER_VIBRATION:
        rcv.vibration = bool(p1)
    elif cmd == CMD_MUTE_ALL:
        rcv.mute = p1
        for a in state.alarms:
            a.mute = p1
    elif cmd == CMD_DRIFT and alarm == 0xFF:
        for a in state.alarms:
            a.drift = bool(p1)
    elif alarm < 8 and state.alarms[alarm].paired:
        a = state.alarms[alarm]
        if cmd == CMD_ALARM_VOLUME:
            a.vol = p1
        elif cmd == CMD_ALARM_TONE:
            a.tone = p1
        elif cmd == CMD_ALARM_SENSITIVITY:
            a.sens = p1
        elif cmd == CMD_ALARM_COLOR:
            if p4 > 5:
                return ACK_BAD_PARAM
            a.color = p4
            a.colors[p4] = (p3, p2, p1)      # B G R on the wire
        elif cmd == CMD_ALARM_MUTE:
            a.mute = p1
        elif cmd == CMD_DROP_BACK:
            a.drop_back = bool(p1)
        elif cmd == CMD_DRIFT:
            a.drift = bool(p1)
            a.drift_cm = p3
        else:
            return ACK_FAIL
    else:
        return ACK_BAD_PARAM
    return ACK_OK
