idf_component_register(
    SRCS "src/web_ui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES test_runner cable_link report cable_test length_cal esp_http_server
    EMBED_TXTFILES "www/index.html"
)
