#include "cable_test/model.hpp"

#include <array>

namespace cable_test {
namespace {

using Mapping = std::array<std::optional<LineIndex>, kLineCount>;

constexpr std::array<LineIndex, kLineCount> kStraight{0, 1, 2, 3, 4, 5, 6, 7};
// 1000BASE-T-Crossover: alle vier Paare getauscht (1<->3, 2<->6, 4<->7, 5<->8).
constexpr std::array<LineIndex, kLineCount> kCrossoverGigabit{2, 5, 0, 6, 7, 1, 3, 4};
// 10/100BASE-T-Crossover: nur die Paare 1-2 und 3-6 getauscht.
constexpr std::array<LineIndex, kLineCount> kCrossover100{2, 5, 0, 3, 4, 1, 6, 7};

[[nodiscard]] bool mapping_equals(const Mapping& mapping,
                                  const std::array<LineIndex, kLineCount>& expected) noexcept {
    for (std::size_t line = 0; line < kLineCount; ++line) {
        if (!mapping[line] || *mapping[line] != expected[line]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool mapping_is_bijective(const Mapping& mapping) noexcept {
    std::array<bool, kLineCount> seen{};
    for (const auto& target : mapping) {
        if (!target || seen[*target]) {
            return false;
        }
        seen[*target] = true;
    }
    return true;
}

void mark_short(Analysis& analysis, std::size_t a, std::size_t b) noexcept {
    analysis.short_between[a][b] = true;
    analysis.short_between[b][a] = true;
}

} // namespace

Analysis analyze(const TestData& data) noexcept {
    Analysis analysis{};
    analysis.idle_activity = data.idle_local_mask != 0 || data.idle_remote_mask != 0;

    for (std::size_t driven = 0; driven < kLineCount; ++driven) {
        const RoundSample& round = data.rounds[driven];
        const LineMask remote = round.remote_mask;

        // Wird auf der Master-Seite eine *andere* Leitung hoch gelesen, obwohl
        // nur `driven` getrieben wird, ist das ein Kurzschluss auf dieser Seite.
        for (std::size_t other = 0; other < kLineCount; ++other) {
            if (other != driven && has_line(round.local_mask, other)) {
                mark_short(analysis, driven, other);
            }
        }

        // Mehrere HIGH-Pegel am Slave bei nur einer getriebenen Leitung
        // bedeuten einen Kurzschluss zwischen genau diesen Adern.
        if (popcount8(remote) > 1) {
            for (std::size_t a = 0; a < kLineCount; ++a) {
                if (!has_line(remote, a)) {
                    continue;
                }
                for (std::size_t b = a + 1; b < kLineCount; ++b) {
                    if (has_line(remote, b)) {
                        mark_short(analysis, a, b);
                    }
                }
            }
        }

        // Eindeutige Verbindung: genau ein HIGH-Pegel am Slave. Bei mehreren
        // Pegeln (Kurzschluss) bleibt die Zuordnung bewusst offen.
        if (popcount8(remote) == 1) {
            for (std::size_t target = 0; target < kLineCount; ++target) {
                if (has_line(remote, target)) {
                    analysis.mapping[driven] = static_cast<LineIndex>(target);
                    break;
                }
            }
        }

        // Keine Verbindung zur Gegenseite: Ader offen.
        if (remote == 0) {
            analysis.open[driven] = true;
        }
    }

    // Eine Kapazitätsmessung an einer offenen Ader schätzt die Entfernung
    // bis zur Unterbrechung (vom Master aus). Bei Kurzschlüssen ist der
    // Messwert nicht interpretierbar und bleibt leer.
    for (std::size_t line = 0; line < kLineCount; ++line) {
        if (analysis.open[line] && data.length_m[line]) {
            analysis.open_distance_m[line] = data.length_m[line];
        }
    }

    // Grobe Klassifikation der Kurzschlussart. Ein harter Kurzschluss bildet
    // mit dem internen Pull-up (~45 kΩ) und dem Pull-down der Gegenader
    // einen Teiler; bei 0 Ω liegt die Spannung bei etwa der halben
    // Versorgungsspannung, bei hochohmigen Fehlern darüber. Die internen
    // Widerstände streuen stark, daher nur eine grobe Einordnung.
    constexpr double kHardShortMaxVoltage = 1.9;
    for (std::size_t line = 0; line < kLineCount; ++line) {
        if (!line_shorted(analysis, line) || !data.short_voltage_v[line]) {
            continue;
        }
        analysis.short_quality[line] = *data.short_voltage_v[line] < kHardShortMaxVoltage
                                          ? ShortQuality::Hard
                                          : ShortQuality::Resistive;
    }

    const bool wiring_clean = !analysis.idle_activity && !any_short(analysis) && !any_open(analysis);
    if (!wiring_clean || !mapping_is_bijective(analysis.mapping)) {
        analysis.kind = CableKind::Faulty;
    } else if (mapping_equals(analysis.mapping, kStraight)) {
        analysis.kind = CableKind::Straight;
    } else if (mapping_equals(analysis.mapping, kCrossoverGigabit) ||
               mapping_equals(analysis.mapping, kCrossover100)) {
        analysis.kind = CableKind::Crossover;
    } else {
        analysis.kind = CableKind::Custom;
    }

    return analysis;
}

bool any_short(const Analysis& analysis) noexcept {
    for (std::size_t a = 0; a < kLineCount; ++a) {
        for (std::size_t b = a + 1; b < kLineCount; ++b) {
            if (analysis.short_between[a][b]) {
                return true;
            }
        }
    }
    return false;
}

bool any_open(const Analysis& analysis) noexcept {
    for (const bool open : analysis.open) {
        if (open) {
            return true;
        }
    }
    return false;
}

bool line_shorted(const Analysis& analysis, std::size_t line) noexcept {
    if (line >= kLineCount) {
        return false;
    }
    for (std::size_t other = 0; other < kLineCount; ++other) {
        if (analysis.short_between[line][other]) {
            return true;
        }
    }
    return false;
}

std::size_t open_count(const Analysis& analysis) noexcept {
    std::size_t count = 0;
    for (const bool open : analysis.open) {
        if (open) {
            ++count;
        }
    }
    return count;
}

const char* short_quality_name(ShortQuality quality) noexcept {
    switch (quality) {
    case ShortQuality::Hard:
        return "hard";
    case ShortQuality::Resistive:
        return "resistive";
    case ShortQuality::Unknown:
    default:
        return "unknown";
    }
}

const char* kind_name(CableKind kind) noexcept {
    switch (kind) {
    case CableKind::Straight:
        return "straight";
    case CableKind::Crossover:
        return "crossover";
    case CableKind::Custom:
        return "custom";
    case CableKind::Faulty:
        return "faulty";
    case CableKind::Unknown:
    default:
        return "unknown";
    }
}

} // namespace cable_test
