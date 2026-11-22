#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include "display_ui/display_ui.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

#include "cable_test/version.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "font8x8.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "report/report.hpp"
#include "rj45_port/pin_map.hpp"

namespace cable_tester {
namespace {

constexpr char kTag[] = "display";

constexpr std::uint16_t kBlack = 0x0000;
constexpr std::uint16_t kWhite = 0xFFFF;
constexpr std::uint16_t kHeaderBg = 0x0010;
constexpr std::uint16_t kGreen = 0x07E0;
constexpr std::uint16_t kYellow = 0xFFE0;
constexpr std::uint16_t kRed = 0xF800;
constexpr std::uint16_t kGray = 0x8410;
constexpr std::uint16_t kCyan = 0x07FF;

constexpr int kPixelWidth = display::Ili9488::kWidth;
constexpr int kLineHeight = 16;

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void to_rgb666(std::uint16_t color, std::uint8_t out[3]) {
    out[0] = static_cast<std::uint8_t>(((color & 0xF800) >> 8) | ((color & 0x8000) >> 13));
    out[1] = static_cast<std::uint8_t>((color & 0x07E0) >> 3);
    out[2] = static_cast<std::uint8_t>(((color & 0x001F) << 3) | ((color & 0x0010) >> 2));
}

} // namespace

DisplayUi::DisplayUi(TestRunner& runner, MasterLink& link, const char* ap_ssid)
    : runner_(runner), link_(link), ap_ssid_(ap_ssid) {}

esp_err_t DisplayUi::start() {
    // Touch-CS schon vor der Display-Initialisierung deselektieren, damit
    // der XPT2046 den gemeinsamen SPI-Bus während der Init-Sequenz nicht
    // treibt (T_DO teilt sich GPIO 14 mit dem Display-SDO).
    gpio_config_t touch_cs{};
    touch_cs.pin_bit_mask = 1ULL << static_cast<int>(board::display::kTouchCs);
    touch_cs.mode = GPIO_MODE_OUTPUT;
    touch_cs.pull_up_en = GPIO_PULLUP_DISABLE;
    touch_cs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    touch_cs.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&touch_cs);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Touch-CS-GPIO: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(board::display::kTouchCs, 1);

    gpio_config_t touch{};
    touch.pin_bit_mask = 1ULL << static_cast<int>(board::display::kTouchIrq);
    touch.mode = GPIO_MODE_INPUT;
    touch.pull_up_en = GPIO_PULLUP_ENABLE;
    touch.pull_down_en = GPIO_PULLDOWN_DISABLE;
    touch.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&touch);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Touch-IRQ-GPIO: %s", esp_err_to_name(err));
        return err;
    }

    err = panel_.init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ILI9488-Initialisierung: %s", esp_err_to_name(err));
        return err;
    }

    draw_header();
    draw_content();
    draw_footer();

    const BaseType_t ok = xTaskCreate(task_entry, "display_ui", 12288, this, 4, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "Display-Task konnte nicht erstellt werden");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "Display-UI bereit");
    return ESP_OK;
}

void DisplayUi::task_entry(void* arg) {
    static_cast<DisplayUi*>(arg)->task_loop();
    vTaskDelete(nullptr);
}

void DisplayUi::task_loop() {
    std::uint32_t last_header = now_ms();
    for (;;) {
        const std::uint32_t now = now_ms();
        refresh_result();

        const bool busy = testing_ || runner_.busy();
        if (busy != last_busy_) {
            last_busy_ = busy;
            draw_content();
            draw_footer();
        }

        if (now - last_header >= 1000) {
            draw_header();
            draw_footer();
            last_header = now;
        }

        if (has_result_ && !busy && now - page_since_ms_ >= 6000) {
            page_ = (page_ + 1) % kPageCount;
            page_since_ms_ = now;
            draw_content();
            draw_footer();
        }

        if (touch_pressed()) {
            handle_touch();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void DisplayUi::refresh_result() {
    TestResult fresh;
    std::uint32_t generation = 0;
    if (!runner_.last_result(fresh, generation) || generation == result_generation_) {
        return;
    }

    result_ = fresh;
    result_generation_ = generation;
    has_result_ = true;
    error_ = false;
    page_ = 0;
    page_since_ms_ = now_ms();
    if (!testing_) {
        draw_content();
        draw_footer();
    }
}

bool DisplayUi::touch_pressed() {
    const bool raw = gpio_get_level(board::display::kTouchIrq) == 0;
    if (raw) {
        if (touch_hold_ < 3) {
            ++touch_hold_;
        }
    } else {
        touch_hold_ = 0;
        touch_latched_ = false;
    }

    if (touch_hold_ >= 2 && !touch_latched_) {
        touch_latched_ = true;
        return true;
    }
    return false;
}

void DisplayUi::handle_touch() {
    if (testing_ || runner_.busy()) {
        return;
    }

    if (!link_.paired()) {
        error_ = true;
        has_result_ = false;
        std::snprintf(error_text_, sizeof(error_text_), "Slave nicht verbunden");
        draw_content();
        draw_footer();
        return;
    }

    testing_ = true;
    error_ = false;
    draw_content();
    draw_footer();

    TestResult fresh;
    const esp_err_t err = runner_.run(fresh);
    testing_ = false;

    if (err == ESP_OK) {
        result_ = fresh;
        has_result_ = true;
        error_ = false;
        page_ = 0;
        page_since_ms_ = now_ms();

        // Generation übernehmen, damit refresh_result() das gerade
        // gezeichnete Ergebnis nicht ein zweites Mal zeichnet.
        TestResult cached;
        std::uint32_t generation = 0;
        if (runner_.last_result(cached, generation)) {
            result_ = cached;
            result_generation_ = generation;
        }
    } else {
        error_ = true;
        has_result_ = false;
        if (err == ESP_ERR_INVALID_STATE) {
            std::snprintf(error_text_, sizeof(error_text_), "%s",
                          link_.paired() ? "Test laeuft bereits" : "Slave nicht verbunden");
        } else {
            std::snprintf(error_text_, sizeof(error_text_), "Messung fehlgeschlagen (%s)",
                          esp_err_to_name(err));
        }
    }

    // Ein noch gehaltener Finger soll nicht sofort einen zweiten Test starten.
    touch_latched_ = true;
    draw_content();
    draw_footer();
}

void DisplayUi::draw_header() {
    char line[64];
    std::snprintf(line, sizeof(line), "KABELTESTER %s", cable_test::kFirmwareVersion);
    draw_text_row(0, line, kWhite, kHeaderBg);

    std::snprintf(line, sizeof(line), "WLAN: %.21s", ap_ssid_ != nullptr ? ap_ssid_ : "-");
    draw_text_row(1, line, kWhite, kHeaderBg);

    if (link_.paired()) {
        std::snprintf(line, sizeof(line), "Slave: verbunden  RSSI %d",
                      static_cast<int>(link_.rssi()));
        draw_text_row(2, line, kGreen, kHeaderBg);
    } else {
        draw_text_row(2, "Slave: getrennt", kRed, kHeaderBg);
    }
}

void DisplayUi::draw_footer() {
    char line[64];
    std::uint16_t color = kWhite;

    if (testing_ || runner_.busy()) {
        std::snprintf(line, sizeof(line), "Test laeuft...");
        color = kYellow;
    } else if (!link_.paired()) {
        std::snprintf(line, sizeof(line), "SLAVE NICHT VERBUNDEN");
        color = kRed;
    } else if (error_) {
        std::snprintf(line, sizeof(line), "TOUCH = NEUER TEST");
        color = kYellow;
    } else if (has_result_) {
        std::snprintf(line, sizeof(line), "TOUCH=TEST  SEITE %d/%d", page_ + 1, kPageCount);
        color = kCyan;
    } else {
        std::snprintf(line, sizeof(line), "TOUCH = TEST STARTEN");
        color = kCyan;
    }

    draw_text_row(kFooterRow, line, color, kHeaderBg);
}

void DisplayUi::draw_content() {
    std::array<std::string, kContentRows> lines{};
    int count = 0;
    std::uint16_t first_color = kWhite;
    const bool busy = testing_ || runner_.busy();

    if (busy) {
        lines[count++] = "Test laeuft...";
        lines[count++] = "";
        lines[count++] = "Bitte warten.";
        first_color = kYellow;
    } else if (error_) {
        lines[count++] = "FEHLER";
        lines[count++] = error_text_;
        lines[count++] = "";
        lines[count++] = "Touch = neuer Versuch";
        first_color = kRed;
    } else if (has_result_) {
        if (page_ == 0) {
            build_summary(lines, count);
            switch (result_.analysis.kind) {
            case cable_test::CableKind::Straight:
            case cable_test::CableKind::Crossover:
                first_color = kGreen;
                break;
            case cable_test::CableKind::Custom:
                first_color = kYellow;
                break;
            case cable_test::CableKind::Faulty:
                first_color = kRed;
                break;
            default:
                first_color = kGray;
                break;
            }
        } else if (page_ == 1) {
            build_raw(lines, count);
            first_color = kWhite;
        } else {
            build_matrix(lines, count);
            first_color = kCyan;
        }
    } else {
        lines[count++] = "Bereit.";
        lines[count++] = "Kabel anstecken und Slave";
        lines[count++] = "verbinden. Dann Display";
        lines[count++] = "beruehren oder Web-UI:";
        lines[count++] = "http://192.168.4.1/";
        first_color = kWhite;
    }

    draw_content_lines(lines, count, first_color);
}

void DisplayUi::draw_content_lines(const std::array<std::string, kContentRows>& lines, int count,
                                   std::uint16_t first_color) {
    for (int row = 0; row < kContentRows; ++row) {
        const char* text = row < count ? lines[static_cast<std::size_t>(row)].c_str() : "";
        const std::uint16_t color = row == 0 ? first_color : kWhite;
        draw_text_row(kHeaderRows + row, text, color, kBlack);
    }
}

void DisplayUi::build_summary(std::array<std::string, kContentRows>& lines, int& count) const {
    const std::string text = report::format_text(result_.data, result_.analysis);
    wrap_text(text, lines, count);
}

void DisplayUi::build_raw(std::array<std::string, kContentRows>& lines, int& count) const {
    char buffer[64];

    lines[count++] = "ROHDATEN";
    std::snprintf(buffer, sizeof(buffer), "Idle A 0x%02X  B 0x%02X",
                  static_cast<unsigned>(result_.data.idle_local_mask),
                  static_cast<unsigned>(result_.data.idle_remote_mask));
    lines[count++] = buffer;
    lines[count++] = "Runden (A/B):";

    for (std::size_t i = 0; i < cable_test::kLineCount; ++i) {
        std::snprintf(buffer, sizeof(buffer), " %zu: 0x%02X / 0x%02X", i + 1,
                      static_cast<unsigned>(result_.data.rounds[i].local_mask),
                      static_cast<unsigned>(result_.data.rounds[i].remote_mask));
        lines[count++] = buffer;
    }

    lines[count++] = "Laengen (m):";
    for (int group = 0; group < 2; ++group) {
        std::string line;
        for (int i = 0; i < 4; ++i) {
            const std::size_t index = static_cast<std::size_t>(group * 4 + i);
            char value[16];
            if (result_.data.length_m[index]) {
                std::snprintf(value, sizeof(value), "%.1f", *result_.data.length_m[index]);
            } else {
                std::snprintf(value, sizeof(value), "--");
            }
            char part[24];
            std::snprintf(part, sizeof(part), "%s%zu:%s", i == 0 ? "" : " ", index + 1, value);
            line += part;
        }
        lines[count++] = line;
    }

    while (count < kContentRows) {
        lines[count++] = "";
    }
}

void DisplayUi::build_matrix(std::array<std::string, kContentRows>& lines, int& count) const {
    lines[count++] = "MATRIX A -> B";
    for (std::size_t a = 0; a < cable_test::kLineCount; ++a) {
        std::string line = "A";
        line += static_cast<char>('1' + static_cast<int>(a));
        line += ':';
        for (std::size_t b = 0; b < cable_test::kLineCount; ++b) {
            line += cable_test::has_line(result_.data.rounds[a].remote_mask, b) ? " X" : " .";
        }
        lines[count++] = line;
    }

    lines[count++] = "";
    lines[count++] = "X = HIGH am Slave";
    std::string local = "Lokal:";
    for (std::size_t a = 0; a < cable_test::kLineCount; ++a) {
        char part[8];
        std::snprintf(part, sizeof(part), " %02X",
                      static_cast<unsigned>(result_.data.rounds[a].local_mask));
        local += part;
    }
    lines[count++] = local;

    while (count < kContentRows) {
        lines[count++] = "";
    }
}

void DisplayUi::wrap_text(const std::string& text,
                          std::array<std::string, kContentRows>& lines, int& count) {
    count = 0;
    std::string current;

    const auto push = [&](std::string line) {
        if (count < kContentRows) {
            if (!line.empty() && line.back() == ' ') {
                line.pop_back();
            }
            lines[static_cast<std::size_t>(count)] = std::move(line);
            ++count;
        }
    };

    for (char c : text) {
        if (c == '\n') {
            push(current);
            current.clear();
            continue;
        }
        if (c < 32 || static_cast<unsigned char>(c) > 126) {
            c = ' ';
        }
        current.push_back(c);
        if (static_cast<int>(current.size()) >= kCols) {
            const std::size_t space = current.rfind(' ');
            if (space != std::string::npos && space > 0) {
                std::string rest = current.substr(space + 1);
                push(current.substr(0, space));
                current = std::move(rest);
            } else {
                push(current.substr(0, static_cast<std::size_t>(kCols)));
                current.erase(0, static_cast<std::size_t>(kCols));
            }
        }
    }
    if (!current.empty()) {
        push(std::move(current));
    }
}

void DisplayUi::draw_text_row(int row, const char* text, std::uint16_t fg, std::uint16_t bg) {
    if (row < 0 || row >= kRows) {
        return;
    }

    std::uint8_t fg_rgb[3];
    std::uint8_t bg_rgb[3];
    to_rgb666(fg, fg_rgb);
    to_rgb666(bg, bg_rgb);

    std::uint8_t* buffer = line_buffer_.data();
    for (int i = 0; i < kPixelWidth * kLineHeight; ++i) {
        buffer[static_cast<std::size_t>(i) * 3 + 0] = bg_rgb[0];
        buffer[static_cast<std::size_t>(i) * 3 + 1] = bg_rgb[1];
        buffer[static_cast<std::size_t>(i) * 3 + 2] = bg_rgb[2];
    }

    if (text != nullptr) {
        for (int col = 0; col < kCols && text[col] != '\0'; ++col) {
            unsigned char c = static_cast<unsigned char>(text[col]);
            if (c < 32 || c > 126) {
                c = '?';
            }
            const auto& glyph = display::kFont8x8[c];
            for (int gy = 0; gy < 8; ++gy) {
                const std::uint8_t bits = glyph[gy];
                for (int gx = 0; gx < 8; ++gx) {
                    if (((bits >> gx) & 1U) == 0) {
                        continue;
                    }
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int px = col * 16 + gx * 2 + dx;
                            const int py = gy * 2 + dy;
                            const std::size_t index =
                                (static_cast<std::size_t>(py) * kPixelWidth +
                                 static_cast<std::size_t>(px)) * 3;
                            buffer[index + 0] = fg_rgb[0];
                            buffer[index + 1] = fg_rgb[1];
                            buffer[index + 2] = fg_rgb[2];
                        }
                    }
                }
            }
        }
    }

    panel_.draw_rgb666(0, row * kLineHeight, kPixelWidth, kLineHeight, buffer);
}

} // namespace cable_tester

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
