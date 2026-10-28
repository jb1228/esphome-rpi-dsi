from pathlib import Path

import esphome.codegen as cg
from esphome.components import display, esp32, i2c
from esphome.components.const import BYTE_ORDER_LITTLE
from esphome.components.display import CONF_SHOW_TEST_CARD
import esphome.config_validation as cv
from esphome.const import (
    CONF_AUTO_CLEAR_ENABLED,
    CONF_CHANNEL,
    CONF_I2C_ID,
    CONF_ID,
    CONF_LAMBDA,
    CONF_PAGES,
    CONF_ROTATION,
    CONF_UPDATE_INTERVAL,
)
from esphome.final_validate import full_config
from esphome.types import ConfigType

from . import rpi_dsi_ns

CONFLICTS_WITH = ["mipi_dsi"]
DEPENDENCIES = ["esp32", "i2c", "psram"]

RpiDsi = rpi_dsi_ns.class_("RpiDsi", display.Display)


def _add_metadata(config: ConfigType) -> ConfigType:
    display.add_metadata(
        config[CONF_ID],
        800,
        480,
        has_hardware_rotation=False,
        byte_order=BYTE_ORDER_LITTLE,
        has_writer=config.get(CONF_AUTO_CLEAR_ENABLED) is True
        or CONF_LAMBDA in config
        or CONF_PAGES in config
        or config.get(CONF_SHOW_TEST_CARD) is True,
        rotation=config.get(CONF_ROTATION, 0),
        draw_rounding=1,
    )
    return config


CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(RpiDsi),
            cv.Required(CONF_I2C_ID): cv.use_id(i2c.InternalI2CBus),
            cv.Optional(CONF_UPDATE_INTERVAL): cv.update_interval,
        }
    ),
    cv.only_on_esp32,
    esp32.only_on_variant(supported=[esp32.VARIANT_ESP32P4]),
    esp32.require_idf_version(cv.Version(6, 1, 0), "rpi_dsi"),
    _add_metadata,
)


def _final_validate(config: ConfigType) -> None:
    for ldo in full_config.get().get("esp_ldo", []):
        if ldo[CONF_CHANNEL] == 3:
            raise cv.Invalid("rpi_dsi manages LDO channel 3; remove its esp_ldo entry")

    if CONF_LAMBDA not in config and CONF_PAGES not in config:
        if display.has_external_writer(config[CONF_ID]):
            if (
                not config.get(CONF_SHOW_TEST_CARD)
                and config.get(CONF_AUTO_CLEAR_ENABLED) is not True
            ):
                config.setdefault(CONF_UPDATE_INTERVAL, cv.update_interval("never"))
        else:
            config.setdefault(CONF_SHOW_TEST_CARD, True)
    config.setdefault(CONF_UPDATE_INTERVAL, cv.update_interval("1s"))


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    # Use the adjacent local IDF component, including its reviewed compatibility change.
    esp32.add_idf_component(
        name="esp_lcd_rpi_dsi",
        path=str(Path(__file__).resolve().parent.parent / "esp_lcd_rpi_dsi"),
    )
    esp32.include_builtin_idf_component("esp_lcd")

    bus = await cg.get_variable(config[CONF_I2C_ID])
    var = cg.new_Pvariable(config[CONF_ID], bus)
    await display.register_display(var, config)
    await display.add_writer(var, config)
