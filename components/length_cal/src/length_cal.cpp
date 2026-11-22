#include "length_cal/length_cal.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>

#include "esp_log.h"
#include "nvs.h"

namespace cable_tester::length_cal {
namespace {

constexpr char kTag[] = "len_cal";
constexpr char kNamespace[] = "len_cal";
constexpr char kKeyScale[] = "scale";
constexpr double kMinScale = 0.2;
constexpr double kMaxScale = 5.0;

std::array<double, cable_test::kLineCount> g_scale{};
bool g_initialized = false;
std::mutex g_mutex;

bool valid_scale(double value) {
    return std::isfinite(value) && value >= kMinScale && value <= kMaxScale;
}

void use_defaults() {
    g_scale.fill(1.0);
}

} // namespace

esp_err_t init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    use_defaults();
    g_initialized = true;

    nvs_handle_t handle{};
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "Keine Laengenkalibrierung gespeichert, Skala 1.0");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "nvs_open: %s (Skala 1.0)", esp_err_to_name(err));
        return err;
    }

    std::array<float, cable_test::kLineCount> stored{};
    std::size_t length = sizeof(stored);
    err = nvs_get_blob(handle, kKeyScale, stored.data(), &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "Keine Laengenkalibrierung gespeichert, Skala 1.0");
        return ESP_OK;
    }
    if (err != ESP_OK || length != sizeof(stored)) {
        ESP_LOGW(kTag, "Kalibrierung ungueltig (%s), Skala 1.0", esp_err_to_name(err));
        return ESP_OK;
    }

    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        const double value = static_cast<double>(stored[line]);
        if (valid_scale(value)) {
            g_scale[line] = value;
        } else {
            ESP_LOGW(kTag, "Ader %zu: ungueltige Skala %.3f, verwende 1.0", line + 1, value);
        }
    }
    ESP_LOGI(kTag, "Laengenkalibrierung aus NVS geladen");
    return ESP_OK;
}

std::array<double, cable_test::kLineCount> scale() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        use_defaults();
    }
    return g_scale;
}

esp_err_t store(const std::array<double, cable_test::kLineCount>& scale) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::array<float, cable_test::kLineCount> stored{};
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!valid_scale(scale[line])) {
            return ESP_ERR_INVALID_ARG;
        }
        stored[line] = static_cast<float>(scale[line]);
    }

    nvs_handle_t handle{};
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(handle, kKeyScale, stored.data(), sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_set_blob: %s", esp_err_to_name(err));
        return err;
    }

    g_scale = scale;
    g_initialized = true;
    ESP_LOGI(kTag, "Laengenkalibrierung gespeichert");
    return ESP_OK;
}

esp_err_t reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    nvs_handle_t handle{};
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_erase_key(handle, kKeyScale);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_erase_key: %s", esp_err_to_name(err));
        return err;
    }

    use_defaults();
    g_initialized = true;
    ESP_LOGI(kTag, "Laengenkalibrierung zurueckgesetzt");
    return ESP_OK;
}

CalibrationOutcome calibrate(TestRunner& runner, double reference_m) {
    CalibrationOutcome outcome;
    outcome.scale = scale();

    if (!std::isfinite(reference_m) || reference_m < 0.1 || reference_m > 300.0) {
        outcome.status = CalibrationStatus::InvalidReference;
        return outcome;
    }

    TestResult result;
    const esp_err_t run_err = runner.run(result);
    if (run_err == ESP_ERR_INVALID_STATE) {
        outcome.status = CalibrationStatus::NotReady;
        return outcome;
    }
    if (run_err != ESP_OK) {
        outcome.status = CalibrationStatus::MeasurementFailed;
        return outcome;
    }
    if (result.analysis.idle_activity || cable_test::any_short(result.analysis) ||
        cable_test::any_open(result.analysis)) {
        outcome.status = CalibrationStatus::CableNotClean;
        return outcome;
    }

    auto candidate = scale();
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!result.data.length_m[line]) {
            continue;
        }
        ++outcome.measured_lines;
        const double measured = *result.data.length_m[line];
        outcome.measured_m[line] = measured;
        if (measured < 0.05) {
            continue;
        }
        const double value = candidate[line] * reference_m / measured;
        if (valid_scale(value)) {
            candidate[line] = value;
            ++outcome.changed_lines;
        }
    }

    if (outcome.changed_lines == 0) {
        outcome.status = CalibrationStatus::NoMeasurements;
        return outcome;
    }

    const esp_err_t store_err = store(candidate);
    if (store_err != ESP_OK) {
        outcome.status = CalibrationStatus::StoreFailed;
        return outcome;
    }
    runner.set_length_scale(candidate);

    outcome.status = CalibrationStatus::Ok;
    outcome.scale = candidate;
    return outcome;
}

const char* status_text_de(CalibrationStatus status) noexcept {
    switch (status) {
    case CalibrationStatus::Ok:
        return "Kalibrierung gespeichert.";
    case CalibrationStatus::InvalidReference:
        return "Referenzlaenge 0,1-300 m.";
    case CalibrationStatus::NotReady:
        return "Slave getrennt oder Test laeuft.";
    case CalibrationStatus::MeasurementFailed:
        return "Messung fehlgeschlagen.";
    case CalibrationStatus::CableNotClean:
        return "Kabel nicht sauber/komplett.";
    case CalibrationStatus::NoMeasurements:
        return "Keine Laengenmesswerte.";
    case CalibrationStatus::StoreFailed:
        return "NVS-Speichern fehlgeschlagen.";
    default:
        return "Unbekannter Fehler.";
    }
}

} // namespace cable_tester::length_cal
