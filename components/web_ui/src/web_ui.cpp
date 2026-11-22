#include "web_ui/web_ui.hpp"

#include <cstdio>
#include <string>
#include <sys/types.h>

#include "cable_test/version.hpp"
#include "esp_log.h"
#include "report/report.hpp"

extern const char index_html_start[] asm("_binary_index_html_start");

namespace cable_tester {
namespace {
constexpr char kTag[] = "web_ui";
}

WebUi::WebUi(TestRunner& runner, MasterLink& link) : runner_(runner), link_(link) {}

esp_err_t WebUi::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    const auto register_uri = [this](const char* uri, httpd_method_t method,
                                     esp_err_t (*handler)(httpd_req_t*)) {
        httpd_uri_t entry{};
        entry.uri = uri;
        entry.method = method;
        entry.handler = handler;
        entry.user_ctx = this;
        return httpd_register_uri_handler(server_, &entry);
    };

    err = register_uri("/", HTTP_GET, &WebUi::index_handler);
    if (err == ESP_OK) {
        err = register_uri("/api/status", HTTP_GET, &WebUi::status_handler);
    }
    if (err == ESP_OK) {
        err = register_uri("/api/test", HTTP_POST, &WebUi::test_handler);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "URI-Registrierung: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(kTag, "Web-UI bereit: http://192.168.4.1/");
    return ESP_OK;
}

esp_err_t WebUi::index_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebUi::status_handler(httpd_req_t* req) {
    const auto* self = static_cast<const WebUi*>(req->user_ctx);

    const auto mac = self->link_.peer_mac();
    char mac_string[18];
    std::snprintf(mac_string, sizeof(mac_string), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                  mac[2], mac[3], mac[4], mac[5]);

    char rssi[8];
    std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(self->link_.rssi()));

    std::string body;
    body.reserve(160);
    body += "{\"role\":\"master\",\"firmware\":\"";
    body += cable_test::kFirmwareVersion;
    body += "\",\"paired\":";
    body += self->link_.paired() ? "true" : "false";
    body += ",\"busy\":";
    body += self->runner_.busy() ? "true" : "false";
    body += ",\"rssi\":";
    body += rssi;
    body += ",\"peerMac\":\"";
    body += mac_string;
    body += "\"}";
    return send_json(req, body);
}

esp_err_t WebUi::test_handler(httpd_req_t* req) {
    auto* self = static_cast<WebUi*>(req->user_ctx);

    if (!self->link_.paired()) {
        return send_error(req, "503 Service Unavailable", "Slave nicht verbunden");
    }
    if (self->runner_.busy()) {
        return send_error(req, "409 Conflict", "Test laeuft bereits");
    }

    TestResult result;
    const esp_err_t err = self->runner_.run(result);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, self->link_.paired() ? "409 Conflict" : "503 Service Unavailable",
                          self->link_.paired() ? "Test laeuft bereits" : "Slave nicht verbunden");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error",
                          "Messung fehlgeschlagen (Verbindungsfehler)");
    }

    return send_json(req, report::to_json(result.data, result.analysis));
}

esp_err_t WebUi::send_json(httpd_req_t* req, const std::string& body) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.c_str(), static_cast<ssize_t>(body.size()));
}

esp_err_t WebUi::send_error(httpd_req_t* req, const char* status, const char* message) {
    std::string body;
    body.reserve(64);
    body += "{\"error\":\"";
    body += message;
    body += "\"}";
    httpd_resp_set_status(req, status);
    return send_json(req, body);
}

} // namespace cable_tester
