#include "sdkconfig.h"

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3

#include "ili9488.hpp"

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rj45_port/pin_map.hpp"

namespace cable_tester::display {
namespace {

// Init-Sequenz (Gamma/Power/MADCTL) und die RGB565->RGB666-Umrechnung
// orientieren sich am MIT-lizenzierten atanisoft/esp_lcd_ili9488-Treiber.
constexpr char kTag[] = "ili9488";

// 0x28 = MV (X/Y tauschen, Landscape) + BGR. Bei gespiegelter/verdrehter
// Anzeige hier experimentieren: 0x20 = Landscape/RGB, 0xE8 = 180 Grad.
constexpr std::uint8_t kMadctl[1] = {0x48}; // Porträt, X gespiegelt, BGR
constexpr std::uint8_t kColorMode18Bit[1] = {0x66};

constexpr std::uint8_t kCmdSwReset = 0x01;
constexpr std::uint8_t kCmdSleepOut = 0x11;
constexpr std::uint8_t kCmdDisplayOn = 0x29;
constexpr std::uint8_t kCmdIdleModeOff = 0x38;
constexpr std::uint8_t kCmdColumnAddress = 0x2A;
constexpr std::uint8_t kCmdRowAddress = 0x2B;
constexpr std::uint8_t kCmdMemoryWrite = 0x2C;

constexpr std::uint8_t kGammaPositive[15] = {
    0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
    0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F,
};
constexpr std::uint8_t kGammaNegative[15] = {
    0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
    0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F,
};

constexpr std::uint8_t kPowerControl1[2] = {0x17, 0x15};
constexpr std::uint8_t kPowerControl2[1] = {0x41};
constexpr std::uint8_t kVcomControl[3] = {0x00, 0x12, 0x80};
constexpr std::uint8_t kInterfaceMode[1] = {0x00};
constexpr std::uint8_t kFrameRate[1] = {0xA0};
constexpr std::uint8_t kInversionControl[1] = {0x02};
constexpr std::uint8_t kDisplayFunction[3] = {0x02, 0x02, 0x3B};
constexpr std::uint8_t kEntryMode[1] = {0xC6};
constexpr std::uint8_t kAdjustControl[4] = {0xA9, 0x51, 0x2C, 0x02};

void put_rgb666(std::uint8_t* out, std::uint16_t rgb565) {
    out[0] = static_cast<std::uint8_t>(((rgb565 & 0xF800) >> 8) | ((rgb565 & 0x8000) >> 13));
    out[1] = static_cast<std::uint8_t>((rgb565 & 0x07E0) >> 3);
    out[2] = static_cast<std::uint8_t>(((rgb565 & 0x001F) << 3) | ((rgb565 & 0x0010) >> 2));
}

} // namespace

esp_err_t Ili9488::reset() {
    gpio_set_level(board::display::kRst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(board::display::kRst, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t Ili9488::transfer(const std::uint8_t* tx, std::size_t len, bool keep_cs, bool dc) {
    if (dev_ == nullptr || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_set_level(board::display::kDc, dc ? 1 : 0);

    spi_transaction_t transaction{};
    transaction.length = len * 8;
    transaction.tx_buffer = tx;
    transaction.flags = keep_cs ? SPI_TRANS_CS_KEEP_ACTIVE : 0;
    return spi_device_polling_transmit(dev_, &transaction);
}

esp_err_t Ili9488::cmd(std::uint8_t command) {
    esp_err_t err = spi_device_acquire_bus(dev_, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }
    err = transfer(&command, 1, false, false);
    spi_device_release_bus(dev_);
    return err;
}

esp_err_t Ili9488::write_command(std::uint8_t command, const std::uint8_t* params,
                                 std::size_t param_len) {
    esp_err_t err = spi_device_acquire_bus(dev_, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }
    err = transfer(&command, 1, param_len > 0, false);
    if (err == ESP_OK && param_len > 0) {
        err = transfer(params, param_len, false, true);
    }
    spi_device_release_bus(dev_);
    return err;
}

esp_err_t Ili9488::set_window(int x, int y, int w, int h) {
    const std::uint8_t columns[4] = {
        static_cast<std::uint8_t>(x >> 8),
        static_cast<std::uint8_t>(x & 0xFF),
        static_cast<std::uint8_t>((x + w - 1) >> 8),
        static_cast<std::uint8_t>((x + w - 1) & 0xFF),
    };
    const std::uint8_t rows[4] = {
        static_cast<std::uint8_t>(y >> 8),
        static_cast<std::uint8_t>(y & 0xFF),
        static_cast<std::uint8_t>((y + h - 1) >> 8),
        static_cast<std::uint8_t>((y + h - 1) & 0xFF),
    };

    esp_err_t err = write_command(kCmdColumnAddress, columns, sizeof(columns));
    if (err == ESP_OK) {
        err = write_command(kCmdRowAddress, rows, sizeof(rows));
    }
    return err;
}

esp_err_t Ili9488::init() {
    if (initialized_) {
        return ESP_OK;
    }

    gpio_config_t io{};
    io.pin_bit_mask = (1ULL << static_cast<int>(board::display::kDc)) |
                      (1ULL << static_cast<int>(board::display::kRst)) |
                      (1ULL << static_cast<int>(board::display::kBacklight));
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    // T_IRQ als optionaler, schneller Beruehrungs-Hinweis. Der XPT2046
    // zieht die Leitung im Beruehrungsfall nach LOW.
    gpio_config_t irq_config{};
    irq_config.pin_bit_mask = 1ULL << static_cast<int>(board::display::kTouchIrq);
    irq_config.mode = GPIO_MODE_INPUT;
    irq_config.pull_up_en = GPIO_PULLUP_ENABLE;
    irq_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irq_config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&irq_config) != ESP_OK) {
        ESP_LOGW(kTag, "Touch-IRQ-GPIO konnte nicht konfiguriert werden");
    }

    // P-MOSFET-Backlight: HIGH = aus, LOW = an.
    gpio_set_level(board::display::kBacklight, 1);
    err = reset();
    if (err != ESP_OK) {
        return err;
    }

    spi_bus_config_t bus_config{};
    bus_config.mosi_io_num = board::display::kSpiMosi;
    bus_config.miso_io_num = board::display::kSpiMiso;
    bus_config.sclk_io_num = board::display::kSpiSclk;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = static_cast<int>(kMaxTransferBytes);

    err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t device_config{};
    device_config.clock_speed_hz = 20 * 1000 * 1000;
    device_config.mode = 0;
    device_config.spics_io_num = board::display::kCs;
    device_config.queue_size = 1;

    err = spi_bus_add_device(SPI2_HOST, &device_config, &dev_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "spi_bus_add_device: %s", esp_err_to_name(err));
        return err;
    }

    // XPT2046 als zweites Gerät am selben SPI2-Bus. Der SPI-Treiber
    // verwaltet T_CS; der Touch wird über Druck (Z1/Z2) und X/Y gelesen.
    spi_device_interface_config_t touch_config{};
    touch_config.clock_speed_hz = 2 * 1000 * 1000;
    touch_config.mode = 0;
    touch_config.spics_io_num = board::display::kTouchCs;
    touch_config.queue_size = 1;
    err = spi_bus_add_device(SPI2_HOST, &touch_config, &touch_dev_);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Touch-SPI-Device: %s", esp_err_to_name(err));
        touch_dev_ = nullptr;
    } else {
        // Erste Konvertierung nach dem Power-Up ist verrauscht.
        (void)read_touch_adc(0xD1);
    }

    err = cmd(kCmdSwReset);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    if ((err = write_command(0xE0, kGammaPositive, sizeof(kGammaPositive))) != ESP_OK ||
        (err = write_command(0xE1, kGammaNegative, sizeof(kGammaNegative))) != ESP_OK ||
        (err = write_command(0xC0, kPowerControl1, sizeof(kPowerControl1))) != ESP_OK ||
        (err = write_command(0xC1, kPowerControl2, sizeof(kPowerControl2))) != ESP_OK ||
        (err = write_command(0xC5, kVcomControl, sizeof(kVcomControl))) != ESP_OK ||
        (err = write_command(0x36, kMadctl, sizeof(kMadctl))) != ESP_OK ||
        (err = write_command(0x3A, kColorMode18Bit, sizeof(kColorMode18Bit))) != ESP_OK ||
        (err = write_command(0xB0, kInterfaceMode, sizeof(kInterfaceMode))) != ESP_OK ||
        (err = write_command(0xB1, kFrameRate, sizeof(kFrameRate))) != ESP_OK ||
        (err = write_command(0xB4, kInversionControl, sizeof(kInversionControl))) != ESP_OK ||
        (err = write_command(0xB6, kDisplayFunction, sizeof(kDisplayFunction))) != ESP_OK ||
        (err = write_command(0xB7, kEntryMode, sizeof(kEntryMode))) != ESP_OK ||
        (err = write_command(0xF7, kAdjustControl, sizeof(kAdjustControl))) != ESP_OK) {
        ESP_LOGE(kTag, "Initialisierung fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    if ((err = cmd(kCmdSleepOut)) != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    if ((err = cmd(kCmdIdleModeOff)) != ESP_OK) {
        return err;
    }
    if ((err = cmd(kCmdDisplayOn)) != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    backlight(true);
    initialized_ = true;
    ESP_LOGI(kTag, "ILI9488 initialisiert (%dx%d, SPI2, 20 MHz)", kWidth, kHeight);
    return ESP_OK;
}

void Ili9488::draw_rgb666(int x, int y, int w, int h, const std::uint8_t* data) {
    if (!initialized_ || data == nullptr || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x + w > kWidth || y + h > kHeight) {
        return;
    }
    if (set_window(x, y, w, h) != ESP_OK) {
        return;
    }

    esp_err_t err = spi_device_acquire_bus(dev_, portMAX_DELAY);
    if (err != ESP_OK) {
        return;
    }
    const std::uint8_t command = kCmdMemoryWrite;
    err = transfer(&command, 1, true, false);
    if (err == ESP_OK) {
        err = transfer(data, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3,
                       false, true);
    }
    spi_device_release_bus(dev_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "draw_rgb666: %s", esp_err_to_name(err));
    }
}

void Ili9488::fill_rect(int x, int y, int w, int h, std::uint16_t rgb565) {
    if (!initialized_ || w <= 0 || h <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x + w > kWidth || y + h > kHeight) {
        return;
    }

    static std::uint8_t line[static_cast<std::size_t>(kWidth) * 3];
    for (int i = 0; i < w; ++i) {
        put_rgb666(&line[static_cast<std::size_t>(i) * 3], rgb565);
    }

    if (set_window(x, y, w, h) != ESP_OK) {
        return;
    }

    esp_err_t err = spi_device_acquire_bus(dev_, portMAX_DELAY);
    if (err != ESP_OK) {
        return;
    }
    const std::uint8_t command = kCmdMemoryWrite;
    err = transfer(&command, 1, true, false);
    for (int row = 0; err == ESP_OK && row < h; ++row) {
        err = transfer(line, static_cast<std::size_t>(w) * 3, row + 1 < h, true);
    }
    spi_device_release_bus(dev_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "fill_rect: %s", esp_err_to_name(err));
    }
}

std::uint16_t Ili9488::read_touch_adc(std::uint8_t command) {
    if (touch_dev_ == nullptr) {
        return 0;
    }

    std::uint8_t tx[3] = {command, 0, 0};
    std::uint8_t rx[3] = {0, 0, 0};
    spi_transaction_t transaction{};
    transaction.length = 24;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    if (spi_device_polling_transmit(touch_dev_, &transaction) != ESP_OK) {
        return 0;
    }
    return static_cast<std::uint16_t>((rx[1] << 5) | (rx[2] >> 3));
}

bool Ili9488::read_touch(std::uint16_t& raw_x, std::uint16_t& raw_y, int& pressure) {
    if (touch_dev_ == nullptr) {
        return false;
    }

    // Vor den SPI-Zugriffen abtasten: T_IRQ kann sich waehrend der
    // Konvertierungen aendern, ist aber ein guter schneller Hinweis.
    const bool irq_low = gpio_get_level(board::display::kTouchIrq) == 0;

    // Z1/Z2 mit ADC an; zwei Samples mitteln.
    std::uint32_t z1_sum = 0;
    std::uint32_t z2_sum = 0;
    z1_sum += read_touch_adc(0xB1);
    z2_sum += read_touch_adc(0xC1);
    z1_sum += read_touch_adc(0xB1);
    z2_sum += read_touch_adc(0xC1);
    const std::uint16_t z1 = static_cast<std::uint16_t>(z1_sum / 2);
    const std::uint16_t z2 = static_cast<std::uint16_t>(z2_sum / 2);
    pressure = static_cast<int>(z1) + 0xFFF - static_cast<int>(z2);
    if (pressure < kTouchThreshold && !irq_low) {
        return false;
    }

    raw_x = read_touch_adc(0xD1);
    raw_y = read_touch_adc(0x91);
    return true;
}

void Ili9488::backlight(bool on) {
    // LOW schaltet den P-MOSFET (Q1) durch.
    gpio_set_level(board::display::kBacklight, on ? 0 : 1);
}

} // namespace cable_tester::display

#endif // CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
