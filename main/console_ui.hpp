#pragma once

namespace cable_tester {
class TestRunner;
class MasterLink;
} // namespace cable_tester

namespace cable_tester::console_ui {

// Startet die UART-REPL (115200 8N1) mit rollenabhängigen Kommandos.
void start(TestRunner& runner, MasterLink& link);
void start_slave();

} // namespace cable_tester::console_ui
