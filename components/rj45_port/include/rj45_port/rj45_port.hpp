#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "cable_test/model.hpp"
#include "driver/gpio.h"
#include "esp_err.h"

namespace cable_tester {

struct LengthConfig {
    double pullup_ohm{45000.0};
    double pf_per_meter{50.0};
    double baseline_pf{8.0};
    // t = R*C*ln(1/(1 - Vih/Vdd)); Vih = 0.75*Vdd -> ln(4).
    double threshold_factor{1.386};
    std::uint32_t discharge_ms{5};
    std::int64_t timeout_us{100000};
};

// Ein RJ45-Port als 8 bidirektionale Leitungen.
class Rj45Port {
public:
    explicit Rj45Port(const std::array<gpio_num_t, cable_test::kLineCount>& pins);

    esp_err_t init();

    // Sicherer Zustand: alle Leitungen Eingang mit Pulldown.
    void allInputsPullDown();
    // Hochohmig, z. B. für die Kapazitäts-/Längenmessung.
    void allFloating();

    // Treibt genau eine Leitung HIGH; alle anderen bleiben unangetastet.
    void driveHigh(std::size_t line);
    // Gibt eine Leitung wieder frei (Eingang mit Pulldown).
    void release(std::size_t line);

    [[nodiscard]] cable_test::LineMask sample() const;

    // Experimentelle Längenschätzung über die Ladezeit der Leitungskapazität.
    // Erwartet, dass alle Leitungen hochohmig sind (allFloating()).
    [[nodiscard]] std::optional<double> measureLengthMeters(
        std::size_t line, const LengthConfig& config = {});

    [[nodiscard]] const std::array<gpio_num_t, cable_test::kLineCount>& pins() const {
        return pins_;
    }

private:
    void setInputPullDown(gpio_num_t pin) const;
    void setFloating(gpio_num_t pin) const;

    std::array<gpio_num_t, cable_test::kLineCount> pins_;
};

} // namespace cable_tester
