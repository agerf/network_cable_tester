#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

#include "cable_link/protocol.hpp"
#include "cable_test/model.hpp"
#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace cable_tester {

// ESP-NOW-Verbindung auf der Master-Seite. Der Master steuert den Test und
// fragt den Slave synchron per Request/Reply ab.
class MasterLink {
public:
    MasterLink() = default;
    MasterLink(const MasterLink&) = delete;
    MasterLink& operator=(const MasterLink&) = delete;

    esp_err_t init();

    [[nodiscard]] bool paired() const { return paired_.load(); }
    [[nodiscard]] std::array<std::uint8_t, 6> peer_mac() const;
    [[nodiscard]] std::int8_t rssi() const { return rssi_.load(); }

    [[nodiscard]] std::optional<cable_test::LineMask> request_idle(
        std::uint32_t timeout_ms = protocol::kRpcTimeoutMs);
    [[nodiscard]] std::optional<cable_test::LineMask> request_measure(
        std::uint8_t line, std::uint32_t timeout_ms = protocol::kRpcTimeoutMs);
    bool request_float(std::uint32_t timeout_ms = protocol::kRpcTimeoutMs);
    bool request_release(std::uint32_t timeout_ms = protocol::kRpcTimeoutMs);

private:
    struct RxFrame {
        protocol::Frame frame;
        std::int8_t rssi;
        std::array<std::uint8_t, 6> src;
    };
    struct Pending {
        protocol::MsgType expected{protocol::MsgType::Pong};
        std::uint8_t seq{0};
        bool active{false};
    };

    static void recv_cb(const esp_now_recv_info_t* info, const std::uint8_t* data, int len);
    static void worker_entry(void* arg);
    void worker_loop();
    void handle_frame(const RxFrame& rx);
    bool ensure_peer(const std::uint8_t* mac);
    std::optional<protocol::Frame> rpc(protocol::MsgType request, protocol::MsgType expected,
                                       std::uint8_t line, std::uint32_t timeout_ms);

    static MasterLink* s_instance;

    QueueHandle_t rx_queue_{nullptr};
    SemaphoreHandle_t reply_sem_{nullptr};
    TaskHandle_t worker_{nullptr};

    mutable std::mutex mac_mutex_;
    std::array<std::uint8_t, 6> peer_mac_{};
    std::atomic<bool> paired_{false};
    std::atomic<std::int8_t> rssi_{0};

    std::mutex rpc_mutex_;
    std::mutex sync_mutex_;
    Pending pending_{};
    protocol::Frame reply_{};
    std::uint8_t next_seq_{1};
};

} // namespace cable_tester
