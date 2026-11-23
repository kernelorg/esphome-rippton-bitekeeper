#pragma once

// button / switch / number / event platforms and the polled binary sensor,
// sensor and text sensor types of bitekeeper.

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "bitekeeper.h"

#ifdef USE_ESP32

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#include "esphome/core/preferences.h"
#endif
#ifdef USE_EVENT
#include "esphome/components/event/event.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome {
namespace bitekeeper {

static constexpr uint32_t BK_ENTITY_CHECK_MS = 1000;

#ifdef USE_BUTTON
enum class BkButtonType : uint8_t { SCAN, PAIR, FORGET, POLL };

class BiteKeeperButton : public button::Button, public Component, public Parented<BiteKeeper> {
 public:
  void set_type(BkButtonType t) { this->type_ = t; }
  void dump_config() override;

 protected:
  void press_action() override;
  BkButtonType type_{BkButtonType::SCAN};
};
#endif

#ifdef USE_SWITCH
/// Scanning + the link to the cube (start / stop).
class BiteKeeperSwitch : public switch_::Switch, public Component, public Parented<BiteKeeper> {
 public:
  void loop() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
  uint32_t last_check_{0};
};
#endif

#ifdef USE_NUMBER
/// Keep-alive period in seconds (0 = off), saved over a reboot.
class BiteKeeperNumber : public number::Number, public Component, public Parented<BiteKeeper> {
 public:
  void set_restore_value(bool r) { this->restore_value_ = r; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(float value) override;
  bool restore_value_{true};
  ESPPreferenceObject pref_;
};
#endif

#ifdef USE_NUMBER
enum class BkSettingType : uint8_t { ALARM_VOLUME, ALARM_TONE, ALARM_SENSITIVITY, ALARM_COLOR, RECEIVER_VOLUME };

/// An alarm / receiver setting: shows the cube's value, sends a change.
class BiteKeeperSettingNumber : public number::Number, public Component, public Parented<BiteKeeper> {
 public:
  void set_type(BkSettingType t) { this->type_ = t; }
  void set_alarm(uint8_t n) { this->alarm_ = n; }
  void loop() override;
  void dump_config() override;

 protected:
  void control(float value) override;
  optional<float> current_() const;
  BkSettingType type_{BkSettingType::ALARM_VOLUME};
  uint8_t alarm_{1};
  uint32_t last_check_{0};
  uint32_t command_at_{0};
};
#endif

#ifdef USE_SWITCH
enum class BkSwitchType : uint8_t { ALARM_MUTE, DROP_BACK, MUTE_ALL, VIBRATION, DRIFT };

/// Alarm mute / drop-back mode (state from the cube), mute everything,
/// receiver vibration and the drift alarm (last command sent).
class BiteKeeperSettingSwitch : public switch_::Switch, public Component, public Parented<BiteKeeper> {
 public:
  void set_type(BkSwitchType t) { this->type_ = t; }
  void set_alarm(uint8_t n) { this->alarm_ = n; }
  void loop() override;
  void dump_config() override;
  bool assumed_state() override { return !this->current_().has_value(); }

 protected:
  void write_state(bool state) override;
  optional<bool> current_() const;
  BkSwitchType type_{BkSwitchType::ALARM_MUTE};
  uint8_t alarm_{1};
  uint32_t last_check_{0};
  uint32_t command_at_{0};
};
#endif

#ifdef USE_EVENT
/// "bite" / "drop" events of the selected alarms (bit N-1 = alarm N, 0 = all).
class BiteKeeperEvent : public event::Event, public Component, public Parented<BiteKeeper> {
 public:
  void set_alarms(uint8_t mask) { this->alarms_ = mask; }
  void setup() override;
  void dump_config() override;

 protected:
  uint8_t alarms_{0};
};
#endif

#ifdef USE_BINARY_SENSOR
/// Alarm N is online (unknown while no alarm is paired in that position).
class BiteKeeperAlarmBinarySensor : public binary_sensor::BinarySensor, public Component, public Parented<BiteKeeper> {
 public:
  void set_alarm(uint8_t n) { this->alarm_ = n; }
  void loop() override;
  void dump_config() override;

 protected:
  uint8_t alarm_{1};
  uint32_t last_check_{0};
};
#endif

#ifdef USE_SENSOR
/// Drift distance of alarm N, cm.
class BiteKeeperDriftSensor : public sensor::Sensor, public Component, public Parented<BiteKeeper> {
 public:
  void set_alarm(uint8_t n) { this->alarm_ = n; }
  void loop() override;
  void dump_config() override;

 protected:
  uint8_t alarm_{1};
  uint32_t last_check_{0};
};

/// Bites + drop-backs since boot.
class BiteKeeperCountSensor : public sensor::Sensor, public Component, public Parented<BiteKeeper> {
 public:
  void loop() override;
  void dump_config() override;

 protected:
  uint32_t last_check_{0};
  bool published_{false};
};
#endif

#ifdef USE_TEXT_SENSOR
enum class BkTextType : uint8_t { LAST_EVENT, SCAN_RESULTS, PAIRED_MAC };

class BiteKeeperTextSensor : public text_sensor::TextSensor, public Component, public Parented<BiteKeeper> {
 public:
  void set_type(BkTextType t) { this->type_ = t; }
  void loop() override;
  void dump_config() override;

 protected:
  std::string value_() const;
  BkTextType type_{BkTextType::LAST_EVENT};
  uint32_t last_check_{0};
  bool published_{false};
};
#endif

}  // namespace bitekeeper
}  // namespace esphome

#endif  // USE_ESP32
