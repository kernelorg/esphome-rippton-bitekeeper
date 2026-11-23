[English](README.md) | [Русский](README.ru.md) | **Українська** | [Polski](README.pl.md)

# esphome-rippton-bitekeeper

Зовнішній компонент ESPHome `bitekeeper` для приймача сигналізаторів
клювання **Bite Cube** (`Bite CubeR-xxxx`). ESP32 підключається до куба
через Bluetooth LE замість мобільного застосунку, отримує події клювання та
сходу від 8 сигналізаторів і читає телеметрію куба. Усе доступне як
сутності, тригери та дії ESPHome — лямбди не потрібні.

Вимоги: ESP32 зі стандартними компонентами ESPHome `esp32_ble_tracker` і
`ble_client` (Arduino або esp-idf).

## Що робить

| | |
|---|---|
| Підключення | Підписується на FFF1 (notify) сервісу FFF0 і запускається сам, щойно піднявся BLE (`auto_start`) |
| Keep-alive | Через `connect_delay` після підключення, потім кожні `poll_interval` надсилає у FFF2 три запити даних. Без них куб перестає надсилати дані |
| Сторож | Немає даних довше `max(watchdog_min, poll_interval×2+5 с)` → перепідключення. BLE-стек не завжди повідомляє про втрату зв'язку |
| Події | Клювання (ривок уперед) і схід (ривок назад) для кожного сигналізатора. Тривале клювання повторюється не частіше `bite_repeat`. Одразу після підключення хибних подій немає |
| Телеметрія | Приймач: гучність, режим, батарея, дрейф. Кожен сигналізатор: гучність, тон, чутливість, колір, батарея, онлайн, прив'язаний. Напруги згладжуються (EMA α = 1/4) |
| Налаштування | Гучність, тон, чутливість, колір, mute, сигнал сходу та дрейфу кожного сигналізатора; гучність і вібрація приймача, вимкнення звуку всього — діями, сутностями `number` і `switch`. Куб підтверджує кожну команду |
| Пошук сигналізаторів | Які з 8 позицій зайняті, читається зі знімка куба (~раз на 15 с) — кількість задавати не потрібно |
| Пошук і прив'язка | Показує приймачі поруч (сервіс FFF0, до 8) і прив'язується до найсильнішого або до заданого MAC. Прив'язка переживає оновлення прошивки та зміни YAML |

## Швидкий старт

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
    mac_address: 00:00:00:00:00:00   # замінюється збереженою прив'язкою
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
    name: "Bite alarm"               # подія Home Assistant: bite / drop
```

**Прив'язка, один раз:** натисніть *Scan for receivers*, зачекайте кілька
секунд (куб має бути ввімкнений і поруч), потім *Pair receiver* — компонент
прив'яжеться до найсильнішого знайденого приймача. Прив'язка зберігається.
Повний приклад — [`example/bitekeeper.yaml`](example/bitekeeper.yaml).

## Конфігурація

```yaml
bitekeeper:
  id: cube_dev
  ble_client_id: cube
  auto_start: true            # false: запускати / зупиняти через bitekeeper.start / stop
  poll_interval: 15s          # період keep-alive; 0 = вимк.
  connect_delay: 1500ms       # пауза після підключення перед першим keep-alive
  watchdog_min: 30s           # мінімальна тиша перед перепідключенням
  bite_repeat: 1s             # інтервал повтору тривалого клювання
  stop_scan_on_connect: true  # false, якщо іншому BLE-клієнту потрібно знайти свій пристрій
```

## Тригери

Сигналізатори скрізь у YAML нумеруються **1..8**. `on_bite`, `on_drop` і
`on_alarm` приймають необов'язковий фільтр `alarm:` — один номер або
список, а `on_alarm` ще й `direction: bite | drop`.

| Тригер | Змінні | Коли |
|---|---|---|
| `on_bite` | `alarm` | ривок уперед (клювання) |
| `on_drop` | `alarm` | ривок назад (схід) |
| `on_alarm` | `alarm`, `direction` (0 клювання / 1 схід) | обидва випадки |
| `on_connect` / `on_disconnect` | — | зв'язок із кубом є / зник |
| `on_alarms_change` | `count` | змінився набір прив'язаних сигналізаторів |
| `on_data` | — | надійшов будь-який кадр телеметрії |
| `on_command_ack` | `command`, `status` (0 успіх, 1 помилка, 3 не підтримується, 4 неправильний параметр, 5 триває оновлення прошивки) | куб відповів на команду налаштування |

```yaml
bitekeeper:
  # …
  on_bite:                     # будь-який сигналізатор
    - light.turn_on: bite_led
    - delay: 3s
    - light.turn_off: bite_led
  on_alarm:                    # лише клювання на сигналізаторі 2
    - alarm: 2
      direction: bite
      then:
        - switch.turn_on: buzzer
  on_drop:                     # схід на сигналізаторі 1 або 3
    - alarm:
        - 1
        - 3
      then:
        - logger.log: "Drop-back on an outer rod"
  on_disconnect:
    - logger.log: "Cube lost"
```

## Сутності

```yaml
event:
  - platform: bitekeeper
    alarm: 2                   # необов'язково: номер або список; без нього — усі сигналізатори
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
| `event` | — | події `bite` / `drop` сигналізаторів з `alarm:` (без нього — усіх) — для автоматизацій Home Assistant |
| `sensor` | `receiver_voltage` | батарея приймача, В |
| | `alarm_voltage` + `alarm: N` | батарея сигналізатора N, В (*unknown*, поки в цій позиції немає прив'язаного сигналізатора) |
| | `alarm_count` | прив'язані сигналізатори |
| | `event_count` | клювання + сходи від завантаження |
| | `drift` | дрейф, см |
| | `rssi` | сигнал прив'язаного приймача, дБм |
| | `alarm_drift` + `alarm: N` | дрейф сигналізатора N, см |
| `binary_sensor` | `connected` (за замовчуванням) | зв'язок із кубом |
| | `alarm_online` + `alarm: N` | сигналізатор N онлайн |
| `text_sensor` | `status` | `Connected` / `Disconnected` |
| | `paired_name`, `paired_mac` | прив'язаний приймач |
| | `alarms` | усі прив'язані сигналізатори: `1: 3.92 V online; 3: 3.85 V offline` |
| | `last_event` | `2: bite` / `3: drop` |
| | `scan_results` | приймачі з останнього пошуку: `MAC назва -70 dBm; …` |
| `button` | `scan`, `pair`, `forget`, `poll` | шукати приймачі; прив'язатися до найсильнішого знайденого; забути прив'язку; надіслати keep-alive зараз |
| `switch` | `running` (за замовчуванням) | пошук і зв'язок із кубом (start / stop) |
| | `alarm_mute` / `drop_back` + `alarm: N` | mute / сигнал сходу сигналізатора N (стан від куба) |
| | `mute_all`, `vibration`, `drift` | вимкнути звук усього, вібрація приймача, дрейф усіх сигналізаторів (остання надіслана команда) |
| `number` | `poll_interval` (за замовчуванням) | період keep-alive у с, 0 = вимк.; зберігається (`restore_value`, за замовчуванням `true`) |
| | `alarm_volume`, `alarm_tone`, `alarm_sensitivity`, `alarm_color` + `alarm: N` | налаштування сигналізатора N: 0..9, 0..8, 0..9, 0..5 (збережений колір); показують значення куба |
| | `receiver_volume` | гучність приймача 0..9 |

### Невідома кількість сигналізаторів

У кубі 0..8 сигналізаторів, і які позиції зайняті, стає відомо лише після
звіту куба. Сутності ESPHome задаються під час збирання, тому
використовуйте сутності, що не залежать від кількості, — `event` без
`alarm:`, `alarm_count`, текстовий датчик `alarms`, `on_alarms_change`, —
або додайте сутності для позицій, які ви знаєте.

## Дії

| Дія | |
|---|---|
| `bitekeeper.start` / `bitekeeper.stop` | запустити / зупинити пошук і зв'язок (з `auto_start: false` або щоб звільнити радіо, наприклад на час OTA) |
| `bitekeeper.scan_clear` | очистити список знайдених приймачів і почати пошук |
| `bitekeeper.pair` | без параметрів: прив'язатися до найсильнішого знайденого приймача; з `mac:` (і `name:`): до цього приймача |
| `bitekeeper.forget` | забути прив'язаний приймач |
| `bitekeeper.poll` | надіслати keep-alive зараз |
| `bitekeeper.set_poll_interval` | `interval: 30s` — змінити період keep-alive |
| `bitekeeper.set_alarm` | `alarm:` 1..8 і будь-які з `volume`, `tone`, `sensitivity`, `color`, `mute`, `drop_back` |
| `bitekeeper.set_receiver` | будь-які з `volume`, `vibration`, `mute_all` |
| `bitekeeper.set_drift` | `alarm:` + `speed:` + `distance:` для одного сигналізатора або `enabled:` для всіх |
| `bitekeeper.send_command` | сирий `COMMAND_INT`: `command`, `alarm_index` (від нуля, `0xFF` = усі), `param1..4` — див. [`docs/PROTOCOL.md`](docs/PROTOCOL.md) |

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

## API з лямбд

`slot(i)` приймає індекс від нуля: сигналізатор *N* — це `slot(N - 1)`.

| Метод | |
|---|---|
| `is_connected()`, `is_running()`, `is_paired()` | |
| `paired_mac()`, `paired_name()`, `paired_rssi()` | |
| `alarm_count()`, `paired_alarms()` → `{1, 3, …}` | |
| `receiver()` → `{vol, mode, mv, mv_ema, drift_cm, drift_enabled}` | |
| `slot(i)` → `{vol, tone, sens, color, mv, mv_ema, online, paired, r, g, b}` | |
| `slot_color_rgb(i, r, g, b)` | колір сигналізатора в RGB (пресети 0..4, 5 = власний) |
| `last_direction(i)` | 0 клювання / 1 схід / 0xFF ще не було |
| `last_event_alarm()`, `last_event_direction()`, `last_event_ms()` | остання подія (сигналізатор 1..8, 0 = ще не було) |
| `event_count()` | подій від завантаження |
| `scan_clear()`, `scan_results()` → `{mac, name, rssi}`, `pair_strongest()`, `pair_scan_result(i)` | |
| `pair(mac, name)`, `forget()`, `poll()`, `set_poll_interval(ms)` | |
| `set_alarm_volume / _tone / _sensitivity / _color / _mute / _drop_back(alarm, value)`, `set_alarm_drift(alarm, speed, distance)`, `set_drift(on)`, `set_receiver_volume(v)`, `set_receiver_vibration(on)`, `set_mute_all(on)`, `send_command(cmd, alarm_index, p1, p2, p3, p4)` | налаштування; `false`, якщо немає зв'язку або значення поза діапазоном |
| `last_ack_command()`, `last_ack_status()` | останній `COMMAND_ACK` |

## Необов'язковий налагоджувальний інтерфейс (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) — пакет для невеликого
горизонтального дисплея (240×135) з двома кнопками; для звичайної роботи
він не потрібен.

```yaml
packages:
  bitekeeper_ui: !include
    file: <path>/esphome-rippton-bitekeeper/ui/lvgl_debug.yaml
    vars:
      bitekeeper_id: cube_dev
      display_id: lcd
      rotation: "90"             # горизонтальний поворот без перевертання
      key_left: btn_left         # binary_sensor: ліва кнопка
      key_right: btn_right       # binary_sensor: права кнопка
      backlight_id: backlight
      deep_sleep_id: deep_sleep_ctl
```

| Сторінка | |
|---|---|
| Status | приймач і зв'язок, батарея приймача, прив'язані / онлайн сигналізатори, остання подія; круглий індикатор блимає червоним під час клювання, жовтим під час сходу |
| Alarms | усі прив'язані сигналізатори: батарея, онлайн, гучність, тон, чутливість |
| Pair | прив'язаний приймач і знайдені приймачі; *Scan*, *Pair*, *Forget* |
| Settings | перевертання екрана на 180°, deep sleep через 30 с / 1 / 3 / 5 хв або ніколи (за замовчуванням) |

Кнопки: ліва — перевести фокус (червона рамка), права — натиснути; коли
екран перевернуто, вони міняються місцями. Під час deep sleep плата не
слухає куб і пропускає клювання, тому за замовчуванням сон вимкнений.

## Приклади

| Файл | |
|---|---|
| [`example/bitekeeper.yaml`](example/bitekeeper.yaml) | міст у Home Assistant: події по вудках, датчики, кнопки прив'язки, світлодіод під час клювання, реакції на один сигналізатор |
| [`example/bitekeeper_lvgl_tdisplay.yaml`](example/bitekeeper_lvgl_tdisplay.yaml) | налагоджувальна прошивка LilyGO T-Display з інтерфейсом; лише BLE, без Wi-Fi |

Скопіюйте `example/secrets.yaml.example` в `example/secrets.yaml` і
зберіть із кореня репозиторію:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 \
  compile example/bitekeeper.yaml
```

## Протокол та інструменти

- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — структура GATT, порядок підключення, формати кадрів.
- [`tools/emulator/`](tools/emulator/) — емулятор куба як BLE-периферії (`bless`): надсилає дані приймача, знімок і дрейф, відповідає на запити даних, застосовує та підтверджує налаштування, клювання та сходи вводяться з консолі. `pip install bless`, `python3 tools/emulator/peripheral.py`.

## Обмеження

- Лише ESP32.
- Сутності, фільтри тригерів і налагоджувальний інтерфейс цієї версії збираються та проходять перевірку конфігурації, але ще не перевірені на приймачі.
