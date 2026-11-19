#include "esphome/core/log.h"
#include "levoit_sensor.h"

namespace esphome {
namespace levoit {

static const char *const TAG = "levoit.sensor";

void LevoitSensor::setup() {
  switch (this->purpose_) {
    case LevoitSensorPurpose::AIR_QUALITY:
      this->parent_->register_state_listener(
        static_cast<uint32_t>(LevoitState::AIR_QUALITY_CHANGE),
        [this](uint32_t currentBits) {
          this->publish_state(this->parent_->air_quality);
          this->parent_->set_request_state(0, static_cast<uint32_t>(LevoitState::AIR_QUALITY_CHANGE), false);
        }
      );
      break;
    case LevoitSensorPurpose::PM25:
      this->parent_->register_state_listener(
        static_cast<uint32_t>(LevoitState::PM25_CHANGE) |
        static_cast<uint32_t>(LevoitState::PM25_NAN),
        [this](uint32_t currentBits) {
          if (currentBits & static_cast<uint32_t>(LevoitState::PM25_NAN)) {
            this->publish_state(NAN);
            this->parent_->set_request_state(0, static_cast<uint32_t>(LevoitState::PM25_NAN), false);
          } else {
            uint16_t integer_part = this->parent_->pm25_value / 10;
            uint16_t decimal_part = this->parent_->pm25_value % 10;
            float pm25Publish = integer_part + (decimal_part / 10.0);

            this->publish_state(pm25Publish);
            this->parent_->set_request_state(0, static_cast<uint32_t>(LevoitState::PM25_CHANGE), false);
          }
        }
      );
      break;
  }

}

void LevoitSensor::dump_config() { LOG_SENSOR("", "Levoit Sensor", this); }

// Dust added per 10 s tick is FILTER_LOSS_TABLE / 6, rows = fan level 0..3, columns = PM2.5 level 0..4
static const uint16_t FILTER_LOSS_TABLE[4][5] = {
  {100, 300, 750, 1250, 2000},
  {166, 500, 1250, 2083, 3333},
  {225, 675, 1687, 2812, 4500},
  {383, 1150, 2875, 4791, 7666},
};
static const uint16_t FILTER_PM25_LEVELS[4] = {10, 50, 100, 150};
static const uint32_t FILTER_REFERENCE_LOSS_RATE = 520;
static const uint32_t FILTER_MIN_LIFETIME_MINUTES = 172800;  // 2880 h
static const uint32_t FILTER_MAX_LIFETIME_MINUTES = 240000;  // 4000 h
static const uint32_t FILTER_TOTAL_CMM = 9369000;
static const uint32_t FILTER_DUST_LIMIT = 281070000;
static const uint16_t FILTER_HIGH_PM25 = 49;

struct LevoitFilterState {
  uint32_t useMinutes;
  uint32_t dust;
};

static uint8_t filter_calc_percent(uint32_t value, uint32_t limit) {
  if (value > limit) {
    return 0;
  }
  uint32_t percent = (100 * (limit - value)) / limit;
  return percent <= 99 ? percent + 1 : percent;
}

void LevoitFilter::setup() {
  pref_ = global_preferences->make_preference<LevoitFilterState>(fnv1_hash("levoit_filter"), true);
  LevoitFilterState state{};
  if (pref_.load(&state)) {
    useMinutes_ = state.useMinutes;
    dust_ = state.dust;
  } else {
    save_();
  }
  publish_();
}

void LevoitFilter::update() {
  bool power = false;
  uint8_t fanLevel = 0xFF;
  uint16_t pm25 = 0;
  bool statusSeen = false;
  bool resetRequested = false;
  if (!parent_->get_filter_inputs(power, fanLevel, pm25, statusSeen, resetRequested)) {
    return;
  }

  if (resetRequested) {
    ESP_LOGI(TAG, "Filter: reset");
    useMinutes_ = 0;
    dust_ = 0;
    save_();
    publish_();
  }

  // stock: the PM2.5 monitor starts with the purifier, the reference loss rate ends with it
  if (power && !wasPowered_ && pm25 > FILTER_HIGH_PM25) {
    highPmMinutes_ = 0;
  } else if (!power) {
    useReferenceRate_ = false;
  }
  wasPowered_ = power;

  if (power) {
    if (fanLevel != 0xFF) {
      uint8_t pmLevel = 0;
      while (pmLevel < 4 && pm25 > FILTER_PM25_LEVELS[pmLevel]) {
        pmLevel++;
      }
      uint8_t fanIndex = fanLevel > 3 ? 3 : fanLevel;
      uint32_t lossRate = useReferenceRate_ ? FILTER_REFERENCE_LOSS_RATE : FILTER_LOSS_TABLE[fanIndex][pmLevel];
      dust_ = dust_ + lossRate / 6 > FILTER_DUST_LIMIT ? FILTER_DUST_LIMIT : dust_ + lossRate / 6;
    }

    if (++ticks_ >= 6) {
      ticks_ = 0;
      // 30 consecutive minutes of PM2.5 >= 50 switch to the reference loss rate
      if (pm25 > FILTER_HIGH_PM25) {
        useReferenceRate_ = ++highPmMinutes_ > 29;
      } else {
        useReferenceRate_ = false;
        highPmMinutes_ = -1;
      }
      uint8_t previousPercent = get_percent_();
      useMinutes_++;
      if (useMinutes_ % 15 == 0 || get_percent_() != previousPercent) {
        save_();
      }
      publish_();
    }
  }

  // replace-filter LED, once the MCU has talked to us
  bool ledOn = get_percent_() == 0;
  if (statusSeen && (!ledSent_ || ledOn != ledOn_)) {
    ESP_LOGI(TAG, "Filter: replace-filter LED %s", ledOn ? "on" : "off");
    parent_->set_filter_led(ledOn);
    ledSent_ = true;
    ledOn_ = ledOn;
  }
}

uint8_t LevoitFilter::get_percent_() {
  uint8_t minPercent = filter_calc_percent(useMinutes_, FILTER_MIN_LIFETIME_MINUTES);
  uint8_t maxPercent = filter_calc_percent(useMinutes_, FILTER_MAX_LIFETIME_MINUTES);
  uint8_t dustPercent = dust_ > FILTER_TOTAL_CMM * 10 ? 0 : filter_calc_percent(dust_ / 10, FILTER_TOTAL_CMM);
  if (dustPercent < minPercent) {
    return minPercent;
  }
  return dustPercent > maxPercent ? maxPercent : dustPercent;
}

void LevoitFilter::save_() {
  LevoitFilterState state{useMinutes_, dust_};
  pref_.save(&state);
}

void LevoitFilter::publish_() {
  if (lifeSensor_ != nullptr) {
    lifeSensor_->publish_state(get_percent_());
  }
  if (runtimeSensor_ != nullptr) {
    runtimeSensor_->publish_state(useMinutes_ / 60.0f);
  }
}

void LevoitFilter::dump_config() {
  ESP_LOGCONFIG(TAG, "Levoit Filter:");
  ESP_LOGCONFIG(TAG, "  Use time: %lu min, dust: %lu, life: %u%%", (unsigned long) useMinutes_, (unsigned long) dust_,
                get_percent_());
  LOG_SENSOR("  ", "Filter Life", lifeSensor_);
  LOG_SENSOR("  ", "Filter Runtime", runtimeSensor_);
}

}  // namespace levoit
}  // namespace esphome
