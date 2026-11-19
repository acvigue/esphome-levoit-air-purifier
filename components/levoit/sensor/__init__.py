import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_OUTPUT_ID,
    CONF_PM_2_5,
    UNIT_MICROGRAMS_PER_CUBIC_METER,
    ICON_BLUR,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_AQI,
    DEVICE_CLASS_DURATION,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_PERCENT,
    UNIT_HOUR,
)

from .. import levoit_ns, CONF_LEVOIT_ID, Levoit

DEPENDENCIES = ["levoit"]
CODEOWNERS = ["@acvigue"]

CONF_AIR_QUALITY = "air_quality"
CONF_FILTER_LIFE = "filter_life"
CONF_FILTER_RUNTIME = "filter_runtime"
CONF_FILTER_ID = "filter_id"
CONF_FILTER_INITIAL_RUNTIME_MINUTES = "filter_initial_runtime_minutes"
CONF_FILTER_INITIAL_DUST = "filter_initial_dust"

LevoitSensor = levoit_ns.class_("LevoitSensor", cg.Component, sensor.Sensor)
LevoitSensorPurpose = levoit_ns.enum("LevoitSensorPurpose")
LevoitFilter = levoit_ns.class_("LevoitFilter", cg.PollingComponent)

CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(CONF_LEVOIT_ID): cv.use_id(Levoit),
        cv.GenerateID(CONF_FILTER_ID): cv.declare_id(LevoitFilter),
        cv.Optional(CONF_PM_2_5): sensor.sensor_schema(LevoitSensor, unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER, icon=ICON_BLUR, accuracy_decimals=1, device_class=DEVICE_CLASS_PM25, state_class=STATE_CLASS_MEASUREMENT),
        cv.Optional(CONF_AIR_QUALITY): sensor.sensor_schema(LevoitSensor, icon=ICON_BLUR, accuracy_decimals=0, device_class=DEVICE_CLASS_AQI, state_class=STATE_CLASS_MEASUREMENT),
        cv.Optional(CONF_FILTER_LIFE): sensor.sensor_schema(unit_of_measurement=UNIT_PERCENT, icon="mdi:air-filter", accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT),
        cv.Optional(CONF_FILTER_RUNTIME): sensor.sensor_schema(unit_of_measurement=UNIT_HOUR, icon="mdi:timer-outline", accuracy_decimals=1, device_class=DEVICE_CLASS_DURATION, state_class=STATE_CLASS_TOTAL_INCREASING),
        # only used when no filter state has been saved yet (first boot), e.g. to carry over the stock firmware values
        cv.Optional(CONF_FILTER_INITIAL_RUNTIME_MINUTES, default=0): cv.uint32_t,
        cv.Optional(CONF_FILTER_INITIAL_DUST, default=0): cv.uint32_t,
    })
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_ID])

    if config_pm25 := config.get(CONF_PM_2_5):
        var = await sensor.new_sensor(config_pm25, parent, LevoitSensorPurpose.PM25)
        await cg.register_component(var, config_pm25)

    if config_air_quality := config.get(CONF_AIR_QUALITY):
        var = await sensor.new_sensor(config_air_quality, parent, LevoitSensorPurpose.AIR_QUALITY)
        await cg.register_component(var, config_air_quality)

    if CONF_FILTER_LIFE in config or CONF_FILTER_RUNTIME in config:
        filter_var = cg.new_Pvariable(config[CONF_FILTER_ID], parent)
        await cg.register_component(filter_var, {})
        cg.add(filter_var.set_initial_state(config[CONF_FILTER_INITIAL_RUNTIME_MINUTES], config[CONF_FILTER_INITIAL_DUST]))

        if config_filter_life := config.get(CONF_FILTER_LIFE):
            var = await sensor.new_sensor(config_filter_life)
            cg.add(filter_var.set_life_sensor(var))

        if config_filter_runtime := config.get(CONF_FILTER_RUNTIME):
            var = await sensor.new_sensor(config_filter_runtime)
            cg.add(filter_var.set_runtime_sensor(var))
