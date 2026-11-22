#include "web_ui/web_ui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>

#include "cable_test/version.hpp"
#include "esp_log.h"
#include "length_cal/length_cal.hpp"
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
    if (err == ESP_OK) {
        err = register_uri("/api/soak", HTTP_POST, &WebUi::soak_handler);
    }
    if (err == ESP_OK) {
        err = register_uri("/api/tone", HTTP_POST, &WebUi::tone_handler);
    }
    if (err == ESP_OK) {
        err = register_uri("/api/len", HTTP_POST, &WebUi::length_cal_handler);
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
    body.reserve(384);
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
    body += "\"";

    const int tone_line = self->runner_.tone_line();
    body += ",\"toneLine\":";
    body += std::to_string(tone_line >= 0 ? tone_line + 1 : 0);
    body += ",\"toneFreq\":";
    body += std::to_string(static_cast<unsigned long>(self->runner_.tone_frequency()));

    body += ",\"lengthScale\":[";
    const auto scale = length_cal::scale();
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (line != 0) {
            body += ',';
        }
        char value[24];
        std::snprintf(value, sizeof(value), "%.4f", scale[line]);
        body += value;
    }
    body += "]";
    body += "}";
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

esp_err_t WebUi::soak_handler(httpd_req_t* req) {
    auto* self = static_cast<WebUi*>(req->user_ctx);

    if (!self->link_.paired()) {
        return send_error(req, "503 Service Unavailable", "Slave nicht verbunden");
    }
    if (self->runner_.busy()) {
        return send_error(req, "409 Conflict", "Test laeuft bereits");
    }

    std::uint16_t runs = 30;
    std::uint32_t interval = 300;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[16];
        if (httpd_query_key_value(query, "runs", value, sizeof(value)) == ESP_OK) {
            const long parsed = std::strtol(value, nullptr, 10);
            if (parsed >= 1 && parsed <= 200) {
                runs = static_cast<std::uint16_t>(parsed);
            }
        }
        if (httpd_query_key_value(query, "interval", value, sizeof(value)) == ESP_OK) {
            const long parsed = std::strtol(value, nullptr, 10);
            if (parsed >= 0 && parsed <= 5000) {
                interval = static_cast<std::uint32_t>(parsed);
            }
        }
    }

    TestResult result;
    cable_test::SoakSummary summary;
    const esp_err_t err = self->runner_.run_soak({runs, interval}, result, summary);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, self->link_.paired() ? "409 Conflict" : "503 Service Unavailable",
                          self->link_.paired() ? "Test laeuft bereits" : "Slave nicht verbunden");
    }
    if (err != ESP_OK && summary.runs == 0) {
        return send_error(req, "500 Internal Server Error",
                          "Wackeltest fehlgeschlagen (Verbindungsfehler)");
    }
    return send_json(req, report::to_json(result.data, result.analysis, &summary));
}

esp_err_t WebUi::tone_handler(httpd_req_t* req) {
    auto* self = static_cast<WebUi*>(req->user_ctx);

    bool off = false;
    unsigned long line = 0;
    unsigned long freq = 1000;
    char query[96];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[16];
        if (httpd_query_key_value(query, "off", value, sizeof(value)) == ESP_OK) {
            off = std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0;
        }
        if (httpd_query_key_value(query, "line", value, sizeof(value)) == ESP_OK) {
            const long parsed = std::strtol(value, nullptr, 10);
            if (parsed >= 1 && parsed <= 8) {
                line = static_cast<unsigned long>(parsed);
            }
        }
        if (httpd_query_key_value(query, "freq", value, sizeof(value)) == ESP_OK) {
            const long parsed = std::strtol(value, nullptr, 10);
            if (parsed >= 50 && parsed <= 10000) {
                freq = static_cast<unsigned long>(parsed);
            }
        }
    }

    if (off) {
        self->runner_.stop_tone();
    } else {
        if (line == 0) {
            return send_error(req, "400 Bad Request", "Ader 1-8 fehlt");
        }
        const esp_err_t err =
            self->runner_.start_tone(static_cast<std::size_t>(line - 1),
                                     static_cast<std::uint32_t>(freq));
        if (err == ESP_ERR_INVALID_STATE) {
            return send_error(req, "409 Conflict", "Test laeuft bereits");
        }
        if (err != ESP_OK) {
            return send_error(req, "500 Internal Server Error",
                              "Suchton konnte nicht gestartet werden");
        }
    }

    const int tone_line = self->runner_.tone_line();
    std::string body;
    body.reserve(64);
    body += "{\"toneLine\":";
    body += std::to_string(tone_line >= 0 ? tone_line + 1 : 0);
    body += ",\"toneFreq\":";
    body += std::to_string(static_cast<unsigned long>(self->runner_.tone_frequency()));
    body += "}";
    return send_json(req, body);
}

esp_err_t WebUi::length_cal_handler(httpd_req_t* req) {
    auto* self = static_cast<WebUi*>(req->user_ctx);

    std::string action = "status";
    double meters = 0.0;
    char query[96];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[24];
        if (httpd_query_key_value(query, "action", value, sizeof(value)) == ESP_OK) {
            action = value;
        }
        if (httpd_query_key_value(query, "meters", value, sizeof(value)) == ESP_OK) {
            char* end = nullptr;
            meters = std::strtod(value, &end);
            if (end == value || *end != '\0') {
                meters = 0.0;
            }
        }
    }

    std::string body;
    body.reserve(320);
    const auto append_scale = [&body](const auto& scale_values) {
        body += '[';
        for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
            if (line != 0) {
                body += ',';
            }
            char text[24];
            std::snprintf(text, sizeof(text), "%.4f", scale_values[line]);
            body += text;
        }
        body += ']';
    };

    if (action == "cal") {
        if (meters <= 0.0) {
            return send_error(req, "400 Bad Request", "Referenzlaenge fehlt");
        }
        const auto outcome = length_cal::calibrate(self->runner_, meters);
        if (outcome.status != length_cal::CalibrationStatus::Ok) {
            const char* status =
                outcome.status == length_cal::CalibrationStatus::InvalidReference
                    ? "400 Bad Request"
                    : "409 Conflict";
            return send_error(req, status, length_cal::status_text_de(outcome.status));
        }

        body += "{\"ok\":true,\"message\":\"";
        body += length_cal::status_text_de(outcome.status);
        body += "\",\"changed\":";
        body += std::to_string(static_cast<unsigned long>(outcome.changed_lines));
        body += ",\"measured\":";
        body += std::to_string(static_cast<unsigned long>(outcome.measured_lines));
        body += ",\"scale\":";
        append_scale(outcome.scale);
        body += ",\"measuredM\":[";
        for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
            if (line != 0) {
                body += ',';
            }
            if (outcome.measured_m[line]) {
                char text[24];
                std::snprintf(text, sizeof(text), "%.2f", *outcome.measured_m[line]);
                body += text;
            } else {
                body += "null";
            }
        }
        body += "]}";
        return send_json(req, body);
    }

    if (action == "reset") {
        const esp_err_t err = length_cal::reset();
        if (err != ESP_OK) {
            return send_error(req, "500 Internal Server Error",
                              "Zuruecksetzen fehlgeschlagen");
        }
        self->runner_.set_length_scale(length_cal::scale());
        body += "{\"ok\":true,\"message\":\"Kalibrierung geloescht.\",\"scale\":";
        append_scale(length_cal::scale());
        body += "}";
        return send_json(req, body);
    }

    body += "{\"scale\":";
    append_scale(length_cal::scale());
    body += "}";
    return send_json(req, body);
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
