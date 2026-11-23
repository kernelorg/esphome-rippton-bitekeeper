#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#ifdef USE_ESP32

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include <esp_gattc_api.h>
#include <string>
#include <vector>

namespace esphome {
namespace bitekeeper {

namespace espbt = esphome::esp32_ble_tracker;

static constexpr uint8_t MAX_SLOTS = 8;
static constexpr uint8_t MAX_SCAN = 8;

/// Receiver (the cube itself) telemetry: FE 1A + FE 10 frames.
struct Receiver {
  uint8_t vol{0};
  uint8_t mode{0};
  uint8_t mute{0};
  uint8_t charge{0};   // charging state (low nibble of the status byte)
  uint8_t tone_status{0};
  uint16_t mv{0};      // raw cell voltage, mV
  uint16_t mv_ema{0};  // smoothed (alpha 1/4, min ±1 step)
  uint16_t version{0};
  uint8_t drift_cm{0};       // alarm 1's drift values (kept for the drift sensor)
  uint8_t drift_enabled{0};
};

/// One bonded bite alarm: FE A0 snapshot blocks.
struct Slot {
  uint8_t vol{0};     // 0..9
  uint8_t tone{0};    // 0..8
  uint8_t sens{0};    // 0..9
  uint8_t color{0};   // index 0..5 into colors[]
  uint8_t charge{0};
  uint8_t mute{0};
  bool drop_back{false};   // drop-back alarm enabled
  bool drift{false};       // drift ("weather") alarm enabled
  uint8_t status{0};
  uint16_t mv{0};
  uint16_t mv_ema{0};
  uint16_t version{0};
  uint8_t online{0};
  uint8_t paired{0};  // a paired alarm sits in this position (version != 0)
  uint8_t colors[6][3]{};  // the 6 stored colours, R G B
  uint8_t r{0}, g{0}, b{0};  // colors[color]
  uint8_t drift_cm{0};       // drift distance / status (wave message)
  uint8_t drift_status{0};
};

/// COMMAND_INT ids (message 4).
enum CubeCommand : uint8_t {
  CMD_ALARM_VOLUME = 0,
  CMD_ALARM_TONE = 1,
  CMD_ALARM_SENSITIVITY = 2,
  CMD_ALARM_COLOR = 3,
  CMD_ALARM_MUTE = 4,
  CMD_RECEIVER_VOLUME = 5,
  CMD_RECEIVER_VIBRATION = 7,
  CMD_MUTE_ALL = 8,
  CMD_DRIFT = 10,
  CMD_DROP_BACK = 11,
  CMD_RGB_LEVEL = 12,
  CMD_ANTI_THEFT = 13,
  CMD_NAV_LIGHT = 14,
  CMD_REGION = 128,
};

struct ScanEntry {
  std::string mac;
  std::string name;
  int rssi{0};
};

enum Direction : uint8_t { DIR_FORWARD = 0, DIR_BACKWARD = 1 };

class BiteKeeper : public Component, public ble_client::BLEClientNode, public espbt::ESPBTDeviceListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  bool parse_device(const espbt::ESPBTDevice &device) override;

  // ── configuration ──────────────────────────────────────────────────
  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }
  uint32_t get_poll_interval() const { return this->poll_interval_; }
  void set_connect_delay(uint32_t ms) { this->connect_delay_ = ms; }
  void set_watchdog_min(uint32_t ms) { this->watchdog_min_ = ms; }
  void set_stop_scan_on_connect(bool v) { this->stop_scan_on_connect_ = v; }
  void set_bite_repeat(uint32_t ms) { this->bite_repeat_ = ms; }
  /// Start automatically once the BLE stack is up (default). Set false when
  /// the configuration drives start()/stop() itself.
  void set_auto_start(bool v) { this->auto_start_pending_ = v; }

  // ── radio / link control ───────────────────────────────────────────
  /// Start scanning and, if a cube is paired, connect to it.
  void start();
  /// Drop the link and stop scanning.
  void stop();
  bool is_running() const { return this->running_; }
  bool is_connected() const { return this->link_up_; }
  /// Re-send the FFF2 keep-alive triplet now.
  void poll();

  // ── settings (COMMAND_INT); alarm = 1..8. false = no link / bad argument ──
  bool send_command(uint8_t command, uint8_t alarm_index, uint8_t p1, uint8_t p2 = 0, uint8_t p3 = 0, uint8_t p4 = 0);
  bool set_alarm_volume(uint8_t alarm, uint8_t v) { return this->alarm_cmd_(CMD_ALARM_VOLUME, alarm, v, 9); }
  bool set_alarm_tone(uint8_t alarm, uint8_t v) { return this->alarm_cmd_(CMD_ALARM_TONE, alarm, v, 8); }
  bool set_alarm_sensitivity(uint8_t alarm, uint8_t v) { return this->alarm_cmd_(CMD_ALARM_SENSITIVITY, alarm, v, 9); }
  bool set_alarm_mute(uint8_t alarm, bool on) { return this->alarm_cmd_(CMD_ALARM_MUTE, alarm, on ? 1 : 0, 1); }
  bool set_alarm_drop_back(uint8_t alarm, bool on) { return this->alarm_cmd_(CMD_DROP_BACK, alarm, on ? 1 : 0, 1); }
  /// Select one of the alarm's 6 stored colours (index 0..5).
  bool set_alarm_color(uint8_t alarm, uint8_t index);
  /// Drift alarm of one alarm: speed 10..40 (0.1 cm/s), distance 5..20 cm.
  bool set_alarm_drift(uint8_t alarm, uint8_t speed, uint8_t distance);
  /// Drift alarm on / off for every alarm.
  bool set_drift(bool on) { return this->send_command(CMD_DRIFT, 0xFF, on ? 1 : 0); }
  bool set_receiver_volume(uint8_t v) { return this->send_command(CMD_RECEIVER_VOLUME, 0, v); }
  bool set_receiver_vibration(bool on) { return this->send_command(CMD_RECEIVER_VIBRATION, 0, on ? 1 : 0); }
  bool set_mute_all(bool on) { return this->send_command(CMD_MUTE_ALL, 0xFF, on ? 1 : 0); }
  /// Last COMMAND_ACK: command id and status (0 ok, 1 fail, 2 need request,
  /// 3 not supported, 4 bad parameter, 5 OTA running); 0xFF = none yet.
  uint8_t last_ack_command() const { return this->last_ack_cmd_; }
  uint8_t last_ack_status() const { return this->last_ack_status_; }
  void add_on_command_ack_callback(std::function<void(uint8_t, uint8_t)> &&cb) { this->ack_cb_.add(std::move(cb)); }

  // ── pairing (persisted under a fixed preference key) ───────────────
  void pair(const std::string &mac, const std::string &name = "");
  void pair_scan_result(size_t index);
  /// Pair with the strongest receiver found by the last scan (scan_clear()).
  bool pair_strongest();
  void forget();
  bool is_paired() const { return !this->paired_mac_.empty(); }
  const std::string &paired_mac() const { return this->paired_mac_; }
  const std::string &paired_name() const { return this->paired_name_; }
  int paired_rssi() const { return this->paired_rssi_; }

  // ── scan list (receivers advertising service FFF0) ─────────────────
  void scan_clear();
  void set_scan_collect(bool v) { this->scan_collect_ = v; }
  const std::vector<ScanEntry> &scan_results() const { return this->scan_; }

  // ── telemetry ──────────────────────────────────────────────────────
  const Receiver &receiver() const { return this->rcv_; }
  const Slot &slot(uint8_t i) const { return this->slots_[i % MAX_SLOTS]; }
  uint32_t event_count() const { return this->event_count_; }
  /// Last bite / drop-back: alarm 1..8 (0 = none yet), direction, millis().
  uint8_t last_event_alarm() const { return this->last_event_alarm_; }
  uint8_t last_event_direction() const { return this->last_event_dir_; }
  uint32_t last_event_ms() const { return this->last_event_ms_; }
  /// Number of alarms the cube currently reports as paired (0..8; known
  /// after the first FE A0 snapshot).
  uint8_t alarm_count() const;
  /// Paired alarm numbers (1..8) in ascending order.
  std::vector<uint8_t> paired_alarms() const;
  /// Last direction byte seen for a slot: 0 forward, 1 backward, 0xFF none yet.
  uint8_t last_direction(uint8_t i) const { return this->prev_dir_[i % MAX_SLOTS]; }
  uint32_t last_notify_ms() const { return this->last_notify_ms_; }
  /// Slot LED colour as RGB (presets 0..4, custom 5 from the block bytes).
  void slot_color_rgb(uint8_t slot, uint8_t &r, uint8_t &g, uint8_t &b) const;

  // ── callbacks (triggers) ───────────────────────────────────────────
  void add_on_alarm_callback(std::function<void(uint8_t, uint8_t)> &&cb) { this->alarm_cb_.add(std::move(cb)); }
  void add_on_connect_callback(std::function<void()> &&cb) { this->connect_cb_.add(std::move(cb)); }
  void add_on_disconnect_callback(std::function<void()> &&cb) { this->disconnect_cb_.add(std::move(cb)); }
  void add_on_data_callback(std::function<void()> &&cb) { this->data_cb_.add(std::move(cb)); }
  void add_on_alarms_change_callback(std::function<void(uint8_t)> &&cb) { this->alarms_cb_.add(std::move(cb)); }

#ifdef USE_SENSOR
  void set_receiver_voltage_sensor(sensor::Sensor *s) { this->rcv_voltage_sensor_ = s; }
  void set_drift_sensor(sensor::Sensor *s) { this->drift_sensor_ = s; }
  void set_rssi_sensor(sensor::Sensor *s) { this->rssi_sensor_ = s; }
  void set_slot_voltage_sensor(uint8_t slot, sensor::Sensor *s) { this->slot_voltage_sensors_[slot % MAX_SLOTS] = s; }
  void set_alarm_count_sensor(sensor::Sensor *s) { this->alarm_count_sensor_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_connected_binary_sensor(binary_sensor::BinarySensor *s) { this->connected_bs_ = s; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_status_text_sensor(text_sensor::TextSensor *s) { this->status_ts_ = s; }
  void set_paired_name_text_sensor(text_sensor::TextSensor *s) { this->paired_name_ts_ = s; }
  void set_alarms_text_sensor(text_sensor::TextSensor *s) { this->alarms_ts_ = s; }
#endif

 protected:
  struct PairPref {
    char mac[18];
    char name[30];
  };

  void apply_address_();
  void save_pair_();
  void set_link_(bool up);
  void handle_notify_(const uint8_t *buf, size_t len);
  void handle_alarm_frame_(const uint8_t *p, uint8_t len);
  void handle_snapshot_frame_(const uint8_t *p, uint8_t len);
  void watchdog_();
  void write_poll_(uint8_t step);
  bool write_frame_(const uint8_t *data, size_t len);
  bool alarm_cmd_(uint8_t command, uint8_t alarm, uint8_t value, uint8_t max);
  void handle_rx_frame_(const uint8_t *p, uint8_t len);
  void handle_wave_frame_(const uint8_t *p, uint8_t len);
  void handle_ack_frame_(const uint8_t *p, uint8_t len);
  void publish_telemetry_();
  static uint16_t ema_(uint16_t prev, uint16_t now);

  // config
  uint32_t poll_interval_{15000};
  uint32_t connect_delay_{1500};
  uint32_t watchdog_min_{30000};
  uint32_t bite_repeat_{1000};
  bool stop_scan_on_connect_{true};
  bool auto_start_pending_{true};

  // state
  bool running_{false};
  bool link_up_{false};
  bool reconnecting_{false};
  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};
  uint32_t connected_at_{0};
  bool first_poll_pending_{false};
  uint32_t last_poll_ms_{0};
  uint32_t last_notify_ms_{0};
  uint32_t last_watchdog_ms_{0};

  std::string paired_mac_;
  std::string paired_name_;
  int paired_rssi_{0};
  ESPPreferenceObject pref_;

  bool scan_collect_{false};
  std::vector<ScanEntry> scan_;

  Receiver rcv_;
  Slot slots_[MAX_SLOTS];
  uint8_t prev_dir_[MAX_SLOTS];
  uint32_t last_bite_ms_[MAX_SLOTS]{};
  uint32_t event_count_{0};
  uint8_t last_event_alarm_{0};
  uint8_t last_event_dir_{0};
  uint32_t last_event_ms_{0};
  bool data_dirty_{false};
  uint8_t paired_mask_{0};
  bool have_snapshot_{false};

  CallbackManager<void(uint8_t, uint8_t)> alarm_cb_;
  CallbackManager<void()> connect_cb_;
  CallbackManager<void()> disconnect_cb_;
  CallbackManager<void()> data_cb_;
  CallbackManager<void(uint8_t)> alarms_cb_;
  CallbackManager<void(uint8_t, uint8_t)> ack_cb_;
  uint8_t tx_seq_{1};
  uint8_t last_ack_cmd_{0xFF};
  uint8_t last_ack_status_{0xFF};

#ifdef USE_SENSOR
  sensor::Sensor *rcv_voltage_sensor_{nullptr};
  sensor::Sensor *drift_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  sensor::Sensor *alarm_count_sensor_{nullptr};
  sensor::Sensor *slot_voltage_sensors_[MAX_SLOTS]{};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *connected_bs_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *status_ts_{nullptr};
  text_sensor::TextSensor *paired_name_ts_{nullptr};
  text_sensor::TextSensor *alarms_ts_{nullptr};
#endif
};

// ── triggers ───────────────────────────────────────────────────────────

// Triggers take an alarm filter: bit N-1 = alarm N, 0 = every alarm.
class AlarmTrigger : public Trigger<uint8_t, uint8_t> {
 public:
  /// direction: DIR_FORWARD / DIR_BACKWARD, or -1 for both.
  AlarmTrigger(BiteKeeper *p, uint8_t alarms, int8_t direction) {
    // Exposed to YAML as alarm number 1..8 (slot index + 1).
    p->add_on_alarm_callback([this, alarms, direction](uint8_t slot, uint8_t dir) {
      if ((alarms == 0 || (alarms & (1u << slot))) && (direction < 0 || dir == direction))
        this->trigger(slot + 1, dir);
    });
  }
};

class BiteTrigger : public Trigger<uint8_t> {
 public:
  BiteTrigger(BiteKeeper *p, uint8_t alarms) {
    p->add_on_alarm_callback([this, alarms](uint8_t slot, uint8_t dir) {
      if (dir == DIR_FORWARD && (alarms == 0 || (alarms & (1u << slot))))
        this->trigger(slot + 1);
    });
  }
};

class DropTrigger : public Trigger<uint8_t> {
 public:
  DropTrigger(BiteKeeper *p, uint8_t alarms) {
    p->add_on_alarm_callback([this, alarms](uint8_t slot, uint8_t dir) {
      if (dir == DIR_BACKWARD && (alarms == 0 || (alarms & (1u << slot))))
        this->trigger(slot + 1);
    });
  }
};

class ConnectTrigger : public Trigger<> {
 public:
  explicit ConnectTrigger(BiteKeeper *p) {
    p->add_on_connect_callback([this]() { this->trigger(); });
  }
};

class DisconnectTrigger : public Trigger<> {
 public:
  explicit DisconnectTrigger(BiteKeeper *p) {
    p->add_on_disconnect_callback([this]() { this->trigger(); });
  }
};

class DataTrigger : public Trigger<> {
 public:
  explicit DataTrigger(BiteKeeper *p) {
    p->add_on_data_callback([this]() { this->trigger(); });
  }
};

class CommandAckTrigger : public Trigger<uint8_t, uint8_t> {
 public:
  explicit CommandAckTrigger(BiteKeeper *p) {
    p->add_on_command_ack_callback([this](uint8_t cmd, uint8_t status) { this->trigger(cmd, status); });
  }
};

class AlarmsChangeTrigger : public Trigger<uint8_t> {
 public:
  explicit AlarmsChangeTrigger(BiteKeeper *p) {
    p->add_on_alarms_change_callback([this](uint8_t count) { this->trigger(count); });
  }
};

// ── actions ────────────────────────────────────────────────────────────

template<typename... Ts> class StartAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  void play(const Ts &...x) override { this->parent_->start(); }
};

template<typename... Ts> class StopAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

template<typename... Ts> class PollAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  void play(const Ts &...x) override { this->parent_->poll(); }
};

template<typename... Ts> class ForgetAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  void play(const Ts &...x) override { this->parent_->forget(); }
};

template<typename... Ts> class ScanClearAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  void play(const Ts &...x) override { this->parent_->scan_clear(); }
};

template<typename... Ts> class PairAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  TEMPLATABLE_VALUE(std::string, mac)
  TEMPLATABLE_VALUE(std::string, name)
  void play(const Ts &...x) override {
    // No MAC: the strongest receiver found by the last scan.
    if (!this->mac_.has_value()) {
      this->parent_->pair_strongest();
      return;
    }
    this->parent_->pair(this->mac_.value(x...), this->name_.value_or(x..., std::string()));
  }
};

template<typename... Ts> class SetAlarmAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  TEMPLATABLE_VALUE(uint8_t, alarm)
  TEMPLATABLE_VALUE(uint8_t, volume)
  TEMPLATABLE_VALUE(uint8_t, tone)
  TEMPLATABLE_VALUE(uint8_t, sensitivity)
  TEMPLATABLE_VALUE(uint8_t, color)
  TEMPLATABLE_VALUE(bool, mute)
  TEMPLATABLE_VALUE(bool, drop_back)
  void play(const Ts &...x) override {
    auto *bk = this->parent_;
    uint8_t a = this->alarm_.value(x...);
    if (this->volume_.has_value())
      bk->set_alarm_volume(a, this->volume_.value(x...));
    if (this->tone_.has_value())
      bk->set_alarm_tone(a, this->tone_.value(x...));
    if (this->sensitivity_.has_value())
      bk->set_alarm_sensitivity(a, this->sensitivity_.value(x...));
    if (this->color_.has_value())
      bk->set_alarm_color(a, this->color_.value(x...));
    if (this->mute_.has_value())
      bk->set_alarm_mute(a, this->mute_.value(x...));
    if (this->drop_back_.has_value())
      bk->set_alarm_drop_back(a, this->drop_back_.value(x...));
  }
};

template<typename... Ts> class SetReceiverAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  TEMPLATABLE_VALUE(uint8_t, volume)
  TEMPLATABLE_VALUE(bool, vibration)
  TEMPLATABLE_VALUE(bool, mute_all)
  void play(const Ts &...x) override {
    if (this->volume_.has_value())
      this->parent_->set_receiver_volume(this->volume_.value(x...));
    if (this->vibration_.has_value())
      this->parent_->set_receiver_vibration(this->vibration_.value(x...));
    if (this->mute_all_.has_value())
      this->parent_->set_mute_all(this->mute_all_.value(x...));
  }
};

template<typename... Ts> class SetDriftAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  TEMPLATABLE_VALUE(uint8_t, alarm)
  TEMPLATABLE_VALUE(bool, enabled)
  TEMPLATABLE_VALUE(uint8_t, speed)
  TEMPLATABLE_VALUE(uint8_t, distance)
  void play(const Ts &...x) override {
    if (this->alarm_.has_value())
      this->parent_->set_alarm_drift(this->alarm_.value(x...), this->speed_.value(x...), this->distance_.value(x...));
    else
      this->parent_->set_drift(this->enabled_.value_or(x..., true));
  }
};

template<typename... Ts> class SendCommandAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  TEMPLATABLE_VALUE(uint8_t, command)
  TEMPLATABLE_VALUE(uint8_t, alarm_index)
  TEMPLATABLE_VALUE(uint8_t, param1)
  TEMPLATABLE_VALUE(uint8_t, param2)
  TEMPLATABLE_VALUE(uint8_t, param3)
  TEMPLATABLE_VALUE(uint8_t, param4)
  void play(const Ts &...x) override {
    this->parent_->send_command(this->command_.value(x...), this->alarm_index_.value_or(x..., 0),
                                this->param1_.value_or(x..., 0), this->param2_.value_or(x..., 0),
                                this->param3_.value_or(x..., 0), this->param4_.value_or(x..., 0));
  }
};

template<typename... Ts> class SetPollIntervalAction : public Action<Ts...>, public Parented<BiteKeeper> {
 public:
  TEMPLATABLE_VALUE(uint32_t, interval)
  void play(const Ts &...x) override { this->parent_->set_poll_interval(this->interval_.value(x...)); }
};

}  // namespace bitekeeper
}  // namespace esphome

#endif  // USE_ESP32
