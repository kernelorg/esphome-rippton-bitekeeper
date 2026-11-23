**English** | [Русский](README.ru.md) | [Українська](README.uk.md) | [Polski](README.pl.md)

# esphome-rippton-bitekeeper

ESPHome external component `bitekeeper` for the **Bite Cube** bite-alarm
receiver (`Bite CubeR-xxxx`). The ESP32 connects to the cube over Bluetooth
LE instead of the mobile app, receives bite / drop-back events from up to 8
alarms and reads the cube's telemetry. Everything is available as ESPHome
entities, triggers and actions — no lambdas needed.

Requirements: ESP32 with the standard ESPHome `esp32_ble_tracker` and
`ble_client` components (Arduino or esp-idf).

## What it does

| | |
|---|---|
| Connection | Subscribes to FFF1 (notify) on service FFF0 and starts by itself once the BLE stack is up (`auto_start`) |
| Keep-alive | `connect_delay` after connecting, then every `poll_interval`, sends three data requests to FFF2. Without them the cube stops streaming |
| Watchdog | No data for `max(watchdog_min, poll_interval×2+5 s)` → reconnect. The BLE stack does not always report a dropped link |
| Events | Bite (forward pull) and drop-back (backward pull) per alarm. A held bite repeats at most every `bite_repeat`. No phantom event right after connecting |
| Telemetry | Receiver: volume, mode, battery, drift. Each alarm: volume, tone, sensitivity, colour, battery, online, paired. Batteries are smoothed (EMA α = 1/4) |
| Settings | Volume, tone, sensitivity, colour, mute, drop-back and drift alarm of each alarm; receiver volume, vibration, mute everything — as actions, `number` and `switch` entities. The cube acknowledges every command |
| Alarm discovery | Which of the 8 positions hold a paired alarm is read from the cube's snapshot (~every 15 s) — the count is not configured |
| Scan and pairing | Lists receivers nearby (service FFF0, up to 8) and pairs with the strongest one or a given MAC. The pairing survives firmware updates and YAML changes |

## Quick start

```yaml
external_components:
  - source: github://kernelorg/esphome-rippton-bitekeeper@main
    components:
      - bitekeeper

esp32_ble_tracker:
  scan_parameters:
    active: true
    interval: 1100ms
    window: 80ms
    continuous: false

ble_client:
  - id: cube
    mac_address: 00:00:00:00:00:00   # replaced at runtime by the stored pairing
    auto_connect: true

bitekeeper:
  id: cube_dev
  ble_client_id: cube
  on_bite:
    - light.turn_on: bite_led
    - delay: 3s
    - light.turn_off: bite_led

button:
  - platform: bitekeeper
    type: scan
    name: "Scan for receivers"
  - platform: bitekeeper
    type: pair
    name: "Pair receiver"

event:
  - platform: bitekeeper
    name: "Bite alarm"               # Home Assistant event entity: bite / drop
```

**Pairing, once:** press *Scan for receivers*, wait a few seconds (the cube
must be on and close), then *Pair receiver* — it pairs with the strongest
receiver found. The pairing is stored. The full example is in
[`example/bitekeeper.yaml`](example/bitekeeper.yaml).

## Configuration

```yaml
bitekeeper:
  id: cube_dev
  ble_client_id: cube
  auto_start: true            # false: start / stop it with bitekeeper.start / stop
  poll_interval: 15s          # keep-alive period; 0 = off
  connect_delay: 1500ms       # wait after connecting before the first keep-alive
  watchdog_min: 30s           # minimum silence before reconnecting
  bite_repeat: 1s             # repeat interval for a held bite
  stop_scan_on_connect: true  # false if another BLE client must still find its device
```

## Triggers

Alarms are numbered **1..8** everywhere in YAML. `on_bite`, `on_drop` and
`on_alarm` take an optional `alarm:` filter — one number or a list — and
`on_alarm` also `direction: bite | drop`.

| Trigger | Variables | When |
|---|---|---|
| `on_bite` | `alarm` | forward pull (bite) |
| `on_drop` | `alarm` | backward pull (drop-back) |
| `on_alarm` | `alarm`, `direction` (0 bite / 1 drop-back) | both of the above |
| `on_connect` / `on_disconnect` | — | link to the cube up / down |
| `on_alarms_change` | `count` | the set of paired alarms changed |
| `on_data` | — | any telemetry frame arrived |
| `on_command_ack` | `command`, `status` (0 ok, 1 failed, 3 not supported, 4 bad parameter, 5 firmware update running) | the cube answered a settings command |

```yaml
bitekeeper:
  # …
  on_bite:                     # every alarm
    - light.turn_on: bite_led
    - delay: 3s
    - light.turn_off: bite_led
  on_alarm:                    # only a bite on alarm 2
    - alarm: 2
      direction: bite
      then:
        - switch.turn_on: buzzer
  on_drop:                     # drop-back on alarm 1 or 3
    - alarm:
        - 1
        - 3
      then:
        - logger.log: "Drop-back on an outer rod"
  on_disconnect:
    - logger.log: "Cube lost"
```

## Entities

```yaml
event:
  - platform: bitekeeper
    alarm: 2                   # optional: one number or a list; omitted = every alarm
    name: "Rod 2"
sensor:
  - platform: bitekeeper
    type: receiver_voltage
    name: "Receiver battery"
  - platform: bitekeeper
    type: alarm_count
    name: "Paired alarms"
  - platform: bitekeeper
    type: alarm_voltage
    alarm: 1
    name: "Alarm 1 battery"
binary_sensor:
  - platform: bitekeeper
    name: "Cube connected"
  - platform: bitekeeper
    type: alarm_online
    alarm: 1
    name: "Alarm 1 online"
text_sensor:
  - platform: bitekeeper
    type: alarms
    name: "Alarms"
  - platform: bitekeeper
    type: last_event
    name: "Last bite"
button:
  - platform: bitekeeper
    type: forget
    name: "Forget receiver"
switch:
  - platform: bitekeeper
    name: "Cube link"
number:
  - platform: bitekeeper
    name: "Keep-alive interval"
    mode: box
```

| Platform | `type` | |
|---|---|---|
| `event` | — | event types `bite` / `drop` of the alarms in `alarm:` (all when omitted) — for Home Assistant automations |
| `sensor` | `receiver_voltage` | receiver battery, V |
| | `alarm_voltage` + `alarm: N` | battery of alarm N, V (*unknown* while no alarm is paired there) |
| | `alarm_count` | paired alarms |
| | `event_count` | bites + drop-backs since boot |
| | `drift` | drift, cm |
| | `rssi` | signal of the paired receiver, dBm |
| | `alarm_drift` + `alarm: N` | drift distance of alarm N, cm |
| `binary_sensor` | `connected` (default) | link to the cube |
| | `alarm_online` + `alarm: N` | alarm N is online |
| `text_sensor` | `status` | `Connected` / `Disconnected` |
| | `paired_name`, `paired_mac` | the paired receiver |
| | `alarms` | every paired alarm: `1: 3.92 V online; 3: 3.85 V offline` |
| | `last_event` | `2: bite` / `3: drop` |
| | `scan_results` | receivers found by the last scan: `MAC name -70 dBm; …` |
| `button` | `scan`, `pair`, `forget`, `poll` | look for receivers; pair with the strongest one found; forget the pairing; send the keep-alive now |
| `switch` | `running` (default) | scanning and the link to the cube (start / stop) |
| | `alarm_mute` / `drop_back` + `alarm: N` | mute / drop-back alarm of alarm N (state from the cube) |
| | `mute_all`, `vibration`, `drift` | mute everything, receiver vibration, drift alarm of every alarm (last command sent) |
| `number` | `poll_interval` (default) | keep-alive period in s, 0 = off; saved (`restore_value`, default `true`) |
| | `alarm_volume`, `alarm_tone`, `alarm_sensitivity`, `alarm_color` + `alarm: N` | settings of alarm N: 0..9, 0..8, 0..9, 0..5 (stored colour); show the cube's value |
| | `receiver_volume` | receiver volume 0..9 |

### Unknown number of alarms

The cube holds 0..8 alarms, and which positions are used is only known once
the cube reports it. ESPHome entities are fixed at compile time, so use the
entities that do not depend on the count — `event` without `alarm:`,
`alarm_count`, the `alarms` text sensor, `on_alarms_change` — or add
per-alarm entities for the positions you know.

## Actions

| Action | |
|---|---|
| `bitekeeper.start` / `bitekeeper.stop` | start / stop scanning and the link (with `auto_start: false`, or to free the radio, e.g. during OTA) |
| `bitekeeper.scan_clear` | clear the list of found receivers and start collecting |
| `bitekeeper.pair` | without options: pair with the strongest receiver found; with `mac:` (and `name:`): that receiver |
| `bitekeeper.forget` | forget the paired receiver |
| `bitekeeper.poll` | send the keep-alive now |
| `bitekeeper.set_poll_interval` | `interval: 30s` — change the keep-alive period |
| `bitekeeper.set_alarm` | `alarm:` 1..8 plus any of `volume`, `tone`, `sensitivity`, `color`, `mute`, `drop_back` |
| `bitekeeper.set_receiver` | any of `volume`, `vibration`, `mute_all` |
| `bitekeeper.set_drift` | `alarm:` + `speed:` + `distance:` for one alarm, or `enabled:` for every alarm |
| `bitekeeper.send_command` | raw `COMMAND_INT`: `command`, `alarm_index` (0-based, `0xFF` = all), `param1..4` — see [`docs/PROTOCOL.md`](docs/PROTOCOL.md) |

```yaml
- bitekeeper.pair:
    id: cube_dev
    mac: "C0:D6:3C:00:00:00"
    name: "Bite CubeR-0000"
```

```yaml
- bitekeeper.set_alarm:        # alarm 1..8; any of the fields
    alarm: 2
    volume: 3                  # 0..9
    tone: 4                    # 0..8
    sensitivity: 6             # 0..9
    color: 1                   # 0..5 — one of the alarm's 6 stored colours
    mute: false
    drop_back: true
- bitekeeper.set_receiver:
    volume: 7                  # 0..9
    vibration: true
    mute_all: false
- bitekeeper.set_drift:        # drift alarm of one alarm …
    alarm: 2
    speed: 20                  # 10..40 = 1.0..4.0 cm/s
    distance: 10               # 5..20 cm
- bitekeeper.set_drift:        # … or on / off for every alarm
    enabled: false
```

## Lambda API

`slot(i)` takes a 0-based index: alarm *N* is `slot(N - 1)`.

| Method | |
|---|---|
| `is_connected()`, `is_running()`, `is_paired()` | |
| `paired_mac()`, `paired_name()`, `paired_rssi()` | |
| `alarm_count()`, `paired_alarms()` → `{1, 3, …}` | |
| `receiver()` → `{vol, mode, mv, mv_ema, drift_cm, drift_enabled}` | |
| `slot(i)` → `{vol, tone, sens, color, mv, mv_ema, online, paired, r, g, b}` | |
| `slot_color_rgb(i, r, g, b)` | alarm colour as RGB (presets 0..4, 5 = custom) |
| `last_direction(i)` | 0 bite / 1 drop-back / 0xFF nothing yet |
| `last_event_alarm()`, `last_event_direction()`, `last_event_ms()` | the last event (alarm 1..8, 0 = none yet) |
| `event_count()` | events since boot |
| `scan_clear()`, `scan_results()` → `{mac, name, rssi}`, `pair_strongest()`, `pair_scan_result(i)` | |
| `pair(mac, name)`, `forget()`, `poll()`, `set_poll_interval(ms)` | |
| `set_alarm_volume / _tone / _sensitivity / _color / _mute / _drop_back(alarm, value)`, `set_alarm_drift(alarm, speed, distance)`, `set_drift(on)`, `set_receiver_volume(v)`, `set_receiver_vibration(on)`, `set_mute_all(on)`, `send_command(cmd, alarm_index, p1, p2, p3, p4)` | settings; `false` when there is no link or a value is out of range |
| `last_ack_command()`, `last_ack_status()` | the last `COMMAND_ACK` |

## Optional on-device debug UI (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) is a package for a small landscape
display (240×135) with two buttons; it is not needed for normal operation.

```yaml
packages:
  bitekeeper_ui: !include
    file: <path>/esphome-rippton-bitekeeper/ui/lvgl_debug.yaml
    vars:
      bitekeeper_id: cube_dev
      display_id: lcd
      rotation: "90"             # landscape rotation when not flipped
      key_left: btn_left         # binary_sensor: left button
      key_right: btn_right       # binary_sensor: right button
      backlight_id: backlight
      deep_sleep_id: deep_sleep_ctl
```

| Page | |
|---|---|
| Status | receiver and link, receiver battery, paired / online alarms, the last event; a round LED flashes red on a bite, yellow on a drop-back |
| Alarms | every paired alarm: battery, online, volume, tone, sensitivity |
| Pair | the paired receiver and the receivers found; *Scan*, *Pair*, *Forget* |
| Settings | flip the screen 180°, deep sleep after 30 s / 1 / 3 / 5 min or never (default) |

Buttons: left = move the focus (red frame), right = press; with the screen
flipped they swap. Deep sleep stops listening to the cube — bites are missed
while the board sleeps — so it is off by default.

## Examples

| File | |
|---|---|
| [`example/bitekeeper.yaml`](example/bitekeeper.yaml) | Home Assistant bridge: event entities per rod, sensors, pairing buttons, LED on bites, reactions to one alarm |
| [`example/bitekeeper_lvgl_tdisplay.yaml`](example/bitekeeper_lvgl_tdisplay.yaml) | LilyGO T-Display debug firmware with the UI; BLE only, no Wi-Fi |

Copy `example/secrets.yaml.example` to `example/secrets.yaml` and build from
the repository root:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 \
  compile example/bitekeeper.yaml
```

## Protocol and tools

- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — GATT layout, connection flow, frame formats.
- [`tools/emulator/`](tools/emulator/) — cube emulator as a BLE peripheral (`bless`): streams the receiver, snapshot and drift messages, answers data requests, applies and acknowledges settings, injects bites and drop-backs from the console. `pip install bless`, `python3 tools/emulator/peripheral.py`.

## Limitations

- ESP32 only.
- The entities, trigger filters and the debug UI of this version are built and validated but not yet tested on a receiver.
