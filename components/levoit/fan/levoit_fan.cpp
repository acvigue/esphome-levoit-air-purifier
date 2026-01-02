#include "esphome/core/log.h"
#include "levoit_fan.h"

namespace esphome {
namespace levoit {

static const char *const TAG = "levoit.fan";

void LevoitFan::setup() {
  uint32_t listenMask = this->parent_->fanChangeMask;

  listenMask |= static_cast<uint32_t>(LevoitState::POWER) |
                static_cast<uint32_t>(LevoitState::FAN_MANUAL) |
                static_cast<uint32_t>(LevoitState::FAN_SLEEP) |
                static_cast<uint32_t>(LevoitState::FAN_AUTO);

  this->parent_->register_state_listener(listenMask,
    [this](uint32_t currentBits) {
      this->state = (currentBits & static_cast<uint32_t>(LevoitState::POWER));

      if (!this->state || currentBits & static_cast<uint32_t>(LevoitState::FAN_MANUAL))
      {
        this->set_preset_mode_("Manual");
        uint8_t newSpeed = 0;

        if (currentBits & static_cast<uint32_t>(LevoitState::FAN_SPEED1))
          newSpeed = 1;
        else if (currentBits & static_cast<uint32_t>(LevoitState::FAN_SPEED2))
          newSpeed = 2;
        else if (currentBits & static_cast<uint32_t>(LevoitState::FAN_SPEED3))
          newSpeed = 3;
        else if (currentBits & static_cast<uint32_t>(LevoitState::FAN_SPEED4))
          newSpeed = 4;
        
        this->speed = newSpeed;
      }
      else if (currentBits & static_cast<uint32_t>(LevoitState::FAN_AUTO))
      {
        this->set_preset_mode_("Auto");
        this->speed = 0;
      }
      else if (currentBits & static_cast<uint32_t>(LevoitState::FAN_SLEEP))
      {
        this->set_preset_mode_("Sleep");
        this->speed = 0;
      }
      else
      {
        ESP_LOGW(TAG, "Fan preset mode not set: %u", currentBits);
      }

      this->publish_state();
    }
  );

  // Construct traits
  switch (this->parent_->device_model_) {
    case LevoitDeviceModel::CORE_400S:
      this->traits_ = fan::FanTraits(false, true, false, 4);
      this->traits_.set_supported_preset_modes({"Manual", "Sleep", "Auto"});
      break;
    case LevoitDeviceModel::CORE_300S:
      this->traits_ = fan::FanTraits(false, true, false, 3);
      this->traits_.set_supported_preset_modes({"Manual", "Sleep", "Auto"});
      break;
    case LevoitDeviceModel::CORE_200S:
    default:
      this->traits_ = fan::FanTraits(false, true, false, 3);
      this->traits_.set_supported_preset_modes({"Manual", "Sleep"});
      break;
  }
}


void LevoitFan::dump_config() { LOG_FAN("", "Levoit Fan", this); }

void LevoitFan::control(const fan::FanCall &call) {
  uint32_t onMask = 0;
  uint32_t offMask = 0;

  if (call.get_state().has_value()) {
    bool newPowerState = *call.get_state();
    ESP_LOGV(TAG, "Setting fan power = %s", ONOFF(newPowerState));
    
    if (newPowerState) {
      onMask |= static_cast<uint32_t>(LevoitState::POWER) | static_cast<uint32_t>(LevoitState::FAN_MANUAL);
    } else {
      offMask |= static_cast<uint32_t>(LevoitState::POWER);
    }

  }

  if (call.get_speed().has_value()) {
    uint8_t targetSpeed = *call.get_speed();
    ESP_LOGV(TAG, "Setting fan speed = %u", targetSpeed);

    switch (targetSpeed) {
      case 0:
        // send power off
        offMask |= static_cast<uint32_t>(LevoitState::POWER);
        break;
      case 1:
        onMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED1) | static_cast<uint32_t>(LevoitState::FAN_MANUAL);
        offMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED2) | static_cast<uint32_t>(LevoitState::FAN_SPEED3) |
                   static_cast<uint32_t>(LevoitState::FAN_SPEED4);
        break;
      case 2:
        onMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED2) | static_cast<uint32_t>(LevoitState::FAN_MANUAL);
        offMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED1) | static_cast<uint32_t>(LevoitState::FAN_SPEED3) |
                   static_cast<uint32_t>(LevoitState::FAN_SPEED4);
        break;
      case 3:
        onMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED3) | static_cast<uint32_t>(LevoitState::FAN_MANUAL);
        offMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED1) | static_cast<uint32_t>(LevoitState::FAN_SPEED2) |
                   static_cast<uint32_t>(LevoitState::FAN_SPEED4);
        break;
      case 4:
        onMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED4) | static_cast<uint32_t>(LevoitState::FAN_MANUAL);
        offMask |= static_cast<uint32_t>(LevoitState::FAN_SPEED1) | static_cast<uint32_t>(LevoitState::FAN_SPEED2) |
                   static_cast<uint32_t>(LevoitState::FAN_SPEED3);
        break;
    }
  }

  if (call.has_preset_mode()) {
    std::string mode = call.get_preset_mode();
    ESP_LOGV(TAG, "Setting fan mode = %s", mode.c_str());
    if (mode == "Manual") {
      onMask |= static_cast<uint32_t>(LevoitState::FAN_MANUAL) + static_cast<uint32_t>(LevoitState::POWER);
      offMask |= static_cast<uint32_t>(LevoitState::FAN_AUTO) + static_cast<uint32_t>(LevoitState::FAN_SLEEP);
    } else if (mode == "Auto") {
      onMask |= static_cast<uint32_t>(LevoitState::FAN_AUTO) + static_cast<uint32_t>(LevoitState::POWER);
      offMask |= static_cast<uint32_t>(LevoitState::FAN_MANUAL) + static_cast<uint32_t>(LevoitState::FAN_SLEEP);
    } else if (mode == "Sleep") {
      onMask |= static_cast<uint32_t>(LevoitState::FAN_SLEEP) + static_cast<uint32_t>(LevoitState::POWER);
      offMask |= static_cast<uint32_t>(LevoitState::FAN_MANUAL) + static_cast<uint32_t>(LevoitState::FAN_AUTO);
    }
  }

  if (onMask || offMask) {
    // when going from off to fan speed, don't send on
    if (onMask & static_cast<uint32_t>(LevoitState::POWER) && onMask & this->parent_->fanChangeMask)
      onMask &= ~static_cast<uint32_t>(LevoitState::POWER);
    this->parent_->set_request_state(onMask, offMask);
  }
    
}

}  // namespace levoit
}  // namespace esphome
