#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/levoit/levoit.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace levoit {

enum LevoitSensorPurpose : uint8_t { PM25, AIR_QUALITY };

class LevoitSensor : public Component, public sensor::Sensor {
 public:
  LevoitSensor(Levoit *parent, LevoitSensorPurpose purpose) : parent_(parent), purpose_(purpose) {}
  void setup() override;
  void dump_config() override;

 protected:
  Levoit *parent_;
  LevoitSensorPurpose purpose_;
};

// Filter life tracking, ported from the stock Levoit Core 300S ESP32 firmware (1.1.09).
// Running time counts minutes while powered on, dust accumulates every 10 s from a fan level x PM2.5 loss table.
// The reported percentage follows the dust model, clamped between the 2880 h and 4000 h running time lines.
class LevoitFilter : public PollingComponent {
 public:
  LevoitFilter(Levoit *parent) : PollingComponent(10000), parent_(parent) {}
  void setup() override;
  void update() override;
  void dump_config() override;
  void set_initial_state(uint32_t useMinutes, uint32_t dust) {
    useMinutes_ = useMinutes;
    dust_ = dust;
  }
  void set_life_sensor(sensor::Sensor *lifeSensor) { lifeSensor_ = lifeSensor; }
  void set_runtime_sensor(sensor::Sensor *runtimeSensor) { runtimeSensor_ = runtimeSensor; }

 protected:
  uint8_t get_percent_();
  void save_();
  void publish_();

  Levoit *parent_;
  sensor::Sensor *lifeSensor_ = nullptr;
  sensor::Sensor *runtimeSensor_ = nullptr;
  ESPPreferenceObject pref_;
  uint32_t useMinutes_ = 0;
  uint32_t dust_ = 0;
  uint8_t ticks_ = 0;
  bool wasPowered_ = false;
  // "purify cycle" monitor: consecutive minutes with PM2.5 >= 50
  int32_t highPmMinutes_ = -1;
  bool useReferenceRate_ = false;
  bool ledSent_ = false;
  bool ledOn_ = false;
};

}  // namespace levoit
}  // namespace esphome
