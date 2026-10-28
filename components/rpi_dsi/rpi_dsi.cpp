#include "rpi_dsi.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "esphome/core/log.h"
#include "esp_cache.h"

namespace esphome::rpi_dsi {

ESPHOME_LOG_TAG(TAG, "display.rpi_dsi");

void RpiDsi::setup() {
  i2c_master_bus_handle_t bus = nullptr;
  auto err = i2c_master_get_bus_handle(this->i2c_bus_->get_port(), &bus);
  if (err != ESP_OK || bus == nullptr) {
    this->fail_(LOG_STR("I2C bus unavailable"), err != ESP_OK ? err : ESP_ERR_INVALID_STATE);
    return;
  }

  esp_lcd_rpi_dsi_config_t config{};
  config.i2c.bus = bus;
  config.ldo_chan = 3;
  config.ldo_voltage_mv = 2500;
  config.dsi_bus_id = 0;
  config.lane_bit_rate_mbps = 600;
  config.dpi_clock_freq_mhz = 26;
  config.timing = {
      .h_size = WIDTH,
      .v_size = HEIGHT,
      .hsync_pulse_width = 2,
      .hsync_back_porch = 46,
      .hsync_front_porch = 210,
      .vsync_pulse_width = 20,
      .vsync_back_porch = 4,
      .vsync_front_porch = 22,
  };
  config.brightness = this->brightness_;
  config.flags.continuous_clock = true;
  config.flags.non_burst_video = true;
  config.flags.disable_lp = false;
  config.flags.enable_touch = false;
  config.flags.use_dma2d = false;

  err = esp_lcd_rpi_dsi_start(&config, &this->driver_);
  if (err != ESP_OK) {
    this->fail_(LOG_STR("Display initialization failed"), err);
    return;
  }

  auto panel = esp_lcd_rpi_dsi_get_panel(this->driver_);
  if (panel == nullptr) {
    this->fail_(LOG_STR("Display panel unavailable"), ESP_ERR_INVALID_STATE);
    return;
  }
  void *framebuffer = nullptr;
  err = esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &framebuffer);
  if (err != ESP_OK || framebuffer == nullptr) {
    this->fail_(LOG_STR("Display framebuffer unavailable"), err != ESP_OK ? err : ESP_ERR_INVALID_STATE);
    return;
  }
  this->framebuffer_ = static_cast<uint8_t *>(framebuffer);
  // Also applies zero, which the driver's initial configuration treats as its default brightness.
  this->set_brightness(this->brightness_);
}

void RpiDsi::set_brightness(uint8_t brightness) {
  if (this->driver_ != nullptr && this->brightness_applied_ && this->brightness_ == brightness)
    return;
  this->brightness_ = brightness;
  if (this->driver_ == nullptr)
    return;

  auto err = esp_lcd_rpi_dsi_set_brightness(this->driver_, brightness);
  if (err != ESP_OK) {
    this->brightness_applied_ = false;
    ESP_LOGW(TAG, "Backlight brightness update failed: %s", esp_err_to_name(err));
    this->status_set_warning(LOG_STR("Backlight brightness update failed"));
    return;
  }
  this->brightness_applied_ = true;
  this->status_clear_warning();
}

void RpiDsi::fail_(const LogString *reason, esp_err_t error) {
  ESP_LOGE(TAG, "%s: %s", LOG_STR_ARG(reason), esp_err_to_name(error));
  this->mark_failed(reason);
  this->framebuffer_ = nullptr;
  this->brightness_applied_ = false;
  if (this->driver_ != nullptr) {
    auto err = esp_lcd_rpi_dsi_stop(this->driver_);
    this->driver_ = nullptr;
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Display cleanup failed: %s", esp_err_to_name(err));
    }
  }
}

void RpiDsi::update() {
  if (this->framebuffer_ == nullptr)
    return;

  if (this->show_test_card_ || this->page_ != nullptr || this->writer_.has_value() || this->auto_clear_enabled_) {
    this->updating_ = true;
    this->do_update_();
    this->updating_ = false;
  } else {
    this->stop_poller();
  }
  this->flush_();
}

void RpiDsi::flush_() {
  if (!this->dirty_ || this->framebuffer_ == nullptr)
    return;

  // The driver owns an aligned framebuffer; the fixed full-frame size is also cache-line aligned.
  auto err = esp_cache_msync(this->framebuffer_, FRAMEBUFFER_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (err != ESP_OK) {
    this->fail_(LOG_STR("Framebuffer cache synchronization failed"), err);
    return;
  }
  this->dirty_ = false;
}

int RpiDsi::get_width() {
  return display::rotation_swaps_axes(this->rotation_) ? HEIGHT : WIDTH;
}

int RpiDsi::get_height() {
  return display::rotation_swaps_axes(this->rotation_) ? WIDTH : HEIGHT;
}

void RpiDsi::draw_pixel_at(int x, int y, Color color) {
  if (this->framebuffer_ == nullptr || x < 0 || y < 0 || x >= this->get_width() || y >= this->get_height() ||
      this->is_point_clipped(x, y))
    return;

  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
      std::swap(x, y);
      x = WIDTH - x - 1;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = WIDTH - x - 1;
      y = HEIGHT - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      std::swap(x, y);
      y = HEIGHT - y - 1;
      break;
    default:
      break;
  }

  auto *pixel = this->framebuffer_ + (y * WIDTH + x) * 3;
  if (pixel[0] != color.b || pixel[1] != color.g || pixel[2] != color.r) {
    pixel[0] = color.b;
    pixel[1] = color.g;
    pixel[2] = color.r;
    this->dirty_ = true;
  }
  this->feed_wdt_per_pixel_();
}

void RpiDsi::fill(Color color) {
  if (this->framebuffer_ == nullptr)
    return;
  if (this->is_clipping()) {
    Display::fill(color);
    return;
  }
  for (size_t offset = 0; offset < FRAMEBUFFER_SIZE; offset += 3) {
    auto *pixel = this->framebuffer_ + offset;
    if (pixel[0] != color.b || pixel[1] != color.g || pixel[2] != color.r) {
      pixel[0] = color.b;
      pixel[1] = color.g;
      pixel[2] = color.r;
      this->dirty_ = true;
    }
    this->feed_wdt_per_pixel_();
  }
}

void RpiDsi::draw_pixels_internal(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                  display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset,
                                  int x_pad) {
  if (this->framebuffer_ == nullptr || w <= 0 || h <= 0)
    return;

  if ((bitness != display::COLOR_BITNESS_888 && bitness != display::COLOR_BITNESS_565) ||
      this->rotation_ != display::DISPLAY_ROTATION_0_DEGREES ||
      this->is_clipping()) {
    Display::draw_pixels_internal(x_start, y_start, w, h, ptr, bitness, big_endian, x_offset, y_offset, x_pad);
  } else {
    const int first_col = std::max(0, -x_start);
    const int end_col = std::min(w, WIDTH - x_start);
    const int first_row = std::max(0, -y_start);
    const int end_row = std::min(h, HEIGHT - y_start);
    if (first_col < end_col && first_row < end_row) {
      const size_t source_bytes_per_pixel = bitness == display::COLOR_BITNESS_565 ? 2 : 3;
      const size_t stride = static_cast<size_t>(x_offset + w + x_pad) * source_bytes_per_pixel;
      const size_t row_bytes = (end_col - first_col) * 3;
      for (int row = first_row; row < end_row; row++) {
        const auto *source = ptr + (y_offset + row) * stride + (x_offset + first_col) * source_bytes_per_pixel;
        auto *destination = this->framebuffer_ + ((y_start + row) * WIDTH + x_start + first_col) * 3;
        if (bitness == display::COLOR_BITNESS_888) {
          if (std::memcmp(destination, source, row_bytes) != 0) {
            std::memcpy(destination, source, row_bytes);
            this->dirty_ = true;
          }
        } else {
          // LVGL supplies RGB565. Convert each row directly into the borrowed framebuffer.
          for (int col = 0; col < end_col - first_col; col++, destination += 3) {
            const auto color = display::ColorUtil::to_color(
                display::ColorUtil::read_packed<display::COLOR_BITNESS_565>(source, col, big_endian),
                display::COLOR_BITNESS_565);
            if (destination[0] != color.b || destination[1] != color.g || destination[2] != color.r) {
              destination[0] = color.b;
              destination[1] = color.g;
              destination[2] = color.r;
              this->dirty_ = true;
            }
            this->feed_wdt_per_pixel_();
          }
        }
      }
    }
  }
  if (!this->updating_)
    this->flush_();
}

void RpiDsi::dump_config() {
  LOG_DISPLAY("", "Hosyond RPi DSI", this);
  ESP_LOGCONFIG(TAG,
                "  Native size: 800x480, RGB888\n"
                "  DSI: 1 lane at 600 Mbps, pixel clock 26 MHz\n"
                "  Framebuffer: borrowed from IDF, %u bytes",
                static_cast<unsigned>(FRAMEBUFFER_SIZE));
}

}  // namespace esphome::rpi_dsi
