# Bite Cube receiver — BLE protocol

The receiver advertises as `Bite CubeR-xxxxxxxx` (LE legacy advertising,
channel 38 only) and exchanges framed messages over one GATT service.

## GATT

| Service | Characteristic | Value handle | CCC handle | Properties | Role |
|---|---|---|---|---|---|
| `0x1801` Generic Attribute | `0x2A05` Service Changed | `0x000b` | `0x000d` | read, indicate | standard |
| **`0xFFF0`** | **`0xFFF1`** | `0x0010` | `0x0011` | read, notify | **cube → host** |
| **`0xFFF0`** | **`0xFFF2`** | `0x0014` | `0x0015` | read, write, write without response | **host → cube** |
| `02F00000-…-FE00` | `02F0…FF00` … `FF05` | `0x001e` … `0x002c` | | | not used |

## Connection

1. Connect, exchange the ATT MTU (up to 512), discover the services.
2. Enable notifications on `0xFFF1` (write `01 00` to its CCC descriptor).
3. Wait ~1 s, then send three **data requests** (message 3) to `0xFFF2` as
   write-without-response: `1` (receiver), `2` (region), `0` (alarms):

   ```
   FE 01 00 01 02 03 01 81 18
   FE 01 00 01 02 03 02 1A 2A
   FE 01 00 01 02 03 00 08 09
   ```

4. The cube streams its messages on `0xFFF1`. Repeat the three requests
   every ~15 s; without them the cube stops streaming after ~30 s.

## Frame

```
FE  LEN  SEQ  SYSID  COMPID  MSGID  PAYLOAD[LEN]  CRC_L  CRC_H
```

| Field | |
|---|---|
| `FE` | start byte |
| `LEN` | payload length |
| `SEQ` | sequence number (the two halves of an alarm snapshot share one) |
| `SYSID`, `COMPID` | host → cube `01 02`; cube → host `02 02` |
| `MSGID` | message id (tables below) |
| `CRC` | 16 bit, low byte first |

One notification may carry several frames back to back — walk forward by
`LEN + 8`. Multi-byte fields inside payloads are little-endian.

### CRC

X.25 (CRC-16/MCRF4XX) step with the seed **`0xFF1E`**, over `LEN SEQ SYSID
COMPID MSGID PAYLOAD` (everything but the start byte and the CRC):

```c
uint16_t crc = 0xFF1E;
for (size_t i = 1; i < 6 + len; i++) {
  uint8_t t = frame[i] ^ (crc & 0xFF);
  t ^= t << 4;
  crc = (crc >> 8) ^ (t << 8) ^ (t << 3) ^ (t >> 4);
}
frame[6 + len] = crc & 0xFF;
frame[7 + len] = crc >> 8;
```

## Cube → host messages

| MSGID | Message | LEN | |
|---|---|---|---|
| 0 | `TX_DATA` | 160 | alarm snapshot, 4 alarms per frame (two frames cover 8) |
| 1 | `RX_DATA` | 26 | receiver |
| 2 | `ALARM_DATA` | 12 | bite / drop-back event |
| 3 | `REGION` | 12 | region |
| 5 | `COMMAND_ACK` | 2 | answer to a command |
| 6 | `MESSAGE_ACK` | | |
| 9 | `OTA_OK` | 1 | firmware update finished |
| 10 | `WAVE` | 16 | drift of every alarm |

### `TX_DATA` (0) — alarm snapshot

4 records of 40 bytes. The first record's `id` is 0 (alarms 1–4) or 4
(alarms 5–8). An empty position is all zeros apart from its `id`.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `id` — alarm index 0..7 |
| 1 | 1 | volume 0..9 |
| 2 | 1 | tone 0..8 |
| 3 | 1 | sensitivity 0..9 |
| 4 | 1 | colour — index 0..5 into the colour table below |
| 5 | 1 | bits 0–3 charging state; bits 4–5 `01` = drop-back alarm on; bits 6–7 `01` = brightness |
| 6 | 1 | bits 0–3 mute; bits 4–5 `01` = drift alarm on |
| 7 | 1 | status (non-zero = online) |
| 8 | 2 | battery voltage, mV |
| 10 | 2 | firmware version (0 = no alarm paired here) |
| 12 | 2 | name |
| 14 | 2 | CRC of the custom sound |
| 16 | 24 | 6 colours × `B G R A` |

### `RX_DATA` (1) — receiver

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | volume |
| 1 | 1 | bits 0–3 charging state; high nibble `1` = brightness |
| 2 | 1 | tone state |
| 3 | 1 | mute |
| 4 | 1 | mode |
| 5 | 1 | reserved |
| 6 | 2 | battery voltage, mV |
| 8 | 2 | firmware version |
| 10 | 16 | CRC of each alarm's custom sound, alarms 1..8 |

### `ALARM_DATA` (2) — event

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | alarm index 0..7 |
| 1 | 1 | tone |
| 2 | 1 | colour index |
| 3 | 1 | direction: 0 forward (bite), 1 backward (drop-back) |
| 4 | 4 | colour `B G R A` |
| 8 | 2 | CRC of the custom sound |
| 10 | 2 | reserved |

While the line keeps running the cube repeats the event (~8 per second).

### `WAVE` (10) — drift

8 × `{distance (cm), status}`, alarms 1..8.

### `COMMAND_ACK` (5)

`{command, status}` — `command` is the `COMMAND_INT` id; status:

| Status | |
|---|---|
| 0 | success |
| 1 | failed |
| 2 | data request needed first |
| 3 | not supported |
| 4 | bad parameter |
| 5 | firmware update running |

## Host → cube messages

### `DATA_REQ` (3), LEN 1

`{what}`: 0 alarms (snapshot), 1 receiver, 2 region, 3 all alarms.

### `COMMAND_INT` (4), LEN 8

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | command |
| 1 | 1 | alarm index 0..7 (`0xFF` = every alarm, `0` for receiver commands) |
| 2 | 2 | reserved, 0 |
| 4 | 4 | `param1` … `param4` |

| Command | Setting | Parameters |
|---|---|---|
| 0 | alarm volume | `param1` 0..9 |
| 1 | alarm tone | `param1` 0..8 |
| 2 | alarm sensitivity | `param1` 0..9 |
| 3 | alarm colour | `param1..3` = B, G, R of the colour, `param4` = its index 0..5 |
| 4 | alarm mute | `param1` 1 / 0 |
| 5 | receiver volume | `param1` |
| 6 | receiver mute | `param1` 1 / 0 |
| 7 | receiver vibration | `param1` 1 / 0 |
| 8 | mute everything (alarm `0xFF`) | `param1` 1 / 0 |
| 9 | go to sleep | |
| 10 | drift alarm | alarm `0xFF`: `param1` 1 / 0 for every alarm; one alarm: `param1` 1, `param2` speed 10..40 (0.1 cm/s), `param3` distance 5..20 cm |
| 11 | drop-back alarm | `param1` 1 / 0 |
| 12 | RGB brightness level | `param1` 1 / 0 |
| 13 | anti-theft mode | `param1` |
| 14 | navigation light | `param1` |
| 15 | light mode | |
| 16 | sensitivity setting | |
| 17 | enter memory look-up | |
| 128 | region mode | `param1` |

The cube answers with `COMMAND_ACK` and sends a fresh snapshot.

## Cadence

| Situation | Stream |
|---|---|
| Idle, no data requests | silent after ~30 s |
| After the data requests | `RX_DATA`, `REGION`, `TX_DATA` × 2, `WAVE` about every 15 s |
| Motion on an alarm | `ALARM_DATA` about 8 times a second while it lasts |
| A setting changed | `TX_DATA` pair at once; `RX_DATA` every 2–3 s for ~30 s, then every 15 s |
| A button on an alarm | fresh `TX_DATA` |
| An alarm switched on / off | the next `TX_DATA` reflects it |
