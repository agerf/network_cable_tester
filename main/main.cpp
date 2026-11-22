#include "app.hpp"
#include "sdkconfig.h"

extern "C" void app_main() {
#if CONFIG_CABLE_TESTER_ROLE_MASTER
    cable_tester::app::run_master();
#else
    cable_tester::app::run_slave();
#endif
}
