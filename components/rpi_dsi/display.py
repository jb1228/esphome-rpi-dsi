import esphome.codegen as cg
from esphome.components import display, esp32, i2c
from esphome.components.const import BYTE_ORDER_LITTLE
from esphome.components.display import CONF_SHOW_TEST_CARD
from esphome.components.mipi import (
    CONF_HSYNC_BACK_PORCH,
    CONF_HSYNC_FRONT_PORCH,
    CONF_HSYNC_PULSE_WIDTH,
    CONF_PCLK_FREQUENCY,
    CONF_VSYNC_BACK_PORCH,
    CONF_VSYNC_FRONT_PORCH,
    CONF_VSYNC_PULSE_WIDTH,
)
import esphome.config_validation as cv
from esphome.const import (
    CONF_AUTO_CLEAR_ENABLED,
    CONF_CHANNEL,
    CONF_DIMENSIONS,
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
RpiDsiConfig = cg.global_ns.struct("esp_lcd_rpi_dsi_config_t")

CONF_LANE_BIT_RATE = "lane_bit_rate"
CONF_LDO_CHANNEL = "ldo_channel"
CONF_LDO_VOLTAGE = "ldo_voltage"
CONF_DSI_BUS_ID = "dsi_bus_id"
CONF_CONTINUOUS_CLOCK = "continuous_clock"
CONF_NON_BURST_VIDEO = "non_burst_video"
CONF_DISABLE_LP = "disable_lp"
CONF_DISABLE_FRAME_ACK = "disable_frame_ack"

TIMING_OPTIONS = (
    CONF_HSYNC_PULSE_WIDTH,
    CONF_HSYNC_BACK_PORCH,
    CONF_HSYNC_FRONT_PORCH,
    CONF_VSYNC_PULSE_WIDTH,
    CONF_VSYNC_BACK_PORCH,
    CONF_VSYNC_FRONT_PORCH,
)


def _add_metadata(config: ConfigType) -> ConfigType:
    display.add_metadata(
        config[CONF_ID],
        *config[CONF_DIMENSIONS],
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
            cv.Required(CONF_DIMENSIONS): cv.dimensions,
            cv.Required(CONF_LANE_BIT_RATE): cv.All(
                cv.bps, cv.Range(min=80e6, max=1500e6)
            ),
            cv.Required(CONF_PCLK_FREQUENCY): cv.All(
                cv.frequency, cv.Range(min=4e6, max=100e6)
            ),
            **{cv.Required(option): cv.uint32_t for option in TIMING_OPTIONS},
            cv.Optional(CONF_LDO_CHANNEL, default=3): cv.one_of(-1, 3, 4, int=True),
            cv.Optional(CONF_LDO_VOLTAGE, default="2.5V"): cv.All(
                cv.voltage, cv.Range(min=0.5, max=2.7)
            ),
            cv.Optional(CONF_DSI_BUS_ID, default=0): cv.one_of(0, int=True),
            cv.Optional(CONF_CONTINUOUS_CLOCK, default=True): cv.boolean,
            cv.Optional(CONF_NON_BURST_VIDEO, default=True): cv.boolean,
            cv.Optional(CONF_DISABLE_LP, default=False): cv.boolean,
            cv.Optional(CONF_DISABLE_FRAME_ACK, default=True): cv.boolean,
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
        if ldo[CONF_CHANNEL] == config[CONF_LDO_CHANNEL]:
            raise cv.Invalid(
                f"rpi_dsi manages LDO channel {config[CONF_LDO_CHANNEL]}; "
                "remove its esp_ldo entry or set ldo_channel: -1"
            )

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
    esp32.add_idf_component(
        name="esp_lcd_rpi_dsi",
        repo="https://github.com/jb1228/esp_lcd_rpi_dsi.git",
        ref="8cbaa74421928055473d1ed8be942e10a3a5c798",
    )
    esp32.include_builtin_idf_component("esp_lcd")

    bus = await cg.get_variable(config[CONF_I2C_ID])
    width, height = config[CONF_DIMENSIONS]
    driver_config = cg.StructInitializer(
        RpiDsiConfig,
        ("ldo_chan", config[CONF_LDO_CHANNEL]),
        ("ldo_voltage_mv", int(round(config[CONF_LDO_VOLTAGE] * 1000))),
        ("dsi_bus_id", config[CONF_DSI_BUS_ID]),
        ("lane_bit_rate_mbps", config[CONF_LANE_BIT_RATE] / 1e6),
        ("dpi_clock_freq_mhz", config[CONF_PCLK_FREQUENCY] / 1e6),
        (
            "timing",
            cg.StructInitializer(
                cg.RawExpression(""),
                ("h_size", width),
                ("v_size", height),
                *((option, config[option]) for option in TIMING_OPTIONS),
            ),
        ),
        (
            "flags",
            cg.StructInitializer(
                cg.RawExpression(""),
                ("continuous_clock", config[CONF_CONTINUOUS_CLOCK]),
                ("non_burst_video", config[CONF_NON_BURST_VIDEO]),
                ("disable_lp", config[CONF_DISABLE_LP]),
                # ESPHome owns touch input and writes directly into the framebuffer.
                ("enable_touch", False),
                ("use_dma2d", False),
                ("disable_frame_ack", config[CONF_DISABLE_FRAME_ACK]),
            ),
        ),
    )
    var = cg.new_Pvariable(config[CONF_ID], bus, driver_config)
    await display.register_display(var, config)
    await display.add_writer(var, config)
