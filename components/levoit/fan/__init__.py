import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan
from esphome.const import (
    CONF_OUTPUT_ID
)

from .. import levoit_ns, CONF_LEVOIT_ID, Levoit

DEPENDENCIES = ["levoit"]
CODEOWNERS = ["@acvigue"]

LevoitFan = levoit_ns.class_("LevoitFan", cg.Component, fan.Fan)

CONFIG_SCHEMA = fan.fan_schema(LevoitFan).extend(
    {
        cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(LevoitFan),
        cv.GenerateID(CONF_LEVOIT_ID): cv.use_id(Levoit),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_ID])

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent)
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
