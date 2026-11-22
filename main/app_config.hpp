#pragma once

namespace cable_tester::app {

// WLAN-Access-Point des Masters (für die Web-UI).
inline constexpr char kApSsid[] = "CableTester";
inline constexpr char kApPassword[] = "cabletest123";
static_assert(sizeof(kApPassword) - 1 >= 8, "WPA2-Passwort braucht mindestens 8 Zeichen");

} // namespace cable_tester::app
