#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <freertos/semphr.h>
#include <vector>

namespace esphome {
namespace levoit {

enum class LevoitDeviceModel : uint8_t { NONE, CORE_200S, CORE_300S, CORE_400S, CORE_600S };
enum class LevoitPacketType : uint8_t { SEND_MESSAGE = 0x22, ACK_MESSAGE = 0x12, ERROR = 0x52 };
enum class LevoitPayloadType : uint32_t {
  STATUS_REQUEST = 0x013140,
  STATUS_RESPONSE = 0x013040,
  AUTO_STATUS = 0x016040,  // I only know this value for 200S, so might be wrong
  SET_FAN_AUTO_MODE = 0x01E6A5,
  SET_FAN_MANUAL = 0x0160A2,
  SET_FAN_MODE = 0x01E0A5,
  SET_DISPLAY_LOCK = 0x0100D1,
  SET_WIFI_STATUS_LED = 0x0129A1,
  SET_POWER_STATE = 0x0100A0,
  SET_SCREEN_BRIGHTNESS = 0x0105A1,
  SET_FILTER_LED = 0x01E2A5,
  SET_RESET_FILTER = 0x01E4A5,
  TIMER_STATUS = 0x0165A2,
  SET_TIMER_TIME = 0x0164A2,
  TIMER_START_OR_CLEAR = 0x0166A2,
  SET_NIGHTLIGHT = 0x0103A0
};

enum class LevoitState : uint32_t {
  POWER               = 1 << 0,
  FAN_MANUAL          = 1 << 1,
  FAN_AUTO            = 1 << 2,
  FAN_SLEEP           = 1 << 3,
  DISPLAY             = 1 << 4,
  DISPLAY_LOCK        = 1 << 5,
  FAN_SPEED1          = 1 << 6,
  FAN_SPEED2          = 1 << 7,
  FAN_SPEED3          = 1 << 8,
  FAN_SPEED4          = 1 << 9,
  NIGHTLIGHT_OFF      = 1 << 10,
  NIGHTLIGHT_LOW      = 1 << 11,
  NIGHTLIGHT_HIGH     = 1 << 12,
  AUTO_DEFAULT        = 1 << 13,
  AUTO_QUIET          = 1 << 14,
  AUTO_EFFICIENT      = 1 << 15,
  AIR_QUALITY_CHANGE  = 1 << 16,
  PM25_NAN            = 1 << 17,
  PM25_CHANGE         = 1 << 18,
  WIFI_CONNECTED      = 1 << 19,
  HA_CONNECTED        = 1 << 20,
  FILTER_RESET        = 1 << 21,
  WIFI_LIGHT_SOLID    = 1 << 22,
  WIFI_LIGHT_FLASH    = 1 << 23,
  WIFI_LIGHT_OFF      = 1 << 24
};

struct LevoitStateListener {
  uint32_t mask;
  std::function<void(uint32_t currentBits)> func;
};

#define MAX_PAYLOAD_SIZE 32
struct LevoitCommand {
  LevoitPayloadType payloadType;
  LevoitPacketType packetType;
  uint8_t payload[MAX_PAYLOAD_SIZE] = {0};
  uint8_t payload_len = 0;
};
static_assert(std::is_trivially_copyable<LevoitCommand>::value,
              "LevoitCommand must be trivially copyable for FreeRTOS queue");

using PayloadTypeOverrideMap = std::unordered_map<LevoitDeviceModel, std::unordered_map<LevoitPayloadType, uint32_t>>;

static const PayloadTypeOverrideMap MODEL_SPECIFIC_PAYLOAD_TYPES = {
    {LevoitDeviceModel::CORE_600S,
     {
         {LevoitPayloadType::AUTO_STATUS, 0x014041}
         // ... add other model-specific overrides here ...
     }},
    {LevoitDeviceModel::CORE_400S,
     {
         {LevoitPayloadType::STATUS_REQUEST, 0x01b140},
         {LevoitPayloadType::STATUS_RESPONSE, 0x01b040},
         // ... add other model-specific overrides here ...
     }},
    {LevoitDeviceModel::CORE_200S,
     {
         {LevoitPayloadType::STATUS_REQUEST, 0x016140},
         {LevoitPayloadType::STATUS_RESPONSE, 0x016140},
         {LevoitPayloadType::AUTO_STATUS, 0x016040}
         // ... add other model-specific overrides here ...
     }},
    // ... add other device models and their overrides here ...
};

class Levoit : public Component, public uart::UARTDevice {
 public:
  LevoitDeviceModel device_model_ = LevoitDeviceModel::CORE_300S;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void dump_config() override;
  void set_device_model(std::string model);
  void set_command_delay(int delay);
  void set_command_timeout(int timeout);
  void set_status_poll_seconds(int interval);
  void register_state_listener(uint32_t changeMask, const std::function<void(uint32_t currentBits)> &func);
  void set_request_state(uint32_t onMask, uint32_t offMask, bool acquireMutex = true);
  uint32_t get_model_specific_payload_type(LevoitPayloadType type);
  uint32_t fanChangeMask =
    static_cast<uint32_t>(LevoitState::FAN_SPEED1) |
    static_cast<uint32_t>(LevoitState::FAN_SPEED2) |
    static_cast<uint32_t>(LevoitState::FAN_SPEED3) |
    static_cast<uint32_t>(LevoitState::FAN_SPEED4);
  uint32_t pm25_value = 1000;
  uint8_t air_quality = 255;

 protected:
  QueueHandle_t rx_queue_;
  QueueHandle_t tx_queue_;
  SemaphoreHandle_t stateChangeMutex_;
  TaskHandle_t procTxQueueTaskHandle_;
  TaskHandle_t maintTaskHandle_;
  uint32_t current_state_ = 0;
  uint32_t req_on_state_ = 0;
  uint32_t req_off_state_ = 0;
  uint32_t command_delay_;
  uint32_t command_timeout_;
  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  uint32_t status_poll_seconds;
  uint8_t sequenceNumber_ = 0;
  std::vector<uint8_t> rx_message_;
  std::vector<LevoitStateListener> state_listeners_;
  void rx_queue_task_();
  void process_rx_queue_task_();
  void process_tx_queue_task_();
  void maint_task_();
  void command_sync_();
  void send_command_(const LevoitCommand &command);
  void process_raw_command_(LevoitCommand command);
  void send_raw_command(LevoitCommand command);
  void set_bit_(uint32_t &state, bool condition, LevoitState bit);
  bool validate_message_();
  void handle_payload_(LevoitPayloadType type, uint8_t *payload, size_t len);
  
};

}  // namespace levoit
}  // namespace esphome
