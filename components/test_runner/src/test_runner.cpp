#include "test_runner/test_runner.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cable_tester {
namespace {

// Stellt bei jedem Ausstieg (auch Fehler) den sicheren Portzustand her.
struct PortCleanup {
    Rj45Port& port;
    MasterLink& link;
    bool slave_floating{false};

    ~PortCleanup() {
        port.allInputsPullDown();
        if (slave_floating) {
            link.request_release(150);
        }
    }
};

} // namespace

TestRunner::TestRunner(Rj45Port& port, MasterLink& link) : port_(port), link_(link) {
    length_scale_.fill(1.0);
}

bool TestRunner::busy() const {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    return !lock.owns_lock();
}

esp_err_t TestRunner::run(TestResult& out) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !link_.paired()) {
        return ESP_ERR_INVALID_STATE;
    }
    return run_locked(out, true);
}

esp_err_t TestRunner::run_locked(TestResult& out, bool measure_extras) {
    // Ein laufender Suchton würde die Messung verfälschen.
    port_.stopTone();

    out = TestResult{};
    PortCleanup cleanup{port_, link_};

    // Ruhezustand: ohne Ansteuerung müssen alle Leitungen LOW sein.
    port_.allInputsPullDown();
    vTaskDelay(pdMS_TO_TICKS(protocol::kSettleMs));
    out.data.idle_local_mask = port_.sample();
    const auto idle_remote = link_.request_idle();
    if (!idle_remote) {
        return ESP_FAIL;
    }
    out.data.idle_remote_mask = *idle_remote;

    // Messrunden: genau eine Ader treiben, beide Seiten lesen.
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        port_.allInputsPullDown();
        vTaskDelay(pdMS_TO_TICKS(protocol::kSettleMs));
        port_.driveHigh(line);

        const auto remote =
            link_.request_measure(static_cast<std::uint8_t>(line));
        if (!remote) {
            return ESP_FAIL;
        }
        const auto local = static_cast<cable_test::LineMask>(
            port_.sample() | cable_test::line_bit(line));
        out.data.rounds[line].local_mask = local;
        out.data.rounds[line].remote_mask = *remote;
        port_.release(line);
    }
    port_.allInputsPullDown();

    out.analysis = cable_test::analyze(out.data);

    // Kurzschlussgüte und Längenmessung brauchen den Slave hochohmig.
    // Längen werden auch bei einzelnen offenen Adern gemessen (Bruchort);
    // komplett offene Kabel werden übersprungen (kein Kabel gesteckt).
    const bool any_shorts = cable_test::any_short(out.analysis);
    const bool all_open = cable_test::open_count(out.analysis) == cable_test::kLineCount;
    const bool measure_lengths = !all_open;
    const bool measure_shorts = any_shorts;

    if (measure_extras && !out.analysis.idle_activity &&
        (measure_lengths || measure_shorts)) {
        if (!link_.request_float()) {
            return ESP_FAIL;
        }
        cleanup.slave_floating = true;

        // Kurzschlussgüte: nur Adern messen, die auf der Master-Seite
        // durchgängig sind; bei einer offenen Ader ist kein Strompfad da.
        if (measure_shorts) {
            for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
                if (cable_test::line_shorted(out.analysis, line) &&
                    !out.analysis.open[line]) {
                    out.data.short_voltage_v[line] = port_.measureShortVoltage(line);
                }
            }
            port_.allInputsPullDown();
        }

        if (measure_lengths) {
            port_.allFloating();
            for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
                if (cable_test::line_shorted(out.analysis, line)) {
                    continue;
                }
                out.data.length_m[line] = port_.measureLengthMeters(line);
            }
            port_.allInputsPullDown();
        }

        if (!link_.request_release()) {
            return ESP_FAIL;
        }
        cleanup.slave_floating = false;

        // Per-Ader-Kalibrierung anwenden, bevor die Analyse die
        // Bruchdistanz daraus ableitet.
        std::array<double, cable_test::kLineCount> scale{};
        {
            std::lock_guard<std::mutex> lock(scale_mutex_);
            scale = length_scale_;
        }
        for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
            if (!out.data.length_m[line]) {
                continue;
            }
            const double factor = scale[line];
            if (std::isfinite(factor) && factor > 0.0) {
                *out.data.length_m[line] *= factor;
            }
        }

        out.analysis = cable_test::analyze(out.data);
    }

    {
        std::lock_guard<std::mutex> result_lock(result_mutex_);
        last_result_ = out;
        ++result_generation_;
    }

    return ESP_OK;
}

esp_err_t TestRunner::run_soak(const SoakConfig& config, TestResult& last,
                               cable_test::SoakSummary& summary) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !link_.paired()) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::uint16_t runs = std::clamp<std::uint16_t>(config.runs, 1, 200);
    const std::uint32_t interval_ms = std::min<std::uint32_t>(config.interval_ms, 5000);

    summary = cable_test::SoakSummary{};
    last = TestResult{};

    esp_err_t result = ESP_OK;
    for (std::uint16_t run = 0; run < runs; ++run) {
        TestResult current;
        // Im Wackeltest zaehlt die digitale Stabilitaet; die teure
        // Laengen-/ADC-Messung laeuft nur im abschliessenden Detailtest.
        result = run_locked(current, false);
        if (result != ESP_OK) {
            break;
        }
        cable_test::soak_accumulate(summary, current.analysis);
        last = current;
        if (run + 1 < runs && interval_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(interval_ms));
        }
    }
    if (result == ESP_OK && summary.runs > 0) {
        TestResult detailed;
        const esp_err_t detail_err = run_locked(detailed, true);
        if (detail_err == ESP_OK) {
            last = detailed;
        } else {
            result = detail_err;
        }
    }
    return result;
}

void TestRunner::set_length_scale(
    const std::array<double, cable_test::kLineCount>& scale) {
    std::lock_guard<std::mutex> lock(scale_mutex_);
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        length_scale_[line] =
            (std::isfinite(scale[line]) && scale[line] > 0.0) ? scale[line] : 1.0;
    }
}

std::array<double, cable_test::kLineCount> TestRunner::length_scale() const {
    std::lock_guard<std::mutex> lock(scale_mutex_);
    return length_scale_;
}

esp_err_t TestRunner::start_tone(std::size_t line, std::uint32_t freq_hz) {
    if (busy()) {
        return ESP_ERR_INVALID_STATE;
    }
    return port_.startTone(line, freq_hz);
}

void TestRunner::stop_tone() {
    port_.stopTone();
}

bool TestRunner::tone_active() const {
    return port_.toneActive();
}

int TestRunner::tone_line() const {
    return port_.toneLine();
}

std::uint32_t TestRunner::tone_frequency() const {
    return port_.toneFrequency();
}

bool TestRunner::last_result(TestResult& out, std::uint32_t& generation) const {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (result_generation_ == 0) {
        return false;
    }
    out = last_result_;
    generation = result_generation_;
    return true;
}

} // namespace cable_tester
