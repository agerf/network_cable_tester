#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "cable_link/protocol.hpp"
#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rj45_port/rj45_port.hpp"

namespace cable_tester {

// ESP-NOW-Verbindung auf der Slave-Seite. Der Slave stellt seine Leitungen
// passiv auf Pulldown und meldet auf Anfrage die gelesenen Pegel zurück.
class SlaveLink {
public:
    explicit SlaveLink(Rj45Port& port);
    SlaveLink(const SlaveLink&) = delete;
    SlaveLink& operator=(const SlaveLink&) = delete;

    esp_err_t init();

    [[nodiscard]] bool paired() const { return paired_.load(); }

private:
    struct RxFrame {
        protocol::Frame frame;
        std::array<std::uint8_t, 6> src;
    };

    static void recv_cb(const esp_now_recv_info_t* info, const std::uint8_t* data, int len);
    static void worker_entry(void* arg);
    static void announce_entry(void* arg);
    void worker_loop();
    void announce_loop();
    void handle_frame(const RxFrame& rx);
    void send_frame(const std::uint8_t* mac, protocol::MsgType type, std::uint8_t seq,
                    std::uint8_t line, std::uint8_t mask);
    std::array<std::uint8_t, 6> master_mac() const;

    static SlaveLink* s_instance;

    Rj45Port& port_;
    QueueHandle_t rx_queue_{nullptr};
    TaskHandle_t worker_{nullptr};
    TaskHandle_t announcer_{nullptr};

    mutable std::mutex mac_mutex_;
    std::array<std::uint8_t, 6> master_mac_{};
    std::atomic<bool> paired_{false};
};

} // namespace cable_tester
