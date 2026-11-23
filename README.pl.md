[English](README.md) | [Русский](README.ru.md) | [Українська](README.uk.md) | **Polski**

# esphome-rippton-bitekeeper

Zewnętrzny komponent ESPHome `bitekeeper` dla odbiornika sygnalizatorów
brań **Bite Cube** (`Bite CubeR-xxxx`). ESP32 łączy się z kostką przez
Bluetooth LE zamiast aplikacji mobilnej, odbiera zdarzenia brania i zjazdu
od 8 sygnalizatorów i odczytuje telemetrię kostki. Wszystko jest dostępne
jako encje, wyzwalacze i akcje ESPHome — lambdy nie są potrzebne.

Wymagania: ESP32 ze standardowymi komponentami ESPHome `esp32_ble_tracker`
i `ble_client` (Arduino lub esp-idf).

## Co robi

| | |
|---|---|
| Połączenie | Subskrybuje FFF1 (notify) usługi FFF0 i uruchamia się sam, gdy tylko wystartuje BLE (`auto_start`) |
| Keep-alive | Po `connect_delay` od połączenia, potem co `poll_interval`, wysyła do FFF2 trzy żądania danych. Bez nich kostka przestaje przesyłać dane |
| Strażnik | Brak danych dłużej niż `max(watchdog_min, poll_interval×2+5 s)` → ponowne połączenie. Stos BLE nie zawsze zgłasza utratę połączenia |
| Zdarzenia | Branie (szarpnięcie do przodu) i zjazd (do tyłu) dla każdego sygnalizatora. Długie branie powtarza się nie częściej niż `bite_repeat`. Zaraz po połączeniu nie ma fałszywych zdarzeń |
| Telemetria | Odbiornik: głośność, tryb, bateria, dryf. Każdy sygnalizator: głośność, ton, czułość, kolor, bateria, online, sparowany. Napięcia są wygładzane (EMA α = 1/4) |
| Ustawienia | Głośność, ton, czułość, kolor, wyciszenie, alarm zjazdu i dryfu każdego sygnalizatora; głośność i wibracja odbiornika, wyciszenie wszystkiego — jako akcje i encje `number` oraz `switch`. Kostka potwierdza każde polecenie |
| Wykrywanie sygnalizatorów | Które z 8 pozycji są zajęte, jest odczytywane z migawki kostki (~co 15 s) — liczby nie trzeba podawać |
| Wyszukiwanie i parowanie | Pokazuje odbiorniki w pobliżu (usługa FFF0, do 8) i paruje z najsilniejszym lub z podanym MAC. Parowanie przetrwa aktualizację firmware i zmiany YAML |

## Szybki start

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
    mac_address: 00:00:00:00:00:00   # zastępowany zapisanym parowaniem
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
    name: "Bite alarm"               # zdarzenie Home Assistant: bite / drop
```

**Parowanie, raz:** naciśnij *Scan for receivers*, odczekaj kilka sekund
(kostka musi być włączona i blisko), potem *Pair receiver* — komponent
sparuje się z najsilniejszym znalezionym odbiornikiem. Parowanie jest
zapisywane. Pełny przykład — [`example/bitekeeper.yaml`](example/bitekeeper.yaml).

## Konfiguracja

```yaml
bitekeeper:
  id: cube_dev
  ble_client_id: cube
  auto_start: true            # false: uruchamiaj / zatrzymuj przez bitekeeper.start / stop
  poll_interval: 15s          # okres keep-alive; 0 = wył.
  connect_delay: 1500ms       # pauza po połączeniu przed pierwszym keep-alive
  watchdog_min: 30s           # minimalna cisza przed ponownym połączeniem
  bite_repeat: 1s             # odstęp powtórzeń długiego brania
  stop_scan_on_connect: true  # false, jeśli inny klient BLE musi jeszcze znaleźć swoje urządzenie
```

## Wyzwalacze

Sygnalizatory są wszędzie w YAML numerowane **1..8**. `on_bite`, `on_drop`
i `on_alarm` przyjmują opcjonalny filtr `alarm:` — jeden numer lub listę, a
`on_alarm` także `direction: bite | drop`.

| Wyzwalacz | Zmienne | Kiedy |
|---|---|---|
| `on_bite` | `alarm` | szarpnięcie do przodu (branie) |
| `on_drop` | `alarm` | szarpnięcie do tyłu (zjazd) |
| `on_alarm` | `alarm`, `direction` (0 branie / 1 zjazd) | oba przypadki |
| `on_connect` / `on_disconnect` | — | połączenie z kostką jest / zanikło |
| `on_alarms_change` | `count` | zmienił się zestaw sparowanych sygnalizatorów |
| `on_data` | — | nadeszła dowolna ramka telemetrii |
| `on_command_ack` | `command`, `status` (0 sukces, 1 błąd, 3 nieobsługiwane, 4 zły parametr, 5 trwa aktualizacja firmware) | kostka odpowiedziała na polecenie ustawienia |

```yaml
bitekeeper:
  # …
  on_bite:                     # dowolny sygnalizator
    - light.turn_on: bite_led
    - delay: 3s
    - light.turn_off: bite_led
  on_alarm:                    # tylko branie na sygnalizatorze 2
    - alarm: 2
      direction: bite
      then:
        - switch.turn_on: buzzer
  on_drop:                     # zjazd na sygnalizatorze 1 lub 3
    - alarm:
        - 1
        - 3
      then:
        - logger.log: "Drop-back on an outer rod"
  on_disconnect:
    - logger.log: "Cube lost"
```

## Encje

```yaml
event:
  - platform: bitekeeper
    alarm: 2                   # opcjonalnie: numer lub lista; bez niego — wszystkie sygnalizatory
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

| Platforma | `type` | |
|---|---|---|
| `event` | — | zdarzenia `bite` / `drop` sygnalizatorów z `alarm:` (bez niego — wszystkich) — dla automatyzacji Home Assistant |
| `sensor` | `receiver_voltage` | bateria odbiornika, V |
| | `alarm_voltage` + `alarm: N` | bateria sygnalizatora N, V (*unknown*, dopóki na tej pozycji nie ma sparowanego sygnalizatora) |
| | `alarm_count` | sparowane sygnalizatory |
| | `event_count` | brania + zjazdy od startu |
| | `drift` | dryf, cm |
| | `rssi` | sygnał sparowanego odbiornika, dBm |
| | `alarm_drift` + `alarm: N` | dryf sygnalizatora N, cm |
| `binary_sensor` | `connected` (domyślnie) | połączenie z kostką |
| | `alarm_online` + `alarm: N` | sygnalizator N online |
| `text_sensor` | `status` | `Connected` / `Disconnected` |
| | `paired_name`, `paired_mac` | sparowany odbiornik |
| | `alarms` | wszystkie sparowane sygnalizatory: `1: 3.92 V online; 3: 3.85 V offline` |
| | `last_event` | `2: bite` / `3: drop` |
| | `scan_results` | odbiorniki z ostatniego wyszukiwania: `MAC nazwa -70 dBm; …` |
| `button` | `scan`, `pair`, `forget`, `poll` | szukaj odbiorników; sparuj z najsilniejszym znalezionym; zapomnij parowanie; wyślij keep-alive teraz |
| `switch` | `running` (domyślnie) | wyszukiwanie i połączenie z kostką (start / stop) |
| | `alarm_mute` / `drop_back` + `alarm: N` | wyciszenie / alarm zjazdu sygnalizatora N (stan z kostki) |
| | `mute_all`, `vibration`, `drift` | wycisz wszystko, wibracja odbiornika, dryf wszystkich sygnalizatorów (ostatnie wysłane polecenie) |
| `number` | `poll_interval` (domyślnie) | okres keep-alive w s, 0 = wył.; zapisywany (`restore_value`, domyślnie `true`) |
| | `alarm_volume`, `alarm_tone`, `alarm_sensitivity`, `alarm_color` + `alarm: N` | ustawienia sygnalizatora N: 0..9, 0..8, 0..9, 0..5 (zapisany kolor); pokazują wartość z kostki |
| | `receiver_volume` | głośność odbiornika 0..9 |

### Nieznana liczba sygnalizatorów

Kostka obsługuje 0..8 sygnalizatorów, a które pozycje są zajęte, wiadomo
dopiero po raporcie kostki. Encje ESPHome są ustalane przy kompilacji, więc
używaj encji niezależnych od liczby — `event` bez `alarm:`, `alarm_count`,
czujnika tekstowego `alarms`, `on_alarms_change` — albo dodaj encje dla
pozycji, które znasz.

## Akcje

| Akcja | |
|---|---|
| `bitekeeper.start` / `bitekeeper.stop` | uruchom / zatrzymaj wyszukiwanie i połączenie (z `auto_start: false` lub by zwolnić radio, np. podczas OTA) |
| `bitekeeper.scan_clear` | wyczyść listę znalezionych odbiorników i zacznij wyszukiwanie |
| `bitekeeper.pair` | bez opcji: sparuj z najsilniejszym znalezionym odbiornikiem; z `mac:` (i `name:`): z tym odbiornikiem |
| `bitekeeper.forget` | zapomnij sparowany odbiornik |
| `bitekeeper.poll` | wyślij keep-alive teraz |
| `bitekeeper.set_poll_interval` | `interval: 30s` — zmień okres keep-alive |
| `bitekeeper.set_alarm` | `alarm:` 1..8 i dowolne z `volume`, `tone`, `sensitivity`, `color`, `mute`, `drop_back` |
| `bitekeeper.set_receiver` | dowolne z `volume`, `vibration`, `mute_all` |
| `bitekeeper.set_drift` | `alarm:` + `speed:` + `distance:` dla jednego sygnalizatora albo `enabled:` dla wszystkich |
| `bitekeeper.send_command` | surowe `COMMAND_INT`: `command`, `alarm_index` (od zera, `0xFF` = wszystkie), `param1..4` — zob. [`docs/PROTOCOL.md`](docs/PROTOCOL.md) |

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

## API z lambd

`slot(i)` przyjmuje indeks od zera: sygnalizator *N* to `slot(N - 1)`.

| Metoda | |
|---|---|
| `is_connected()`, `is_running()`, `is_paired()` | |
| `paired_mac()`, `paired_name()`, `paired_rssi()` | |
| `alarm_count()`, `paired_alarms()` → `{1, 3, …}` | |
| `receiver()` → `{vol, mode, mv, mv_ema, drift_cm, drift_enabled}` | |
| `slot(i)` → `{vol, tone, sens, color, mv, mv_ema, online, paired, r, g, b}` | |
| `slot_color_rgb(i, r, g, b)` | kolor sygnalizatora jako RGB (presety 0..4, 5 = własny) |
| `last_direction(i)` | 0 branie / 1 zjazd / 0xFF jeszcze nie było |
| `last_event_alarm()`, `last_event_direction()`, `last_event_ms()` | ostatnie zdarzenie (sygnalizator 1..8, 0 = jeszcze nie było) |
| `event_count()` | zdarzeń od startu |
| `scan_clear()`, `scan_results()` → `{mac, name, rssi}`, `pair_strongest()`, `pair_scan_result(i)` | |
| `pair(mac, name)`, `forget()`, `poll()`, `set_poll_interval(ms)` | |
| `set_alarm_volume / _tone / _sensitivity / _color / _mute / _drop_back(alarm, value)`, `set_alarm_drift(alarm, speed, distance)`, `set_drift(on)`, `set_receiver_volume(v)`, `set_receiver_vibration(on)`, `set_mute_all(on)`, `send_command(cmd, alarm_index, p1, p2, p3, p4)` | ustawienia; `false`, gdy brak połączenia lub wartość poza zakresem |
| `last_ack_command()`, `last_ack_status()` | ostatnie `COMMAND_ACK` |

## Opcjonalny interfejs diagnostyczny (LVGL)

[`ui/lvgl_debug.yaml`](ui/lvgl_debug.yaml) to pakiet dla małego poziomego
wyświetlacza (240×135) z dwoma przyciskami; do zwykłej pracy nie jest
potrzebny.

```yaml
packages:
  bitekeeper_ui: !include
    file: <path>/esphome-rippton-bitekeeper/ui/lvgl_debug.yaml
    vars:
      bitekeeper_id: cube_dev
      display_id: lcd
      rotation: "90"             # obrót poziomy bez odwrócenia
      key_left: btn_left         # binary_sensor: lewy przycisk
      key_right: btn_right       # binary_sensor: prawy przycisk
      backlight_id: backlight
      deep_sleep_id: deep_sleep_ctl
```

| Strona | |
|---|---|
| Status | odbiornik i połączenie, bateria odbiornika, sparowane / online sygnalizatory, ostatnie zdarzenie; okrągła dioda miga na czerwono przy braniu, na żółto przy zjeździe |
| Alarms | wszystkie sparowane sygnalizatory: bateria, online, głośność, ton, czułość |
| Pair | sparowany odbiornik i znalezione odbiorniki; *Scan*, *Pair*, *Forget* |
| Settings | obrót ekranu o 180°, deep sleep po 30 s / 1 / 3 / 5 min lub nigdy (domyślnie) |

Przyciski: lewy — przesuń fokus (czerwona ramka), prawy — naciśnij; po
odwróceniu ekranu zamieniają się rolami. W deep sleep płytka nie słucha
kostki i przegapia brania, dlatego sen jest domyślnie wyłączony.

## Przykłady

| Plik | |
|---|---|
| [`example/bitekeeper.yaml`](example/bitekeeper.yaml) | most do Home Assistant: zdarzenia na wędkę, czujniki, przyciski parowania, dioda przy braniu, reakcje na jeden sygnalizator |
| [`example/bitekeeper_lvgl_tdisplay.yaml`](example/bitekeeper_lvgl_tdisplay.yaml) | firmware diagnostyczny LilyGO T-Display z interfejsem; tylko BLE, bez Wi-Fi |

Skopiuj `example/secrets.yaml.example` do `example/secrets.yaml` i zbuduj z
katalogu głównego repozytorium:

```bash
docker run --rm -v "$PWD":/config -w /config ghcr.io/esphome/esphome:2026.10.1 \
  compile example/bitekeeper.yaml
```

## Protokół i narzędzia

- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — układ GATT, przebieg połączenia, formaty ramek.
- [`tools/emulator/`](tools/emulator/) — emulator kostki jako peryferium BLE (`bless`): wysyła dane odbiornika, migawkę i dryf, odpowiada na żądania danych, stosuje i potwierdza ustawienia, brania i zjazdy wprowadza się z konsoli. `pip install bless`, `python3 tools/emulator/peripheral.py`.

## Ograniczenia

- Tylko ESP32.
- Encje, filtry wyzwalaczy i interfejs diagnostyczny tej wersji kompilują się i przechodzą walidację konfiguracji, ale nie były jeszcze testowane na odbiorniku.
