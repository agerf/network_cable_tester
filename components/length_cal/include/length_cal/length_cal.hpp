#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "cable_test/model.hpp"
#include "esp_err.h"
#include "test_runner/test_runner.hpp"

namespace cable_tester::length_cal {

enum class CalibrationStatus : std::uint8_t {
    Ok = 0,
    InvalidReference,
    NotReady,
    MeasurementFailed,
    CableNotClean,
    NoMeasurements,
    StoreFailed,
};

struct CalibrationOutcome {
    CalibrationStatus status{CalibrationStatus::Ok};
    std::array<double, cable_test::kLineCount> scale{};
    std::array<std::optional<double>, cable_test::kLineCount> measured_m{};
    std::size_t measured_lines{0};
    std::size_t changed_lines{0};
};

// Lädt die per-Ader-Faktoren aus dem NVS (Namespace `len_cal`). Ohne
// gespeicherte Werte gilt 1.0.
esp_err_t init();

// Aktuelle Faktoren (immer 8 gültige Werte, Default 1.0). Rückgabe als Kopie,
// damit Web/Display/Konsole nicht mit einem gleichzeitigen Speichern kollidieren.
std::array<double, cable_test::kLineCount> scale();

// Validiert, speichert im NVS und aktualisiert die aktiven Faktoren.
esp_err_t store(const std::array<double, cable_test::kLineCount>& scale);

// Löscht gespeicherte Faktoren; danach gilt wieder 1.0.
esp_err_t reset();

// Führt einen vollständigen Test aus, prüft das Kabel, berechnet aus der
// Referenzlänge neue Faktoren, speichert sie und wendet sie auf den Runner
// an. Der Aufrufer muss den Runner nicht selbst sperren.
CalibrationOutcome calibrate(TestRunner& runner, double reference_m);

// Deutsche, ASCII-taugliche Statusmeldung für Konsole/Web/Display.
const char* status_text_de(CalibrationStatus status) noexcept;

} // namespace cable_tester::length_cal
