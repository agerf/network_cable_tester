#pragma once

#include <array>

#include "cable_test/model.hpp"
#include "driver/gpio.h"
#include "sdkconfig.h"

namespace cable_tester::board {

// Standard-Mapping: RJ45-Kontakt 1..8 (Index 0..7) -> GPIO.
// Pro Node und Target anpassbar; bitte gegen die eigene Platine prüfen.
//
// Aufbau: Node A = Master = ESP32-S3, Node B = Slave = ESP32-S3.
//
// ESP32-S3 Node A (Master): GPIO 4,5,6,7,8,9,10,11
// ESP32-S3 Node B (Slave):  identisch mit Node A (GPIO 4..11)
//   - beide vermeiden 0/3/45/46 (Strapping), 19/20 (USB), 26..37 (Flash/PSRAM),
//     39..42 (JTAG) und 43/44 (UART0)
// Am Master sind zusätzlich GPIO 12..18 sowie 21/47 für ein SPI-Display
// (ILI9488) reserviert (siehe namespace display unten); sie dürfen nicht in
// kNodeAPins auftauchen. Beide Nodes nutzen dieselbe RJ45-Verdrahtung.
// ESP32 Classic (Fallback, falls doch noch ein WROOM-32 zum Einsatz kommt):
//   GPIO 16,17,18,19,21,22,23,25
//   - vermeidet 0/2/5/12/15 (Strapping), 6..11 (Flash), 34..39 (nur Eingang),
//     1/3 (UART0)
#if CONFIG_IDF_TARGET_ESP32S3
inline constexpr std::array<gpio_num_t, cable_test::kLineCount> kNodeAPins = {
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7,
    GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
};
// Beide S3-Nodes nutzen dieselbe RJ45-Verdrahtung: identische Platine und
// identischer Kabelbaum, die Rolle steckt nur in der Firmware. Ein Display am
// Master verwendet separate, reservierte Pins (siehe namespace display unten).
inline constexpr std::array<gpio_num_t, cable_test::kLineCount> kNodeBPins = kNodeAPins;
#elif CONFIG_IDF_TARGET_ESP32
// Fallback: beide Nodes auf ESP32 Classic (WROOM-32).
inline constexpr std::array<gpio_num_t, cable_test::kLineCount> kNodeAPins = {
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19,
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_25,
};
inline constexpr std::array<gpio_num_t, cable_test::kLineCount> kNodeBPins = {
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19,
    GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_25,
};
#else
#error "Nicht unterstütztes Target: unterstützt werden ESP32-S3 und ESP32 Classic."
#endif

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
// Reservierte Pins für ein SPI-Display (z. B. ILI9488) am Master. Der SPI-Bus
// läuft über die GPIO-Matrix (auf dem S3 für ein ILI9488 unkritisch), MISO ist
// für den optionalen XPT2046-Touchcontroller vorgesehen. Diese GPIOs dürfen
// nicht im RJ45-Mapping (kNodeAPins) verwendet werden; der static_assert unten
// erzwingt das.
namespace display {

inline constexpr gpio_num_t kSpiSclk = GPIO_NUM_12;
inline constexpr gpio_num_t kSpiMosi = GPIO_NUM_13;
inline constexpr gpio_num_t kSpiMiso = GPIO_NUM_14;
inline constexpr gpio_num_t kCs = GPIO_NUM_15;
inline constexpr gpio_num_t kDc = GPIO_NUM_16;
inline constexpr gpio_num_t kRst = GPIO_NUM_17;
inline constexpr gpio_num_t kBacklight = GPIO_NUM_18;
inline constexpr gpio_num_t kTouchCs = GPIO_NUM_21;  // optional (XPT2046)
inline constexpr gpio_num_t kTouchIrq = GPIO_NUM_47; // optional (XPT2046)

} // namespace display
#endif

namespace detail {

constexpr bool pins_unique(const std::array<gpio_num_t, cable_test::kLineCount>& pins) {
    for (std::size_t i = 0; i < pins.size(); ++i) {
        for (std::size_t j = i + 1; j < pins.size(); ++j) {
            if (pins[i] == pins[j]) {
                return false;
            }
        }
    }
    return true;
}

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
constexpr bool contains_pin(const std::array<gpio_num_t, cable_test::kLineCount>& pins,
                            gpio_num_t pin) {
    for (std::size_t i = 0; i < pins.size(); ++i) {
        if (pins[i] == pin) {
            return true;
        }
    }
    return false;
}

constexpr bool display_pins_free() {
    constexpr gpio_num_t reserved[] = {
        display::kSpiSclk, display::kSpiMosi, display::kSpiMiso, display::kCs,
        display::kDc,      display::kRst,     display::kBacklight, display::kTouchCs,
        display::kTouchIrq,
    };
    for (gpio_num_t pin : reserved) {
        if (contains_pin(kNodeAPins, pin)) {
            return false;
        }
    }
    return true;
}
#endif

} // namespace detail

static_assert(detail::pins_unique(kNodeAPins), "kNodeAPins enthält doppelte GPIOs");
static_assert(detail::pins_unique(kNodeBPins), "kNodeBPins enthält doppelte GPIOs");

#if CONFIG_CABLE_TESTER_ROLE_MASTER && CONFIG_IDF_TARGET_ESP32S3
static_assert(detail::display_pins_free(), "Display-Pins kollidieren mit kNodeAPins");
#endif

} // namespace cable_tester::board
