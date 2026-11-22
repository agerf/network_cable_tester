#include "report/report.hpp"

#include <cstdio>
#include <optional>

#include "cable_test/version.hpp"

namespace cable_tester::report {
namespace {

void append_uint(std::string& out, std::size_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%zu", value);
    out += buffer;
}

void append_int(std::string& out, int value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    out += buffer;
}

void append_meter_value(std::string& out, const std::optional<double>& value) {
    if (!value) {
        out += "null";
        return;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", *value);
    out += buffer;
}

const char* quality_label_de(cable_test::ShortQuality quality) noexcept {
    switch (quality) {
    case cable_test::ShortQuality::Hard:
        return "hart";
    case cable_test::ShortQuality::Resistive:
        return "weich";
    case cable_test::ShortQuality::Unknown:
    default:
        return "unbekannt";
    }
}

cable_test::ShortQuality pair_quality(const cable_test::Analysis& analysis,
                                      std::size_t a, std::size_t b) noexcept {
    const auto qa = analysis.short_quality[a];
    const auto qb = analysis.short_quality[b];
    if (qa == cable_test::ShortQuality::Hard || qb == cable_test::ShortQuality::Hard) {
        return cable_test::ShortQuality::Hard;
    }
    if (qa == cable_test::ShortQuality::Resistive ||
        qb == cable_test::ShortQuality::Resistive) {
        return cable_test::ShortQuality::Resistive;
    }
    return cable_test::ShortQuality::Unknown;
}

void append_break_distances(std::string& text, const cable_test::Analysis& analysis) {
    bool any = false;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!analysis.open[line] || !analysis.open_distance_m[line]) {
            continue;
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s%zu: ca. %.1f m", any ? ", " : "",
                      line + 1, *analysis.open_distance_m[line]);
        text += buffer;
        any = true;
    }
    if (any) {
        text += " (vom Master, +-20-30 %)";
    } else {
        text += "keine Messung";
    }
}

void append_line_lengths(std::string& text, const cable_test::TestData& data,
                         const cable_test::Analysis& analysis) {
    bool any_value = false;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (analysis.open[line] || cable_test::line_shorted(analysis, line) ||
            data.length_m[line]) {
            any_value = true;
            break;
        }
    }
    if (!any_value) {
        text += "keine Messwerte";
        return;
    }

    bool first = true;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        char buffer[80];
        if (cable_test::line_shorted(analysis, line)) {
            std::snprintf(buffer, sizeof(buffer), "Ader %zu: Kurzschluss", line + 1);
        } else if (analysis.open[line]) {
            if (analysis.open_distance_m[line]) {
                std::snprintf(buffer, sizeof(buffer), "Ader %zu: offen, Bruch ca. %.1f m",
                              line + 1, *analysis.open_distance_m[line]);
            } else {
                std::snprintf(buffer, sizeof(buffer), "Ader %zu: offen", line + 1);
            }
        } else if (data.length_m[line]) {
            std::snprintf(buffer, sizeof(buffer), "Ader %zu: %.1f m", line + 1,
                          *data.length_m[line]);
        } else {
            std::snprintf(buffer, sizeof(buffer), "Ader %zu: -", line + 1);
        }
        if (!first) {
            text += ", ";
        }
        text += buffer;
        first = false;
    }
}

void append_soak_json(std::string& out, const cable_test::SoakSummary& soak) {
    out += ",\"soak\":{\"runs\":";
    append_uint(out, soak.runs);
    out += ",\"kindChanges\":";
    append_uint(out, soak.kind_changes);
    out += ",\"idleActivityRuns\":";
    append_uint(out, soak.idle_activity_runs);
    out += ",\"allStable\":";
    out += cable_test::soak_all_stable(soak) ? "true" : "false";
    out += ",\"lines\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        const cable_test::SoakLineStats& stats = soak.lines[line];
        out += "{\"connected\":";
        append_uint(out, stats.connected);
        out += ",\"open\":";
        append_uint(out, stats.open);
        out += ",\"shorted\":";
        append_uint(out, stats.shorted);
        out += ",\"targets\":";
        append_uint(out, stats.targets);
        out += ",\"stable\":";
        out += cable_test::soak_line_stable(stats) ? "true" : "false";
        out += '}';
    }
    out += "]}";
}

} // namespace

const char* kind_label_de(cable_test::CableKind kind) noexcept {
    switch (kind) {
    case cable_test::CableKind::Straight:
        return "Patchkabel (1:1)";
    case cable_test::CableKind::Crossover:
        return "Crossover-Kabel";
    case cable_test::CableKind::Custom:
        return "Benutzerdefinierte Belegung";
    case cable_test::CableKind::Faulty:
        return "Fehlerhaft";
    case cable_test::CableKind::Unknown:
    default:
        return "Unbekannt";
    }
}

std::string to_json(const cable_test::TestData& data, const cable_test::Analysis& analysis,
                    const cable_test::SoakSummary* soak) {
    std::string out;
    out.reserve(1536);

    out += "{\"firmware\":\"";
    out += cable_test::kFirmwareVersion;
    out += "\",\"kind\":\"";
    out += cable_test::kind_name(analysis.kind);
    out += "\",\"kindLabel\":\"";
    out += kind_label_de(analysis.kind);
    out += "\",\"idleActivity\":";
    out += analysis.idle_activity ? "true" : "false";
    out += ",\"idleLocalMask\":";
    append_uint(out, data.idle_local_mask);
    out += ",\"idleRemoteMask\":";
    append_uint(out, data.idle_remote_mask);

    out += ",\"mapping\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        append_int(out, analysis.mapping[line] ? static_cast<int>(*analysis.mapping[line]) : -1);
    }
    out += ']';

    out += ",\"rounds\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        out += "{\"driven\":";
        append_uint(out, line);
        out += ",\"localMask\":";
        append_uint(out, data.rounds[line].local_mask);
        out += ",\"remoteMask\":";
        append_uint(out, data.rounds[line].remote_mask);
        out += '}';
    }
    out += ']';

    out += ",\"shorts\":[";
    bool first_short = true;
    for (std::size_t a = 0; a < cable_test::kLineCount; ++a) {
        for (std::size_t b = a + 1; b < cable_test::kLineCount; ++b) {
            if (!analysis.short_between[a][b]) {
                continue;
            }
            if (!first_short) {
                out += ',';
            }
            first_short = false;
            out += '[';
            append_uint(out, a);
            out += ',';
            append_uint(out, b);
            out += ']';
        }
    }
    out += ']';

    out += ",\"opens\":[";
    bool first_open = true;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!analysis.open[line]) {
            continue;
        }
        if (!first_open) {
            out += ',';
        }
        first_open = false;
        append_uint(out, line);
    }
    out += ']';

    out += ",\"lengthM\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        append_meter_value(out, data.length_m[line]);
    }
    out += ']';

    out += ",\"openDistanceM\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        append_meter_value(out, analysis.open_distance_m[line]);
    }
    out += ']';

    out += ",\"shortQuality\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        out += '"';
        out += cable_test::short_quality_name(analysis.short_quality[line]);
        out += '"';
    }
    out += ']';

    out += ",\"shortVoltageV\":[";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            out += ',';
        }
        append_meter_value(out, data.short_voltage_v[line]);
    }
    out += ']';

    if (soak != nullptr) {
        append_soak_json(out, *soak);
    }

    out += "}\n";
    return out;
}

std::string format_text(const cable_test::TestData& data, const cable_test::Analysis& analysis) {
    std::string text;
    text += "Kabeltyp: ";
    text += kind_label_de(analysis.kind);
    text += '\n';

    text += "Verdrahtung: ";
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        char buffer[32];
        if (analysis.mapping[line]) {
            std::snprintf(buffer, sizeof(buffer), "%zu->%zu", line + 1,
                          static_cast<std::size_t>(*analysis.mapping[line]) + 1);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%zu->?", line + 1);
        }
        if (line != 0) {
            text += ' ';
        }
        text += buffer;
    }
    text += '\n';

    text += "Kurzschluesse: ";
    bool has_short = false;
    for (std::size_t a = 0; a < cable_test::kLineCount; ++a) {
        for (std::size_t b = a + 1; b < cable_test::kLineCount; ++b) {
            if (!analysis.short_between[a][b]) {
                continue;
            }
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s%zu-%zu (%s)", has_short ? ", " : "",
                          a + 1, b + 1, quality_label_de(pair_quality(analysis, a, b)));
            text += buffer;
            has_short = true;
        }
    }
    text += has_short ? "\n" : "keine\n";

    text += "Unterbrechungen: ";
    bool has_open = false;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!analysis.open[line]) {
            continue;
        }
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%s%zu", has_open ? ", " : "", line + 1);
        text += buffer;
        has_open = true;
    }
    text += has_open ? "\n" : "keine\n";
    if (has_open) {
        text += "Bruchdistanz: ";
        append_break_distances(text, analysis);
        text += '\n';
    }
    if (cable_test::open_count(analysis) == cable_test::kLineCount) {
        text += "Hinweis: Alle Adern offen - kein Kabel gesteckt oder keine gemeinsame Masse?\n";
    }

    if (analysis.idle_activity) {
        text += "Warnung: Leitungen liegen ohne Ansteuerung auf HIGH "
                "(Fremdspannung/Defekt?).\n";
    }

    text += "Laengen (experimentell, pro Ader): ";
    append_line_lengths(text, data, analysis);
    text += '\n';
    return text;
}

std::string format_soak_text(const cable_test::SoakSummary& summary) {
    std::string text;
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "Wackeltest: %u Laeufe, %u Wechsel der Kabelart",
                  static_cast<unsigned>(summary.runs),
                  static_cast<unsigned>(summary.kind_changes));
    text += buffer;
    if (summary.idle_activity_runs > 0) {
        std::snprintf(buffer, sizeof(buffer), ", %u Laeufe mit Fremdspannung",
                      static_cast<unsigned>(summary.idle_activity_runs));
        text += buffer;
    }
    text += '\n';

    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        const cable_test::SoakLineStats& stats = summary.lines[line];
        std::snprintf(buffer, sizeof(buffer), "Ader %zu: %s (%u/%u verbunden",
                      line + 1, cable_test::soak_line_stable(stats) ? "stabil" : "instabil",
                      static_cast<unsigned>(stats.connected),
                      static_cast<unsigned>(stats.runs));
        text += buffer;
        if (stats.open > 0) {
            std::snprintf(buffer, sizeof(buffer), ", %u offen",
                          static_cast<unsigned>(stats.open));
            text += buffer;
        }
        if (stats.shorted > 0) {
            std::snprintf(buffer, sizeof(buffer), ", %u kurzgeschlossen",
                          static_cast<unsigned>(stats.shorted));
            text += buffer;
        }
        const std::size_t targets = cable_test::popcount8(stats.targets);
        if (targets > 1) {
            text += ", mehrere Zieladern";
        } else if (targets == 1) {
            for (std::size_t target = 0; target < cable_test::kLineCount; ++target) {
                if (cable_test::has_line(stats.targets, target)) {
                    std::snprintf(buffer, sizeof(buffer), ", Ziel B%zu", target + 1);
                    text += buffer;
                    break;
                }
            }
        }
        text += ")\n";
    }

    text += cable_test::soak_all_stable(summary) ? "Gesamt: stabil\n" : "Gesamt: instabil\n";
    return text;
}

} // namespace cable_tester::report
