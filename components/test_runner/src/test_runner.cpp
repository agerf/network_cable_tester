#include "test_runner/test_runner.hpp"

#include <cstddef>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cable_tester {
namespace {

// Stellt bei jedem Ausstieg (auch Fehler) den sicheren Portzustand her.
struct PortCleanup {
    Rj45Port& port;
    MasterLink& link;
    bool slave_floating{false};

    ~PortCleanup() {
        port.allInputsPullDown();
        if (slave_floating) {
            link.request_release(150);
        }
    }
};

} // namespace

TestRunner::TestRunner(Rj45Port& port, MasterLink& link) : port_(port), link_(link) {}

bool TestRunner::busy() const {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    return !lock.owns_lock();
}

esp_err_t TestRunner::run(TestResult& out) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || !link_.paired()) {
        return ESP_ERR_INVALID_STATE;
    }

    out = TestResult{};
    PortCleanup cleanup{port_, link_};

    // Ruhezustand: ohne Ansteuerung müssen alle Leitungen LOW sein.
    port_.allInputsPullDown();
    vTaskDelay(pdMS_TO_TICKS(protocol::kSettleMs));
    out.data.idle_local_mask = port_.sample();
    const auto idle_remote = link_.request_idle();
    if (!idle_remote) {
        return ESP_FAIL;
    }
    out.data.idle_remote_mask = *idle_remote;

    // Messrunden: genau eine Ader treiben, beide Seiten lesen.
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        port_.allInputsPullDown();
        vTaskDelay(pdMS_TO_TICKS(protocol::kSettleMs));
        port_.driveHigh(line);

        const auto remote =
            link_.request_measure(static_cast<std::uint8_t>(line));
        if (!remote) {
            return ESP_FAIL;
        }
        const auto local = static_cast<cable_test::LineMask>(
            port_.sample() | cable_test::line_bit(line));
        out.data.rounds[line].local_mask = local;
        out.data.rounds[line].remote_mask = *remote;
        port_.release(line);
    }
    port_.allInputsPullDown();

    out.analysis = cable_test::analyze(out.data);

    // Längenmessung nur bei elektrisch sauberem Kabel, sonst wären die
    // Kapazitätswerte ohnehin nicht interpretierbar.
    const bool wiring_clean = !out.analysis.idle_activity &&
                              !cable_test::any_short(out.analysis) &&
                              !cable_test::any_open(out.analysis);
    if (wiring_clean) {
        if (!link_.request_float()) {
            return ESP_FAIL;
        }
        cleanup.slave_floating = true;

        port_.allFloating();
        for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
            out.data.length_m[line] = port_.measureLengthMeters(line);
        }
        port_.allInputsPullDown();

        if (!link_.request_release()) {
            return ESP_FAIL;
        }
        cleanup.slave_floating = false;

        out.analysis = cable_test::analyze(out.data);
    }

    return ESP_OK;
}

} // namespace cable_tester
