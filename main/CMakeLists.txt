idf_component_register(
    SRCS "wifi_config.cpp" "main.cpp" "wifi.cpp" "console_ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        cable_test
        rj45_port
        cable_link
        test_runner
        length_cal
        report
        web_ui
        display_ui
        console
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
)

if(CONFIG_CABLE_TESTER_ROLE_MASTER)
    target_sources(${COMPONENT_LIB} PRIVATE "app_master.cpp")
else()
    target_sources(${COMPONENT_LIB} PRIVATE "app_slave.cpp")
endif()
