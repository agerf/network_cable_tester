#include "app.hpp"

#include "cable_link/slave_link.hpp"
#include "cable_test/version.hpp"
#include "console_ui.hpp"
#include "esp_log.h"
#include "rj45_port/pin_map.hpp"
#include "rj45_port/rj45_port.hpp"
#include "wifi.hpp"

namespace cable_tester::app {
namespace {
constexpr char kTag[] = "app";
}

void run_slave() {
    ESP_LOGI(kTag, "Kabeltester v%s - Slave (Node B)", cable_test::kFirmwareVersion);

    ESP_ERROR_CHECK(init_nvs());

    static Rj45Port port(board::kNodeBPins);
    ESP_ERROR_CHECK(port.init());

    ESP_ERROR_CHECK(start_wifi_slave());

    static SlaveLink link(port);
    ESP_ERROR_CHECK(link.init());

    console_ui::start_slave();

    ESP_LOGI(kTag, "Bereit; warte auf Master-Verbindung per ESP-NOW.");
}

} // namespace cable_tester::app
