#pragma once

#include <array>
#include <cstdint>

#include "cable_test/model.hpp"

namespace cable_test {

// Statistik einer Ader über mehrere Testlaeufe (Wackeltest).
struct SoakLineStats {
    std::uint16_t runs{0};
    std::uint16_t connected{0};
    std::uint16_t open{0};
    std::uint16_t shorted{0};
    // Bit n gesetzt: In mindestens einem Lauf war diese Ader mit B<n> verbunden.
    LineMask targets{0};
};

struct SoakSummary {
    std::uint16_t runs{0};
    std::uint16_t idle_activity_runs{0};
    CableKind first_kind{CableKind::Unknown};
    CableKind last_kind{CableKind::Unknown};
    std::uint16_t kind_changes{0};
    std::array<SoakLineStats, kLineCount> lines{};
};

// Ein einzelnes Ergebnis in die laufende Statistik einarbeiten.
void soak_accumulate(SoakSummary& summary, const Analysis& analysis) noexcept;

// Stabil heisst: in allen Laeufen verbunden, nie offen, nie kurzgeschlossen
// und immer dieselbe Zielader.
[[nodiscard]] bool soak_line_stable(const SoakLineStats& stats) noexcept;
[[nodiscard]] bool soak_all_stable(const SoakSummary& summary) noexcept;

} // namespace cable_test
