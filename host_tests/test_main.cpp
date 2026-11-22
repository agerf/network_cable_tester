// Host-Tests für die reine Analyse-Logik (ohne ESP-IDF).
// Build & Run:
//   cmake -S host_tests -B host_tests/build && cmake --build host_tests/build
//   ctest --test-dir host_tests/build --output-on-failure
#include "cable_test/model.hpp"

#include <array>
#include <cstdio>
#include <optional>

namespace {

using cable_test::Analysis;
using cable_test::CableKind;
using cable_test::LineIndex;
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

    if (g_failures != 0) {
        std::fprintf(stderr, "\n%d von %d Checks fehlgeschlagen.\n", g_failures, g_checks);
        return 1;
    }
    std::printf("Alle %d Checks bestanden.\n", g_checks);
    return 0;
}
