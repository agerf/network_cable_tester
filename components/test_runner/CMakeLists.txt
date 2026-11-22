idf_component_register(
    SRCS "src/test_runner.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cable_test rj45_port cable_link
)
