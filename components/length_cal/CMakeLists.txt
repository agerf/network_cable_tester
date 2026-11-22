idf_component_register(
    SRCS "src/length_cal.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cable_test test_runner nvs_flash
)
