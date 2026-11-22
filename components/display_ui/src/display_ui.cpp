#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include "display_ui/display_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "cable_test/version.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "report/report.hpp"
#include "rj45_port/pin_map.hpp"

namespace cable_tester {
namespace {

constexpr char kTag[] = "display";

const lv_color_t kPage = lv_color_hex(0x0D0D0D);
const lv_color_t kSurface = lv_color_hex(0x1A1A19);
const lv_color_t kSurface2 = lv_color_hex(0x232322);
const lv_color_t kInk = lv_color_hex(0xF0EFEC);
const lv_color_t kInk2 = lv_color_hex(0xC3C2B7);
const lv_color_t kMuted = lv_color_hex(0x898781);
const lv_color_t kHairline = lv_color_hex(0x2C2C2A);
const lv_color_t kAccent = lv_color_hex(0x3987E5);
const lv_color_t kAccentSolid = lv_color_hex(0x256ABF);
const lv_color_t kGood = lv_color_hex(0x0CA30C);
const lv_color_t kWarning = lv_color_hex(0xFAB219);
const lv_color_t kCritical = lv_color_hex(0xD03B3B);

constexpr int kWiringWidth = 260;
constexpr int kWiringHeight = 250;
constexpr int kPinRadius = 10;
constexpr int kWireLeft = 24;
constexpr int kWireRight = 236;
constexpr int kWireTop = 24;
constexpr int kWireStep = 29;

constexpr int kCalTargetX[4] = {30, 290, 30, 290};
constexpr int kCalTargetY[4] = {80, 80, 400, 400};
constexpr const char* kCalTargetName[4] = {"oben links", "oben rechts", "unten links",
                                           "unten rechts"};

#if LV_FONT_MONTSERRAT_20
const lv_font_t* const kFont20 = &lv_font_montserrat_20;
#else
const lv_font_t* const kFont20 = &lv_font_montserrat_14;
#endif

#if LV_FONT_MONTSERRAT_28
const lv_font_t* const kFont28 = &lv_font_montserrat_28;
#else
const lv_font_t* const kFont28 = &lv_font_montserrat_14;
#endif

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

std::string short_list(const cable_test::Analysis& analysis) {
    std::string text;
    for (std::size_t a = 0; a < cable_test::kLineCount; ++a) {
        for (std::size_t b = a + 1; b < cable_test::kLineCount; ++b) {
            if (!analysis.short_between[a][b]) {
                continue;
            }
            char part[24];
            std::snprintf(part, sizeof(part), "%s%zu-%zu", text.empty() ? "" : ", ", a + 1,
                          b + 1);
            text += part;
        }
    }
    return text.empty() ? "keine" : text;
}

std::string open_list(const cable_test::Analysis& analysis) {
    std::string text;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (!analysis.open[line]) {
            continue;
        }
        char part[16];
        std::snprintf(part, sizeof(part), "%s%zu", text.empty() ? "" : ", ", line + 1);
        text += part;
    }
    return text.empty() ? "keine" : text;
}

std::size_t count_shorts(const cable_test::Analysis& analysis) {
    std::size_t count = 0;
    for (std::size_t a = 0; a < cable_test::kLineCount; ++a) {
        for (std::size_t b = a + 1; b < cable_test::kLineCount; ++b) {
            if (analysis.short_between[a][b]) {
                ++count;
            }
        }
    }
    return count;
}

std::size_t count_opens(const cable_test::Analysis& analysis) {
    std::size_t count = 0;
    for (std::size_t line = 0; line < cable_test::kLineCount; ++line) {
        if (analysis.open[line]) {
            ++count;
        }
    }
    return count;
}

} // namespace

DisplayUi::DisplayUi(TestRunner& runner, MasterLink& link, const char* ap_ssid)
    : runner_(runner), link_(link), ap_ssid_(ap_ssid) {}

esp_err_t DisplayUi::start() {
    const esp_err_t err = panel_.init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ILI9488-Initialisierung: %s", esp_err_to_name(err));
        return err;
    }

    const BaseType_t ok = xTaskCreate(task_entry, "display_ui", 16384, this, 4, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "Display-Task konnte nicht erstellt werden");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "LVGL-Display-UI bereit");
    return ESP_OK;
}

void DisplayUi::task_entry(void* arg) {
    static_cast<DisplayUi*>(arg)->task_loop();
    vTaskDelete(nullptr);
}

esp_err_t DisplayUi::init_lvgl() {
    lv_init();
    lv_tick_set_cb([]() -> std::uint32_t {
        return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    });

    display_ = lv_display_create(display::Ili9488::kWidth, display::Ili9488::kHeight);
    if (display_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_default(display_);
    lv_display_set_user_data(display_, this);
    lv_display_set_flush_cb(display_, flush_cb);
    lv_display_set_buffers(display_, lv_buffer_.data(), nullptr,
                           static_cast<std::uint32_t>(sizeof(lv_buffer_)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev_, display_);
    lv_indev_set_user_data(indev_, this);
    lv_indev_set_read_cb(indev_, touch_read_cb);

    return ESP_OK;
}

void DisplayUi::task_loop() {
    const esp_err_t err = init_lvgl();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "LVGL-Initialisierung: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    if (!load_calibration()) {
        if (!run_calibration()) {
            apply_default_calibration();
        }
    }

    build_ui();
    update_status();

    std::uint32_t last_status = now_ms();
    for (;;) {
        lv_timer_handler();

        const std::uint32_t now = now_ms();
        if (now - last_status >= 250) {
            update_status();
            last_status = now;
        }

        refresh_result();

        if (calibration_requested_) {
            calibration_requested_ = false;
            const bool ok = run_calibration();
            if (!ok) {
                apply_default_calibration();
            }
            build_ui();
            if (has_result_) {
                apply_result(result_);
            }
            update_status();
            if (!ok) {
                show_error("Kalibrierung fehlgeschlagen - Standardwerte aktiv");
            }
        }

        if (test_requested_) {
            test_requested_ = false;
            run_requested_test();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

lv_obj_t* DisplayUi::make_card(lv_obj_t* parent) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, kSurface, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, kHairline, 0);
    lv_obj_set_style_pad_all(card, 14, 0);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_style_pad_column(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(card, false);
    return card;
}

lv_obj_t* DisplayUi::make_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                               lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* DisplayUi::make_card_title(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, kFont20, 0);
    lv_obj_set_style_text_color(label, kInk2, 0);
    return label;
}

void DisplayUi::build_ui() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, kPage, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(screen, false);

    // ---- header --------------------------------------------------------
    lv_obj_t* header = lv_obj_create(screen);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 100);
    lv_obj_set_style_bg_color(header, kSurface, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 10, 0);
    lv_obj_set_style_pad_row(header, 6, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(header, false);

    lv_obj_t* brand_row = lv_obj_create(header);
    lv_obj_set_width(brand_row, LV_PCT(100));
    lv_obj_set_height(brand_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(brand_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brand_row, 0, 0);
    lv_obj_set_style_pad_all(brand_row, 0, 0);
    lv_obj_set_style_pad_column(brand_row, 10, 0);
    lv_obj_set_flex_flow(brand_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(brand_row, false);

    lv_obj_t* logo = lv_obj_create(brand_row);
    lv_obj_set_size(logo, 40, 40);
    lv_obj_set_style_bg_color(logo, kSurface2, 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(logo, 12, 0);
    lv_obj_set_style_border_width(logo, 1, 0);
    lv_obj_set_style_border_color(logo, kHairline, 0);
    lv_obj_set_scrollable(logo, false);
    lv_obj_t* logo_label = lv_label_create(logo);
    lv_label_set_text(logo_label, "RJ45");
    lv_obj_set_style_text_font(logo_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(logo_label, kAccent, 0);
    lv_obj_center(logo_label);

    lv_obj_t* brand_text = lv_obj_create(brand_row);
    lv_obj_set_height(brand_text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(brand_text, 1);
    lv_obj_set_style_bg_opa(brand_text, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brand_text, 0, 0);
    lv_obj_set_style_pad_all(brand_text, 0, 0);
    lv_obj_set_style_pad_row(brand_text, 0, 0);
    lv_obj_set_flex_flow(brand_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(brand_text, false);

    lv_obj_t* title = lv_label_create(brand_text);
    lv_label_set_text(title, "Kabeltester");
    lv_obj_set_style_text_font(title, kFont20, 0);
    lv_obj_set_style_text_color(title, kInk, 0);

    lv_obj_t* subtitle = lv_label_create(brand_text);
    lv_label_set_text_fmt(subtitle, "Node A / %.18s", ap_ssid_ != nullptr ? ap_ssid_ : "-");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, kMuted, 0);

    lv_obj_t* status_row = lv_obj_create(header);
    lv_obj_set_width(status_row, LV_PCT(100));
    lv_obj_set_height(status_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_row, 0, 0);
    lv_obj_set_style_pad_all(status_row, 0, 0);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(status_row, false);

    status_chip_ = lv_obj_create(status_row);
    lv_obj_set_size(status_chip_, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_bg_color(status_chip_, kSurface2, 0);
    lv_obj_set_style_bg_opa(status_chip_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(status_chip_, 14, 0);
    lv_obj_set_style_border_width(status_chip_, 1, 0);
    lv_obj_set_style_border_color(status_chip_, kHairline, 0);
    lv_obj_set_style_pad_hor(status_chip_, 10, 0);
    lv_obj_set_style_pad_ver(status_chip_, 4, 0);
    lv_obj_set_style_pad_column(status_chip_, 6, 0);
    lv_obj_set_flex_flow(status_chip_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_chip_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(status_chip_, false);

    status_dot_ = lv_obj_create(status_chip_);
    lv_obj_set_size(status_dot_, 8, 8);
    lv_obj_set_style_radius(status_dot_, 4, 0);
    lv_obj_set_style_bg_color(status_dot_, kMuted, 0);
    lv_obj_set_style_bg_opa(status_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot_, 0, 0);
    lv_obj_set_scrollable(status_dot_, false);

    status_label_ = lv_label_create(status_chip_);
    lv_label_set_text(status_label_, "Verbinde ...");
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label_, kInk2, 0);

    lv_obj_t* ip_label = lv_label_create(status_row);
    lv_label_set_text(ip_label, "192.168.4.1");
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ip_label, kMuted, 0);

    // ---- scroll area ---------------------------------------------------
    scroll_ = lv_obj_create(screen);
    lv_obj_set_width(scroll_, LV_PCT(100));
    lv_obj_set_flex_grow(scroll_, 1);
    lv_obj_set_style_bg_opa(scroll_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_, 0, 0);
    lv_obj_set_style_pad_all(scroll_, 12, 0);
    lv_obj_set_style_pad_row(scroll_, 12, 0);
    lv_obj_set_flex_flow(scroll_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroll_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(scroll_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_, LV_SCROLLBAR_MODE_OFF);

    // ---- hero card -----------------------------------------------------
    hero_card_ = make_card(scroll_);
    make_label(hero_card_, "KABELPRUEFUNG", &lv_font_montserrat_14, kMuted);
    make_label(hero_card_, "Beide Enden einstecken, dann testen.",
               kFont20, kInk);
    make_label(hero_card_,
               "Der Slave misst am gegenueberliegenden Ende. Der Test dauert nur wenige "
               "Sekunden.",
               &lv_font_montserrat_14, kInk2);
    make_label(hero_card_,
               "Wichtig: gemeinsame Masse (beide USB-Kabel am selben Host/Hub).",
               &lv_font_montserrat_14, kMuted);

    test_btn_ = lv_button_create(hero_card_);
    lv_obj_set_size(test_btn_, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(test_btn_, kAccentSolid, 0);
    lv_obj_set_style_bg_color(test_btn_, kAccent, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(test_btn_, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_style_radius(test_btn_, 10, 0);
    lv_obj_set_style_border_width(test_btn_, 0, 0);
    lv_obj_set_style_shadow_width(test_btn_, 0, 0);
    lv_obj_add_event_cb(test_btn_, test_button_cb, LV_EVENT_CLICKED, this);

    test_btn_label_ = lv_label_create(test_btn_);
    lv_label_set_text(test_btn_label_, "Kabel testen");
    lv_obj_set_style_text_font(test_btn_label_, kFont20, 0);
    lv_obj_set_style_text_color(test_btn_label_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_opa(test_btn_label_, LV_OPA_60, LV_STATE_DISABLED);
    lv_obj_center(test_btn_label_);

    error_label_ = make_label(hero_card_, "", &lv_font_montserrat_14, kCritical);
    lv_obj_set_style_bg_color(error_label_, kPage, 0);
    lv_obj_set_style_bg_opa(error_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(error_label_, 1, 0);
    lv_obj_set_style_border_color(error_label_, kCritical, 0);
    lv_obj_set_style_radius(error_label_, 10, 0);
    lv_obj_set_style_pad_all(error_label_, 8, 0);
    lv_obj_set_hidden(error_label_, true);

    // ---- result container ---------------------------------------------
    result_container_ = lv_obj_create(scroll_);
    lv_obj_set_width(result_container_, LV_PCT(100));
    lv_obj_set_height(result_container_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(result_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(result_container_, 0, 0);
    lv_obj_set_style_pad_all(result_container_, 0, 0);
    lv_obj_set_style_pad_row(result_container_, 12, 0);
    lv_obj_set_flex_flow(result_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(result_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(result_container_, false);
    lv_obj_set_hidden(result_container_, true);

    verdict_card_ = make_card(result_container_);
    lv_obj_set_flex_flow(verdict_card_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(verdict_card_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(verdict_card_, 12, 0);

    verdict_bar_ = lv_obj_create(verdict_card_);
    lv_obj_set_size(verdict_bar_, 4, 56);
    lv_obj_set_style_radius(verdict_bar_, 2, 0);
    lv_obj_set_style_bg_color(verdict_bar_, kMuted, 0);
    lv_obj_set_style_bg_opa(verdict_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(verdict_bar_, 0, 0);
    lv_obj_set_scrollable(verdict_bar_, false);

    lv_obj_t* verdict_icon = lv_obj_create(verdict_card_);
    lv_obj_set_size(verdict_icon, 48, 48);
    lv_obj_set_style_radius(verdict_icon, 24, 0);
    lv_obj_set_style_bg_color(verdict_icon, kSurface2, 0);
    lv_obj_set_style_bg_opa(verdict_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(verdict_icon, 0, 0);
    lv_obj_set_scrollable(verdict_icon, false);

    verdict_icon_label_ = lv_label_create(verdict_icon);
    lv_label_set_text(verdict_icon_label_, "OK");
    lv_obj_set_style_text_font(verdict_icon_label_, kFont20, 0);
    lv_obj_set_style_text_color(verdict_icon_label_, kGood, 0);
    lv_obj_center(verdict_icon_label_);

    lv_obj_t* verdict_text = lv_obj_create(verdict_card_);
    lv_obj_set_height(verdict_text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(verdict_text, 1);
    lv_obj_set_style_bg_opa(verdict_text, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(verdict_text, 0, 0);
    lv_obj_set_style_pad_all(verdict_text, 0, 0);
    lv_obj_set_style_pad_row(verdict_text, 2, 0);
    lv_obj_set_flex_flow(verdict_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(verdict_text, false);

    lv_obj_t* verdict_eyebrow = lv_label_create(verdict_text);
    lv_label_set_text(verdict_eyebrow, "ERGEBNIS");
    lv_obj_set_style_text_font(verdict_eyebrow, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(verdict_eyebrow, kMuted, 0);

    verdict_kind_ = lv_label_create(verdict_text);
    lv_label_set_text(verdict_kind_, "-");
    lv_label_set_long_mode(verdict_kind_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(verdict_kind_, LV_PCT(100));
    lv_obj_set_style_text_font(verdict_kind_, kFont28, 0);
    lv_obj_set_style_text_color(verdict_kind_, kInk, 0);

    verdict_detail_ = lv_label_create(verdict_text);
    lv_label_set_text(verdict_detail_, "-");
    lv_label_set_long_mode(verdict_detail_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(verdict_detail_, LV_PCT(100));
    lv_obj_set_style_text_font(verdict_detail_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(verdict_detail_, kInk2, 0);

    // ---- wiring card ---------------------------------------------------
    lv_obj_t* wiring_card = make_card(result_container_);
    make_card_title(wiring_card, "Verdrahtung");
    wiring_area_ = lv_obj_create(wiring_card);
    lv_obj_set_width(wiring_area_, LV_PCT(100));
    lv_obj_set_height(wiring_area_, kWiringHeight);
    lv_obj_set_style_bg_opa(wiring_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wiring_area_, 0, 0);
    lv_obj_set_style_pad_all(wiring_area_, 0, 0);
    lv_obj_set_scrollable(wiring_area_, false);
    make_label(wiring_card,
               "Durchgezogen = Verbindung / gestrichelt = offen / gelb = Kurzschluss",
               &lv_font_montserrat_14, kMuted);

    // ---- matrix card ---------------------------------------------------
    lv_obj_t* matrix_card = make_card(result_container_);
    make_card_title(matrix_card, "Messmatrix");
    matrix_area_ = lv_obj_create(matrix_card);
    lv_obj_set_width(matrix_area_, LV_PCT(100));
    lv_obj_set_height(matrix_area_, 212);
    lv_obj_set_style_bg_opa(matrix_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(matrix_area_, 0, 0);
    lv_obj_set_style_pad_all(matrix_area_, 0, 0);
    lv_obj_set_scrollable(matrix_area_, false);
    make_label(matrix_card, "Zeile: getriebene Ader A / Spalte: HIGH an B",
               &lv_font_montserrat_14, kMuted);

    // ---- diagnosis card ------------------------------------------------
    lv_obj_t* diag_card = make_card(result_container_);
    make_card_title(diag_card, "Diagnose");
    diag_area_ = lv_obj_create(diag_card);
    lv_obj_set_width(diag_area_, LV_PCT(100));
    lv_obj_set_height(diag_area_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(diag_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(diag_area_, 0, 0);
    lv_obj_set_style_pad_all(diag_area_, 0, 0);
    lv_obj_set_style_pad_row(diag_area_, 6, 0);
    lv_obj_set_flex_flow(diag_area_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(diag_area_, false);

    // ---- lengths card --------------------------------------------------
    lv_obj_t* length_card = make_card(result_container_);
    make_card_title(length_card, "Laengen (experimentell)");
    length_area_ = lv_obj_create(length_card);
    lv_obj_set_width(length_area_, LV_PCT(100));
    lv_obj_set_height(length_area_, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(length_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(length_area_, 0, 0);
    lv_obj_set_style_pad_all(length_area_, 0, 0);
    lv_obj_set_style_pad_row(length_area_, 4, 0);
    lv_obj_set_flex_flow(length_area_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(length_area_, false);
    make_label(length_card,
               "Schaetzung ueber die Leitungskapazitaet; +/-20-30 %. Nur bei sauberer "
               "Verdrahtung.",
               &lv_font_montserrat_14, kMuted);

    // ---- raw data card -------------------------------------------------
    lv_obj_t* raw_card = make_card(result_container_);
    make_card_title(raw_card, "Rohdaten");
    raw_label_ = make_label(raw_card, "", &lv_font_montserrat_14, kMuted);

    // ---- calibration button -------------------------------------------
    lv_obj_t* cal_btn = lv_button_create(scroll_);
    lv_obj_set_size(cal_btn, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(cal_btn, kSurface, 0);
    lv_obj_set_style_bg_opa(cal_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cal_btn, 10, 0);
    lv_obj_set_style_border_width(cal_btn, 1, 0);
    lv_obj_set_style_border_color(cal_btn, kHairline, 0);
    lv_obj_set_style_shadow_width(cal_btn, 0, 0);
    lv_obj_add_event_cb(cal_btn, calibration_button_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* cal_label = lv_label_create(cal_btn);
    lv_label_set_text(cal_label, "Touch kalibrieren");
    lv_obj_set_style_text_font(cal_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cal_label, kMuted, 0);
    lv_obj_center(cal_label);

    // ---- footer note ---------------------------------------------------
    make_label(scroll_,
               "T568A und T568B sind elektrisch identisch. Nur spannungsfreie Kabel testen.",
               &lv_font_montserrat_14, kMuted);
    status_dirty_ = true;
}

void DisplayUi::update_status() {
    const bool busy = testing_ || runner_.busy();
    const bool paired = link_.paired();
    const int rssi = static_cast<int>(link_.rssi());

    if (!status_dirty_ && busy == last_busy_ && paired == last_paired_ &&
        rssi == last_rssi_ && last_button_disabled_ == (busy || !paired)) {
        return;
    }
    status_dirty_ = false;
    last_busy_ = busy;
    last_paired_ = paired;
    last_rssi_ = rssi;
    last_button_disabled_ = busy || !paired;

    lv_obj_set_style_bg_color(status_dot_, busy ? kWarning : (paired ? kGood : kCritical), 0);

    char text[48];
    if (busy) {
        std::snprintf(text, sizeof(text), "Test laeuft ...");
    } else if (paired) {
        std::snprintf(text, sizeof(text), "Verbunden / %d dBm", rssi);
    } else {
        std::snprintf(text, sizeof(text), "Slave getrennt");
    }
    lv_label_set_text(status_label_, text);

    if (last_button_disabled_) {
        lv_obj_add_state(test_btn_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(test_btn_, LV_STATE_DISABLED);
    }
    lv_label_set_text(test_btn_label_, busy ? "Test laeuft ..." : "Kabel testen");
}

void DisplayUi::refresh_result() {
    TestResult fresh;
    std::uint32_t generation = 0;
    if (!runner_.last_result(fresh, generation) || generation == result_generation_) {
        return;
    }
    result_generation_ = generation;
    apply_result(fresh);
}

void DisplayUi::run_requested_test() {
    if (!testing_) {
        return;
    }

    hide_error();
    update_status();
    lv_refr_now(display_);

    TestResult fresh;
    const esp_err_t err = runner_.run(fresh);
    testing_ = false;

    if (err == ESP_OK) {
        TestResult cached;
        std::uint32_t generation = 0;
        if (runner_.last_result(cached, generation)) {
            result_generation_ = generation;
            apply_result(cached);
        } else {
            apply_result(fresh);
        }
    } else {
        char message[80];
        if (err == ESP_ERR_INVALID_STATE) {
            std::snprintf(message, sizeof(message), "%s",
                          link_.paired() ? "Test laeuft bereits" : "Slave nicht verbunden");
        } else {
            std::snprintf(message, sizeof(message), "Messung fehlgeschlagen (%s)",
                          esp_err_to_name(err));
        }
        show_error(message);
    }
    update_status();
}

void DisplayUi::test_button_cb(lv_event_t* event) {
    auto* self = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (self == nullptr || self->testing_ || self->runner_.busy() || !self->link_.paired()) {
        return;
    }
    self->testing_ = true;
    self->test_requested_ = true;
    self->hide_error();
    self->update_status();
}

void DisplayUi::apply_result(const TestResult& result) {
    result_ = result;
    has_result_ = true;
    hide_error();

    const cable_test::CableKind kind = result.analysis.kind;
    const lv_color_t color = kind_color(kind);
    lv_obj_set_style_bg_color(verdict_bar_, color, 0);
    lv_obj_set_style_text_color(verdict_icon_label_, color, 0);
    lv_label_set_text(verdict_icon_label_, kind_icon(kind));
    lv_label_set_text(verdict_kind_, report::kind_label_de(kind));

    const std::size_t shorts = count_shorts(result.analysis);
    const std::size_t opens = count_opens(result.analysis);
    char detail[160];
    switch (kind) {
    case cable_test::CableKind::Straight:
        std::snprintf(detail, sizeof(detail), "1:1-Belegung, alle Adern verbunden.");
        break;
    case cable_test::CableKind::Crossover:
        std::snprintf(detail, sizeof(detail), "Paartausch erkannt.");
        break;
    case cable_test::CableKind::Custom:
        std::snprintf(detail, sizeof(detail),
                      "Abweichende Belegung ohne Kurzschluss/Unterbrechung.");
        break;
    case cable_test::CableKind::Faulty:
        std::snprintf(detail, sizeof(detail), "%zu Kurzschluesse, %zu Unterbrechungen.",
                      shorts, opens);
        break;
    default:
        std::snprintf(detail, sizeof(detail), "Keine Auswertung.");
        break;
    }
    if (result.analysis.idle_activity) {
        std::size_t used = std::strlen(detail);
        std::snprintf(detail + used, sizeof(detail) - used, " Fremdspannung erkannt.");
    }
    lv_label_set_text(verdict_detail_, detail);

    rebuild_wiring();
    rebuild_matrix();
    rebuild_diagnostics();
    rebuild_lengths();
    rebuild_raw();

    lv_obj_set_hidden(result_container_, false);
    lv_obj_update_layout(scroll_);
    lv_obj_scroll_to_view(verdict_card_, LV_ANIM_ON);
}

void DisplayUi::show_error(const char* message) {
    lv_label_set_text(error_label_, message);
    lv_obj_set_hidden(error_label_, false);
    lv_obj_update_layout(scroll_);
    lv_obj_scroll_to_view(hero_card_, LV_ANIM_ON);
}

void DisplayUi::hide_error() {
    lv_label_set_text(error_label_, "");
    lv_obj_set_hidden(error_label_, true);
}

void DisplayUi::rebuild_wiring() {
    lv_obj_clean(wiring_area_);
    int slot = 0;

    for (std::size_t driven = 0; driven < cable_test::kLineCount; ++driven) {
        const cable_test::LineMask remote = result_.data.rounds[driven].remote_mask;
        const cable_test::LineMask local =
            static_cast<cable_test::LineMask>(result_.data.rounds[driven].local_mask &
                                              ~cable_test::line_bit(driven));
        const int y1 = kWireTop + static_cast<int>(driven) * kWireStep;
        const std::size_t targets = cable_test::popcount8(remote);
        const bool shorted = targets > 1 || local != 0;

        if (targets == 0) {
            if (slot < kLinePointSlots) {
                auto& points = line_points_[static_cast<std::size_t>(slot++)];
                points[0] = {static_cast<lv_value_precise_t>(kWireLeft + kPinRadius),
                             static_cast<lv_value_precise_t>(y1)};
                points[1] = {static_cast<lv_value_precise_t>(kWireLeft + 62),
                             static_cast<lv_value_precise_t>(y1)};
                lv_obj_t* line = lv_line_create(wiring_area_);
                lv_obj_set_size(line, LV_PCT(100), LV_PCT(100));
                lv_obj_set_pos(line, 0, 0);
                lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(line, 0, 0);
                lv_obj_set_style_pad_all(line, 0, 0);
                lv_obj_set_style_line_width(line, 2, 0);
                lv_obj_set_style_line_color(line, kCritical, 0);
                lv_obj_set_style_line_rounded(line, true, 0);
                lv_obj_set_style_line_dash_width(line, 4, 0);
                lv_obj_set_style_line_dash_gap(line, 3, 0);
                lv_line_set_points(line, points.data(), 2);

                lv_obj_t* open_mark = lv_label_create(wiring_area_);
                lv_label_set_text(open_mark, "X");
                lv_obj_set_style_text_font(open_mark, kFont20, 0);
                lv_obj_set_style_text_color(open_mark, kCritical, 0);
                lv_obj_set_pos(open_mark, kWireLeft + 68, y1 - 12);
            }
            continue;
        }

        for (std::size_t target = 0; target < cable_test::kLineCount; ++target) {
            if (!cable_test::has_line(remote, target) || slot >= kLinePointSlots) {
                continue;
            }
            const int y2 = kWireTop + static_cast<int>(target) * kWireStep;
            auto& points = line_points_[static_cast<std::size_t>(slot++)];
            points[0] = {static_cast<lv_value_precise_t>(kWireLeft + kPinRadius),
                         static_cast<lv_value_precise_t>(y1)};
            points[1] = {static_cast<lv_value_precise_t>(kWireRight - kPinRadius),
                         static_cast<lv_value_precise_t>(y2)};

            lv_obj_t* line = lv_line_create(wiring_area_);
            lv_obj_set_size(line, LV_PCT(100), LV_PCT(100));
            lv_obj_set_pos(line, 0, 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(line, 0, 0);
            lv_obj_set_style_pad_all(line, 0, 0);
            lv_obj_set_style_line_width(line, shorted ? 3 : 2, 0);
            lv_obj_set_style_line_color(line, shorted ? kWarning : kAccent, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            lv_line_set_points(line, points.data(), 2);
        }
    }

    // Pins
    for (int line = 0; line < static_cast<int>(cable_test::kLineCount); ++line) {
        const int y = kWireTop + line * kWireStep;
        const int xs[2] = {kWireLeft, kWireRight};
        for (int x : xs) {
            lv_obj_t* pin = lv_obj_create(wiring_area_);
            lv_obj_set_size(pin, 2 * kPinRadius, 2 * kPinRadius);
            lv_obj_set_pos(pin, x - kPinRadius, y - kPinRadius);
            lv_obj_set_style_bg_color(pin, kSurface2, 0);
            lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(pin, kPinRadius, 0);
            lv_obj_set_style_border_width(pin, 1, 0);
            lv_obj_set_style_border_color(pin, kHairline, 0);
            lv_obj_set_scrollable(pin, false);

            lv_obj_t* label = lv_label_create(pin);
            lv_label_set_text_fmt(label, "%d", line + 1);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(label, kInk, 0);
            lv_obj_center(label);
        }
    }

    // Local-short rings around the A pins
    for (std::size_t driven = 0; driven < cable_test::kLineCount; ++driven) {
        const cable_test::LineMask local =
            static_cast<cable_test::LineMask>(result_.data.rounds[driven].local_mask &
                                              ~cable_test::line_bit(driven));
        if (local == 0) {
            continue;
        }
        const int y = kWireTop + static_cast<int>(driven) * kWireStep;
        lv_obj_t* ring = lv_obj_create(wiring_area_);
        lv_obj_set_size(ring, 2 * (kPinRadius + 4), 2 * (kPinRadius + 4));
        lv_obj_set_pos(ring, kWireLeft - kPinRadius - 4, y - kPinRadius - 4);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, kWarning, 0);
        lv_obj_set_style_radius(ring, kPinRadius + 4, 0);
        lv_obj_set_scrollable(ring, false);
    }

    lv_obj_t* label_a = lv_label_create(wiring_area_);
    lv_label_set_text(label_a, "A");
    lv_obj_set_style_text_font(label_a, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_a, kMuted, 0);
    lv_obj_set_pos(label_a, kWireLeft - 5, 0);

    lv_obj_t* label_b = lv_label_create(wiring_area_);
    lv_label_set_text(label_b, "B");
    lv_obj_set_style_text_font(label_b, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_b, kMuted, 0);
    lv_obj_set_pos(label_b, kWireRight - 5, 0);
}

void DisplayUi::rebuild_matrix() {
    lv_obj_clean(matrix_area_);

    static const int32_t col_dsc[] = {36, 24, 24, 24, 24, 24, 24, 24, 24,
                                      LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {20, 24, 24, 24, 24, 24, 24, 24, 24,
                                      LV_GRID_TEMPLATE_LAST};

    lv_obj_t* grid = lv_obj_create(matrix_area_);
    lv_obj_set_size(grid, 36 + 8 * 24, 20 + 8 * 24);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_radius(grid, 0, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_center(grid);

    for (int column = 0; column < static_cast<int>(cable_test::kLineCount); ++column) {
        lv_obj_t* header = lv_label_create(grid);
        lv_label_set_text_fmt(header, "B%d", column + 1);
        lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(header, kMuted, 0);
        lv_obj_set_grid_cell(header, LV_GRID_ALIGN_CENTER, column + 1, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
    }

    for (int row = 0; row < static_cast<int>(cable_test::kLineCount); ++row) {
        const cable_test::LineMask remote = result_.data.rounds[row].remote_mask;
        const cable_test::LineMask local =
            static_cast<cable_test::LineMask>(result_.data.rounds[row].local_mask &
                                              ~cable_test::line_bit(
                                                  static_cast<std::size_t>(row)));
        const std::size_t targets = cable_test::popcount8(remote);

        char row_text[8];
        lv_color_t row_color = kInk2;
        if (targets == 0) {
            std::snprintf(row_text, sizeof(row_text), "A%dX", row + 1);
            row_color = kCritical;
        } else if (targets > 1 || local != 0) {
            std::snprintf(row_text, sizeof(row_text), "A%d!", row + 1);
            row_color = kWarning;
        } else {
            std::snprintf(row_text, sizeof(row_text), "A%d", row + 1);
        }

        lv_obj_t* header = lv_label_create(grid);
        lv_label_set_text(header, row_text);
        lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(header, row_color, 0);
        lv_obj_set_grid_cell(header, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER,
                             row + 1, 1);

        for (int column = 0; column < static_cast<int>(cable_test::kLineCount); ++column) {
            if (!cable_test::has_line(remote, static_cast<std::size_t>(column))) {
                continue;
            }
            lv_obj_t* dot = lv_obj_create(grid);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_radius(dot, 5, 0);
            lv_obj_set_style_bg_color(dot, (targets > 1 || local != 0) ? kWarning : kAccent,
                                      0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_scrollable(dot, false);
            lv_obj_set_grid_cell(dot, LV_GRID_ALIGN_CENTER, column + 1, 1,
                                 LV_GRID_ALIGN_CENTER, row + 1, 1);
        }
    }
}

void DisplayUi::add_diag_row(lv_obj_t* parent, const char* icon, lv_color_t icon_color,
                             const char* name, const char* value, lv_color_t value_color) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* icon_label = lv_label_create(row);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_width(icon_label, 24);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(icon_label, icon_color, 0);

    lv_obj_t* name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, 108);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name_label, kMuted, 0);

    lv_obj_t* value_label = lv_label_create(row);
    lv_label_set_text(value_label, value);
    lv_obj_set_flex_grow(value_label, 1);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value_label, value_color, 0);
}

void DisplayUi::rebuild_diagnostics() {
    lv_obj_clean(diag_area_);

    const std::size_t shorts = count_shorts(result_.analysis);
    const std::size_t opens = count_opens(result_.analysis);
    const std::string shorts_text = short_list(result_.analysis);
    const std::string opens_text = open_list(result_.analysis);

    add_diag_row(diag_area_, kind_icon(result_.analysis.kind),
                 kind_color(result_.analysis.kind), "Kabeltyp",
                 report::kind_label_de(result_.analysis.kind),
                 kind_color(result_.analysis.kind));
    add_diag_row(diag_area_, shorts == 0 ? "OK" : "!", shorts == 0 ? kGood : kWarning,
                 "Kurzschluesse", shorts_text.c_str(),
                 shorts == 0 ? kGood : kWarning);
    add_diag_row(diag_area_, opens == 0 ? "OK" : "X", opens == 0 ? kGood : kCritical,
                 "Unterbrechungen", opens_text.c_str(),
                 opens == 0 ? kGood : kCritical);
    add_diag_row(diag_area_, result_.analysis.idle_activity ? "!" : "OK",
                 result_.analysis.idle_activity ? kCritical : kGood, "Fremdspannung",
                 result_.analysis.idle_activity ? "erkannt" : "keine",
                 result_.analysis.idle_activity ? kCritical : kGood);
}

void DisplayUi::rebuild_lengths() {
    lv_obj_clean(length_area_);

    static constexpr std::array<std::array<std::size_t, 2>, 4> pairs = {{
        {0, 1}, {2, 5}, {3, 4}, {6, 7}}};

    bool any = false;
    for (const auto& pair : pairs) {
        const auto a = result_.data.length_m[pair[0]];
        const auto b = result_.data.length_m[pair[1]];
        if (!a && !b) {
            continue;
        }
        any = true;
        const double meters = a && b ? (*a + *b) / 2.0 : (a ? *a : *b);

        lv_obj_t* row = lv_obj_create(length_area_);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollable(row, false);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text_fmt(name, "Paar %zu-%zu", pair[0] + 1, pair[1] + 1);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, kMuted, 0);

        lv_obj_t* value = lv_label_create(row);
        lv_label_set_text_fmt(value, "%.1f m", meters);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(value, kInk, 0);
    }

    if (!any) {
        lv_obj_t* empty = lv_label_create(length_area_);
        lv_label_set_text(empty, "Keine Messwerte");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, kMuted, 0);
    }
}

void DisplayUi::rebuild_raw() {
    char line[64];
    std::string text;

    std::snprintf(line, sizeof(line), "Idle A 0x%02X / B 0x%02X\n",
                  static_cast<unsigned>(result_.data.idle_local_mask),
                  static_cast<unsigned>(result_.data.idle_remote_mask));
    text += line;

    for (std::size_t i = 0; i < cable_test::kLineCount; ++i) {
        std::snprintf(line, sizeof(line), "%zu: A 0x%02X / B 0x%02X\n", i + 1,
                      static_cast<unsigned>(result_.data.rounds[i].local_mask),
                      static_cast<unsigned>(result_.data.rounds[i].remote_mask));
        text += line;
    }
    lv_label_set_text(raw_label_, text.c_str());
}

void DisplayUi::flush_cb(lv_display_t* display, const lv_area_t* area,
                         std::uint8_t* px_map) {
    auto* self = static_cast<DisplayUi*>(lv_display_get_user_data(display));
    if (self != nullptr) {
        self->flush(area, px_map);
    }
    lv_display_flush_ready(display);
}

void DisplayUi::flush(const lv_area_t* area, std::uint8_t* px_map) {
    if (area == nullptr || px_map == nullptr) {
        return;
    }
    const int width = static_cast<int>(lv_area_get_width(area));
    const int height = static_cast<int>(lv_area_get_height(area));
    if (width <= 0 || height <= 0 ||
        width * height > kFlushWidth * kFlushHeight) {
        return;
    }

    const auto* source = reinterpret_cast<const std::uint16_t*>(px_map);
    std::uint8_t* target = flush_buffer_.data();
    for (int i = 0; i < width * height; ++i) {
        const std::uint16_t color = source[i];
        target[static_cast<std::size_t>(i) * 3 + 0] =
            static_cast<std::uint8_t>(((color & 0xF800) >> 8) | ((color & 0x8000) >> 13));
        target[static_cast<std::size_t>(i) * 3 + 1] =
            static_cast<std::uint8_t>((color & 0x07E0) >> 3);
        target[static_cast<std::size_t>(i) * 3 + 2] =
            static_cast<std::uint8_t>(((color & 0x001F) << 3) | ((color & 0x0010) >> 2));
    }

    panel_.draw_rgb666(area->x1, area->y1, width, height, target);
}

void DisplayUi::touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    auto* self = static_cast<DisplayUi*>(lv_indev_get_user_data(indev));
    if (self != nullptr) {
        self->read_touch(data);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void DisplayUi::read_touch(lv_indev_data_t* data) {
    std::uint16_t raw_x = 0;
    std::uint16_t raw_y = 0;
    int pressure = 0;
    if (!panel_.read_touch(raw_x, raw_y, pressure)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    const int x = map_screen_x(raw_x, raw_y);
    const int y = map_screen_y(raw_x, raw_y);

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;

    const std::uint32_t now = now_ms();
    if (now - last_touch_log_ms_ >= 1000) {
        last_touch_log_ms_ = now;
        ESP_LOGI(kTag, "Touch raw x=%u y=%u z=%d -> %d,%d", raw_x, raw_y, pressure, x,
                 y);
    }
}

bool DisplayUi::load_calibration() {
    nvs_handle_t handle{};
    if (nvs_open("touch_cal", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    TouchCalibration cal{};
    std::size_t size = sizeof(cal);
    const esp_err_t err = nvs_get_blob(handle, "cal", &cal, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(cal) || cal.magic != kCalibrationMagic) {
        return false;
    }
    calibration_ = cal;
    ESP_LOGI(kTag, "Touch-Kalibrierung aus NVS geladen");
    return true;
}

void DisplayUi::save_calibration() {
    calibration_.magic = kCalibrationMagic;
    nvs_handle_t handle{};
    esp_err_t err = nvs_open("touch_cal", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Kalibrierung: nvs_open: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, "cal", &calibration_, sizeof(calibration_));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Kalibrierung speichern: %s", esp_err_to_name(err));
    }
}

void DisplayUi::apply_default_calibration() {
    calibration_ = {};
    calibration_.magic = kCalibrationMagic;
    calibration_.a = static_cast<float>(display::Ili9488::kWidth - 1) /
                     static_cast<float>(kRawXMax - kRawXMin);
    calibration_.b = 0.0F;
    calibration_.c = -calibration_.a * static_cast<float>(kRawXMin);
    calibration_.d = 0.0F;
    calibration_.e = static_cast<float>(display::Ili9488::kHeight - 1) /
                     static_cast<float>(kRawYMax - kRawYMin);
    calibration_.f = -calibration_.e * static_cast<float>(kRawYMin);
    ESP_LOGW(kTag, "Touch-Standardkalibrierung aktiv");
}

void DisplayUi::show_calibration_target(int index) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_layout(screen, LV_LAYOUT_NONE);
    lv_obj_set_style_bg_color(screen, kPage, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Touch-Kalibrierung");
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(title, kFont20, 0);
    lv_obj_set_style_text_color(title, kInk, 0);
    lv_obj_set_pos(title, 0, 5);

    lv_obj_t* hint = lv_label_create(screen);
    lv_label_set_text_fmt(hint, "Tippe auf das Kreuz: %s", kCalTargetName[index]);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, kInk2, 0);
    lv_obj_set_pos(hint, 0, 32);

    lv_obj_t* target = lv_obj_create(screen);
    lv_obj_set_size(target, 52, 52);
    lv_obj_set_pos(target, kCalTargetX[index] - 26, kCalTargetY[index] - 26);
    lv_obj_set_style_bg_opa(target, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(target, 2, 0);
    lv_obj_set_style_border_color(target, kAccent, 0);
    lv_obj_set_style_radius(target, 26, 0);
    lv_obj_set_scrollable(target, false);

    lv_obj_t* cross = lv_label_create(target);
    lv_label_set_text(cross, "+");
    lv_obj_set_style_text_font(cross, kFont28, 0);
    lv_obj_set_style_text_color(cross, kAccent, 0);
    lv_obj_center(cross);

    lv_obj_t* step = lv_label_create(screen);
    lv_label_set_text_fmt(step, "Schritt %d von 4", index + 1);
    lv_obj_set_width(step, LV_PCT(100));
    lv_obj_set_style_text_align(step, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(step, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(step, kMuted, 0);
    lv_obj_set_pos(step, 0, 438);

    lv_obj_t* release = lv_label_create(screen);
    lv_label_set_text(release, "Nach dem Tippen loslassen.");
    lv_obj_set_width(release, LV_PCT(100));
    lv_obj_set_style_text_align(release, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(release, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(release, kMuted, 0);
    lv_obj_set_pos(release, 0, 456);

    lv_refr_now(display_);
}

bool DisplayUi::capture_calibration_point(int index, std::uint16_t& raw_x,
                                          std::uint16_t& raw_y) {
    show_calibration_target(index);
    const std::uint32_t start = now_ms();
    std::uint32_t sum_x = 0;
    std::uint32_t sum_y = 0;
    int samples = 0;
    bool touched = false;

    while (now_ms() - start < 10000) {
        lv_timer_handler();
        std::uint16_t rx = 0;
        std::uint16_t ry = 0;
        int pressure = 0;
        if (panel_.read_touch(rx, ry, pressure)) {
            sum_x += rx;
            sum_y += ry;
            if (++samples >= 3) {
                raw_x = static_cast<std::uint16_t>(sum_x / 3);
                raw_y = static_cast<std::uint16_t>(sum_y / 3);
                touched = true;
                break;
            }
        } else {
            samples = 0;
            sum_x = 0;
            sum_y = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (!touched) {
        ESP_LOGW(kTag, "Kalibrierpunkt %d: keine Beruehrung", index + 1);
        return false;
    }

    const std::uint32_t release_start = now_ms();
    while (now_ms() - release_start < 3000) {
        lv_timer_handler();
        std::uint16_t rx = 0;
        std::uint16_t ry = 0;
        int pressure = 0;
        if (!panel_.read_touch(rx, ry, pressure)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI(kTag, "Kalibrierpunkt %d: raw x=%u y=%u", index + 1, raw_x, raw_y);
    return true;
}

bool DisplayUi::run_calibration() {
    lv_indev_enable(indev_, false);
    struct IndevGuard {
        lv_indev_t* indev;
        ~IndevGuard() { lv_indev_enable(indev, true); }
    } guard{indev_};

    std::uint16_t raw[4][2] = {};
    for (int i = 0; i < 4; ++i) {
        if (!capture_calibration_point(i, raw[i][0], raw[i][1])) {
            return false;
        }
    }

    const double x0 = raw[0][0];
    const double y0 = raw[0][1];
    const double x1 = raw[1][0];
    const double y1 = raw[1][1];
    const double x2 = raw[2][0];
    const double y2 = raw[2][1];
    const double det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(det) < 1000.0) {
        ESP_LOGW(kTag, "Kalibrierung: Punkte kollinear (det=%.1f)", det);
        return false;
    }

    const double sx0 = kCalTargetX[0];
    const double sy0 = kCalTargetY[0];
    const double sx1 = kCalTargetX[1];
    const double sy1 = kCalTargetY[1];
    const double sx2 = kCalTargetX[2];
    const double sy2 = kCalTargetY[2];

    const double a = ((sx1 - sx0) * (y2 - y0) - (sx2 - sx0) * (y1 - y0)) / det;
    const double b = ((x1 - x0) * (sx2 - sx0) - (x2 - x0) * (sx1 - sx0)) / det;
    const double c = sx0 - a * x0 - b * y0;
    const double d = ((sy1 - sy0) * (y2 - y0) - (sy2 - sy0) * (y1 - y0)) / det;
    const double e = ((x1 - x0) * (sy2 - sy0) - (x2 - x0) * (sy1 - sy0)) / det;
    const double f = sy0 - d * x0 - e * y0;

    calibration_ = {};
    calibration_.magic = kCalibrationMagic;
    calibration_.a = static_cast<float>(a);
    calibration_.b = static_cast<float>(b);
    calibration_.c = static_cast<float>(c);
    calibration_.d = static_cast<float>(d);
    calibration_.e = static_cast<float>(e);
    calibration_.f = static_cast<float>(f);

    const int vx = static_cast<int>(std::lround(a * raw[3][0] + b * raw[3][1] + c));
    const int vy = static_cast<int>(std::lround(d * raw[3][0] + e * raw[3][1] + f));
    ESP_LOGI(kTag, "Kalibrierung: Kontrollpunkt %d,%d (Soll %d,%d)", vx, vy,
             kCalTargetX[3], kCalTargetY[3]);
    if (std::abs(vx - kCalTargetX[3]) > 100 || std::abs(vy - kCalTargetY[3]) > 100) {
        ESP_LOGW(kTag, "Kalibrierung: Kontrollpunkt weicht stark ab");
    }

    save_calibration();
    ESP_LOGI(kTag, "Touch-Kalibrierung gespeichert");
    return true;
}

int DisplayUi::map_screen_x(int raw_x, int raw_y) const {
    const float value = calibration_.a * static_cast<float>(raw_x) +
                        calibration_.b * static_cast<float>(raw_y) + calibration_.c;
    const int x = static_cast<int>(std::lround(value));
    return std::max(0, std::min(display::Ili9488::kWidth - 1, x));
}

int DisplayUi::map_screen_y(int raw_x, int raw_y) const {
    const float value = calibration_.d * static_cast<float>(raw_x) +
                        calibration_.e * static_cast<float>(raw_y) + calibration_.f;
    const int y = static_cast<int>(std::lround(value));
    return std::max(0, std::min(display::Ili9488::kHeight - 1, y));
}

void DisplayUi::calibration_button_cb(lv_event_t* event) {
    auto* self = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->calibration_requested_ = true;
    }
}

lv_color_t DisplayUi::kind_color(cable_test::CableKind kind) {
    switch (kind) {
    case cable_test::CableKind::Straight:
    case cable_test::CableKind::Crossover:
        return kGood;
    case cable_test::CableKind::Custom:
        return kWarning;
    case cable_test::CableKind::Faulty:
        return kCritical;
    default:
        return kMuted;
    }
}

const char* DisplayUi::kind_icon(cable_test::CableKind kind) {
    switch (kind) {
    case cable_test::CableKind::Straight:
    case cable_test::CableKind::Crossover:
        return "OK";
    case cable_test::CableKind::Custom:
        return "!";
    case cable_test::CableKind::Faulty:
        return "X";
    default:
        return "?";
    }
}

} // namespace cable_tester

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
