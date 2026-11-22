#include "rj45_port/rj45_port.hpp"

#include <cstddef>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cable_tester {

Rj45Port::Rj45Port(const std::array<gpio_num_t, cable_test::kLineCount>& pins) : pins_(pins) {}

esp_err_t Rj45Port::init() {
    gpio_config_t config{};
    for (const gpio_num_t pin : pins_) {
        config.pin_bit_mask |= (1ULL << static_cast<unsigned>(pin));
    }
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&config);
}

void Rj45Port::setInputPullDown(gpio_num_t pin) const {
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
}

void Rj45Port::setFloating(gpio_num_t pin) const {
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
}

void Rj45Port::allInputsPullDown() {
    for (const gpio_num_t pin : pins_) {
        setInputPullDown(pin);
    }
}

void Rj45Port::allFloating() {
    for (const gpio_num_t pin : pins_) {
        setFloating(pin);
    }
}

void Rj45Port::driveHigh(std::size_t line) {
    const gpio_num_t pin = pins_[line];
    gpio_set_pull_mode(pin, GPIO_FLOATING);
    gpio_set_level(pin, 1);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

void Rj45Port::release(std::size_t line) {
    setInputPullDown(pins_[line]);
}

cable_test::LineMask Rj45Port::sample() const {
    cable_test::LineMask mask = 0;
    for (std::size_t line = 0; line < pins_.size(); ++line) {
        if (gpio_get_level(pins_[line]) != 0) {
            mask = static_cast<cable_test::LineMask>(mask | cable_test::line_bit(line));
        }
    }
    return mask;
}

std::optional<double> Rj45Port::measureLengthMeters(std::size_t line,
                                                    const LengthConfig& config) {
    const gpio_num_t pin = pins_[line];

    // Ader entladen; alle anderen Leitungen sind bereits hochohmig.
    gpio_set_pull_mode(pin, GPIO_FLOATING);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(config.discharge_ms));

    // Auf Eingang mit Pull-up umschalten und Ladezeit messen.
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    const std::int64_t start = esp_timer_get_time();
    std::int64_t elapsed = -1;
    while (true) {
        if (gpio_get_level(pin) != 0) {
            elapsed = esp_timer_get_time() - start;
            break;
        }
        if (esp_timer_get_time() - start > config.timeout_us) {
            break;
        }
    }
    // Leitung bleibt hochohmig; der Aufrufer stellt den sicheren Zustand her.
    gpio_set_pull_mode(pin, GPIO_FLOATING);

    if (elapsed < 0) {
        return std::nullopt;
    }

    const double seconds = static_cast<double>(elapsed) * 1e-6;
    const double capacitance_f = seconds / (config.pullup_ohm * config.threshold_factor);
    const double meters =
        (capacitance_f - config.baseline_pf * 1e-12) / (config.pf_per_meter * 1e-12);
    if (meters < 0.0 || meters > 300.0) {
        return std::nullopt;
    }
    return meters;
}

} // namespace cable_tester
