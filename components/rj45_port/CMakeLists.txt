idf_component_register(
    SRCS "src/rj45_port.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cable_test esp_driver_gpio esp_timer
)
