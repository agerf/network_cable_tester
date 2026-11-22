// Host-Tests für die reine Analyse-Logik (ohne ESP-IDF).
// Build & Run:
//   cmake -S host_tests -B host_tests/build && cmake --build host_tests/build
//   ctest --test-dir host_tests/build --output-on-failure
#include "cable_test/model.hpp"
#include "cable_test/soak.hpp"
#include "report/report.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace {

using cable_test::Analysis;
using cable_test::CableKind;
using cable_test::LineIndex;
using cable_test::ShortQuality;
using cable_test::SoakSummary;
using cable_test::line_bit;
using cable_test::TestData;

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

TestData make_mapped(const std::array<LineIndex, cable_test::kLineCount>& mapping) {
    TestData data{};
    for (std::size_t line = 0; line < mapping.size(); ++line) {
        data.rounds[line].local_mask = line_bit(line);
        data.rounds[line].remote_mask = line_bit(mapping[line]);
    }
    return data;
}

TestData straight_data() {
    return make_mapped({0, 1, 2, 3, 4, 5, 6, 7});
}

void test_straight() {
    const Analysis result = cable_test::analyze(straight_data());
    CHECK(result.kind == CableKind::Straight);
    CHECK(!cable_test::any_short(result));
    CHECK(!cable_test::any_open(result));
    CHECK(!result.idle_activity);
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        CHECK(result.mapping[line] == std::optional<LineIndex>{static_cast<LineIndex>(line)});
    }
}

void test_crossover_gigabit() {
    const TestData data = make_mapped({2, 5, 0, 6, 7, 1, 3, 4});
    const Analysis result = cable_test::analyze(data);
    CHECK(result.kind == CableKind::Crossover);
    CHECK(!cable_test::any_short(result));
    CHECK(!cable_test::any_open(result));
    CHECK(result.mapping[0] == std::optional<LineIndex>{2});
    CHECK(result.mapping[1] == std::optional<LineIndex>{5});
}

void test_crossover_100() {
    const TestData data = make_mapped({2, 5, 0, 3, 4, 1, 6, 7});
    const Analysis result = cable_test::analyze(data);
    CHECK(result.kind == CableKind::Crossover);
    CHECK(!cable_test::any_short(result));
}

void test_simple_swap_is_custom_not_short() {
    // Reiner Tausch 1<->2 darf NICHT als Kurzschluss gelten.
    const TestData data = make_mapped({1, 0, 2, 3, 4, 5, 6, 7});
    const Analysis result = cable_test::analyze(data);
    CHECK(result.kind == CableKind::Custom);
    CHECK(!cable_test::any_short(result));
    CHECK(!cable_test::any_open(result));
}

void test_open_line() {
    TestData data = straight_data();
    data.rounds[2].remote_mask = 0;
    const Analysis result = cable_test::analyze(data);
    CHECK(result.open[2]);
    CHECK(!result.open[0]);
    CHECK(result.kind == CableKind::Faulty);
    CHECK(!result.mapping[2].has_value());
}

void test_short_at_remote_end() {
    // Straight-Kabel, Adern 1 und 2 am Slave-Stecker kurzgeschlossen.
    // Beide Adern sind durchgehend -> auch die Master-Seite sieht den Kurzschluss.
    TestData data = straight_data();
    for (std::size_t driven = 0; driven <= 1; ++driven) {
        data.rounds[driven].local_mask = static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
        data.rounds[driven].remote_mask = static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
    }
    const Analysis result = cable_test::analyze(data);
    CHECK(result.short_between[0][1]);
    CHECK(result.short_between[1][0]);
    CHECK(result.kind == CableKind::Faulty);
    CHECK(!result.mapping[0].has_value());
    CHECK(!result.mapping[1].has_value());
}

void test_short_visible_only_at_remote_end() {
    // Ader 2 ist direkt am Master unterbrochen, am Slave aber mit Ader 1
    // kurzgeschlossen: Der Slave sieht zwei High-Pegel, der Master nur einen.
    TestData data = straight_data();
    data.rounds[0].local_mask = line_bit(0);
    data.rounds[0].remote_mask = static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
    data.rounds[1].local_mask = line_bit(1);
    data.rounds[1].remote_mask = 0; // offen
    const Analysis result = cable_test::analyze(data);
    CHECK(result.short_between[0][1]);
    CHECK(result.open[1]);
    CHECK(result.kind == CableKind::Faulty);
}

void test_duplicate_mapping_is_faulty() {
    const TestData data = make_mapped({0, 0, 2, 3, 4, 5, 6, 7});
    const Analysis result = cable_test::analyze(data);
    CHECK(result.kind == CableKind::Faulty);
    CHECK(!cable_test::any_short(result));
}

void test_idle_activity_is_faulty() {
    TestData data = straight_data();
    data.idle_local_mask = line_bit(3);
    const Analysis result = cable_test::analyze(data);
    CHECK(result.idle_activity);
    CHECK(result.kind == CableKind::Faulty);
}

void test_all_lines_open() {
    const TestData data{};
    const Analysis result = cable_test::analyze(data);
    CHECK(cable_test::any_open(result));
    CHECK(result.kind == CableKind::Faulty);
    for (const bool open : result.open) {
        CHECK(open);
    }
}

void test_open_distance_from_length() {
    TestData data = straight_data();
    data.rounds[2].remote_mask = 0;
    data.length_m[2] = 7.4;
    data.length_m[0] = 20.0;
    const Analysis result = cable_test::analyze(data);
    CHECK(result.open[2]);
    CHECK(result.open_distance_m[2].has_value());
    if (result.open_distance_m[2]) {
        CHECK(std::fabs(*result.open_distance_m[2] - 7.4) < 1e-9);
    }
    // Nur offene Adern bekommen eine Bruchdistanz.
    CHECK(!result.open_distance_m[0].has_value());
    CHECK(!result.open_distance_m[1].has_value());
}

void test_short_quality_classification() {
    TestData data = straight_data();
    for (std::size_t driven = 0; driven <= 1; ++driven) {
        data.rounds[driven].local_mask =
            static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
        data.rounds[driven].remote_mask =
            static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
    }
    data.short_voltage_v[0] = 1.6;
    data.short_voltage_v[1] = 2.4;
    const Analysis result = cable_test::analyze(data);
    CHECK(result.short_quality[0] == ShortQuality::Hard);
    CHECK(result.short_quality[1] == ShortQuality::Resistive);
    CHECK(result.short_quality[2] == ShortQuality::Unknown);

    // Ohne ADC-Messwert bleibt die Qualitaet unbekannt.
    TestData unknown = straight_data();
    for (std::size_t driven = 0; driven <= 1; ++driven) {
        unknown.rounds[driven].local_mask =
            static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
        unknown.rounds[driven].remote_mask =
            static_cast<cable_test::LineMask>(line_bit(0) | line_bit(1));
    }
    const Analysis unknown_result = cable_test::analyze(unknown);
    CHECK(unknown_result.short_quality[0] == ShortQuality::Unknown);
}

void test_soak_stable() {
    SoakSummary summary;
    for (int run = 0; run < 3; ++run) {
        cable_test::soak_accumulate(summary, cable_test::analyze(straight_data()));
    }
    CHECK(summary.runs == 3);
    CHECK(summary.kind_changes == 0);
    CHECK(cable_test::soak_all_stable(summary));
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        CHECK(summary.lines[line].connected == 3);
        CHECK(summary.lines[line].targets == line_bit(line));
        CHECK(cable_test::soak_line_stable(summary.lines[line]));
    }
}

void test_soak_open_variation() {
    SoakSummary summary;
    cable_test::soak_accumulate(summary, cable_test::analyze(straight_data()));
    cable_test::soak_accumulate(summary, cable_test::analyze(straight_data()));
    TestData bad = straight_data();
    bad.rounds[2].remote_mask = 0;
    cable_test::soak_accumulate(summary, cable_test::analyze(bad));

    CHECK(summary.lines[2].open == 1);
    CHECK(summary.lines[2].connected == 2);
    CHECK(!cable_test::soak_line_stable(summary.lines[2]));
    CHECK(cable_test::soak_line_stable(summary.lines[0]));
    CHECK(!cable_test::soak_all_stable(summary));
}

void test_soak_mapping_change() {
    SoakSummary summary;
    cable_test::soak_accumulate(summary, cable_test::analyze(straight_data()));
    cable_test::soak_accumulate(
        summary, cable_test::analyze(make_mapped({1, 0, 2, 3, 4, 5, 6, 7})));
    CHECK(summary.kind_changes == 1);
    CHECK(cable_test::popcount8(summary.lines[0].targets) == 2);
    CHECK(!cable_test::soak_line_stable(summary.lines[0]));
    CHECK(cable_test::soak_line_stable(summary.lines[2]));
}

void test_report_json_and_text() {
    TestData data = straight_data();
    data.length_m[0] = 10.0;
    data.length_m[1] = 10.5;
    data.rounds[2].remote_mask = 0;
    data.length_m[2] = 7.4;
    const Analysis analysis = cable_test::analyze(data);

    SoakSummary summary;
    cable_test::soak_accumulate(summary, analysis);

    const std::string json = cable_tester::report::to_json(data, analysis, &summary);
    CHECK(!json.empty());
    CHECK(json.front() == '{');
    CHECK(json.find("\"openDistanceM\":[null,null,7.40") != std::string::npos);
    CHECK(json.find("\"shortQuality\":[") != std::string::npos);
    CHECK(json.find("\"soak\":{\"runs\":1") != std::string::npos);
    CHECK(json.find("\"allStable\":false") != std::string::npos);

    const std::string text = cable_tester::report::format_text(data, analysis);
    CHECK(text.find("Ader 1: 10.0 m") != std::string::npos);
    CHECK(text.find("Ader 3: offen, Bruch ca. 7.4 m") != std::string::npos);
    CHECK(text.find("Laengen (experimentell, pro Ader)") != std::string::npos);

    const std::string soak_text = cable_tester::report::format_soak_text(summary);
    CHECK(soak_text.find("Wackeltest: 1 Laeufe") != std::string::npos);
    CHECK(soak_text.find("Ader 3: instabil") != std::string::npos);
}

} // namespace

int main() {
    test_straight();
    test_crossover_gigabit();
    test_crossover_100();
    test_simple_swap_is_custom_not_short();
    test_open_line();
    test_short_at_remote_end();
    test_short_visible_only_at_remote_end();
    test_duplicate_mapping_is_faulty();
    test_idle_activity_is_faulty();
    test_all_lines_open();
    test_open_distance_from_length();
    test_short_quality_classification();
    test_soak_stable();
    test_soak_open_variation();
    test_soak_mapping_change();
    test_report_json_and_text();

    if (g_failures != 0) {
        std::fprintf(stderr, "\n%d von %d Checks fehlgeschlagen.\n", g_failures, g_checks);
        return 1;
    }
    std::printf("Alle %d Checks bestanden.\n", g_checks);
    return 0;
}
