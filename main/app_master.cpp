#include "app.hpp"

#include "sdkconfig.h"

#include "cable_link/master_link.hpp"
#include "cable_test/version.hpp"
#include "console_ui.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "rj45_port/pin_map.hpp"
#include "rj45_port/rj45_port.hpp"
#include "test_runner/test_runner.hpp"
#include "web_ui/web_ui.hpp"
#include "wifi.hpp"
#include "wifi_config.hpp"

#if CONFIG_IDF_TARGET_ESP32S3
#include "display_ui/display_ui.hpp"
#endif

namespace cable_tester::app {
namespace {
constexpr char kTag[] = "app";
}

void run_master() {
    ESP_LOGI(kTag, "Kabeltester v%s - Master (Node A)", cable_test::kFirmwareVersion);

    ESP_ERROR_CHECK(init_nvs());

    const esp_err_t wifi_cfg_err = init_wifi_config();
    if (wifi_cfg_err != ESP_OK) {
        ESP_LOGW(kTag, "WLAN-Zugangsdaten nicht geladen (%s), Defaults werden verwendet",
                 esp_err_to_name(wifi_cfg_err));
    }

    static Rj45Port port(board::kNodeAPins);
    ESP_ERROR_CHECK(port.init());

    ESP_ERROR_CHECK(start_wifi_master());

    static MasterLink link;
    ESP_ERROR_CHECK(link.init());

    static TestRunner runner(port, link);
    static WebUi web(runner, link);
    ESP_ERROR_CHECK(web.start());

#if CONFIG_IDF_TARGET_ESP32S3
    // Das Display ist optional: ohne angeschlossenes Modul läuft der Rest
    // (Web-UI, Konsole, ESP-NOW) unverändert weiter.
    static DisplayUi display(runner, link, ap_ssid());
    const esp_err_t display_err = display.start();
    if (display_err != ESP_OK) {
        ESP_LOGW(kTag, "Display nicht verfügbar (%s); Web-UI/Konsole laufen weiter",
                 esp_err_to_name(display_err));
    }
#endif

    console_ui::start(runner, link);

    ESP_LOGI(kTag, "Bereit: WLAN \"%s\" (Passwort \"%s\") -> http://192.168.4.1/", ap_ssid(),
             ap_password());
    ESP_LOGI(kTag, "Seriell: 'test' startet einen Test, 'status' zeigt den Link, "
                   "'wifi' zeigt/aendert die WLAN-Zugangsdaten, 'reboot' startet neu.");
}

} // namespace cable_tester::app
