#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cable_test/model.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#endif

namespace cable_tester {

struct LengthConfig {
    double pullup_ohm{45000.0};
    double pf_per_meter{50.0};
    double baseline_pf{8.0};
    // t = R*C*ln(1/(1 - Vih/Vdd)); Vih = 0.75*Vdd -> ln(4).
    double threshold_factor{1.386};
    std::uint32_t discharge_ms{5};
    std::int64_t timeout_us{100000};
    // Median aus mehreren Messungen; reduziert Ausreisser durch Rauschen.
    std::uint32_t samples{5};
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
    // Erwartet, dass alle Leitungen hochohmig sind (allFloating()). Liefert
    // den Median aus config.samples Einzelmessungen.
    [[nodiscard]] std::optional<double> measureLengthMeters(
        std::size_t line, const LengthConfig& config = {});

    // Experimentelle Kurzschlussgüte: Die Ader wird über den internen
    // Pull-up (~45 kΩ) gehalten, alle anderen Adern liegen auf Pull-down,
    // der Slave muss hochohmig sein. Ein harter Kurzschluss ergibt ca. die
    // halbe Versorgungsspannung, ein hochohmiger Fehler mehr. Die internen
    // Widerstände streuen; nur eine grobe Einordnung. Auf dem klassischen
    // ESP32 nicht verfügbar (nullopt).
    [[nodiscard]] std::optional<double> measureShortVoltage(std::size_t line);

    // Rechteck-Tongenerator (0/3,3 V) auf einer Ader zum Aufspüren mit
    // einem Induktionspiepser. Läuft, bis stopTone() oder der nächste Test
    // ihn beendet. Nur für spannungsfreie Kabel gedacht.
    esp_err_t startTone(std::size_t line, std::uint32_t freq_hz);
    void stopTone();
    [[nodiscard]] bool toneActive() const;
    // 0-basierter Aderindex; -1, wenn kein Ton laeuft.
    [[nodiscard]] int toneLine() const;
    // 0, wenn kein Ton laeuft.
    [[nodiscard]] std::uint32_t toneFrequency() const;

    [[nodiscard]] const std::array<gpio_num_t, cable_test::kLineCount>& pins() const {
        return pins_;
    }

private:
    void setInputPullDown(gpio_num_t pin) const;
    void setFloating(gpio_num_t pin) const;
    [[nodiscard]] std::optional<double> measureLengthSampleMeters(
        std::size_t line, const LengthConfig& config);
    esp_err_t ensureAdc();
    static void toneCallback(void* arg);

    std::array<gpio_num_t, cable_test::kLineCount> pins_;

#if CONFIG_IDF_TARGET_ESP32S3
    adc_oneshot_unit_handle_t adc1_{nullptr};
    adc_oneshot_unit_handle_t adc2_{nullptr};
    std::array<adc_cali_handle_t, cable_test::kLineCount> adc_cali_{};
    std::array<adc_unit_t, cable_test::kLineCount> adc_units_{};
    std::array<adc_channel_t, cable_test::kLineCount> adc_channels_{};
    std::array<bool, cable_test::kLineCount> adc_channel_ok_{};
    bool adc_ready_{false};
    bool adc_failed_{false};
#endif

    esp_timer_handle_t tone_timer_{nullptr};
    gpio_num_t tone_pin_{GPIO_NUM_NC};
    std::size_t tone_line_{0};
    std::uint32_t tone_freq_{0};
    bool tone_level_{false};
};

} // namespace cable_tester
