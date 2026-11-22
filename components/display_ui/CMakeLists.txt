idf_component_register(
    SRCS "src/ili9488.cpp" "src/display_ui.cpp"
    INCLUDE_DIRS "include" "src"
    REQUIRES
        cable_test
        cable_link
        rj45_port
        test_runner
        length_cal
        esp_driver_spi
        esp_driver_gpio
        esp_timer
        nvs_flash
        lvgl__lvgl
)
