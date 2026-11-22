#pragma once

#include <string>

#include "cable_link/master_link.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "test_runner/test_runner.hpp"

namespace cable_tester {

// HTTP-Server auf dem Master: liefert die eingebettete Single-Page-UI und
// die JSON-Endpunkte /api/status sowie /api/test.
class WebUi {
public:
    WebUi(TestRunner& runner, MasterLink& link);
    WebUi(const WebUi&) = delete;
    WebUi& operator=(const WebUi&) = delete;

    esp_err_t start();

private:
    static esp_err_t index_handler(httpd_req_t* req);
    static esp_err_t status_handler(httpd_req_t* req);
    static esp_err_t test_handler(httpd_req_t* req);
    static esp_err_t soak_handler(httpd_req_t* req);
    static esp_err_t tone_handler(httpd_req_t* req);
    static esp_err_t length_cal_handler(httpd_req_t* req);
    static esp_err_t send_json(httpd_req_t* req, const std::string& body);
    static esp_err_t send_error(httpd_req_t* req, const char* status, const char* message);

    TestRunner& runner_;
    MasterLink& link_;
    httpd_handle_t server_{nullptr};
};

} // namespace cable_tester
