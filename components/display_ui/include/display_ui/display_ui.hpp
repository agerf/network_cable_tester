#pragma once

#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "cable_link/master_link.hpp"
#include "esp_err.h"
#include "ili9488.hpp"
#include "lvgl.h"
#include "test_runner/test_runner.hpp"

namespace cable_tester {

// Grafische Master-Oberfläche auf dem ILI9488 (Porträt 320x480) mit LVGL.
// Aufbau und Farben orientieren sich an der Web-UI. Touch läuft über den
// XPT2046 am gemeinsamen SPI2-Bus; die Rohwerte werden auf 320x480 gemappt.
class DisplayUi {
public:
    DisplayUi(TestRunner& runner, MasterLink& link, const char* ap_ssid);
    DisplayUi(const DisplayUi&) = delete;
    DisplayUi& operator=(const DisplayUi&) = delete;

    esp_err_t start();

private:
    static constexpr int kFlushWidth = 320;
    static constexpr int kFlushHeight = 24;
    static constexpr int kLinePointSlots = 64;

    // Kalibrierung des XPT2046; bei Bedarf nach den Debug-Logs anpassen.
    static constexpr int kRawXMin = 200;
    static constexpr int kRawXMax = 3900;
    static constexpr int kRawYMin = 200;
    static constexpr int kRawYMax = 3900;
    static constexpr std::uint32_t kCalibrationMagic = 0x5443414C; // "TCAL"

    // Feste Display-Seiten statt eines Scroll-Containers.
    static constexpr int kPageTest = 0;
    static constexpr int kPageResult = 1;
    static constexpr int kPageWiring = 2;
    static constexpr int kPageMatrix = 3;
    static constexpr int kPageCount = 4;

    static void task_entry(void* arg);
    void task_loop();

    esp_err_t init_lvgl();
    void build_ui();
    void build_header(lv_obj_t* screen);
    void build_nav(lv_obj_t* screen);
    void build_test_page(lv_obj_t* page);
    void build_result_page(lv_obj_t* page);
    void build_wiring_page(lv_obj_t* page);
    void build_matrix_page(lv_obj_t* page);
    void show_page(int page);
    void show_matrix_view(bool raw);
    void update_status();
    void refresh_result();
    void run_requested_test();
    void apply_result(const TestResult& result);
    void show_error(const char* message);
    void hide_error();

    void rebuild_wiring();
    void rebuild_matrix();
    void rebuild_diagnostics();
    void rebuild_lengths();
    void rebuild_raw();

    lv_obj_t* make_page(lv_obj_t* parent);
    lv_obj_t* make_card(lv_obj_t* parent);
    lv_obj_t* make_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                         lv_color_t color);
    lv_obj_t* make_card_title(lv_obj_t* parent, const char* text);
    void add_diag_row(lv_obj_t* parent, const char* icon, lv_color_t icon_color,
                      const char* name, const char* value, lv_color_t value_color);

    static void flush_cb(lv_display_t* display, const lv_area_t* area, std::uint8_t* px_map);
    bool flush(const lv_area_t* area, std::uint8_t* px_map);
    static void flush_wait_cb(lv_display_t* display);
    static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data);
    void read_touch(lv_indev_data_t* data);
    static void test_button_cb(lv_event_t* event);
    static void calibration_button_cb(lv_event_t* event);
    static void tab_button_cb(lv_event_t* event);
    static void view_toggle_cb(lv_event_t* event);

    bool load_calibration();
    void save_calibration();
    void apply_default_calibration();
    bool run_calibration();
    bool capture_calibration_point(int index, std::uint16_t& raw_x,
                                   std::uint16_t& raw_y);
    void show_calibration_target(int index);
    int map_screen_x(int raw_x, int raw_y) const;
    int map_screen_y(int raw_x, int raw_y) const;

    static lv_color_t kind_color(cable_test::CableKind kind);
    static const char* kind_icon(cable_test::CableKind kind);

    TestRunner& runner_;
    MasterLink& link_;
    const char* ap_ssid_;
    display::Ili9488 panel_;

    lv_display_t* display_{nullptr};
    lv_indev_t* indev_{nullptr};
    // Zwei Zeichenpuffer: LVGL rendert den naechsten Streifen, waehrend der
    // vorherige per DMA zum Display geht.
    alignas(4) std::array<std::uint16_t, kFlushWidth * kFlushHeight> lv_buffer_a_{};
    alignas(4) std::array<std::uint16_t, kFlushWidth * kFlushHeight> lv_buffer_b_{};
    alignas(4) std::array<std::uint8_t, kFlushWidth * kFlushHeight * 3> flush_buffer_{};

    lv_obj_t* page_host_{nullptr};
    lv_obj_t* nav_{nullptr};
    std::array<lv_obj_t*, kPageCount> pages_{};
    lv_obj_t* hero_card_{nullptr};
    lv_obj_t* status_chip_{nullptr};
    lv_obj_t* status_dot_{nullptr};
    lv_obj_t* status_label_{nullptr};
    lv_obj_t* test_btn_{nullptr};
    lv_obj_t* test_btn_label_{nullptr};
    lv_obj_t* error_label_{nullptr};
    lv_obj_t* cal_btn_{nullptr};
    lv_obj_t* footer_label_{nullptr};
    lv_obj_t* verdict_card_{nullptr};
    lv_obj_t* verdict_bar_{nullptr};
    lv_obj_t* verdict_icon_label_{nullptr};
    lv_obj_t* verdict_kind_{nullptr};
    lv_obj_t* verdict_detail_{nullptr};
    lv_obj_t* wiring_area_{nullptr};
    lv_obj_t* matrix_card_{nullptr};
    lv_obj_t* matrix_area_{nullptr};
    lv_obj_t* raw_card_{nullptr};
    lv_obj_t* raw_label_{nullptr};
    lv_obj_t* diag_area_{nullptr};
    lv_obj_t* length_area_{nullptr};

    struct TabButton {
        DisplayUi* self{nullptr};
        int page{0};
        lv_obj_t* button{nullptr};
        lv_obj_t* label{nullptr};
    };
    struct ViewToggle {
        DisplayUi* self{nullptr};
        bool raw{false};
        lv_obj_t* button{nullptr};
        lv_obj_t* label{nullptr};
    };

    std::array<TabButton, kPageCount> tabs_{};
    ViewToggle matrix_toggle_{};
    ViewToggle raw_toggle_{};
    int active_page_{kPageTest};
    bool matrix_show_raw_{false};

    std::array<std::array<lv_point_precise_t, 2>, kLinePointSlots> line_points_{};

    struct TouchCalibration {
        std::uint32_t magic{0};
        float a{0.0F};
        float b{0.0F};
        float c{0.0F};
        float d{0.0F};
        float e{0.0F};
        float f{0.0F};
    };

    TouchCalibration calibration_{};
    TestResult result_{};
    std::uint32_t result_generation_{0};
    bool has_result_{false};
    bool testing_{false};
    bool test_requested_{false};
    bool calibration_requested_{false};

    bool last_paired_{false};
    bool last_busy_{false};
    bool last_button_disabled_{false};
    bool status_dirty_{true};
    int last_rssi_{0};

    std::uint32_t last_touch_log_ms_{0};
};

} // namespace cable_tester

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
