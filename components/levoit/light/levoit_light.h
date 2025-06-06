#pragma once

#include "esphome/core/component.h"
#include "esphome/components/levoit/levoit.h"
#include "esphome/components/light/light_output.h"

#include <vector>

namespace esphome {
namespace levoit {

class LevoitLight : public light::LightOutput, public Component {
 public:
  LevoitLight(Levoit *parent) : parent_(parent) {};
  void setup() override;

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    return traits;
  }

  void setup_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;

 protected:
  light::LightState *state_;
  Levoit *parent_;
  bool write_ = false;

  void publish_state(uint32_t currentBits);
};

}  // namespace levoit
}  // namespace esphome
