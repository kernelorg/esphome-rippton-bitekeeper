"""Frame CRC of the Bite Cube protocol.

X.25 (CRC-16/MCRF4XX) step with the seed 0xFF1E, over LEN SEQ SYSID COMPID
MSGID PAYLOAD; appended low byte first. See docs/PROTOCOL.md.
"""
from __future__ import annotations

SEED = 0xFF1E


def frame_crc(frame_without_crc: bytes) -> int:
    """CRC of a frame (starting with 0xFE, without the two CRC bytes)."""
    crc = SEED
    for b in frame_without_crc[1:]:
        t = (b ^ (crc & 0xFF)) & 0xFF
        t ^= (t << 4) & 0xFF
        crc = ((crc >> 8) ^ (t << 8) ^ (t << 3) ^ (t >> 4)) & 0xFFFF
    return crc


def wrap(body: bytes) -> bytes:
    """Append the CRC (low byte first) to a frame body."""
    crc = frame_crc(body)
    return body + bytes([crc & 0xFF, crc >> 8])


def check(frame: bytes) -> bool:
    """True if a complete frame carries a valid CRC."""
    if len(frame) < 8 or frame[0] != 0xFE or len(frame) != frame[1] + 8:
        return False
    crc = frame_crc(frame[:-2])
    return frame[-2] == (crc & 0xFF) and frame[-1] == (crc >> 8)


if __name__ == "__main__":
    # Self-test: the three DATA_REQ keep-alive frames.
    for arg, want in ((1, "8118"), (2, "1a2a"), (0, "0809")):
        f = wrap(bytes([0xFE, 0x01, 0x00, 0x01, 0x02, 0x03, arg]))
        assert f[-2:].hex() == want, (arg, f.hex())
    print("crc ok")
