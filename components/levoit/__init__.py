from esphome.components import time
from esphome import automation
from esphome import pins
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@acvigue"]

CONF_LEVOIT_ID = "levoit"
CONF_LEVOIT_MODEL = "model"
CONF_COMMAND_DELAY = "command_delay"
CONF_COMMAND_TIMEOUT = "command_timeout"
CONF_STATUS_POLL_SECONDS = "status_poll_seconds"

VALID_MODELS = ["core200s", "core300s", "core400s"]

levoit_ns = cg.esphome_ns.namespace("levoit")
Levoit = levoit_ns.class_("Levoit", cg.Component, uart.UARTDevice)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Levoit),
    cv.Optional(CONF_LEVOIT_MODEL, default="core300s"): cv.All(cv.string, cv.one_of(*VALID_MODELS)),
    cv.Optional(CONF_COMMAND_DELAY, default=300): cv.positive_int,
    cv.Optional(CONF_COMMAND_TIMEOUT, default=200): cv.positive_int,
    cv.Optional(CONF_STATUS_POLL_SECONDS, default=0) : cv.positive_int,
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_LEVOIT_MODEL in config:
        cg.add(var.set_device_model(config[CONF_LEVOIT_MODEL]))
    if CONF_COMMAND_DELAY in config:
        cg.add(var.set_command_delay(config[CONF_COMMAND_DELAY]))
    if CONF_COMMAND_TIMEOUT in config:
        cg.add(var.set_command_timeout(config[CONF_COMMAND_TIMEOUT]))
    if CONF_STATUS_POLL_SECONDS in config:
        cg.add(var.set_status_poll_seconds(config[CONF_STATUS_POLL_SECONDS]))
