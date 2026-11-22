#include "rj45_port/rj45_port.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cable_tester {

Rj45Port::Rj45Port(const std::array<gpio_num_t, cable_test::kLineCount>& pins) : pins_(pins) {}

esp_err_t Rj45Port::init() {
    stopTone();

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

std::optional<double> Rj45Port::measureLengthSampleMeters(std::size_t line,
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

std::optional<double> Rj45Port::measureLengthMeters(std::size_t line,
                                                    const LengthConfig& config) {
    if (line >= pins_.size()) {
        return std::nullopt;
    }

    std::array<double, 9> values{};
    const std::size_t samples =
        std::min<std::size_t>(std::max<std::uint32_t>(config.samples, 1), values.size());
    std::size_t count = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const auto value = measureLengthSampleMeters(line, config);
        if (value) {
            values[count++] = *value;
        }
    }
    if (count == 0) {
        return std::nullopt;
    }

    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    const double median = (count % 2 == 1)
                              ? values[count / 2]
                              : (values[count / 2 - 1] + values[count / 2]) / 2.0;
    return median;
}

esp_err_t Rj45Port::ensureAdc() {
#if CONFIG_IDF_TARGET_ESP32S3
    if (adc_ready_) {
        return ESP_OK;
    }
    if (adc_failed_) {
        return ESP_FAIL;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg{};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &adc1_);
    if (err != ESP_OK) {
        adc1_ = nullptr;
        adc_failed_ = true;
        return err;
    }

    // ADC2 teilt sich mit WiFi; wenn die Unit nicht verfügbar ist, bleibt
    // nur Ader 8 ohne ADC-Messung. ADC1 deckt GPIO 4-10 ab.
    unit_cfg.unit_id = ADC_UNIT_2;
    if (adc_oneshot_new_unit(&unit_cfg, &adc2_) != ESP_OK) {
        adc2_ = nullptr;
    }

    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

    bool any_channel = false;
    for (std::size_t line = 0; line < pins_.size(); ++line) {
        adc_unit_t unit = ADC_UNIT_1;
        adc_channel_t channel = ADC_CHANNEL_0;
        if (adc_oneshot_io_to_channel(static_cast<int>(pins_[line]), &unit, &channel) != ESP_OK) {
            continue;
        }
        adc_oneshot_unit_handle_t handle = (unit == ADC_UNIT_1) ? adc1_ : adc2_;
        if (handle == nullptr) {
            continue;
        }
        if (adc_oneshot_config_channel(handle, channel, &chan_cfg) != ESP_OK) {
            continue;
        }
        adc_units_[line] = unit;
        adc_channels_[line] = channel;
        adc_channel_ok_[line] = true;
        any_channel = true;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cali_cfg{};
        cali_cfg.unit_id = unit;
        cali_cfg.chan = channel;
        cali_cfg.atten = ADC_ATTEN_DB_12;
        cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_[line]) != ESP_OK) {
            adc_cali_[line] = nullptr;
        }
#endif
    }

    if (!any_channel) {
        adc_failed_ = true;
        return ESP_ERR_NOT_FOUND;
    }
    adc_ready_ = true;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

std::optional<double> Rj45Port::measureShortVoltage(std::size_t line) {
#if CONFIG_IDF_TARGET_ESP32S3
    if (line >= pins_.size()) {
        return std::nullopt;
    }
    if (ensureAdc() != ESP_OK || !adc_channel_ok_[line]) {
        return std::nullopt;
    }

    // Andere Adern definiert nach GND, die Messader über den internen
    // Pull-up. Der Slave muss währenddessen hochohmig sein.
    allInputsPullDown();
    const gpio_num_t pin = pins_[line];
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(2));

    const adc_oneshot_unit_handle_t handle =
        (adc_units_[line] == ADC_UNIT_1) ? adc1_ : adc2_;
    int raw = 0;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = adc_oneshot_read(handle, adc_channels_[line], &raw);
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // Ader wieder in den sicheren Zustand bringen.
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
    if (err != ESP_OK) {
        return std::nullopt;
    }

    int millivolts = 0;
    if (adc_cali_[line] != nullptr &&
        adc_cali_chan_raw_to_voltage(adc_cali_[line], adc_channels_[line], raw, &millivolts) ==
            ESP_OK) {
        return static_cast<double>(millivolts) / 1000.0;
    }
    // Ohne eFuse-Kalibrierung: grobe lineare Umrechnung.
    return static_cast<double>(raw) * 3.3 / 4095.0;
#else
    (void)line;
    return std::nullopt;
#endif
}

void Rj45Port::toneCallback(void* arg) {
    auto* self = static_cast<Rj45Port*>(arg);
    if (self == nullptr || self->tone_pin_ == GPIO_NUM_NC) {
        return;
    }
    self->tone_level_ = !self->tone_level_;
    gpio_set_level(self->tone_pin_, self->tone_level_ ? 1 : 0);
}

esp_err_t Rj45Port::startTone(std::size_t line, std::uint32_t freq_hz) {
    if (line >= pins_.size()) {
        return ESP_ERR_INVALID_ARG;
    }
    if (freq_hz < 50) {
        freq_hz = 50;
    }
    if (freq_hz > 10000) {
        freq_hz = 10000;
    }

    stopTone();
    allInputsPullDown();
    const gpio_num_t pin = pins_[line];
    gpio_set_pull_mode(pin, GPIO_FLOATING);
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    tone_pin_ = pin;
    tone_line_ = line;
    tone_freq_ = freq_hz;
    tone_level_ = false;

    esp_timer_create_args_t args{};
    args.callback = &Rj45Port::toneCallback;
    args.arg = this;
    args.name = "rj45_tone";
    esp_err_t err = esp_timer_create(&args, &tone_timer_);
    if (err != ESP_OK) {
        tone_timer_ = nullptr;
        release(line);
        return err;
    }

    const std::uint64_t half_period_us = 500000ULL / freq_hz;
    err = esp_timer_start_periodic(tone_timer_, half_period_us);
    if (err != ESP_OK) {
        esp_timer_delete(tone_timer_);
        tone_timer_ = nullptr;
        release(line);
        return err;
    }
    return ESP_OK;
}

void Rj45Port::stopTone() {
    if (tone_timer_ != nullptr) {
        esp_timer_stop(tone_timer_);
        esp_timer_delete(tone_timer_);
        tone_timer_ = nullptr;
    }
    if (tone_pin_ != GPIO_NUM_NC) {
        gpio_set_level(tone_pin_, 0);
        gpio_set_direction(tone_pin_, GPIO_MODE_INPUT);
        gpio_set_pull_mode(tone_pin_, GPIO_PULLDOWN_ONLY);
        tone_pin_ = GPIO_NUM_NC;
    }
    tone_line_ = 0;
    tone_freq_ = 0;
    tone_level_ = false;
}

bool Rj45Port::toneActive() const {
    return tone_timer_ != nullptr;
}

int Rj45Port::toneLine() const {
    return toneActive() ? static_cast<int>(tone_line_) : -1;
}

std::uint32_t Rj45Port::toneFrequency() const {
    return toneActive() ? tone_freq_ : 0;
}

} // namespace cable_tester
