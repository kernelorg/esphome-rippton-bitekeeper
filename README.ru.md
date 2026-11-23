[English](README.md) | **Русский** | [Українська](README.uk.md) | [Polski](README.pl.md)

# esphome-rippton-bitekeeper

Внешний компонент ESPHome `bitekeeper` для приёмника сигнализаторов
поклёвки **Bite Cube** (`Bite CubeR-xxxx`). ESP32 подключается к кубу по
Bluetooth LE вместо мобильного приложения, получает события поклёвки и
схода от 8 сигнализаторов и читает телеметрию куба. Всё доступно как
сущности, триггеры и действия ESPHome — лямбды не нужны.

Требования: ESP32 со стандартными компонентами ESPHome `esp32_ble_tracker`
и `ble_client` (Arduino или esp-idf).

## Что делает

| | |
|---|---|
| Подключение | Подписывается на FFF1 (notify) сервиса FFF0 и запускается сам, как только поднялся BLE (`auto_start`) |
| Keep-alive | Через `connect_delay` после подключения, затем каждые `poll_interval` отправляет в FFF2 три запроса данных. Без них куб перестаёт присылать данные |
| Сторож | Нет данных дольше `max(watchdog_min, poll_interval×2+5 с)` → переподключение. BLE-стек не всегда сообщает о потере связи |
| События | Поклёвка (рывок вперёд) и сход (рывок назад) по каждому сигнализатору. Длительная поклёвка повторяется не чаще `bite_repeat`. Сразу после подключения ложных событий нет |
| Телеметрия | Приёмник: громкость, режим, батарея, дрейф. Каждый сигнализатор: громкость, тон, чувствительность, цвет, батарея, онлайн, привязан. Напряжения сглаживаются (EMA α = 1/4) |
| Настройки | Громкость, тон, чувствительность, цвет, mute, сигнал схода и дрейфа каждого сигнализатора; громкость и вибрация приёмника, выключение звука всего — действиями, сущностями `number` и `switch`. Куб подтверждает каждую команду |
| Поиск сигнализаторов | Какие из 8 позиций заняты, читается из снимка куба (~раз в 15 с) — количество задавать не нужно |
| Поиск и привязка | Показывает приёмники рядом (сервис FFF0, до 8) и привязывается к самому сильному или к заданному MAC. Привязка переживает обновление прошивки и правки YAML |

## Быстрый старт

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
    mac_address: 00:00:00:00:00:00   # заменяется сохранённой привязкой
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
    name: "Bite alarm"               # событие Home Assistant: bite / drop
```

**Привязка, один раз:** нажмите *Scan for receivers*, подождите несколько
секунд (куб должен быть включён и рядом), затем *Pair receiver* — компонент
привяжется к самому сильному найденному приёмнику. Привязка сохраняется.
Полный пример — [`example/bitekeeper.yaml`](example/bitekeeper.yaml).

## Конфигурация

```yaml
bitekeeper:
  id: cube_dev
  ble_client_id: cube
  auto_start: true            # false: запускать / останавливать через bitekeeper.start / stop
  poll_interval: 15s          # период keep-alive; 0 = выкл.
  connect_delay: 1500ms       # пауза после подключения перед первым keep-alive
  watchdog_min: 30s           # минимальная тишина перед переподключением
  bite_repeat: 1s             # интервал повтора длительной поклёвки
  stop_scan_on_connect: true  # false, если другому BLE-клиенту нужно найти своё устройство
```

## Триггеры

Сигнализаторы везде в YAML нумеруются **1..8**. `on_bite`, `on_drop` и
`on_alarm` принимают необязательный фильтр `alarm:` — один номер или список,
а `on_alarm` ещё и `direction: bite | drop`.

| Триггер | Переменные | Когда |
|---|---|---|
| `on_bite` | `alarm` | рывок вперёд (поклёвка) |
| `on_drop` | `alarm` | рывок назад (сход) |
| `on_alarm` | `alarm`, `direction` (0 поклёвка / 1 сход) | оба случая |
| `on_connect` / `on_disconnect` | — | связь с кубом есть / пропала |
| `on_alarms_change` | `count` | изменился набор привязанных сигнализаторов |
| `on_data` | — | пришёл любой кадр телеметрии |
| `on_command_ack` | `command`, `status` (0 успех, 1 ошибка, 3 не поддерживается, 4 неверный параметр, 5 идёт обновление прошивки) | куб ответил на команду настройки |

```yaml
bitekeeper:
  # …
  on_bite:                     # любой сигнализатор
    - light.turn_on: bite_led
    - delay: 3s
    - light.turn_off: bite_led
  on_alarm:                    # только поклёвка на сигнализаторе 2
    - alarm: 2
      direction: bite
      then:
        - switch.turn_on: buzzer
  on_drop:                     # сход на сигнализаторе 1 или 3
    - alarm:
        - 1
        - 3
      then:
        - logger.log: "Drop-back on an outer rod"
  on_disconnect:
    - logger.log: "Cube lost"
```

## Сущности

```yaml
event:
  - platform: bitekeeper
    alarm: 2                   # необязательно: номер или список; без него — все сигнализаторы
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

| Платформа | `type` | |
|---|---|---|
| `event` | — | события `bite` / `drop` сигнализаторов из `alarm:` (без него — всех) — для автоматизаций Home Assistant |
| `sensor` | `receiver_voltage` | батарея приёмника, В |
| | `alarm_voltage` + `alarm: N` | батарея сигнализатора N, В (*unknown*, пока в этой позиции нет привязанного сигнализатора) |
| | `alarm_count` | привязанные сигнализаторы |
| | `event_count` | поклёвки + сходы с загрузки |
| | `drift` | дрейф, см |
| | `rssi` | сигнал привязанного приёмника, дБм |
| | `alarm_drift` + `alarm: N` | дрейф сигнализатора N, см |
| `binary_sensor` | `connected` (по умолчанию) | связь с кубом |
| | `alarm_online` + `alarm: N` | сигнализатор N онлайн |
| `text_sensor` | `status` | `Connected` / `Disconnected` |
| | `paired_name`, `paired_mac` | привязанный приёмник |
| | `alarms` | все привязанные сигнализаторы: `1: 3.92 V online; 3: 3.85 V offline` |
| | `last_event` | `2: bite` / `3: drop` |
| | `scan_results` | приёмники из последнего поиска: `MAC имя -70 dBm; …` |
| `button` | `scan`, `pair`, `forget`, `poll` | искать приёмники; привязаться к самому сильному найденному; забыть привязку; отправить keep-alive сейчас |
| `switch` | `running` (по умолчанию) | поиск и связь с кубом (start / stop) |
| | `alarm_mute` / `drop_back` + `alarm: N` | mute / сигнал схода сигнализатора N (состояние от куба) |
| | `mute_all`, `vibration`, `drift` | выключить звук всего, вибрация приёмника, дрейф всех сигнализаторов (последняя отправленная команда) |
| `number` | `poll_interval` (по умолчанию) | период keep-alive в с, 0 = выкл.; сохраняется (`restore_value`, по умолчанию `true`) |
| | `alarm_volume`, `alarm_tone`, `alarm_sensitivity`, `alarm_color` + `alarm: N` | настройки сигнализатора N: 0..9, 0..8, 0..9, 0..5 (сохранённый цвет); показывают значение куба |
| | `receiver_volume` | громкость приёмника 0..9 |

### Неизвестное число сигнализаторов

В кубе 0..8 сигнализаторов, и какие позиции заняты, становится известно
только после отчёта куба. Сущности ESPHome задаются при сборке, поэтому
используйте сущности, не зависящие от количества, — `event` без `alarm:`,
`alarm_count`, текстовый датчик `alarms`, `on_alarms_change`, — или
добавьте сущности для позиций, которые вы знаете.

## Действия

| Действие | |
|---|---|
| `bitekeeper.start` / `bitekeeper.stop` | запустить / остановить поиск и связь (с `auto_start: false` или чтобы освободить радио, например на время OTA) |
| `bitekeeper.scan_clear` | очистить список найденных приёмников и начать поиск |
| `bitekeeper.pair` | без параметров: привязаться к самому сильному найденному приёмнику; с `mac:` (и `name:`): к этому приёмнику |
| `bitekeeper.forget` | забыть привязанный приёмник |
| `bitekeeper.poll` | отправить keep-alive сейчас |
| `bitekeeper.set_poll_interval` | `interval: 30s` — изменить период keep-alive |
| `bitekeeper.set_alarm` | `alarm:` 1..8 и любые из `volume`, `tone`, `sensitivity`, `color`, `mute`, `drop_back` |
| `bitekeeper.set_receiver` | любые из `volume`, `vibration`, `mute_all` |
| `bitekeeper.set_drift` | `alarm:` + `speed:` + `distance:` для одного сигнализатора или `enabled:` для всех |
| `bitekeeper.send_command` | сырой `COMMAND_INT`: `command`, `alarm_index` (с нуля, `0xFF` = все), `param1..4` — см. [`docs/PROTOCOL.md`](docs/PROTOCOL.md) |

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

## API из лямбд

`slot(i)` принимает индекс с нуля: сигнализатор *N* — это `slot(N - 1)`.

| Метод | |
|---|---|
| `is_connected()`, `is_running()`, `is_paired()` | |
| `paired_mac()`, `paired_name()`, `paired_rssi()` | |
| `alarm_count()`, `paired_alarms()` → `{1, 3, …}` | |
| `receiver()` → `{vol, mode, mv, mv_ema, drift_cm, drift_enabled}` | |
| `slot(i)` → `{vol, tone, sens, color, mv, mv_ema, online, paired, r, g, b}` | |
| `slot_color_rgb(i, r, g, b)` | цвет сигнализатора в RGB (пресеты 0..4, 5 = свой) |
| `last_direction(i)` | 0 поклёвка / 1 сход / 0xFF ещё не было |
| `last_event_alarm()`, `last_event_direction()`, `last_event_ms()` | последнее событие (сигнализатор 1..8, 0 = ещё не было) |
| `event_count()` | событий с загрузки |
| `scan_clear()`, `scan_results()` → `{mac, name, rssi}`, `pair_strongest()`, `pair_scan_result(i)` | |
| `pair(mac, name)`, `forget()`, `poll()`, `set_poll_interval(ms)` | |
| `set_alarm_volume / _tone / _sensitivity / _color / _mute / _drop_back(alarm, value)`, `set_alarm_drift(alarm, speed, distance)`, `set_drift(on)`, `set_receiver_volume(v)`, `set_receiver_vibration(on)`, `set_mute_all(on)`, `send_command(cmd, alarm_index, p1, p2, p3, p4)` | настройки; `false`, если нет связи или значение вне диапазона |
| `last_ack_command()`, `last_ack_status()` | последний `COMMAND_ACK` |

## Необязательный отладочный интерфейс (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) — пакет для небольшого
горизонтального дисплея (240×135) с двумя кнопками; для обычной работы он
не нужен.

```yaml
packages:
  bitekeeper_ui: !include
    file: <path>/esphome-rippton-bitekeeper/ui/lvgl_debug.yaml
    vars:
      bitekeeper_id: cube_dev
      display_id: lcd
      rotation: "90"             # горизонтальный поворот без переворота
      key_left: btn_left         # binary_sensor: левая кнопка
      key_right: btn_right       # binary_sensor: правая кнопка
      backlight_id: backlight
      deep_sleep_id: deep_sleep_ctl
```

| Страница | |
|---|---|
| Status | приёмник и связь, батарея приёмника, привязанные / онлайн сигнализаторы, последнее событие; круглый индикатор мигает красным при поклёвке, жёлтым при сходе |
| Alarms | все привязанные сигнализаторы: батарея, онлайн, громкость, тон, чувствительность |
| Pair | привязанный приёмник и найденные приёмники; *Scan*, *Pair*, *Forget* |
| Settings | переворот экрана на 180°, deep sleep через 30 с / 1 / 3 / 5 мин или никогда (по умолчанию) |

Кнопки: левая — перевести фокус (красная рамка), правая — нажать; при
перевёрнутом экране они меняются местами. Во время deep sleep плата не
слушает куб и пропускает поклёвки, поэтому по умолчанию сон выключен.

## Примеры

| Файл | |
|---|---|
| [`example/bitekeeper.yaml`](example/bitekeeper.yaml) | мост в Home Assistant: события по удочкам, датчики, кнопки привязки, светодиод при поклёвке, реакции на один сигнализатор |
| [`example/bitekeeper_lvgl_tdisplay.yaml`](example/bitekeeper_lvgl_tdisplay.yaml) | отладочная прошивка LilyGO T-Display с интерфейсом; только BLE, без Wi-Fi |

Скопируйте `example/secrets.yaml.example` в `example/secrets.yaml` и
соберите из корня репозитория:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 \
  compile example/bitekeeper.yaml
```

## Протокол и инструменты

- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — структура GATT, порядок подключения, форматы кадров.
- [`tools/emulator/`](tools/emulator/) — эмулятор куба как BLE-периферии (`bless`): присылает данные приёмника, снимок и дрейф, отвечает на запросы данных, применяет и подтверждает настройки, поклёвки и сходы вводятся с консоли. `pip install bless`, `python3 tools/emulator/peripheral.py`.

## Ограничения

- Только ESP32.
- Сущности, фильтры триггеров и отладочный интерфейс этой версии собираются и проходят проверку конфигурации, но ещё не проверены на приёмнике.
