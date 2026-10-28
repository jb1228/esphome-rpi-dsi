# ESPHome RPi DSI Display Component

This proof-of-concept ESPHome component supports Raspberry Pi-compatible DSI displays on the ESP32-P4.
**Currently**, it supports only the [Hosyond 5-inch, 800 × 480 Raspberry Pi-compatible DSI touchscreen](https://www.amazon.com/dp/B0CXTFN8K9), but may work on other Raspberry Pi-compatible dispalys with a TC358762 bridge and ATTINY controller
interface. Set the dimensions, timings, and link settings for your panel in YAML.

This `rpi_dsi` display platform uses
[Kuro's ESP-IDF display driver](https://github.com/kurozz/esp_lcd_rpi_dsi), which
handles the panel controller, power sequencing, resets, bridge initialization,
and backlight. This component shares an existing I²C bus and draws directly
into the framebuffer owned by the IDF driver.

## Features and status

- Standard ESPHome display lambdas, pages, test card, clipping, and software rotation.
- RGB565 block conversion and direct packed RGB888 copies into the framebuffer.
- Brightness-only `rpi_dsi` light platform using the driver's backlight API.
- Touch input through ESPHome's native `ft63x6` component.
- Not a replacement or fork of ESPHome's built-in `mipi_dsi` component.

The test card, an existing LVGL interface, touch input, and backlight control were
tested with the original fixed panel configuration on a Waveshare
ESP32-P4-Module-DEV-KIT (ESP32-P4 silicon revision v1.0). Configurable panel settings have not yet been tested on
hardware. Rendering performance is still being tuned; the RGB565 block-conversion
optimization has no measured speedup yet.

## Requirements

- ESP32-P4 with PSRAM and a compatible DSI connection carrying the panel's I²C bus
  and power.
- Developed and tested with ESPHome `2026.11.0-dev`. Older releases have not been validated.
- ESP-IDF `release/v6.1`, including the public DPI `video_burst_type` and
  `disable_frame_ack` fields. The version label `6.1.0` alone does not establish
  that these maintenance-branch APIs are available.
- The modified IDF driver from
  [jb1228/esp_lcd_rpi_dsi](https://github.com/jb1228/esp_lcd_rpi_dsi), pinned to
  [`8cbaa74421928055473d1ed8be942e10a3a5c798`](https://github.com/jb1228/esp_lcd_rpi_dsi/commit/8cbaa74421928055473d1ed8be942e10a3a5c798).

By default, the display driver acquires LDO channel 3 at 2.5 V. Configure this with
`ldo_channel` and `ldo_voltage`, or set `ldo_channel: -1` when the PHY supply is
managed elsewhere. Do not also configure the driver's selected channel under
`esp_ldo`. This component currently cannot be used alongside `mipi_dsi`.

## Display and backlight example

This test-card example uses the driver's Raspberry Pi legacy timings. Replace
them with your panel's settings. Use `engineering_sample: true` if necessary.

```yaml
esphome:
  name: rpi-dsi-display

esp32:
  variant: esp32p4
  engineering_sample: true
  flash_size: 16MB
  framework:
    type: esp-idf
    version: 6.1.0
    source: github://espressif/esp-idf@release/v6.1

logger:

psram:
  mode: hex
  speed: 200MHz

i2c:
  - id: i2c_bus0
    sda: GPIO7
    scl: GPIO8
    frequency: 400kHz
    scan: true

external_components:
  - source:
      type: local
      path: components/esphome-rpi-dsi/components
    components: [rpi_dsi]

display:
  - platform: rpi_dsi
    id: my_display
    i2c_id: i2c_bus0
    dimensions: 800x480
    lane_bit_rate: 700Mbps
    pclk_frequency: 26MHz
    hsync_pulse_width: 2
    hsync_back_porch: 46
    hsync_front_porch: 1
    vsync_pulse_width: 2
    vsync_back_porch: 21
    vsync_front_porch: 7
    continuous_clock: true
    non_burst_video: false
    disable_lp: false
    disable_frame_ack: true
    rotation: 0
    show_test_card: true

light:
  - platform: rpi_dsi
    id: light_display_backlight
    name: Display Backlight
    display_id: my_display
    restore_mode: RESTORE_DEFAULT_ON
```

`i2c_id` must reference an internal ESPHome I²C bus. This component retrieves its
IDF handle using `get_port()` and `i2c_master_get_bus_handle()`.

The light supports normal ESPHome transitions and gamma correction. Zero
brightness turns the backlight off.

## Touch input

Touch is configured separately using native ESPHome functionality. The panel
controller releases touch reset during display initialization, so initialize the
touchscreen afterward using `setup_priority: 300`:

```yaml
touchscreen:
  - platform: ft63x6
    id: my_touchscreen
    display: my_display
    i2c_id: i2c_bus0
    address: 0x38
    setup_priority: 300  # The display initializes at priority 400.
```

Choose the native touch component, address, and axis transforms for your hardware.
The IDF driver's own touch reader remains disabled so ESPHome can own touch input.

## LVGL

```yaml
lvgl:
  displays:
    - my_display
  color_depth: 16
  byte_order: little_endian
  touchscreens:
    - touchscreen_id: my_touchscreen
  pages:
    - id: home
      widgets:
        - label:
            align: CENTER
            text: "RPi DSI display"
```

ESPHome's current LVGL integration accepts RGB565 output. This component converts it
to the panel's RGB888 framebuffer. Packed RGB888 input is also supported by the
display API; four-byte ARGB/XRGB8888 input is not implemented.

## Panel configuration

`dimensions`, `lane_bit_rate`, `pclk_frequency`, and all six `hsync_*` / `vsync_*`
timing values are required. Dimensions use native, unrotated pixel coordinates;
sync widths and porches are in pixels and lines respectively. The component uses
these dimensions for rendering, framebuffer synchronization, and LVGL metadata.

| Optional setting | Default |
| --- | --- |
| `ldo_channel` | `3`; use `-1` for an externally managed supply |
| `ldo_voltage` | `2.5V` |
| `dsi_bus_id` | `0` (the ESP32-P4's only DSI controller) |
| `continuous_clock` | `true` |
| `non_burst_video` | `true` (sync pulses); `false` selects burst with sync pulses |
| `disable_lp` | `false` |
| `disable_frame_ack` | `true` |

The driver supports one DSI data lane and an RGB888 framebuffer with B, G, R bytes
in memory. These are requirements of the supported bridge interface. ESP-IDF may
adjust the requested pixel clock and porches. Backlight brightness is controlled
through the `rpi_dsi` light platform.

The IDF driver retains ownership of power, resets, bridge writes, backlight, and
the framebuffer. CPU writes are synchronized to memory for so the display controller can read them.
This component uses one framebuffer and currently writes back the full framebuffer
after a changed drawing block. It does not provide tear-free animation or an
asynchronous DMA2D rendering pipeline.

ESP-IDF commit used during the original hardware testing:
`9a97f6c54ec638111ce55cd36581b3c192f15207`.

## Credits

- [Kuro's esp_lcd_rpi_dsi](https://github.com/kurozz/esp_lcd_rpi_dsi): panel and bridge driver, licensed GPL-2.0-only.
- [jb1228's driver fork](https://github.com/jb1228/esp_lcd_rpi_dsi): public DPI video configuration and disabled frame acknowledgements.
- [ESP32 PoE Scrypted Viewport](https://github.com/lukehoersten/esp32-poe-scrypted-viewport): original timing settings and hardware setup reference.
