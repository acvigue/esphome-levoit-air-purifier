#include "esphome/core/log.h"
#include "levoit_light.h"

namespace esphome {
namespace levoit {

static const char *const TAG = "levoit.light";

void LevoitLight::setup_state(light::LightState *state) {
  this->state_ = state;
}

void LevoitLight::setup() {
  this->parent_->register_state_listener(
    static_cast<uint32_t>(LevoitState::NIGHTLIGHT_OFF) +
    static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW) +
    static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH),
    [this](uint32_t currentBits) {
      this->publish_state(currentBits);
    }
  );
}

void LevoitLight::publish_state(uint32_t currentBits) {
  this->write_ = false;
  if (currentBits & static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH))
    this->state_->turn_on().set_brightness(1.0).perform();
  else if (currentBits & static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW))
    this->state_->turn_on().set_brightness(0.5).perform();
  else
    this->state_->turn_off().perform();
}

void LevoitLight::write_state(light::LightState *state) {
  if (!this->write_) {
    this->write_ = true;
    return;
  }
  float brightness;
  state->current_values_as_brightness(&brightness);

  std::string level;
  uint32_t onMask = 0;
  uint32_t offMask = 0;

  if (brightness == 0.0) {
    level = "OFF";
    onMask |= static_cast<uint32_t>(LevoitState::NIGHTLIGHT_OFF);
    offMask |= static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW) + static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH);
  }
  else if (brightness < 0.505) {
    level = "LOW";
    onMask |= static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW);
    offMask |= static_cast<uint32_t>(LevoitState::NIGHTLIGHT_OFF) + static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH);
    this->publish_state(static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW));
  }
  else {
    level = "HIGH";
    onMask |= static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH);
    offMask |= static_cast<uint32_t>(LevoitState::NIGHTLIGHT_OFF) + static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW);
    this->publish_state(static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH));
  }
  ESP_LOGD(TAG, "'%s': Setting state %0.6f -> %s", state->get_name().c_str(), brightness, level.c_str());
  this->parent_->set_request_state(onMask, offMask);
  return;
}

}  // namespace levoit
}  // namespace esphome
