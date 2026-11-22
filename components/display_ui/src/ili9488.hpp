#pragma once

#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include <cstddef>
#include <cstdint>

#include "driver/spi_master.h"
#include "esp_err.h"

namespace cable_tester::display {

// Minimaler ILI9488-Treiber für den SPI2-Bus des ESP32-S3.
// Das Panel läuft im 18-Bit-Modus (RGB666), weil der ILI9488 über SPI
// kein 16-Bit-RGB565 zuverlässig verarbeitet.
class Ili9488 {
public:
    static constexpr int kWidth = 480;
    static constexpr int kHeight = 320;
    static constexpr std::size_t kLineBytes = static_cast<std::size_t>(kWidth) * 16 * 3;

    Ili9488() = default;
    Ili9488(const Ili9488&) = delete;
    Ili9488& operator=(const Ili9488&) = delete;

    esp_err_t init();

    // data: RGB666, 3 Byte pro Pixel, Zeilen von links nach rechts.
    void draw_rgb666(int x, int y, int w, int h, const std::uint8_t* data);
    void fill_rect(int x, int y, int w, int h, std::uint16_t rgb565);
    void backlight(bool on);

private:
    esp_err_t reset();
    esp_err_t cmd(std::uint8_t command);
    esp_err_t write_command(std::uint8_t command, const std::uint8_t* params,
                            std::size_t param_len);
    esp_err_t transfer(const std::uint8_t* tx, std::size_t len, bool keep_cs, bool dc);
    esp_err_t set_window(int x, int y, int w, int h);

    spi_device_handle_t dev_{nullptr};
    bool initialized_{false};
};

} // namespace cable_tester::display

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
