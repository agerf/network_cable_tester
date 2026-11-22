#pragma once

#include <string>

#include "cable_test/model.hpp"
#include "cable_test/soak.hpp"

namespace cable_tester::report {

// Kompakter JSON-Report für Web-UI und serielle Konsole. Optional wird die
// Statistik eines Wackeltests mit ausgegeben.
[[nodiscard]] std::string to_json(const cable_test::TestData& data,
                                  const cable_test::Analysis& analysis,
                                  const cable_test::SoakSummary* soak = nullptr);

// Kompakte, menschenlesbare Zusammenfassung (deutsch) für die Konsole.
[[nodiscard]] std::string format_text(const cable_test::TestData& data,
                                      const cable_test::Analysis& analysis);

// Zusammenfassung eines Wackeltests (pro Ader stabil/instabil).
[[nodiscard]] std::string format_soak_text(const cable_test::SoakSummary& summary);

[[nodiscard]] const char* kind_label_de(cable_test::CableKind kind) noexcept;

} // namespace cable_tester::report
