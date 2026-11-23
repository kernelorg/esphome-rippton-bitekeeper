#include "bk_entities.h"

#ifdef USE_ESP32

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace bitekeeper {

static const char *const TAG = "bitekeeper.entity";

// ── button ─────────────────────────────────────────────────────────────
#ifdef USE_BUTTON
void BiteKeeperButton::press_action() {
  switch (this->type_) {
    case BkButtonType::SCAN: this->parent_->scan_clear(); break;
    case BkButtonType::PAIR: this->parent_->pair_strongest(); break;
    case BkButtonType::FORGET: this->parent_->forget(); break;
    case BkButtonType::POLL: this->parent_->poll(); break;
  }
}
void BiteKeeperButton::dump_config() { LOG_BUTTON("", "Bite Cube button", this); }
#endif

// ── switch: running ────────────────────────────────────────────────────
#ifdef USE_SWITCH
void BiteKeeperSwitch::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  bool r = this->parent_->is_running();
  if (!this->has_state() || r != this->state)
    this->publish_state(r);
}

void BiteKeeperSwitch::write_state(bool state) {
  if (state)
    this->parent_->start();
  else
    this->parent_->stop();
  this->publish_state(state);
}
void BiteKeeperSwitch::dump_config() { LOG_SWITCH("", "Bite Cube running", this); }
#endif

// ── number: keep-alive period ──────────────────────────────────────────
#ifdef USE_NUMBER
void BiteKeeperNumber::setup() {
  float value = this->parent_->get_poll_interval() / 1000.0f;
  if (this->restore_value_) {
    this->pref_ = this->make_entity_preference<float>();
    float saved;
    if (this->pref_.load(&saved) && saved >= 0 && saved <= 3600)
      value = saved;
  }
  this->control(value);
}

void BiteKeeperNumber::control(float value) {
  this->parent_->set_poll_interval((uint32_t) (value * 1000.0f));
  this->publish_state(value);
  if (this->restore_value_)
    this->pref_.save(&value);
}
void BiteKeeperNumber::dump_config() { LOG_NUMBER("", "Bite Cube keep-alive", this); }
#endif

// ── number: alarm / receiver settings ──────────────────────────────────
#ifdef USE_NUMBER
static constexpr uint32_t BK_COMMAND_HOLD_MS = 3000;  // the cube reports a change in its next frames

optional<float> BiteKeeperSettingNumber::current_() const {
  auto *bk = this->parent_;
  if (this->type_ == BkSettingType::RECEIVER_VOLUME) {
    if (bk->receiver().mv == 0)
      return {};
    return (float) bk->receiver().vol;
  }
  const Slot &s = bk->slot(this->alarm_ - 1);
  if (!s.paired)
    return {};
  switch (this->type_) {
    case BkSettingType::ALARM_VOLUME: return (float) s.vol;
    case BkSettingType::ALARM_TONE: return (float) s.tone;
    case BkSettingType::ALARM_SENSITIVITY: return (float) s.sens;
    case BkSettingType::ALARM_COLOR: return (float) s.color;
    default: return {};
  }
}

void BiteKeeperSettingNumber::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  if (this->command_at_ != 0 && now - this->command_at_ < BK_COMMAND_HOLD_MS)
    return;
  this->command_at_ = 0;
  auto v = this->current_();
  if (v.has_value() && (!this->has_state() || *v != this->state))
    this->publish_state(*v);
}

void BiteKeeperSettingNumber::control(float value) {
  auto *bk = this->parent_;
  auto v = (uint8_t) value;
  bool ok = false;
  switch (this->type_) {
    case BkSettingType::ALARM_VOLUME: ok = bk->set_alarm_volume(this->alarm_, v); break;
    case BkSettingType::ALARM_TONE: ok = bk->set_alarm_tone(this->alarm_, v); break;
    case BkSettingType::ALARM_SENSITIVITY: ok = bk->set_alarm_sensitivity(this->alarm_, v); break;
    case BkSettingType::ALARM_COLOR: ok = bk->set_alarm_color(this->alarm_, v); break;
    case BkSettingType::RECEIVER_VOLUME: ok = bk->set_receiver_volume(v); break;
  }
  if (!ok)
    return;
  this->command_at_ = millis() | 1u;
  this->publish_state(value);
}
void BiteKeeperSettingNumber::dump_config() { LOG_NUMBER("", "Bite Cube setting", this); }
#endif

// ── switch: alarm / receiver settings ──────────────────────────────────
#ifdef USE_SWITCH
optional<bool> BiteKeeperSettingSwitch::current_() const {
  const Slot &s = this->parent_->slot(this->alarm_ - 1);
  switch (this->type_) {
    case BkSwitchType::ALARM_MUTE:
      if (!s.paired)
        return {};
      return s.mute != 0;
    case BkSwitchType::DROP_BACK:
      if (!s.paired)
        return {};
      return s.drop_back;
    default:
      return {};  // not reported by the cube: last command sent
  }
}

void BiteKeeperSettingSwitch::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  if (this->command_at_ != 0 && now - this->command_at_ < 3000)
    return;
  this->command_at_ = 0;
  auto v = this->current_();
  if (v.has_value() && (!this->has_state() || *v != this->state))
    this->publish_state(*v);
}

void BiteKeeperSettingSwitch::write_state(bool state) {
  auto *bk = this->parent_;
  bool ok = false;
  switch (this->type_) {
    case BkSwitchType::ALARM_MUTE: ok = bk->set_alarm_mute(this->alarm_, state); break;
    case BkSwitchType::DROP_BACK: ok = bk->set_alarm_drop_back(this->alarm_, state); break;
    case BkSwitchType::MUTE_ALL: ok = bk->set_mute_all(state); break;
    case BkSwitchType::VIBRATION: ok = bk->set_receiver_vibration(state); break;
    case BkSwitchType::DRIFT: ok = bk->set_drift(state); break;
  }
  if (!ok)
    return;
  this->command_at_ = millis() | 1u;
  this->publish_state(state);
}
void BiteKeeperSettingSwitch::dump_config() { LOG_SWITCH("", "Bite Cube setting", this); }
#endif

// ── event ──────────────────────────────────────────────────────────────
#ifdef USE_EVENT
void BiteKeeperEvent::setup() {
  this->parent_->add_on_alarm_callback([this](uint8_t slot, uint8_t dir) {
    if (this->alarms_ == 0 || (this->alarms_ & (1u << slot)))
      this->trigger(dir == DIR_FORWARD ? "bite" : "drop");
  });
}
void BiteKeeperEvent::dump_config() { LOG_EVENT("", "Bite Cube event", this); }
#endif

// ── binary sensor: alarm online ────────────────────────────────────────
#ifdef USE_BINARY_SENSOR
void BiteKeeperAlarmBinarySensor::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  const Slot &s = this->parent_->slot(this->alarm_ - 1);
  if (!s.paired)
    return;
  this->publish_state(s.online != 0);
}
void BiteKeeperAlarmBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "Bite Cube alarm online", this); }
#endif

// ── sensor: drift of alarm N ───────────────────────────────────────────
#ifdef USE_SENSOR
void BiteKeeperDriftSensor::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  const Slot &s = this->parent_->slot(this->alarm_ - 1);
  if (!s.paired)
    return;
  float v = (float) s.drift_cm;
  if (std::isnan(this->state) || v != this->state)
    this->publish_state(v);
}
void BiteKeeperDriftSensor::dump_config() { LOG_SENSOR("", "Bite Cube drift", this); }
#endif

// ── sensor: event count ────────────────────────────────────────────────
#ifdef USE_SENSOR
void BiteKeeperCountSensor::loop() {
  uint32_t now = millis();
  if (this->published_ && now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  float v = (float) this->parent_->event_count();
  if (!this->published_ || v != this->state) {
    this->published_ = true;
    this->publish_state(v);
  }
}
void BiteKeeperCountSensor::dump_config() { LOG_SENSOR("", "Bite Cube event count", this); }
#endif

// ── text sensor ────────────────────────────────────────────────────────
#ifdef USE_TEXT_SENSOR
std::string BiteKeeperTextSensor::value_() const {
  auto *bk = this->parent_;
  char b[48];
  switch (this->type_) {
    case BkTextType::LAST_EVENT:
      if (bk->last_event_alarm() == 0)
        return "";
      snprintf(b, sizeof(b), "%u: %s", bk->last_event_alarm(),
               bk->last_event_direction() == DIR_FORWARD ? "bite" : "drop");
      return b;
    case BkTextType::SCAN_RESULTS: {
      std::string out;
      for (const auto &e : bk->scan_results()) {
        snprintf(b, sizeof(b), "%s %s %d dBm", e.mac.c_str(), e.name.c_str(), e.rssi);
        if (out.size() + strlen(b) + 2 > 255)
          break;
        if (!out.empty())
          out += "; ";
        out += b;
      }
      return out;
    }
    case BkTextType::PAIRED_MAC:
      return bk->paired_mac();
  }
  return "";
}

void BiteKeeperTextSensor::loop() {
  uint32_t now = millis();
  if (this->published_ && now - this->last_check_ < BK_ENTITY_CHECK_MS)
    return;
  this->last_check_ = now;
  std::string v = this->value_();
  if (!this->published_ || v != this->state) {
    this->published_ = true;
    this->publish_state(v);
  }
}
void BiteKeeperTextSensor::dump_config() { LOG_TEXT_SENSOR("", "Bite Cube text sensor", this); }
#endif

}  // namespace bitekeeper
}  // namespace esphome

#endif  // USE_ESP32
