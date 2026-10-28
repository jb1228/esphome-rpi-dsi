#pragma once

#include "esphome/components/display/display.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "esp_lcd_rpi_dsi.h"

namespace esphome::rpi_dsi {

class RpiDsi final : public display::Display {
 public:
  explicit RpiDsi(i2c::InternalI2CBus *bus) : i2c_bus_(bus) {}

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }
  int get_width() override;
  int get_height() override;
  void draw_pixel_at(int x, int y, Color color) override;
  void fill(Color color) override;
  void set_brightness(uint8_t brightness);

 protected:
  static constexpr int WIDTH = 800;
  static constexpr int HEIGHT = 480;
  static constexpr size_t FRAMEBUFFER_SIZE = WIDTH * HEIGHT * 3;

  int get_width_internal() override { return WIDTH; }
  int get_height_internal() override { return HEIGHT; }
  void draw_pixels_internal(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                            display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset,
                            int x_pad) override;
  void flush_();
  void fail_(const LogString *reason, esp_err_t error);

  i2c::InternalI2CBus *i2c_bus_;
  esp_lcd_rpi_dsi_handle_t driver_{};
  uint8_t *framebuffer_{nullptr};  // Borrowed from the IDF driver.
  uint8_t brightness_{255};
  bool brightness_applied_{false};
  bool dirty_{false};
  bool updating_{false};
};

}  // namespace esphome::rpi_dsi
