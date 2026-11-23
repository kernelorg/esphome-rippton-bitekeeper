#include "bitekeeper.h"

#ifdef USE_ESP32

#include "esphome/core/hal.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/core/log.h"

#include <cmath>
#include <cstring>

namespace esphome {
namespace bitekeeper {

static const char *const TAG = "bitekeeper";

// GATT layout (see docs/PROTOCOL.md): service FFF0, FFF1 notify (cube →
// host), FFF2 write-without-response (host → cube).
static const uint16_t SVC_UUID = 0xFFF0;
static const uint16_t NOTIFY_UUID = 0xFFF1;
static const uint16_t WRITE_UUID = 0xFFF2;

// Keep-alive: three DATA_REQ frames (message 3) — 0x01 receiver, 0x02 region,
// 0x00 alarms. Without them the cube stops streaming.
static const uint8_t POLL_CMDS[3][9] = {
    {0xFE, 0x01, 0x00, 0x01, 0x02, 0x03, 0x01, 0x81, 0x18},
    {0xFE, 0x01, 0x00, 0x01, 0x02, 0x03, 0x02, 0x1A, 0x2A},
    {0xFE, 0x01, 0x00, 0x01, 0x02, 0x03, 0x00, 0x08, 0x09},
};
static const uint32_t POLL_GAP_MS = 50;  // gap between the three requests
static const uint32_t RECONNECT_DELAY_MS = 500;

// Fixed key: independent of the YAML's globals, so pairing survives
// adding/removing globals or switching framework.
static const uint32_t PAIR_PREF_KEY = 0xB17EC0BE;

// Frames: FE LEN SEQ SYSID COMPID MSGID PAYLOAD[LEN] CRC_L CRC_H.
static constexpr uint8_t MSG_TX_DATA = 0;    // alarm snapshot, 4 × 40 bytes
static constexpr uint8_t MSG_RX_DATA = 1;    // receiver
static constexpr uint8_t MSG_ALARM = 2;      // bite / drop-back event
static constexpr uint8_t MSG_DATA_REQ = 3;   // host → cube: request a message
static constexpr uint8_t MSG_COMMAND = 4;    // host → cube: COMMAND_INT
static constexpr uint8_t MSG_ACK = 5;        // cube → host: COMMAND_ACK
static constexpr uint8_t MSG_WAVE = 10;      // drift of every alarm

// X.25 step, seed 0xFF1E, over LEN..PAYLOAD.
static uint16_t frame_crc(const uint8_t *frame, uint8_t len) {
  uint16_t crc = 0xFF1E;
  for (size_t i = 1; i < 6u + len; i++) {
    uint8_t t = frame[i] ^ (uint8_t) (crc & 0xFF);
    t ^= (uint8_t) (t << 4);
    crc = (crc >> 8) ^ ((uint16_t) t << 8) ^ ((uint16_t) t << 3) ^ (t >> 4);
  }
  return crc;
}

static bool parse_mac(const std::string &s, uint64_t &out) {
  uint64_t v = 0;
  int nibbles = 0;
  for (char c : s) {
    if (c == ':' || c == '-')
      continue;
    v <<= 4;
    if (c >= '0' && c <= '9')
      v |= (c - '0');
    else if (c >= 'a' && c <= 'f')
      v |= (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      v |= (c - 'A' + 10);
    else
      return false;
    nibbles++;
  }
  out = v;
  return nibbles == 12;
}

// ── lifecycle ──────────────────────────────────────────────────────────

void BiteKeeper::setup() {
  memset(this->prev_dir_, 0xFF, sizeof(this->prev_dir_));
  this->pref_ = global_preferences->make_preference<PairPref>(PAIR_PREF_KEY);
  PairPref p{};
  if (this->pref_.load(&p)) {
    p.mac[sizeof(p.mac) - 1] = 0;
    p.name[sizeof(p.name) - 1] = 0;
    this->paired_mac_ = p.mac;
    this->paired_name_ = p.name;
  }
  // The YAML decides when the radio runs (start()/stop()); keep the
  // client parked until then.
  this->parent()->set_enabled(false);
  this->apply_address_();
#ifdef USE_TEXT_SENSOR
  if (this->status_ts_ != nullptr)
    this->status_ts_->publish_state("Disconnected");
  if (this->paired_name_ts_ != nullptr)
    this->paired_name_ts_->publish_state(this->paired_name_);
#endif
#ifdef USE_BINARY_SENSOR
  if (this->connected_bs_ != nullptr)
    this->connected_bs_->publish_initial_state(false);
#endif
}

void BiteKeeper::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Bite Cube receiver:\n"
                "  Paired: %s (%s)\n"
                "  Poll interval: %u ms\n"
                "  Watchdog min: %u ms",
                this->paired_mac_.empty() ? "(none)" : this->paired_mac_.c_str(), this->paired_name_.c_str(),
                (unsigned) this->poll_interval_, (unsigned) this->watchdog_min_);
}

void BiteKeeper::loop() {
  uint32_t now = millis();
  if (this->auto_start_pending_ && esp32_ble::global_ble != nullptr && esp32_ble::global_ble->is_active()) {
    this->auto_start_pending_ = false;
    this->start();
  }
  if (this->link_up_) {
    // Wait for GATT discovery to settle before the first write (~1 s after
    // the MTU exchange).
    if (this->first_poll_pending_ && now - this->connected_at_ >= this->connect_delay_) {
      this->first_poll_pending_ = false;
      this->poll();
    } else if (!this->first_poll_pending_ && this->poll_interval_ > 0 &&
               now - this->last_poll_ms_ >= this->poll_interval_) {
      this->poll();
    }
    if (now - this->last_watchdog_ms_ >= 1000) {
      this->last_watchdog_ms_ = now;
      this->watchdog_();
    }
  }
  if (this->data_dirty_) {
    this->data_dirty_ = false;
    this->publish_telemetry_();
    this->data_cb_.call();
  }
}

// ── link control ───────────────────────────────────────────────────────

void BiteKeeper::apply_address_() {
  uint64_t mac;
  if (!this->paired_mac_.empty() && parse_mac(this->paired_mac_, mac))
    this->parent()->set_address(mac);
}

void BiteKeeper::start() {
  ESP_LOGI(TAG, "radio ON");
  this->running_ = true;
  if (espbt::global_esp32_ble_tracker != nullptr) {
    // With continuous scanning set, the tracker starts a scan by itself when
    // BLE has just come up; start one here only if it is still idle.
    espbt::global_esp32_ble_tracker->set_scan_continuous(true);
    this->set_timeout("scan", 200, []() {
      if (espbt::global_esp32_ble_tracker != nullptr)
        espbt::global_esp32_ble_tracker->start_scan_if_idle();
    });
  }
  if (this->is_paired()) {
    this->apply_address_();
    this->parent()->set_enabled(true);
  }
}

void BiteKeeper::stop() {
  ESP_LOGI(TAG, "radio OFF");
  this->running_ = false;
  this->auto_start_pending_ = false;  // an explicit stop wins over auto_start
  this->cancel_timeout("reconnect");
  this->cancel_timeout("scan");
  this->parent()->set_enabled(false);
  if (espbt::global_esp32_ble_tracker != nullptr)
    espbt::global_esp32_ble_tracker->stop_scan();
  this->set_link_(false);
}

void BiteKeeper::set_link_(bool up) {
  if (up == this->link_up_)
    return;
  this->link_up_ = up;
#ifdef USE_TEXT_SENSOR
  if (this->status_ts_ != nullptr)
    this->status_ts_->publish_state(up ? "Connected" : "Disconnected");
#endif
#ifdef USE_BINARY_SENSOR
  if (this->connected_bs_ != nullptr)
    this->connected_bs_->publish_state(up);
#endif
  if (up)
    this->connect_cb_.call();
  else
    this->disconnect_cb_.call();
}

void BiteKeeper::watchdog_() {
  // ESPHome's ble_client relies on the stack raising a disconnect, but under
  // WiFi/BT coex stress the controller can stop delivering NOTIFY without
  // one. The cube stops streaming ~30 s after the last keep-alive, so wait
  // at least poll*2 + 5 s (or watchdog_min) before declaring the link dead.
  uint32_t threshold = this->watchdog_min_;
  if (this->poll_interval_ > 0) {
    uint32_t want = this->poll_interval_ * 2u + 5000u;
    if (want > threshold)
      threshold = want;
  }
  uint32_t silent = millis() - this->last_notify_ms_;
  if (silent < threshold)
    return;
  ESP_LOGW(TAG, "watchdog: %u ms without NOTIFY (thr=%u) — forcing reconnect", (unsigned) silent,
           (unsigned) threshold);
  this->set_link_(false);
  this->parent()->set_enabled(false);
  // Re-enable later so the BT host sees a clean teardown, not a same-frame
  // toggle.
  this->set_timeout("reconnect", RECONNECT_DELAY_MS, [this]() {
    if (!this->running_)
      return;
    ESP_LOGI(TAG, "watchdog reconnect: re-arming tracker + client");
    this->start();
  });
}

void BiteKeeper::poll() {
  if (!this->link_up_ || this->write_handle_ == 0)
    return;
  ESP_LOGD(TAG, "sending FFF2 keep-alive triplet");
  this->last_poll_ms_ = millis();
  this->write_poll_(0);
}

void BiteKeeper::write_poll_(uint8_t step) {
  if (step >= 3 || !this->link_up_ || this->write_handle_ == 0)
    return;
  auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                      this->write_handle_, sizeof(POLL_CMDS[step]),
                                      const_cast<uint8_t *>(POLL_CMDS[step]), ESP_GATT_WRITE_TYPE_NO_RSP,
                                      ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "poll write %u failed: %d", step, err);
  if (step < 2)
    this->set_timeout("poll", POLL_GAP_MS, [this, step]() { this->write_poll_(step + 1); });
}

// ── GATT ───────────────────────────────────────────────────────────────

void BiteKeeper::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                     esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK)
        ESP_LOGI(TAG, "GATT open %s", this->parent()->address_str());
      break;

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      this->notify_handle_ = 0;
      this->write_handle_ = 0;
      auto *rx = this->parent()->get_characteristic(SVC_UUID, NOTIFY_UUID);
      auto *tx = this->parent()->get_characteristic(SVC_UUID, WRITE_UUID);
      if (rx == nullptr || tx == nullptr) {
        ESP_LOGW(TAG, "FFF0/FFF1/FFF2 not found — not a Bite Cube?");
        break;
      }
      this->notify_handle_ = rx->handle;
      this->write_handle_ = tx->handle;
      auto status = this->parent()->register_for_notify(rx->handle);
      if (status)
        ESP_LOGW(TAG, "register_for_notify failed: %d", status);
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
      if (param->reg_for_notify.status != ESP_GATT_OK || param->reg_for_notify.handle != this->notify_handle_)
        break;
      this->node_state = espbt::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "CONNECTED to %s", this->paired_mac_.c_str());
      // Fresh edge-detection baseline: the first FE 0C after (re)connect
      // only records the resting direction (no phantom alarm).
      memset(this->prev_dir_, 0xFF, sizeof(this->prev_dir_));
      this->connected_at_ = millis();
      this->last_notify_ms_ = millis();  // don't let the watchdog fire before the first frame
      this->last_poll_ms_ = millis();
      this->first_poll_pending_ = true;
      // Pause the tracker once GATT is up — keeps the BT controller's HCI
      // queue and the WiFi/BT coex scheduler from saturating.
      if (this->stop_scan_on_connect_ && espbt::global_esp32_ble_tracker != nullptr)
        espbt::global_esp32_ble_tracker->stop_scan();
      this->set_link_(true);
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle != this->notify_handle_ || param->notify.value_len == 0)
        break;
      this->last_notify_ms_ = millis();
      this->handle_notify_(param->notify.value, param->notify.value_len);
      break;

    case ESP_GATTC_DISCONNECT_EVT:
    case ESP_GATTC_CLOSE_EVT:
      this->notify_handle_ = 0;
      this->write_handle_ = 0;
      if (this->link_up_) {
        ESP_LOGI(TAG, "DISCONNECTED — resuming tracker scan");
        this->set_link_(false);
      }
      if (this->running_ && espbt::global_esp32_ble_tracker != nullptr) {
        espbt::global_esp32_ble_tracker->set_scan_continuous(true);
        espbt::global_esp32_ble_tracker->start_scan_if_idle();
      }
      break;

    default:
      break;
  }
}

// ── advertisements (scan list + paired RSSI) ───────────────────────────

bool BiteKeeper::parse_device(const espbt::ESPBTDevice &device) {
  static const auto FFF0 = espbt::ESPBTUUID::from_uint16(SVC_UUID);
  bool match = false;
  for (auto &u : device.get_service_uuids()) {
    if (u == FFF0) {
      match = true;
      break;
    }
  }
  if (!match)
    return false;
  char mac_buf[espbt::ESPBTDevice::MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  std::string mac = device.address_str_to(mac_buf);
  std::string name = device.get_name().str();
  int rssi = device.get_rssi();

  if (mac == this->paired_mac_) {
    this->paired_rssi_ = rssi;
    if (!name.empty() && name != this->paired_name_) {
      this->paired_name_ = name;
#ifdef USE_TEXT_SENSOR
      if (this->paired_name_ts_ != nullptr)
        this->paired_name_ts_->publish_state(name);
#endif
    }
#ifdef USE_SENSOR
    if (this->rssi_sensor_ != nullptr)
      this->rssi_sensor_->publish_state(rssi);
#endif
  }
  if (!this->scan_collect_)
    return false;
  for (auto &e : this->scan_) {
    if (e.mac == mac) {
      e.rssi = rssi;
      if (!name.empty())
        e.name = name;
      return false;
    }
  }
  if (this->scan_.size() < MAX_SCAN)
    this->scan_.push_back({mac, name, rssi});
  return false;
}

void BiteKeeper::scan_clear() {
  this->scan_.clear();
  this->scan_collect_ = true;
  this->parent()->set_enabled(false);
}

// ── pairing ────────────────────────────────────────────────────────────

void BiteKeeper::save_pair_() {
  PairPref p{};
  strncpy(p.mac, this->paired_mac_.c_str(), sizeof(p.mac) - 1);
  strncpy(p.name, this->paired_name_.c_str(), sizeof(p.name) - 1);
  this->pref_.save(&p);
  global_preferences->sync();
#ifdef USE_TEXT_SENSOR
  if (this->paired_name_ts_ != nullptr)
    this->paired_name_ts_->publish_state(this->paired_name_);
#endif
}

void BiteKeeper::pair(const std::string &mac, const std::string &name) {
  uint64_t v;
  if (!parse_mac(mac, v)) {
    ESP_LOGW(TAG, "pair: invalid MAC '%s'", mac.c_str());
    return;
  }
  this->paired_mac_ = mac;
  this->paired_name_ = name.empty() ? std::string("(unknown)") : name;
  this->scan_collect_ = false;
  this->save_pair_();
  ESP_LOGI(TAG, "paired with %s (%s)", mac.c_str(), this->paired_name_.c_str());
  this->parent()->set_enabled(false);
  this->parent()->set_address(v);
  if (this->running_)
    this->parent()->set_enabled(true);
}

void BiteKeeper::pair_scan_result(size_t index) {
  if (index >= this->scan_.size())
    return;
  ScanEntry e = this->scan_[index];
  this->paired_rssi_ = e.rssi;
  this->pair(e.mac, e.name);
}

bool BiteKeeper::pair_strongest() {
  if (this->scan_.empty()) {
    ESP_LOGW(TAG, "pair: no receiver found yet - scan first (bitekeeper.scan_clear)");
    return false;
  }
  size_t best = 0;
  for (size_t i = 1; i < this->scan_.size(); i++)
    if (this->scan_[i].rssi > this->scan_[best].rssi)
      best = i;
  this->pair_scan_result(best);
  return true;
}

void BiteKeeper::forget() {
  this->parent()->set_enabled(false);
  this->paired_mac_.clear();
  this->paired_name_ = "(unknown)";
  this->paired_rssi_ = 0;
  this->save_pair_();
  this->set_link_(false);
  ESP_LOGI(TAG, "pairing forgotten");
}

// ── frame parser ───────────────────────────────────────────────────────

uint16_t BiteKeeper::ema_(uint16_t prev, uint16_t now) {
  if (prev == 0)
    return now;
  // alpha 1/4 with a forced minimum ±1 step so a slow drift inside the /4
  // dead band (|Δ| ≤ 3 mV) still tracks instead of freezing.
  int delta = (int) now - (int) prev;
  int step = delta / 4;
  if (step == 0 && delta != 0)
    step = delta > 0 ? 1 : -1;
  return (uint16_t) (prev + step);
}

void BiteKeeper::handle_notify_(const uint8_t *buf, size_t total) {
  ESP_LOGV(TAG, "notify %u bytes", (unsigned) total);
  // One notification may carry several frames back to back.
  size_t pos = 0;
  while (pos + 8 <= total) {
    if (buf[pos] != 0xFE) {
      pos++;
      continue;
    }
    const uint8_t *d = buf + pos;
    uint8_t len = d[1];
    size_t flen = (size_t) len + 8;
    if (pos + flen > total)
      break;
    uint16_t crc = frame_crc(d, len);
    if (d[6 + len] != (crc & 0xFF) || d[7 + len] != (crc >> 8)) {
      ESP_LOGV(TAG, "bad CRC (msg %u, len %u) - resync", d[5], len);
      pos++;
      continue;
    }
    const uint8_t *p = d + 6;
    switch (d[5]) {
      case MSG_TX_DATA:
        this->handle_snapshot_frame_(p, len);
        break;
      case MSG_RX_DATA:
        this->handle_rx_frame_(p, len);
        break;
      case MSG_ALARM:
        this->handle_alarm_frame_(p, len);
        break;
      case MSG_WAVE:
        this->handle_wave_frame_(p, len);
        break;
      case MSG_ACK:
        this->handle_ack_frame_(p, len);
        break;
      default:
        break;  // region, message ack, OTA
    }
    pos += flen;
  }
}

void BiteKeeper::handle_rx_frame_(const uint8_t *p, uint8_t len) {
  if (len < 10)
    return;
  this->rcv_.vol = p[0];
  this->rcv_.charge = p[1] & 0x0F;
  this->rcv_.tone_status = p[2];
  this->rcv_.mute = p[3];
  this->rcv_.mode = p[4];
  uint16_t mv = (uint16_t) p[6] | ((uint16_t) p[7] << 8);
  this->rcv_.mv = mv;
  this->rcv_.mv_ema = ema_(this->rcv_.mv_ema, mv);
  this->rcv_.version = (uint16_t) p[8] | ((uint16_t) p[9] << 8);
  this->data_dirty_ = true;
}

void BiteKeeper::handle_wave_frame_(const uint8_t *p, uint8_t len) {
  for (uint8_t i = 0; i < MAX_SLOTS && 2u * i + 1 < len; i++) {
    this->slots_[i].drift_cm = p[2 * i];
    this->slots_[i].drift_status = p[2 * i + 1];
  }
  if (len >= 2) {
    this->rcv_.drift_cm = p[0];
    this->rcv_.drift_enabled = p[1];
  }
  this->data_dirty_ = true;
}

void BiteKeeper::handle_ack_frame_(const uint8_t *p, uint8_t len) {
  if (len < 2)
    return;
  static const char *const STATUS[] = {"ok", "failed", "request first", "not supported", "bad parameter", "OTA running"};
  this->last_ack_cmd_ = p[0];
  this->last_ack_status_ = p[1];
  if (p[1] == 0)
    ESP_LOGD(TAG, "command %u: ok", p[0]);
  else
    ESP_LOGW(TAG, "command %u: %s", p[0], p[1] < 6 ? STATUS[p[1]] : "error");
  this->ack_cb_.call(p[0], p[1]);
}

// ── commands ───────────────────────────────────────────────────────────

bool BiteKeeper::write_frame_(const uint8_t *data, size_t len) {
  if (!this->link_up_ || this->write_handle_ == 0)
    return false;
  auto err = esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
                                      this->write_handle_, len, const_cast<uint8_t *>(data),
                                      ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "write failed: %d", err);
    return false;
  }
  return true;
}

bool BiteKeeper::send_command(uint8_t command, uint8_t alarm_index, uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4) {
  uint8_t f[16] = {0xFE, 8, this->tx_seq_++, 0x01, 0x02, MSG_COMMAND, command, alarm_index, 0, 0, p1, p2, p3, p4};
  uint16_t crc = frame_crc(f, 8);
  f[14] = crc & 0xFF;
  f[15] = crc >> 8;
  ESP_LOGD(TAG, "command %u alarm %u: %u %u %u %u", command, alarm_index, p1, p2, p3, p4);
  bool ok = this->write_frame_(f, sizeof(f));
  if (!ok)
    ESP_LOGW(TAG, "command %u not sent: no link to the cube", command);
  return ok;
}

bool BiteKeeper::alarm_cmd_(uint8_t command, uint8_t alarm, uint8_t value, uint8_t max) {
  if (alarm < 1 || alarm > MAX_SLOTS || value > max) {
    ESP_LOGW(TAG, "command %u: alarm %u / value %u out of range (alarm 1..8, value 0..%u)", command, alarm, value, max);
    return false;
  }
  return this->send_command(command, alarm - 1, value);
}

bool BiteKeeper::set_alarm_color(uint8_t alarm, uint8_t index) {
  if (alarm < 1 || alarm > MAX_SLOTS || index > 5) {
    ESP_LOGW(TAG, "colour: alarm %u / index %u out of range (alarm 1..8, index 0..5)", alarm, index);
    return false;
  }
  const uint8_t *rgb = this->slots_[alarm - 1].colors[index];
  // The colour travels as B, G, R plus its index.
  return this->send_command(CMD_ALARM_COLOR, alarm - 1, rgb[2], rgb[1], rgb[0], index);
}

bool BiteKeeper::set_alarm_drift(uint8_t alarm, uint8_t speed, uint8_t distance) {
  if (alarm < 1 || alarm > MAX_SLOTS || speed < 10 || speed > 40 || distance < 5 || distance > 20) {
    ESP_LOGW(TAG, "drift: alarm 1..8, speed 10..40, distance 5..20 (got %u, %u, %u)", alarm, speed, distance);
    return false;
  }
  return this->send_command(CMD_DRIFT, alarm - 1, 1, speed, distance);
}

void BiteKeeper::handle_alarm_frame_(const uint8_t *p, uint8_t len) {
  // serial, tone, colour, direction, B G R A, sound CRC
  if (len < 4)
    return;
  uint8_t aid = p[0];
  uint8_t direction = p[3];
  if (aid >= MAX_SLOTS)
    return;
  uint32_t now = millis();
  uint8_t prev = this->prev_dir_[aid];
  bool emit = false;
  if (prev == 0xFF) {
    // First packet after (re)connect: baseline only.
  } else if (direction == DIR_FORWARD) {
    // Fresh transition into 0 fires immediately; a sustained pull re-alerts
    // at most once per bite_repeat.
    if (prev != DIR_FORWARD || now - this->last_bite_ms_[aid] >= this->bite_repeat_) {
      emit = true;
      this->last_bite_ms_[aid] = now;
    }
  } else if (direction == DIR_BACKWARD && prev != DIR_BACKWARD) {
    emit = true;
  }
  this->prev_dir_[aid] = direction;
  if (!emit)
    return;
  this->event_count_++;
  this->last_event_alarm_ = aid + 1;
  this->last_event_dir_ = direction;
  this->last_event_ms_ = millis() | 1u;
  ESP_LOGI(TAG, "%s on alarm %u", direction == DIR_FORWARD ? "BITE" : "DROP", aid + 1);
  this->alarm_cb_.call(aid, direction);
}

void BiteKeeper::handle_snapshot_frame_(const uint8_t *p, uint8_t len) {
  // 4 records of 40 bytes; the first record's id says which half (0 or 4).
  static constexpr size_t REC = 40;
  uint8_t base = p[0];
  if (len < REC || (base != 0 && base != 4))
    return;
  for (size_t i = 0; i < 4 && (i + 1) * REC <= len; i++) {
    const uint8_t *r = p + i * REC;
    Slot &s = this->slots_[base + i];
    s.vol = r[1];
    s.tone = r[2];
    s.sens = r[3];
    s.color = r[4];
    s.charge = r[5] & 0x0F;
    s.drop_back = (r[5] & 0x30) == 0x10;
    s.mute = r[6] & 0x0F;
    s.drift = (r[6] & 0x30) == 0x10;
    s.status = r[7];
    s.online = r[7];
    uint16_t mv = (uint16_t) r[8] | ((uint16_t) r[9] << 8);
    s.mv = mv;
    s.mv_ema = ema_(s.mv_ema, mv);
    s.version = (uint16_t) r[10] | ((uint16_t) r[11] << 8);
    s.paired = s.version != 0 ? 1 : 0;  // empty positions are all zeros
    for (int k = 0; k < 6; k++) {         // colours stored as B G R A
      s.colors[k][0] = r[16 + 4 * k + 2];
      s.colors[k][1] = r[16 + 4 * k + 1];
      s.colors[k][2] = r[16 + 4 * k + 0];
    }
    const uint8_t *c = s.colors[s.color < 6 ? s.color : 0];
    s.r = c[0];
    s.g = c[1];
    s.b = c[2];
  }
  this->data_dirty_ = true;
  uint8_t mask = 0;
  for (uint8_t i = 0; i < MAX_SLOTS; i++)
    if (this->slots_[i].paired)
      mask |= 1u << i;
  if (!this->have_snapshot_ || mask != this->paired_mask_) {
    this->have_snapshot_ = true;
    this->paired_mask_ = mask;
    ESP_LOGI(TAG, "paired alarms: %u (mask 0x%02X)", this->alarm_count(), mask);
    this->alarms_cb_.call(this->alarm_count());
  }
}

uint8_t BiteKeeper::alarm_count() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_SLOTS; i++)
    n += this->slots_[i].paired ? 1 : 0;
  return n;
}

std::vector<uint8_t> BiteKeeper::paired_alarms() const {
  std::vector<uint8_t> out;
  for (uint8_t i = 0; i < MAX_SLOTS; i++)
    if (this->slots_[i].paired)
      out.push_back(i + 1);
  return out;
}

void BiteKeeper::slot_color_rgb(uint8_t slot, uint8_t &r, uint8_t &g, uint8_t &b) const {
  const Slot &s = this->slots_[slot % MAX_SLOTS];
  r = g = b = 255;
  switch (s.color) {
    case 0: r = 255; g = 0;   b = 0;   break;  // Red
    case 1: r = 0;   g = 255; b = 0;   break;  // Green
    case 2: r = 255; g = 255; b = 0;   break;  // Yellow
    case 3: r = 0;   g = 128; b = 255; break;  // Blue
    case 4: r = 180; g = 0;   b = 255; break;  // Violet
    case 5:                                     // Custom (block RGB)
      if (s.r | s.g | s.b) {
        r = s.r;
        g = s.g;
        b = s.b;
      }
      break;
    default:
      break;
  }
}

void BiteKeeper::publish_telemetry_() {
#ifdef USE_SENSOR
  if (this->rcv_voltage_sensor_ != nullptr && this->rcv_.mv_ema != 0) {
    float v = this->rcv_.mv_ema / 1000.0f;
    if (this->rcv_voltage_sensor_->get_raw_state() != v)
      this->rcv_voltage_sensor_->publish_state(v);
  }
  if (this->drift_sensor_ != nullptr && this->drift_sensor_->get_raw_state() != this->rcv_.drift_cm)
    this->drift_sensor_->publish_state(this->rcv_.drift_cm);
  if (this->have_snapshot_) {
    if (this->alarm_count_sensor_ != nullptr && this->alarm_count_sensor_->get_raw_state() != this->alarm_count())
      this->alarm_count_sensor_->publish_state(this->alarm_count());
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
      auto *s = this->slot_voltage_sensors_[i];
      if (s == nullptr)
        continue;
      // No alarm in this slot → "unknown" instead of a stale value.
      float v = (this->slots_[i].paired && this->slots_[i].mv_ema != 0) ? this->slots_[i].mv_ema / 1000.0f : NAN;
      float prev = s->get_raw_state();
      if (std::isnan(v) ? !std::isnan(prev) : prev != v)
        s->publish_state(v);
    }
  }
#endif
#ifdef USE_TEXT_SENSOR
  if (this->alarms_ts_ != nullptr && this->have_snapshot_) {
    // e.g. "1: 3.92 V online; 3: 3.85 V offline" — one entry per paired alarm.
    std::string out;
    char buf[40];
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
      const Slot &sl = this->slots_[i];
      if (!sl.paired)
        continue;
      snprintf(buf, sizeof(buf), "%s%u: %u.%02u V %s", out.empty() ? "" : "; ", i + 1,
               (unsigned) (sl.mv_ema / 1000), (unsigned) ((sl.mv_ema % 1000) / 10), sl.online ? "online" : "offline");
      out += buf;
    }
    if (out.empty())
      out = "none";
    if (this->alarms_ts_->get_state() != out)
      this->alarms_ts_->publish_state(out);
  }
#endif
}

}  // namespace bitekeeper
}  // namespace esphome

#endif  // USE_ESP32
