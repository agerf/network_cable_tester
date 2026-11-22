#pragma once

#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include <cstddef>
#include <cstdint>

#include "driver/spi_master.h"
#include "esp_err.h"

namespace cable_tester::display {

// Minimaler ILI9488-Treiber für den SPI2-Bus des ESP32-S3, Porträt 320x480.
// Das Panel läuft im 18-Bit-Modus (RGB666), weil der ILI9488 über SPI
// kein 16-Bit-RGB565 zuverlässig verarbeitet.
class Ili9488 {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 480;
    // Maximaler RGB666-Transfer eines LVGL-Flushs (320 x 24 Pixel).
    static constexpr std::size_t kMaxTransferBytes =
        static_cast<std::size_t>(kWidth) * 24 * 3;

    // Druck-Schwelle des XPT2046: z = z1 + 0xFFF - z2.
    static constexpr int kTouchThreshold = 200;

    Ili9488() = default;
    Ili9488(const Ili9488&) = delete;
    Ili9488& operator=(const Ili9488&) = delete;

    esp_err_t init();

    // data: RGB666, 3 Byte pro Pixel, Zeilen von links nach rechts.
    // Startet den Datentransfer per DMA und kehrt sofort zurück. Der Puffer
    // muss bis wait_draw() gültig bleiben. Liefert ESP_OK, wenn der Transfer
    // läuft und später mit wait_draw() abgeschlossen werden muss.
    esp_err_t draw_rgb666_async(int x, int y, int w, int h, const std::uint8_t* data);
    // Wartet auf den mit draw_rgb666_async() gestarteten Transfer.
    esp_err_t wait_draw();
    void fill_rect(int x, int y, int w, int h, std::uint16_t rgb565);
    void backlight(bool on);

    // Liest den XPT2046 über den gemeinsamen SPI-Bus. Liefert true, wenn
    // eine Berührung erkannt wurde; raw_x/raw_y sind dann gültig.
    bool read_touch(std::uint16_t& raw_x, std::uint16_t& raw_y, int& pressure);

private:
    esp_err_t reset();
    esp_err_t cmd(std::uint8_t command);
    esp_err_t write_command(std::uint8_t command, const std::uint8_t* params,
                            std::size_t param_len);
    esp_err_t transfer(const std::uint8_t* tx, std::size_t len, bool keep_cs, bool dc);
    esp_err_t set_window(int x, int y, int w, int h);
    std::uint16_t read_touch_adc(std::uint8_t command);

    spi_device_handle_t dev_{nullptr};
    spi_device_handle_t touch_dev_{nullptr};
    spi_transaction_t draw_transaction_{};
    bool draw_pending_{false};
    bool initialized_{false};
};

} // namespace cable_tester::display

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
