#pragma once

#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include <array>
#include <cstdint>
#include <string>

#include "cable_link/master_link.hpp"
#include "esp_err.h"
#include "ili9488.hpp"
#include "test_runner/test_runner.hpp"

namespace cable_tester {

// Einfache Display-Oberfläche für den Master. Zeigt Verbindungsstatus,
// Testzusammenfassung und Rohdaten; eine Berührung startet einen neuen Test.
// Die Seiten wechseln nach einem Ergebnis automatisch.
class DisplayUi {
public:
    DisplayUi(TestRunner& runner, MasterLink& link, const char* ap_ssid);
    DisplayUi(const DisplayUi&) = delete;
    DisplayUi& operator=(const DisplayUi&) = delete;

    esp_err_t start();

private:
    static constexpr int kRows = 20;
    static constexpr int kCols = 30;
    static constexpr int kContentRows = 16;
    static constexpr int kHeaderRows = 3;
    static constexpr int kFooterRow = kRows - 1;
    static constexpr int kPageCount = 3;

    static void task_entry(void* arg);
    void task_loop();

    void draw_header();
    void draw_footer();
    void draw_content();
    void draw_text_row(int row, const char* text, std::uint16_t fg, std::uint16_t bg);
    void draw_content_lines(const std::array<std::string, kContentRows>& lines, int count,
                            std::uint16_t first_color);

    bool touch_pressed();
    void handle_touch();
    void refresh_result();
    void build_summary(std::array<std::string, kContentRows>& lines, int& count) const;
    void build_raw(std::array<std::string, kContentRows>& lines, int& count) const;
    void build_matrix(std::array<std::string, kContentRows>& lines, int& count) const;
    static void wrap_text(const std::string& text,
                          std::array<std::string, kContentRows>& lines, int& count);

    TestRunner& runner_;
    MasterLink& link_;
    const char* ap_ssid_;
    display::Ili9488 panel_;
    std::array<std::uint8_t, display::Ili9488::kLineBytes> line_buffer_{};

    TestResult result_{};
    std::uint32_t result_generation_{0};
    bool has_result_{false};
    int page_{0};
    std::uint32_t page_since_ms_{0};
    bool testing_{false};
    bool error_{false};
    char error_text_[64]{};

    bool touch_latched_{false};
    int touch_hold_{0};
    bool last_busy_{false};
};

} // namespace cable_tester

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
