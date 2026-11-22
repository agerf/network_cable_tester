#include "console_ui.hpp"

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER
#include "cable_link/master_link.hpp"
#include "cable_test/version.hpp"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "nvs.h"
#include "report/report.hpp"
#include "test_runner/test_runner.hpp"
#include "wifi.hpp"
#include "wifi_config.hpp"
#endif

namespace cable_tester::console_ui {
namespace {

constexpr char kTag[] = "console";

#if CONFIG_CABLE_TESTER_ROLE_MASTER
TestRunner* g_runner = nullptr;
MasterLink* g_link = nullptr;

void format_mac(const std::array<std::uint8_t, 6>& mac, char* out, std::size_t size) {
    std::snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
}

void print_wifi_usage() {
    std::printf("Nutzung:\n"
                "  wifi [status]         Zugangsdaten, IP-Adresse, Kanal und Clients anzeigen\n"
                "  wifi ssid <name>      SSID setzen (1-32 Zeichen, bei Leerzeichen \"...\")\n"
                "  wifi pass <passwort>  Passwort setzen (8-63 druckbare Zeichen)\n"
                "  wifi reset            Defaults wiederherstellen\n"
                "Aenderungen werden im NVS gespeichert und sofort angewendet.\n");
}

void print_wifi_status() {
    std::printf("SSID: %s\nPasswort: %s\nQuelle: %s\n", app::ap_ssid(), app::ap_password(),
                app::ap_credentials_custom() ? "NVS (seriell gesetzt)" : "Default");

    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info{};
    if (ap_netif != nullptr && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        std::printf("AP-IP:   " IPSTR "/" IPSTR ", Gateway " IPSTR "\n", IP2STR(&ip_info.ip),
                    IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
    } else {
        std::printf("AP-IP:   noch keine Adresse\n");
    }

    std::uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) == ESP_OK) {
        std::printf("Kanal:   %u\n", static_cast<unsigned>(primary));
    }

    std::uint8_t ap_mac[6]{};
    if (esp_wifi_get_mac(WIFI_IF_AP, ap_mac) == ESP_OK) {
        std::printf("AP-MAC:  " MACSTR "\n", MAC2STR(ap_mac));
    }
    std::uint8_t sta_mac[6]{};
    if (esp_wifi_get_mac(WIFI_IF_STA, sta_mac) == ESP_OK) {
        std::printf("STA-MAC: " MACSTR "\n", MAC2STR(sta_mac));
    }

    wifi_sta_list_t stations{};
    if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK) {
        std::printf("Clients: %d\n", stations.num);
        wifi_sta_mac_ip_list_t stations_ip{};
        if (esp_wifi_ap_get_sta_list_with_ip(&stations, &stations_ip) == ESP_OK) {
            for (int i = 0; i < stations_ip.num; ++i) {
                std::printf("  " MACSTR "  " IPSTR "\n", MAC2STR(stations_ip.sta[i].mac),
                            IP2STR(&stations_ip.sta[i].ip));
            }
        } else {
            for (int i = 0; i < stations.num; ++i) {
                std::printf("  " MACSTR "\n", MAC2STR(stations.sta[i].mac));
            }
        }
    }
}

int commit_and_apply(esp_err_t err, const char* success_text) {
    if (err != ESP_OK) {
        std::printf("Speichern fehlgeschlagen (%s).\n", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_ARG) {
            std::printf("SSID: 1-32 Zeichen; Passwort: 8-63 druckbare Zeichen.\n");
        }
        return 1;
    }
    const esp_err_t apply_err = app::apply_ap_credentials(app::ap_ssid(), app::ap_password());
    if (apply_err != ESP_OK) {
        std::printf("Gespeichert, aber Anwenden fehlgeschlagen (%s); gilt nach Neustart.\n",
                    esp_err_to_name(apply_err));
        return 1;
    }
    std::printf("%s\n", success_text);
    return 0;
}

int cmd_wifi(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "status") == 0)) {
        print_wifi_status();
        return 0;
    }
    if (std::strcmp(argv[1], "ssid") == 0) {
        if (argc != 3) {
            print_wifi_usage();
            return 1;
        }
        return commit_and_apply(
            app::set_ap_ssid(argv[2]),
            "SSID gespeichert und angewendet. WLAN-Clients muessen sich neu verbinden.");
    }
    if (std::strcmp(argv[1], "pass") == 0) {
        if (argc != 3) {
            print_wifi_usage();
            return 1;
        }
        return commit_and_apply(
            app::set_ap_password(argv[2]),
            "Passwort gespeichert und angewendet. WLAN-Clients muessen sich neu verbinden.");
    }
    if (std::strcmp(argv[1], "reset") == 0) {
        if (argc != 2) {
            print_wifi_usage();
            return 1;
        }
        return commit_and_apply(
            app::reset_ap_credentials(),
            "Defaults wiederhergestellt und angewendet. WLAN-Clients muessen sich neu verbinden.");
    }
    print_wifi_usage();
    return 1;
}

int cmd_test(int /*argc*/, char** /*argv*/) {
    if (g_runner == nullptr || g_link == nullptr) {
        return 1;
    }
    if (!g_link->paired()) {
        std::printf("Slave nicht verbunden.\n");
        return 1;
    }
    if (g_runner->busy()) {
        std::printf("Test laeuft bereits.\n");
        return 1;
    }

    std::printf("Test laeuft ...\n");
    TestResult result;
    const esp_err_t err = g_runner->run(result);
    if (err != ESP_OK) {
        std::printf("Messung fehlgeschlagen (%s).\n", esp_err_to_name(err));
        return 1;
    }

    const std::string text = report::format_text(result.data, result.analysis);
    std::printf("%s", text.c_str());
    std::printf("JSON: %s", report::to_json(result.data, result.analysis).c_str());
    return 0;
}

int cmd_status(int /*argc*/, char** /*argv*/) {
    if (g_runner == nullptr || g_link == nullptr) {
        return 1;
    }
    char mac[18];
    if (g_link->paired()) {
        format_mac(g_link->peer_mac(), mac, sizeof(mac));
    } else {
        std::snprintf(mac, sizeof(mac), "--");
    }
    std::printf("Firmware %s | Slave %s (%s) | RSSI %d dBm | %s\n",
                cable_test::kFirmwareVersion, g_link->paired() ? "verbunden" : "getrennt", mac,
                static_cast<int>(g_link->rssi()), g_runner->busy() ? "Test laeuft" : "bereit");
    return 0;
}
#else
int cmd_status_slave(int /*argc*/, char** /*argv*/) {
    std::printf("Slave bereit; wartet auf den Master (ESP-NOW).\n");
    return 0;
}
#endif

#if CONFIG_CABLE_TESTER_ROLE_MASTER
int cmd_touchcal(int /*argc*/, char** /*argv*/) {
    nvs_handle_t handle{};
    esp_err_t err = nvs_open("touch_cal", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        std::printf("nvs_open(touch_cal): %s\n", esp_err_to_name(err));
        return 1;
    }
    err = nvs_erase_key(handle, "cal");
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        std::printf("Touch-Kalibrierung loeschen fehlgeschlagen (%s).\n",
                    esp_err_to_name(err));
        return 1;
    }
    std::printf("Touch-Kalibrierung geloescht. Mit 'reboot' neu starten; "
                "das Display kalibriert danach neu.\n");
    return 0;
}

#endif

int cmd_reboot(int /*argc*/, char** /*argv*/) {
    std::printf("Neustart ...\n");
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
    return 0;
}

void register_commands() {
#if CONFIG_CABLE_TESTER_ROLE_MASTER
    esp_console_cmd_t test_cmd{};
    test_cmd.command = "test";
    test_cmd.help = "Kabeltest starten und Ergebnis ausgeben";
    test_cmd.func = &cmd_test;
    ESP_ERROR_CHECK(esp_console_cmd_register(&test_cmd));

    esp_console_cmd_t status_cmd{};
    status_cmd.command = "status";
    status_cmd.help = "Verbindungs- und Teststatus anzeigen";
    status_cmd.func = &cmd_status;
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));

    esp_console_cmd_t wifi_cmd{};
    wifi_cmd.command = "wifi";
    wifi_cmd.help = "WLAN-SSID/Passwort anzeigen, setzen oder zuruecksetzen";
    wifi_cmd.func = &cmd_wifi;
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_cmd));

    esp_console_cmd_t touchcal_cmd{};
    touchcal_cmd.command = "touchcal";
    touchcal_cmd.help = "Touch-Kalibrierung loeschen (Neustart erforderlich)";
    touchcal_cmd.func = &cmd_touchcal;
    ESP_ERROR_CHECK(esp_console_cmd_register(&touchcal_cmd));
#else
    esp_console_cmd_t status_cmd{};
    status_cmd.command = "status";
    status_cmd.help = "Slave-Status anzeigen";
    status_cmd.func = &cmd_status_slave;
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));
#endif

    esp_console_cmd_t reboot_cmd{};
    reboot_cmd.command = "reboot";
    reboot_cmd.help = "Board neu starten";
    reboot_cmd.func = &cmd_reboot;
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));
}

void start_repl() {
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "cable>";
    repl_config.max_cmdline_length = 128;
    repl_config.task_stack_size = 6144;

    esp_console_repl_t* repl = nullptr;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_console_new_repl_usb_serial_jtag: %s", esp_err_to_name(err));
        return;
    }
#else
    esp_console_dev_uart_config_t dev_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&dev_config, &repl_config, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_console_new_repl_uart: %s", esp_err_to_name(err));
        return;
    }
#endif

    register_commands();

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_console_start_repl: %s", esp_err_to_name(err));
    }
}

} // namespace

void start(TestRunner& runner, MasterLink& link) {
#if CONFIG_CABLE_TESTER_ROLE_MASTER
    g_runner = &runner;
    g_link = &link;
    start_repl();
#else
    (void)runner;
    (void)link;
#endif
}

void start_slave() {
#if !CONFIG_CABLE_TESTER_ROLE_MASTER
    start_repl();
#endif
}

} // namespace cable_tester::console_ui
