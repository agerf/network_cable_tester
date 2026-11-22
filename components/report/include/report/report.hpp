#pragma once

#include <string>

#include "cable_test/model.hpp"

namespace cable_tester::report {

// Kompakter JSON-Report für Web-UI und serielle Konsole.
[[nodiscard]] std::string to_json(const cable_test::TestData& data,
                                  const cable_test::Analysis& analysis);

// Kompakte, menschenlesbare Zusammenfassung (deutsch) für die Konsole.
[[nodiscard]] std::string format_text(const cable_test::TestData& data,
                                      const cable_test::Analysis& analysis);

[[nodiscard]] const char* kind_label_de(cable_test::CableKind kind) noexcept;

} // namespace cable_tester::report
