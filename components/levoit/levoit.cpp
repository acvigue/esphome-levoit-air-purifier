#include "levoit.h"
#include "esphome/components/network/util.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"
#include "esphome.h"

namespace esphome {
namespace levoit {

static const char *const TAG = "levoit";

void Levoit::setup() {
  ESP_LOGI(TAG, "Setting up Levoit %s", device_model_ == LevoitDeviceModel::CORE_300S ? "Core 300S" : "Core 400S");

  rx_queue_ = xQueueCreate(256, sizeof(uint8_t));
  if (rx_queue_ == NULL) {
      ESP_LOGE(TAG, "Failed to create rx queue");
      return;
  }

  tx_queue_ = xQueueCreate(8, sizeof(LevoitCommand));
  if (tx_queue_ == NULL) {
      ESP_LOGE(TAG, "Failed to create tx queue");
      return;
  }

  stateChangeMutex_ = xSemaphoreCreateMutex();
  if (stateChangeMutex_ == NULL) {
      ESP_LOGE(TAG, "Failed to create stateChangeMutex");
      return;
  }

  xTaskCreatePinnedToCore(
      [](void *param) { static_cast<Levoit *>(param)->rx_queue_task_(); },
      "RxQueueTask", 2048, this, 4, NULL, tskNO_AFFINITY);


  xTaskCreatePinnedToCore(
      [](void *param) { static_cast<Levoit *>(param)->process_tx_queue_task_(); },
      "TxQueueTask", 4096, this, 3, &procTxQueueTaskHandle_, tskNO_AFFINITY);

  xTaskCreatePinnedToCore(
      [](void *param) { static_cast<Levoit *>(param)->process_rx_queue_task_(); },
      "ProcRxQueueTask", 4096, this, 2, NULL, tskNO_AFFINITY);

  xTaskCreatePinnedToCore(
      [](void *param) { static_cast<Levoit *>(param)->maint_task_(); },
      "MaintTask", 4096, this, 1, &maintTaskHandle_, tskNO_AFFINITY);

}

void Levoit::maint_task_() {
  uint32_t lastStatusPollTime = xTaskGetTickCount();

  while (true) {
    // wait with timeout for notification
    while (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(1000)) > 0) {
      if (xSemaphoreTake(stateChangeMutex_, portMAX_DELAY) == pdTRUE) {
        command_sync_();
        xSemaphoreGive(stateChangeMutex_);
      }
    }
    if (xSemaphoreTake(stateChangeMutex_, portMAX_DELAY) == pdTRUE) {
      uint32_t previousState = current_state_;

      bool wifiConnected = wifi::global_wifi_component->is_connected();
      bool haConnected = wifiConnected && esphome::api::global_api_server != nullptr && api::global_api_server->is_connected();
      bool wifiSolid = wifiConnected && haConnected;
      bool wifiFlash = wifiConnected && !haConnected;
      bool wifiOff = !wifiConnected && !haConnected;

      set_bit_(current_state_, wifiConnected, LevoitState::WIFI_CONNECTED);
      set_bit_(current_state_, haConnected, LevoitState::HA_CONNECTED);
      set_bit_(current_state_, wifiSolid, LevoitState::WIFI_LIGHT_SOLID);
      set_bit_(current_state_, wifiFlash, LevoitState::WIFI_LIGHT_FLASH);
      set_bit_(current_state_, wifiOff, LevoitState::WIFI_LIGHT_OFF);
      

      if (previousState != current_state_) {
        ESP_LOGV(TAG, "State Changed from %u to %u", previousState, current_state_);

        uint32_t wifiLights = 
          static_cast<uint32_t>(LevoitState::WIFI_LIGHT_SOLID) +
          static_cast<uint32_t>(LevoitState::WIFI_LIGHT_FLASH) +
          static_cast<uint32_t>(LevoitState::WIFI_LIGHT_OFF);

        // check if lights need to be changed
        if ((previousState & wifiLights) != (current_state_ & wifiLights)) {
          if (wifiConnected || haConnected) {
            if (haConnected) {
              // send solid
              send_command_(LevoitCommand {
                .payloadType = LevoitPayloadType::SET_WIFI_STATUS_LED,
                .packetType = LevoitPacketType::SEND_MESSAGE,
                .payload = {0x00, 0x01, 0x7D, 0x00, 0x7D, 0x00, 0x00}
              });
            } else {
              // Blink
              send_command_(LevoitCommand {
                .payloadType = LevoitPayloadType::SET_WIFI_STATUS_LED,
                .packetType = LevoitPacketType::SEND_MESSAGE,
                .payload = {0x00, 0x02, 0xF4, 0x01, 0xF4, 0x01, 0x00}
              }); 
            }
          } else {
            // Off
            send_command_(LevoitCommand {
              .payloadType = LevoitPayloadType::SET_WIFI_STATUS_LED,
              .packetType = LevoitPacketType::SEND_MESSAGE,
              .payload = {0x00, 0x00, 0xF4, 0x01, 0xF4, 0x01, 0x00}
            });
          }
        }
      }

      uint32_t removeBits = current_state_ & req_on_state_;
      if (removeBits)
        req_on_state_ &= ~removeBits;

      removeBits = ~current_state_ & req_off_state_;
      if (removeBits)
          req_off_state_ &= ~removeBits;

      xSemaphoreGive(stateChangeMutex_);
    }
    if (this->status_poll_seconds > 0 && (xTaskGetTickCount() - lastStatusPollTime) >= pdMS_TO_TICKS(this->status_poll_seconds * 1000)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::STATUS_REQUEST,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00}
      });
      lastStatusPollTime = xTaskGetTickCount();
    }
  }
}

void Levoit::command_sync_() {
  if (req_on_state_ || req_off_state_) {
    // switches          
    if (req_on_state_ & static_cast<uint32_t>(LevoitState::POWER) || req_off_state_ & static_cast<uint32_t>(LevoitState::POWER)) {
      bool commandState = req_on_state_ & static_cast<uint32_t>(LevoitState::POWER);
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_POWER_STATE,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, commandState}
      });
    }

    if (req_on_state_ & static_cast<uint32_t>(LevoitState::DISPLAY) || req_off_state_ & static_cast<uint32_t>(LevoitState::DISPLAY)) {
        bool commandState = req_on_state_ & static_cast<uint32_t>(LevoitState::DISPLAY);
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_SCREEN_BRIGHTNESS,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, commandState ? (uint8_t) 0x64 : (uint8_t) 0x00}
        });
    }

    if (req_on_state_ & static_cast<uint32_t>(LevoitState::DISPLAY_LOCK) || req_off_state_ & static_cast<uint32_t>(LevoitState::DISPLAY_LOCK)) {
      bool commandState = req_on_state_ & static_cast<uint32_t>(LevoitState::DISPLAY_LOCK);
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_DISPLAY_LOCK,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, commandState}
      });
    }

    // fan mode
    if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_MANUAL)) {
      if (device_model_ == LevoitDeviceModel::CORE_400S)
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_FAN_MANUAL,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, 0x00, 0x01, 0x01}
        });
      else
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_FAN_MODE,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, 0x00}
        });
    } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_AUTO))
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_FAN_MODE,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x02}
      });
    else if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_SLEEP))
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_FAN_MODE,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x01}
      });          

    // fan speed
    if (req_on_state_ && fanChangeMask && (current_state_ && static_cast<uint32_t>(LevoitState::POWER) || current_state_ && static_cast<uint32_t>(LevoitState::FAN_MANUAL)))  {
      if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_SPEED1)) {
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_FAN_MANUAL,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, 0x00, 0x01, 0x01}
        });
      } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_SPEED2)) {
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_FAN_MANUAL,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, 0x00, 0x01, 0x02}
        });
      } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_SPEED3)) {
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_FAN_MANUAL,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, 0x00, 0x01, 0x03}
        });
      } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::FAN_SPEED4)) {
        send_command_(LevoitCommand {
          .payloadType = LevoitPayloadType::SET_FAN_MANUAL,
          .packetType = LevoitPacketType::SEND_MESSAGE,
          .payload = {0x00, 0x00, 0x01, 0x04}
        });
      }
    }

    //Fan Auto Mode
    if (req_on_state_ & static_cast<uint32_t>(LevoitState::AUTO_DEFAULT)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_FAN_AUTO_MODE,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x00, 0x00, 0x00}
      });
    } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::AUTO_QUIET)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_FAN_AUTO_MODE,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x01, 0x00, 0x00}
      });
    } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::AUTO_EFFICIENT)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_FAN_AUTO_MODE,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x02, 0x00, 0x00}
      });
    }

    //Night Light
    if (req_on_state_ & static_cast<uint32_t>(LevoitState::NIGHTLIGHT_OFF)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_NIGHTLIGHT,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x00, 0x00}
      });            
    } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::NIGHTLIGHT_LOW)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_NIGHTLIGHT,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x00, 0x32}
      });   
    } else if (req_on_state_ & static_cast<uint32_t>(LevoitState::NIGHTLIGHT_HIGH)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_NIGHTLIGHT,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x00, 0x64}
      });   
    }

    // Filter Reset
    if (req_on_state_ & static_cast<uint32_t>(LevoitState::FILTER_RESET)) {
      send_command_(LevoitCommand {
        .payloadType = LevoitPayloadType::SET_RESET_FILTER,
        .packetType = LevoitPacketType::SEND_MESSAGE,
        .payload = {0x00, 0x00}
      });
      // setting that its done, not sure how to check filter status yet
      current_state_ &= ~static_cast<uint32_t>(LevoitState::FILTER_RESET);
    }

    if (req_off_state_ & static_cast<uint32_t>(LevoitState::AIR_QUALITY_CHANGE))
      current_state_ &= ~static_cast<uint32_t>(LevoitState::AIR_QUALITY_CHANGE);

    if (req_off_state_ & static_cast<uint32_t>(LevoitState::PM25_NAN))
      current_state_ &= ~static_cast<uint32_t>(LevoitState::PM25_NAN);

    if (req_off_state_ & static_cast<uint32_t>(LevoitState::PM25_CHANGE))
      current_state_ &= ~static_cast<uint32_t>(LevoitState::PM25_CHANGE);

  }
}

void Levoit::rx_queue_task_() {
  uint8_t c;
  while (true) {
    while (this->available()) {
      this->read_byte(&c);
      if (xQueueSend(rx_queue_, &c, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send data to rx queue.");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void Levoit::process_rx_queue_task_() {
  uint8_t c;
  while (xQueueReceive(rx_queue_, &c, portMAX_DELAY) == pdPASS) {
    this->rx_message_.push_back(c);
    if (!this->validate_message_()) {
      this->rx_message_.clear();
    } else {
      this->last_rx_char_timestamp_ = millis();
    }
  }
}

void Levoit::dump_config() { 
    ESP_LOGCONFIG(TAG, "Levoit!"); 
    ESP_LOGCONFIG(TAG, "  Command Delay: %d ms", this->command_delay_);
    ESP_LOGCONFIG(TAG, "  Command Timeout: %d ms", this->command_timeout_);
    ESP_LOGCONFIG(TAG, "  Status Poll Seconds: %d s", this->status_poll_seconds);
}

bool Levoit::validate_message_() {
  uint32_t at = this->rx_message_.size() - 1;
  auto *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  if (at == 0)
    return new_byte == 0xA5;

  if (at == 1) {
    if (new_byte == 0x52) {
      ESP_LOGE(TAG, "Received error response, ignoring packet");
      if (xSemaphoreTake(stateChangeMutex_, portMAX_DELAY) == pdTRUE) {
        req_off_state_ = 0;
        req_on_state_ = 0;
        xSemaphoreGive(stateChangeMutex_);
      }
      return false;
    }
    return (new_byte == 0x12) || (new_byte == 0x22);
  }

  uint8_t sequenceNumber = data[2];
  if (at == 2)
    return true;

  uint8_t payloadLength = data[3];
  if (at == 3) {
    return true;
  }

  if (at == 4)
    return (new_byte == 0x00);

  uint8_t payloadChecksum = data[5];
  if (at == 5) {
    return true;
  }

  if (at - 5 < payloadLength) {
    return true;
  }

  uint8_t calc_checksum = 255;
  for (uint8_t i = 0; i < 6 + payloadLength; i++) {
    if (i != 5) {
      calc_checksum -= data[i];
    }
  }

  if (payloadChecksum != calc_checksum) {
    ESP_LOGE(TAG, "Received invalid message checksum, ignoring packet");
    return false;
  }

  // valid message
  const uint8_t *message_data = data + 6;

  LevoitPayloadType payloadType = 
    (LevoitPayloadType) (message_data[2] | (message_data[1] << 8) | (message_data[0] << 16) | (0x00 << 24));

  uint8_t *payload_data = data + 9;

  // If it's not a 1-byte ACK response, handle the payload.
  if (data[1] != 0x12 || payloadLength - 3 != 1) {
    this->handle_payload_(payloadType, payload_data, payloadLength - 3);
  } else if (data[1] == 0x12) {
    ESP_LOGV(TAG, "Received ACK (%06x)", (uint32_t) payloadType);
  }

  // acknowledge packet if required
  if (data[1] == 0x22) {
    LevoitCommand acknowledgeResponse = {
        .payloadType = payloadType, .packetType = LevoitPacketType::ACK_MESSAGE, .payload = {0x00}};
    this->send_raw_command(acknowledgeResponse);
  } else if (data[1] == 0x12) {
    // notify wait for ACK
    xTaskNotifyGive(procTxQueueTaskHandle_);
  }

  this->sequenceNumber_ = sequenceNumber + 1;

  // return false to reset rx buffer
  return false;
}

void Levoit::handle_payload_(LevoitPayloadType type, uint8_t *payload, size_t len) {
  LevoitPayloadType payloadType = static_cast<LevoitPayloadType>(get_model_specific_payload_type(type));

  ESP_LOGV(TAG, "Received command (%06x): %s", (uint32_t) payloadType, format_hex_pretty(payload, len).c_str());
  
  if (payloadType == static_cast<LevoitPayloadType>(get_model_specific_payload_type(LevoitPayloadType::STATUS_RESPONSE)) 
        || payloadType == static_cast<LevoitPayloadType>(get_model_specific_payload_type(LevoitPayloadType::AUTO_STATUS))) {
    if (xSemaphoreTake(stateChangeMutex_, portMAX_DELAY) == pdTRUE) {
      uint32_t previousState = current_state_;
      bool power = payload[4];
      bool display = payload[device_model_ == LevoitDeviceModel::CORE_400S ? 9 : 7] != 0x00;
      bool displayLock = payload[device_model_ == LevoitDeviceModel::CORE_200S ? 11 : 14] != 0x00;

      uint8_t fanSpeedIndex = 9;
      switch (device_model_) {
        case LevoitDeviceModel::CORE_400S: fanSpeedIndex = 7; break;
        case LevoitDeviceModel::CORE_200S: fanSpeedIndex = 6; break;
      }
      uint8_t fanSpeed = power ? payload[fanSpeedIndex] : 0;
      bool fan1 = fanSpeed == 1; bool fan2 = fanSpeed == 2;
      bool fan3 = fanSpeed == 3; bool fan4 = fanSpeed == 4;

      bool fanManual = payload[5] == 0x00;
      bool fanAuto  = payload[5] == 0x02;
      bool fanSleep  = payload[5] == 0x01;

      bool autoDefault = payload[15] == 0x00;
      bool autoQuiet = payload[15] == 0x01;
      bool autoEfficient = payload[15] == 0x02;

      bool nightLightOff = payload[12] == 0x00;
      bool nightLightLow = payload[12] == 0x32;
      bool nightLightHigh = payload[12] == 0x64;

      bool pm25NAN = false;
      bool pm25Change = false;
      bool airQualityChange = false;

      pm25NAN = (payload[12] == 0xFF && payload[13] == 0xFF);
      if (!pm25NAN) {
        uint16_t raw_value = (payload[13] << 8) + payload[12];
        uint32_t new_pm25Value = (raw_value * 10);
        
        if (new_pm25Value != pm25_value) {
          pm25Change = true;
          pm25_value = new_pm25Value;
        }
      }

      uint8_t newAirQuality = payload[11];
      if (newAirQuality != air_quality) {
        airQualityChange = true;
        air_quality = newAirQuality;
      }

      set_bit_(current_state_, power, LevoitState::POWER);
      set_bit_(current_state_, display, LevoitState::DISPLAY);
      set_bit_(current_state_, displayLock, LevoitState::DISPLAY_LOCK);
      set_bit_(current_state_, fan1, LevoitState::FAN_SPEED1);
      set_bit_(current_state_, fan2, LevoitState::FAN_SPEED2);
      set_bit_(current_state_, fan3, LevoitState::FAN_SPEED3);
      set_bit_(current_state_, fan4, LevoitState::FAN_SPEED4);
      set_bit_(current_state_, fanManual, LevoitState::FAN_MANUAL);
      set_bit_(current_state_, fanAuto, LevoitState::FAN_AUTO);
      set_bit_(current_state_, fanSleep, LevoitState::FAN_SLEEP);
      set_bit_(current_state_, autoDefault, LevoitState::AUTO_DEFAULT);
      set_bit_(current_state_, autoQuiet, LevoitState::AUTO_QUIET);
      set_bit_(current_state_, autoEfficient, LevoitState::AUTO_EFFICIENT);
      set_bit_(current_state_, nightLightOff, LevoitState::NIGHTLIGHT_OFF);
      set_bit_(current_state_, nightLightLow, LevoitState::NIGHTLIGHT_LOW);
      set_bit_(current_state_, nightLightHigh, LevoitState::NIGHTLIGHT_HIGH);
      set_bit_(current_state_, pm25NAN, LevoitState::PM25_NAN);
      set_bit_(current_state_, pm25Change, LevoitState::PM25_CHANGE);
      set_bit_(current_state_, airQualityChange, LevoitState::AIR_QUALITY_CHANGE);

      if (previousState != current_state_) {
        ESP_LOGV(TAG, "State Changed from %u to %u", previousState, current_state_);

        uint32_t removeBits = current_state_ & req_on_state_;
        if (removeBits)
          req_on_state_ &= ~removeBits;

        removeBits = ~current_state_ & req_off_state_;
        if (removeBits)
            req_off_state_ &= ~removeBits;

        // Run through listeners
        for (auto &listener : this->state_listeners_) {
          uint32_t currentBits = (current_state_ & listener.mask);
          if ((previousState & listener.mask) != currentBits) {
            listener.func(currentBits);
          }
        }
        ESP_LOGV(TAG, "Current State: %u, Requested On: %u, Request Off: %u", current_state_, req_on_state_, req_off_state_);
      }
      xSemaphoreGive(stateChangeMutex_);
      xTaskNotifyGive(maintTaskHandle_);
    }
  }
}

void Levoit::set_request_state(uint32_t onMask, uint32_t offMask, bool aquireMutex) {
    if (aquireMutex && xSemaphoreTake(stateChangeMutex_, portMAX_DELAY) == pdTRUE) {
      if (onMask & offMask) {
        ESP_LOGE(TAG, "set_request_state - tried to set same bit on and off");
        return;
      }

      // Filter out bits in onMask that are already on in current_state_
      onMask &= ~current_state_;

      // Filter out bits in offMask that are already off in current_state_
      offMask &= current_state_;

      if (onMask > 0) {
        req_on_state_ |= onMask;
        req_off_state_ &= ~onMask;
      }

      if (offMask > 0) {
        req_off_state_ |= offMask;
        req_on_state_ &= ~offMask;
      }

      ESP_LOGV(TAG, "set_request_state - Current State: %u, Requested On: %u, Request Off: %u", current_state_, req_on_state_, req_off_state_);

      if (aquireMutex)
        xSemaphoreGive(stateChangeMutex_);

      xTaskNotifyGive(maintTaskHandle_);
    }
}

void Levoit::set_bit_(uint32_t &state, bool condition, LevoitState bit) {
    if (condition)
        state |= static_cast<uint32_t>(bit);
    else
        state &= ~static_cast<uint32_t>(bit);
}

void Levoit::register_state_listener(uint32_t changeMask, const std::function<void(uint32_t currentBits)> &func) {
  auto listener = LevoitStateListener{
      .mask = changeMask,
      .func = func,
  };
  this->state_listeners_.push_back(listener);
}

void Levoit::process_tx_queue_task_() {
  LevoitCommand command;
  while (xQueueReceive(tx_queue_, &command, portMAX_DELAY) == pdPASS) {
    process_raw_command_(command);
    vTaskDelay(pdMS_TO_TICKS(command_delay_));
  }
}

void Levoit::send_raw_command(LevoitCommand command) {
  if (xQueueSend(tx_queue_, &command, pdMS_TO_TICKS(10)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to send data to tx queue.");
  }
}

void Levoit::process_raw_command_(LevoitCommand command) {
  this->last_command_timestamp_ = millis();

  sequenceNumber_++;

  uint8_t payloadTypeByte1 = ((uint32_t) command.payloadType >> 16) & 0xff;
  uint8_t payloadTypeByte2 = ((uint32_t) command.payloadType >> 8) & 0xff;
  uint8_t payloadTypeByte3 = (uint32_t) command.payloadType & 0xff;

  // Initialize the outgoing packet
  std::vector<uint8_t> rawPacket = {
    0xA5,
    (uint8_t) command.packetType,
    sequenceNumber_,
    (uint8_t) (command.payload.size() + 3),
    0x00,
    0x00,
    payloadTypeByte1,
    payloadTypeByte2,
    payloadTypeByte3
  };

  if (!command.payload.empty())
    rawPacket.insert(rawPacket.end(), command.payload.begin(), command.payload.end());

  // Calculate checksum & insert into packet
  uint8_t checksum = 255;
  for (uint8_t i = 0; i < rawPacket.size(); i++) {
    if (i != 5) {
      checksum -= rawPacket[i];
    }
  }
  rawPacket[5] = checksum;

  const char *packetTypeStr = (command.packetType == LevoitPacketType::ACK_MESSAGE)
                                  ? "ACK"
                                  : ((command.packetType == LevoitPacketType::SEND_MESSAGE) ? "CMD" : "UNKNOWN");
  this->write_array(rawPacket);
  ESP_LOGV(TAG, "Sending %s (%06x): %s", packetTypeStr, (uint32_t) command.payloadType,
           format_hex_pretty(rawPacket.data(), rawPacket.size()).c_str());

  // await ACK, not really needed but helps with pacing commands
  // command will retry if state is not achieved
  if (command.packetType != LevoitPacketType::ACK_MESSAGE && ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(command_timeout_)) == 0) {
    ESP_LOGW(TAG, "Timeout waiting for ACK for command %s (%06x): %s", packetTypeStr, (uint32_t) command.payloadType,
          format_hex_pretty(rawPacket.data(), rawPacket.size()).c_str());
  }
}

void Levoit::send_command_(const LevoitCommand &command) {
  auto modified_command = command;
  modified_command.payloadType = static_cast<LevoitPayloadType>(get_model_specific_payload_type(command.payloadType));
  if (xQueueSend(tx_queue_, &modified_command, pdMS_TO_TICKS(10)) != pdTRUE) {
    ESP_LOGE(TAG, "Failed to send data to tx queue.");
  }
}

void Levoit::set_command_delay(int delay) {
  command_delay_ = delay;
}

void Levoit::set_command_timeout(int timeout) {
  command_timeout_ = timeout;
}

void Levoit::set_status_poll_seconds(int interval) {
  status_poll_seconds = interval;
}

void Levoit::set_device_model(std::string model) {
  if (model == "core300s") {
    device_model_ = LevoitDeviceModel::CORE_300S;
  } else if (model == "core400s") {
    device_model_ = LevoitDeviceModel::CORE_400S;
  } else if (model == "core200s") {
    device_model_ = LevoitDeviceModel::CORE_200S;
  } else {
    ESP_LOGW(TAG, "Unknown device model: %s", model.c_str());
  }
}

uint32_t Levoit::get_model_specific_payload_type(LevoitPayloadType type) {
  auto model_itr = MODEL_SPECIFIC_PAYLOAD_TYPES.find(device_model_);
  if (model_itr != MODEL_SPECIFIC_PAYLOAD_TYPES.end()) {
    auto payload_itr = model_itr->second.find(type);
    if (payload_itr != model_itr->second.end()) {
      return payload_itr->second;
    }
  }
  // If no override is found, return the default payload
  return static_cast<uint32_t>(type);
}

}  // namespace levoit
}  // namespace esphome
