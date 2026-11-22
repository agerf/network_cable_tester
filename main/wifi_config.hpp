#pragma once

#include "esp_err.h"

namespace cable_tester::app {

// Lädt die AP-Zugangsdaten aus dem NVS. Sind keine (gültigen) Werte
// gespeichert, gelten die Defaults aus app_config.hpp.
// Muss nach init_nvs() und vor start_wifi_master() aufgerufen werden.
esp_err_t init_wifi_config();

// Aktive Zugangsdaten (immer null-terminiert, nie nullptr).
const char* ap_ssid();
const char* ap_password();

// true, sobald mindestens ein Wert aus dem NVS statt aus den Defaults kommt.
bool ap_credentials_custom();

// Validieren, in NVS speichern und die aktiven Werte aktualisieren.
// Das Anwenden auf die laufende WiFi-Instanz übernimmt
// apply_ap_credentials() aus wifi.hpp.
esp_err_t set_ap_ssid(const char* ssid);
esp_err_t set_ap_password(const char* password);

// Löscht die gespeicherten Werte und stellt die Defaults wieder her.
esp_err_t reset_ap_credentials();

} // namespace cable_tester::app
