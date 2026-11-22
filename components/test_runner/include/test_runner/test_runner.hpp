#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "cable_link/master_link.hpp"
#include "cable_test/model.hpp"
#include "cable_test/soak.hpp"
#include "esp_err.h"
#include "rj45_port/rj45_port.hpp"

namespace cable_tester {

struct TestResult {
    cable_test::TestData data{};
    cable_test::Analysis analysis{};
};

// Parameter für den Wackeltest: mehrere vollständige Testläufe mit Pause.
struct SoakConfig {
    std::uint16_t runs{30};
    std::uint32_t interval_ms{300};
};

// Führt einen kompletten Kabeltest auf der Master-Seite aus:
//   1. Ruhezustand prüfen
//   2. 8 Messrunden (Master treibt, Slave liest)
//   3. optional Kurzschlussgüte per ADC und Längen-/Fehlerortmessung
//   4. Analyse (Verdrahtung, Kurzschlüsse, Unterbrechungen, Kabeltyp)
class TestRunner {
public:
    TestRunner(Rj45Port& port, MasterLink& link);

    // ESP_ERR_INVALID_STATE: Test läuft bereits oder Slave ist nicht verbunden.
    // ESP_FAIL: Link-Fehler während der Messung.
    esp_err_t run(TestResult& out);

    // Wiederholt den digitalen Test `runs`-mal im Abstand `interval_ms` und
    // sammelt pro Ader, ob das Ergebnis stabil bleibt. Die Längen-/ADC-Messung
    // läuft nur im abschließenden Detailtest; `last` enthält dieses Ergebnis.
    esp_err_t run_soak(const SoakConfig& config, TestResult& last,
                       cable_test::SoakSummary& summary);

    [[nodiscard]] bool busy() const;

    // Per-Ader-Kalibrierfaktor für die Längenmessung (1.0 = unkalibriert).
    void set_length_scale(const std::array<double, cable_test::kLineCount>& scale);
    [[nodiscard]] std::array<double, cable_test::kLineCount> length_scale() const;

    // Tongenerator für die Adernsuche. start_tone lehnt ab, wenn ein Test
    // läuft; ein neuer Test stoppt einen laufenden Ton automatisch.
    esp_err_t start_tone(std::size_t line, std::uint32_t freq_hz);
    void stop_tone();
    [[nodiscard]] bool tone_active() const;
    [[nodiscard]] int tone_line() const;
    [[nodiscard]] std::uint32_t tone_frequency() const;

    // Kopiert das letzte erfolgreich abgeschlossene Ergebnis. Der Zähler
    // `generation` steigt bei jedem neuen Ergebnis, damit ein Anzeige-Task
    // Änderungen erkennen kann, ohne die Daten selbst zu vergleichen.
    [[nodiscard]] bool last_result(TestResult& out, std::uint32_t& generation) const;

private:
    esp_err_t run_locked(TestResult& out, bool measure_extras);

    Rj45Port& port_;
    MasterLink& link_;
    mutable std::mutex mutex_;

    mutable std::mutex scale_mutex_;
    std::array<double, cable_test::kLineCount> length_scale_{};

    mutable std::mutex result_mutex_;
    TestResult last_result_{};
    std::uint32_t result_generation_{0};
};

} // namespace cable_tester
