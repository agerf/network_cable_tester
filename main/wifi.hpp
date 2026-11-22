#pragma once

#include "esp_err.h"

namespace cable_tester::app {

esp_err_t init_nvs();

// Master: WLAN im APSTA-Modus. Der AP dient der Web-UI, die STA-Schnittstelle
// ist die ESP-NOW-Schnittstelle (Kanal identisch zum AP).
esp_err_t start_wifi_master();

// Wendet neue AP-Zugangsdaten im laufenden Betrieb an. Der AP startet dabei
// neu; verbundene WLAN-Clients (Web-UI) müssen sich neu verbinden.
esp_err_t apply_ap_credentials(const char* ssid, const char* password);

// Slave: reine STA-Schnittstelle ohne Verbindung, fest auf dem ESP-NOW-Kanal.
esp_err_t start_wifi_slave();

} // namespace cable_tester::app
