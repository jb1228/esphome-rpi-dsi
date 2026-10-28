#pragma once

#include <cmath>

#include "esphome/components/light/light_output.h"
#include "esphome/components/rpi_dsi/rpi_dsi.h"

namespace esphome::rpi_dsi {

class RpiDsiLight final : public light::LightOutput {
 public:
  explicit RpiDsiLight(RpiDsi *display) : display_(display) {}

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    return traits;
  }

  void write_state(light::LightState *state) override {
    float brightness;
    state->current_values_as_brightness(&brightness);
    this->display_->set_brightness(static_cast<uint8_t>(std::lround(brightness * 255.0f)));
  }

 protected:
  RpiDsi *display_;
};

}  // namespace esphome::rpi_dsi
