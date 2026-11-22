idf_component_register(
    SRCS "src/master_link.cpp" "src/slave_link.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cable_test rj45_port esp_wifi
)
