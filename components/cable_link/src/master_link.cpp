#include "cable_link/master_link.hpp"

#include <cstring>

#include "cable_test/version.hpp"
#include "esp_log.h"

namespace cable_tester {
namespace {
constexpr char kTag[] = "master_link";
}

MasterLink* MasterLink::s_instance = nullptr;

esp_err_t MasterLink::init() {
    if (s_instance != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_instance = this;

    rx_queue_ = xQueueCreate(16, sizeof(RxFrame));
    reply_sem_ = xSemaphoreCreateBinary();
    if (rx_queue_ == nullptr || reply_sem_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_now_init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_now_register_recv_cb(&MasterLink::recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "recv_cb: %s", esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(&MasterLink::worker_entry, "link_master", 4096, this, 5, &worker_) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(kTag, "ESP-NOW-Master bereit");
    return ESP_OK;
}

void MasterLink::recv_cb(const esp_now_recv_info_t* info, const std::uint8_t* data, int len) {
    MasterLink* self = s_instance;
    if (self == nullptr || info == nullptr || data == nullptr ||
        len != static_cast<int>(sizeof(protocol::Frame))) {
        return;
    }

    RxFrame rx{};
    std::memcpy(&rx.frame, data, sizeof(protocol::Frame));
    std::memcpy(rx.src.data(), info->src_addr, rx.src.size());
    rx.rssi = info->rx_ctrl != nullptr ? info->rx_ctrl->rssi : 0;
    self->rssi_.store(rx.rssi);
    xQueueSend(self->rx_queue_, &rx, 0);
}

void MasterLink::worker_entry(void* arg) {
    static_cast<MasterLink*>(arg)->worker_loop();
}

void MasterLink::worker_loop() {
    RxFrame rx{};
    for (;;) {
        if (xQueueReceive(rx_queue_, &rx, portMAX_DELAY) == pdTRUE) {
            handle_frame(rx);
        }
    }
}

void MasterLink::handle_frame(const RxFrame& rx) {
    const protocol::Frame& frame = rx.frame;
    if (frame.magic != protocol::kMagic || frame.version != protocol::kVersion) {
        return;
    }

    if (frame.type == protocol::MsgType::Hello) {
        {
            std::lock_guard<std::mutex> lock(mac_mutex_);
            peer_mac_ = rx.src;
        }
        const bool was_paired = paired_.exchange(true);
        ensure_peer(rx.src.data());

        protocol::Frame ack{};
        ack.magic = protocol::kMagic;
        ack.version = protocol::kVersion;
        ack.type = protocol::MsgType::HelloAck;
        ack.seq = frame.seq;
        ack.fw_version = cable_test::kFirmwareVersionNumber;
        esp_now_send(rx.src.data(), reinterpret_cast<const std::uint8_t*>(&ack), sizeof(ack));

        if (!was_paired) {
            ESP_LOGI(kTag, "Slave verbunden: %02X:%02X:%02X:%02X:%02X:%02X", rx.src[0], rx.src[1],
                     rx.src[2], rx.src[3], rx.src[4], rx.src[5]);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(sync_mutex_);
    if (pending_.active && frame.seq == pending_.seq && frame.type == pending_.expected) {
        pending_.active = false;
        reply_ = frame;
        xSemaphoreGive(reply_sem_);
    }
}

bool MasterLink::ensure_peer(const std::uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer{};
    std::memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    peer.channel = 0; // aktueller Kanal
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    const esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(kTag, "add_peer: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

std::array<std::uint8_t, 6> MasterLink::peer_mac() const {
    std::lock_guard<std::mutex> lock(mac_mutex_);
    return peer_mac_;
}

std::optional<protocol::Frame> MasterLink::rpc(protocol::MsgType request,
                                               protocol::MsgType expected, std::uint8_t line,
                                               std::uint32_t timeout_ms) {
    if (!paired_.load()) {
        return std::nullopt;
    }
    const auto mac = peer_mac();
    std::lock_guard<std::mutex> rpc_lock(rpc_mutex_);

    for (int attempt = 0; attempt < protocol::kRpcAttempts; ++attempt) {
        const std::uint8_t seq = next_seq_++;
        {
            std::lock_guard<std::mutex> lock(sync_mutex_);
            pending_.expected = expected;
            pending_.seq = seq;
            pending_.active = true;
        }
        xSemaphoreTake(reply_sem_, 0); // alte Antwort verwerfen

        protocol::Frame frame{};
        frame.magic = protocol::kMagic;
        frame.version = protocol::kVersion;
        frame.type = request;
        frame.seq = seq;
        frame.line = line;
        frame.fw_version = cable_test::kFirmwareVersionNumber;

        const esp_err_t err = esp_now_send(
            mac.data(), reinterpret_cast<const std::uint8_t*>(&frame), sizeof(frame));
        if (err == ESP_OK &&
            xSemaphoreTake(reply_sem_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
            std::lock_guard<std::mutex> lock(sync_mutex_);
            if (reply_.seq == seq && reply_.type == expected) {
                return reply_;
            }
        }

        {
            std::lock_guard<std::mutex> lock(sync_mutex_);
            pending_.active = false;
        }
    }

    ESP_LOGW(kTag, "Keine Antwort vom Slave");
    paired_.store(false);
    return std::nullopt;
}

std::optional<cable_test::LineMask> MasterLink::request_idle(std::uint32_t timeout_ms) {
    const auto reply = rpc(protocol::MsgType::IdleRequest, protocol::MsgType::IdleReply, 0,
                           timeout_ms);
    if (!reply) {
        return std::nullopt;
    }
    return reply->mask;
}

std::optional<cable_test::LineMask> MasterLink::request_measure(std::uint8_t line,
                                                                std::uint32_t timeout_ms) {
    const auto reply = rpc(protocol::MsgType::MeasureRequest, protocol::MsgType::MeasureReply,
                           line, timeout_ms);
    if (!reply || reply->line != line) {
        return std::nullopt;
    }
    return reply->mask;
}

bool MasterLink::request_float(std::uint32_t timeout_ms) {
    return rpc(protocol::MsgType::FloatRequest, protocol::MsgType::Ack, 0, timeout_ms).has_value();
}

bool MasterLink::request_release(std::uint32_t timeout_ms) {
    return rpc(protocol::MsgType::ReleaseRequest, protocol::MsgType::Ack, 0, timeout_ms)
        .has_value();
}

} // namespace cable_tester
