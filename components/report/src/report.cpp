#include "report/report.hpp"

#include <array>
#include <cstdio>
#include <optional>

#include "cable_test/version.hpp"

namespace cable_tester::report {
namespace {

constexpr std::array<std::array<std::size_t, 2>, 4> kPairs = {{{0, 1}, {2, 5}, {3, 4}, {6, 7}}};

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

void append_pair_lengths(std::string& text, const cable_test::TestData& data) {
    bool any = false;
    for (const auto& pair : kPairs) {
        const auto a = data.length_m[pair[0]];
        const auto b = data.length_m[pair[1]];
        if (!a && !b) {
            continue;
        }
        const double meters = a && b ? (*a + *b) / 2.0 : (a ? *a : *b);
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "Paar %zu-%zu: %.1f m", pair[0] + 1, pair[1] + 1,
                      meters);
        if (any) {
            text += ", ";
        }
        text += buffer;
        any = true;
    }
    if (!any) {
        text += "keine Messwerte";
    }
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

std::string to_json(const cable_test::TestData& data, const cable_test::Analysis& analysis) {
    std::string out;
    out.reserve(1024);

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
    out += "]}\n";
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
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%s%zu-%zu", has_short ? ", " : "", a + 1,
                          b + 1);
            text += buffer;
            has_short = true;
        }
    }
    text += has_short ? "\n" : "keine\n";

    text += "Unterbrechungen: ";
    bool has_open = false;
    std::size_t open_count = 0;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!analysis.open[line]) {
            continue;
        }
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%s%zu", has_open ? ", " : "", line + 1);
        text += buffer;
        has_open = true;
        ++open_count;
    }
    text += has_open ? "\n" : "keine\n";
    if (open_count == cable_test::kLineCount) {
        text += "Hinweis: Alle Adern offen - kein Kabel gesteckt oder keine gemeinsame Masse?\n";
    }

    if (analysis.idle_activity) {
        text += "Warnung: Leitungen liegen ohne Ansteuerung auf HIGH "
                "(Fremdspannung/Defekt?).\n";
    }

    text += "Laengen (experimentell): ";
    append_pair_lengths(text, data);
    text += '\n';
    return text;
}

} // namespace cable_tester::report
