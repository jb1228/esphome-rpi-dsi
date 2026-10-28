#pragma once

#include "esphome/components/display/display.h"
#include "esphome/components/i2c/i2c_bus.h"
#include "esp_lcd_rpi_dsi.h"

namespace esphome::rpi_dsi {

class RpiDsi final : public display::Display {
 public:
  RpiDsi(i2c::InternalI2CBus *bus, const esp_lcd_rpi_dsi_config_t &config) : i2c_bus_(bus), config_(config) {}

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
  int get_width_internal() override { return this->config_.timing.h_size; }
  int get_height_internal() override { return this->config_.timing.v_size; }
  size_t get_framebuffer_size_() const {
    return static_cast<size_t>(this->config_.timing.h_size) * this->config_.timing.v_size * 3;
  }
  void draw_pixels_internal(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                            display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset,
                            int x_pad) override;
  void flush_();
  void fail_(const LogString *reason, esp_err_t error);

  i2c::InternalI2CBus *i2c_bus_;
  esp_lcd_rpi_dsi_config_t config_;
  esp_lcd_rpi_dsi_handle_t driver_{};
  uint8_t *framebuffer_{nullptr};  // Borrowed from the IDF driver.
  uint8_t brightness_{255};
  bool brightness_applied_{false};
  bool dirty_{false};
  bool updating_{false};
};

}  // namespace esphome::rpi_dsi
