#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cable_test {

inline constexpr std::size_t kLineCount = 8;
using LineMask = std::uint8_t;
using LineIndex = std::uint8_t;

[[nodiscard]] constexpr LineMask line_bit(std::size_t line) noexcept {
    return static_cast<LineMask>(1u << line);
}

[[nodiscard]] constexpr bool has_line(LineMask mask, std::size_t line) noexcept {
    return (mask & line_bit(line)) != 0;
}

[[nodiscard]] constexpr std::size_t popcount8(LineMask mask) noexcept {
    std::size_t count = 0;
    while (mask != 0) {
        count += static_cast<std::size_t>(mask & 1u);
        mask = static_cast<LineMask>(mask >> 1u);
    }
    return count;
}

// Ergebnis einer Messrunde: die Leitung `driven` (Index 0..7) wird auf der
// Master-Seite auf HIGH getrieben. `local_mask` sind die Pegel, die der
// Master selbst liest, `remote_mask` die Pegel, die der Slave meldet.
struct RoundSample {
    LineMask local_mask{0};
    LineMask remote_mask{0};
};

struct TestData {
    std::array<RoundSample, kLineCount> rounds{};
    LineMask idle_local_mask{0};
    LineMask idle_remote_mask{0};
    // Optionale, experimentelle Längenschätzung pro Ader (Meter).
    std::array<std::optional<double>, kLineCount> length_m{};
};

enum class CableKind : std::uint8_t {
    Unknown = 0,
    Straight,
    Crossover,
    Custom,
    Faulty,
};

struct Analysis {
    CableKind kind{CableKind::Unknown};
    std::array<std::optional<LineIndex>, kLineCount> mapping{};
    std::array<std::array<bool, kLineCount>, kLineCount> short_between{};
    std::array<bool, kLineCount> open{};
    bool idle_activity{false};
};

[[nodiscard]] Analysis analyze(const TestData& data) noexcept;
[[nodiscard]] bool any_short(const Analysis& analysis) noexcept;
[[nodiscard]] bool any_open(const Analysis& analysis) noexcept;

// Stabile, maschinenlesbare Bezeichnung (für JSON).
[[nodiscard]] const char* kind_name(CableKind kind) noexcept;

} // namespace cable_test
