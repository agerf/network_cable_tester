#include "wifi.hpp"

#include <cstring>

#include "cable_link/protocol.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi_config.hpp"

namespace cable_tester::app {
namespace {

constexpr char kTag[] = "wifi";

esp_err_t checked(esp_err_t err, const char* what) {
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "%s: %s", what, esp_err_to_name(err));
    }
    return err;
}

esp_err_t init_netif_and_events() {
    esp_err_t err = checked(esp_netif_init(), "esp_netif_init");
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return checked(err, "event_loop_create");
    }
    return ESP_OK;
}

esp_err_t init_wifi_driver() {
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    return checked(esp_wifi_init(&config), "esp_wifi_init");
}

void log_mac(const char* name, wifi_interface_t interface) {
    std::uint8_t mac[6]{};
    if (esp_wifi_get_mac(interface, mac) == ESP_OK) {
        ESP_LOGI(kTag, "%s-MAC: %02X:%02X:%02X:%02X:%02X:%02X", name, mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
    }
}

} // namespace

esp_err_t init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t start_wifi_master() {
    esp_err_t err = init_netif_and_events();
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    err = init_wifi_driver();
    if (err != ESP_OK) {
        return err;
    }

    const char* ssid = ap_ssid();
    const char* password = ap_password();
    const std::size_t ssid_len = std::strlen(ssid);

    wifi_config_t ap{};
    std::memcpy(ap.ap.ssid, ssid, ssid_len);
    ap.ap.ssid_len = static_cast<std::uint8_t>(ssid_len);
    std::memcpy(ap.ap.password, password, std::strlen(password));
    ap.ap.channel = protocol::kChannel;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.required = false;

    err = checked(esp_wifi_set_mode(WIFI_MODE_APSTA), "set_mode");
    if (err == ESP_OK) {
        err = checked(esp_wifi_set_config(WIFI_IF_AP, &ap), "set_config");
    }
    if (err == ESP_OK) {
        err = checked(esp_wifi_start(), "wifi_start");
    }
    if (err != ESP_OK) {
        return err;
    }

    const esp_err_t channel_err = esp_wifi_set_channel(protocol::kChannel, WIFI_SECOND_CHAN_NONE);
    if (channel_err != ESP_OK) {
        ESP_LOGW(kTag, "set_channel: %s", esp_err_to_name(channel_err));
    }
    esp_wifi_set_ps(WIFI_PS_NONE);

    log_mac("AP", WIFI_IF_AP);
    log_mac("STA", WIFI_IF_STA);
    ESP_LOGI(kTag, "Access Point \"%s\" auf Kanal %u", ssid,
             static_cast<unsigned>(protocol::kChannel));
    return ESP_OK;
}

esp_err_t apply_ap_credentials(const char* ssid, const char* password) {
    if (ssid == nullptr || password == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t ap{};
    esp_err_t err = checked(esp_wifi_get_config(WIFI_IF_AP, &ap), "get_config (AP)");
    if (err != ESP_OK) {
        return err;
    }

    const std::size_t ssid_len = std::strlen(ssid);
    const std::size_t password_len = std::strlen(password);
    if (ssid_len == 0 || ssid_len > sizeof(ap.ap.ssid) || password_len < 8 ||
        password_len > sizeof(ap.ap.password)) {
        return ESP_ERR_INVALID_ARG;
    }

    std::memset(ap.ap.ssid, 0, sizeof(ap.ap.ssid));
    std::memcpy(ap.ap.ssid, ssid, ssid_len);
    ap.ap.ssid_len = static_cast<std::uint8_t>(ssid_len);
    std::memset(ap.ap.password, 0, sizeof(ap.ap.password));
    std::memcpy(ap.ap.password, password, password_len);
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    err = checked(esp_wifi_set_config(WIFI_IF_AP, &ap), "set_config (AP)");
    if (err == ESP_OK) {
        ESP_LOGI(kTag, "AP-Zugangsdaten angewendet: SSID \"%s\"", ssid);
    }
    return err;
}

esp_err_t start_wifi_slave() {
    esp_err_t err = init_netif_and_events();
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_create_default_wifi_sta();

    err = init_wifi_driver();
    if (err != ESP_OK) {
        return err;
    }

    err = checked(esp_wifi_set_mode(WIFI_MODE_STA), "set_mode");
    if (err == ESP_OK) {
        err = checked(esp_wifi_start(), "wifi_start");
    }
    if (err != ESP_OK) {
        return err;
    }

    const esp_err_t channel_err = esp_wifi_set_channel(protocol::kChannel, WIFI_SECOND_CHAN_NONE);
    if (channel_err != ESP_OK) {
        ESP_LOGW(kTag, "set_channel: %s", esp_err_to_name(channel_err));
    }
    esp_wifi_set_ps(WIFI_PS_NONE);

    log_mac("STA", WIFI_IF_STA);
    ESP_LOGI(kTag, "ESP-NOW-Kanal %u", static_cast<unsigned>(protocol::kChannel));
    return ESP_OK;
}

} // namespace cable_tester::app
