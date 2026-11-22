#pragma once

#include <cstdint>
#include <mutex>

#include "cable_link/master_link.hpp"
#include "cable_test/model.hpp"
#include "esp_err.h"
#include "rj45_port/rj45_port.hpp"

namespace cable_tester {

struct TestResult {
    cable_test::TestData data{};
    cable_test::Analysis analysis{};
};

// Führt einen kompletten Kabeltest auf der Master-Seite aus:
//   1. Ruhezustand prüfen
//   2. 8 Messrunden (Master treibt, Slave liest)
//   3. optional experimentelle Längenmessung
//   4. Analyse (Verdrahtung, Kurzschlüsse, Unterbrechungen, Kabeltyp)
class TestRunner {
public:
    TestRunner(Rj45Port& port, MasterLink& link);

    // ESP_ERR_INVALID_STATE: Test läuft bereits oder Slave ist nicht verbunden.
    // ESP_FAIL: Link-Fehler während der Messung.
    esp_err_t run(TestResult& out);

    [[nodiscard]] bool busy() const;

    // Kopiert das letzte erfolgreich abgeschlossene Ergebnis. Der Zähler
    // `generation` steigt bei jedem neuen Ergebnis, damit ein Anzeige-Task
    // Änderungen erkennen kann, ohne die Daten selbst zu vergleichen.
    [[nodiscard]] bool last_result(TestResult& out, std::uint32_t& generation) const;

private:
    Rj45Port& port_;
    MasterLink& link_;
    mutable std::mutex mutex_;

    mutable std::mutex result_mutex_;
    TestResult last_result_{};
    std::uint32_t result_generation_{0};
};

} // namespace cable_tester
