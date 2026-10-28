# ESPHome RPi DSI Display Component

This proof-of-concept ESPHome component supports Raspberry Pi-compatible DSI displays on the ESP32-P4.
**Currently**, it supports only the [Hosyond 5-inch, 800 × 480 Raspberry Pi-compatible DSI touchscreen](https://www.amazon.com/dp/B0CXTFN8K9).

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

The test card, an existing LVGL interface, touch input, and backlight control have
been confirmed working on a [Waveshare ESP32-P4-Module-DEV-KIT](https://www.waveshare.com/esp32-p4-module-dev-kit.htm) with the
[Hosyond display, ASIN B0CXTFN8K9](https://www.amazon.com/dp/B0CXTFN8K9).
Other panels and boards have not been validated. Rendering performance is still
being tuned; the RGB565 block-conversion optimization has no measured speedup yet.

## Requirements

- ESP32-P4 with PSRAM and a compatible DSI connection carrying the panel's I²C bus
  and power.
- Developed and tested with ESPHome `2026.11.0-dev`. Older releases have not been validated.
- ESP-IDF `release/v6.1`, including the public DPI `video_burst_type` and
  `disable_frame_ack` fields. The version label `6.1.0` alone does not establish
  that these maintenance-branch APIs are available.
- The modified IDF driver from
  [jb1228/esp_lcd_rpi_dsi](https://github.com/jb1228/esp_lcd_rpi_dsi), pinned below.

The display driver acquires LDO channel 3 at 2.5 V. Do not also configure that
channel under `esp_ldo`. Other channels, such as a board's GPIO supply on LDO4,
remain independent. This component currently cannot be used alongside `mipi_dsi`.

## Installation

The current code resolves the IDF dependency at
`components/esp_lcd_rpi_dsi` inside this repository. That dependency is not bundled
or downloaded automatically. A GitHub `external_components` source alone is
therefore not a complete installation yet.


## Display and backlight example

This is a minimal test-card configuration. Use `engineering_sample: true` if necessary.

```yaml
esphome:
  name: hosyond-display

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
    transform:
      mirror_x: true
      mirror_y: true
```

Both axes are mirrored to correct the 180° touch offset observed on the Hosyond
panel in landscape orientation. Confirm alignment on your hardware.

The native `ft5x06` component rejected this panel's vendor ID `0x79` during
setup. `ft63x6` uses the compatible touch-report register layout without that
vendor-ID restriction. The IDF driver's own touch reader remains disabled.

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
            text: "Hosyond DSI display"
```

ESPHome's current LVGL integration accepts RGB565 output. This component converts it
to the panel's RGB888 framebuffer. Packed RGB888 input is also supported by the
display API; four-byte ARGB/XRGB8888 input is not implemented.

## Fixed panel profile

Panel timings are currently fixed. Configurable timings are planned for a future update.

| Parameter | Value |
| --- | --- |
| Native resolution and format | 800 × 480, RGB888; B, G, R bytes in memory |
| DSI data lanes and rate | 1 lane, 600 Mbps |
| Requested pixel clock | 26 MHz; ESP-IDF may compensate clock and porch values |
| Horizontal sync / back porch / front porch | 2 / 46 / 210 |
| Vertical sync / back porch / front porch | 20 / 4 / 22 |
| Video mode | Non-burst with sync pulses |
| Clock lane | Continuous high speed |
| Low-power blanking / frame acknowledgements | Allowed / disabled |
| Panel controller / touch address | `0x45` / `0x38` |

The IDF driver retains ownership of power, resets, bridge writes, backlight, and
the framebuffer. CPU writes are synchronized to memory for so the display controller can read them.
This component uses one framebuffer and currently writes back the full framebuffer
after a changed drawing block. It does not provide tear-free animation or an
asynchronous DMA2D rendering pipeline.

ESP-IDF commit used during hardware testing: `9a97f6c54ec638111ce55cd36581b3c192f15207`
The driver fork revision pinned above is
[`27515f2`](https://github.com/jb1228/esp_lcd_rpi_dsi/commit/27515f2dde9525a84a373766586ff3338876c437),
based on upstream `4a3f3b6b09e034c9e54c79ccfaeb10b7f756e1d2`.

## Credits

- [Kuro's esp_lcd_rpi_dsi](https://github.com/kurozz/esp_lcd_rpi_dsi): panel and bridge driver, licensed GPL-2.0-only.
- [jb1228's driver fork](https://github.com/jb1228/esp_lcd_rpi_dsi): public DPI video configuration and disabled frame acknowledgements.
- [ESP32 PoE Scrypted Viewport](https://github.com/lukehoersten/esp32-poe-scrypted-viewport): Hosyond timing settings and hardware setup reference.
