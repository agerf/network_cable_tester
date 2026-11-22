#include "wifi_config.hpp"

#include <cstddef>
#include <cstring>

#include "app_config.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace cable_tester::app {
namespace {

constexpr char kTag[] = "wifi_cfg";
constexpr char kNamespace[] = "wifi_cfg";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPassword[] = "pass";

// 802.11: SSID bis 32 Byte, WPA2-Passphrase 8..63 druckbare Zeichen.
constexpr std::size_t kSsidMaxLen = 32;
constexpr std::size_t kPasswordMinLen = 8;
constexpr std::size_t kPasswordMaxLen = 63;

char g_ssid[kSsidMaxLen + 1] = {};
char g_password[kPasswordMaxLen + 1] = {};
bool g_custom = false;

bool printable(const char* text) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != '\0'; ++p) {
        if (*p < 0x20U || *p == 0x7fU) {
            return false;
        }
    }
    return true;
}

bool valid_ssid(const char* ssid) {
    if (ssid == nullptr) {
        return false;
    }
    const std::size_t len = std::strlen(ssid);
    return len >= 1 && len <= kSsidMaxLen && printable(ssid);
}

bool valid_password(const char* password) {
    if (password == nullptr) {
        return false;
    }
    const std::size_t len = std::strlen(password);
    return len >= kPasswordMinLen && len <= kPasswordMaxLen && printable(password);
}

void copy_string(char* dst, std::size_t size, const char* src) {
    std::strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

void use_defaults() {
    copy_string(g_ssid, sizeof(g_ssid), kApSsid);
    copy_string(g_password, sizeof(g_password), kApPassword);
    g_custom = false;
}

esp_err_t store_string(const char* key, const char* value) {
    nvs_handle_t handle{};
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_set_str(%s): %s", key, esp_err_to_name(err));
    }
    return err;
}

} // namespace

esp_err_t init_wifi_config() {
    use_defaults();

    nvs_handle_t handle{};
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "Keine gespeicherten Zugangsdaten, Defaults aktiv");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "nvs_open: %s (Defaults aktiv)", esp_err_to_name(err));
        return err;
    }

    char ssid_buffer[kSsidMaxLen + 1] = {};
    std::size_t ssid_size = sizeof(ssid_buffer);
    err = nvs_get_str(handle, kKeySsid, ssid_buffer, &ssid_size);
    if (err == ESP_OK && valid_ssid(ssid_buffer)) {
        copy_string(g_ssid, sizeof(g_ssid), ssid_buffer);
        g_custom = true;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(kTag, "SSID aus NVS ungueltig (%s), Default wird verwendet",
                 esp_err_to_name(err));
    }

    char password_buffer[kPasswordMaxLen + 1] = {};
    std::size_t password_size = sizeof(password_buffer);
    err = nvs_get_str(handle, kKeyPassword, password_buffer, &password_size);
    if (err == ESP_OK && valid_password(password_buffer)) {
        copy_string(g_password, sizeof(g_password), password_buffer);
        g_custom = true;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(kTag, "Passwort aus NVS ungueltig (%s), Default wird verwendet",
                 esp_err_to_name(err));
    }

    nvs_close(handle);
    ESP_LOGI(kTag, "AP-SSID \"%s\" (%s)", g_ssid, g_custom ? "aus NVS" : "Default");
    return ESP_OK;
}

const char* ap_ssid() {
    return g_ssid;
}

const char* ap_password() {
    return g_password;
}

bool ap_credentials_custom() {
    return g_custom;
}

esp_err_t set_ap_ssid(const char* ssid) {
    if (!valid_ssid(ssid)) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = store_string(kKeySsid, ssid);
    if (err == ESP_OK) {
        copy_string(g_ssid, sizeof(g_ssid), ssid);
        g_custom = true;
        ESP_LOGI(kTag, "SSID gespeichert: \"%s\"", g_ssid);
    }
    return err;
}

esp_err_t set_ap_password(const char* password) {
    if (!valid_password(password)) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = store_string(kKeyPassword, password);
    if (err == ESP_OK) {
        copy_string(g_password, sizeof(g_password), password);
        g_custom = true;
        ESP_LOGI(kTag, "Passwort gespeichert");
    }
    return err;
}

esp_err_t reset_ap_credentials() {
    nvs_handle_t handle{};
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_erase_all: %s", esp_err_to_name(err));
        return err;
    }
    use_defaults();
    ESP_LOGI(kTag, "Zugangsdaten auf Defaults zurueckgesetzt");
    return ESP_OK;
}

} // namespace cable_tester::app
