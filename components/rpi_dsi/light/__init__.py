import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_DISPLAY_ID
from esphome.types import ConfigType

from ..display import RpiDsi, rpi_dsi_ns

RpiDsiLight = rpi_dsi_ns.class_("RpiDsiLight", light.LightOutput)

CONFIG_SCHEMA = light.light_schema(
    RpiDsiLight, type_=light.LightType.BRIGHTNESS_ONLY
).extend({cv.Required(CONF_DISPLAY_ID): cv.use_id(RpiDsi)})


async def to_code(config: ConfigType) -> None:
    display = await cg.get_variable(config[CONF_DISPLAY_ID])
    await light.new_light(config, display)
