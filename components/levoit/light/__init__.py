import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light

from .. import levoit_ns, CONF_LEVOIT_ID, Levoit

DEPENDENCIES = ["levoit"]
CODEOWNERS = ["@acvigue"]

LevoitLight = levoit_ns.class_("LevoitLight", cg.Component, light.LightOutput)

CONFIG_SCHEMA = light.light_schema(
    LevoitLight, light.LightType.BINARY
).extend(
    {
        cv.GenerateID(CONF_LEVOIT_ID): cv.use_id(Levoit),
    }
)

async def to_code(config):
    parent = await cg.get_variable(config[CONF_LEVOIT_ID])
    var = await light.new_light(config, parent)
    await cg.register_component(var, config)
