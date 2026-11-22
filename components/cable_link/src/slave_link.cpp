#include "cable_link/slave_link.hpp"

#include <cstring>

#include "cable_test/version.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace cable_tester {
namespace {
constexpr char kTag[] = "slave_link";
constexpr std::uint32_t kAnnounceIntervalMs = 3000;
}

SlaveLink* SlaveLink::s_instance = nullptr;

SlaveLink::SlaveLink(Rj45Port& port) : port_(port) {}

esp_err_t SlaveLink::init() {
    if (s_instance != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    s_instance = this;

    rx_queue_ = xQueueCreate(8, sizeof(RxFrame));
    if (rx_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_now_init: %s", esp_err_to_name(err));
        return err;
    }

    esp_now_peer_info_t broadcast{};
    std::memcpy(broadcast.peer_addr, protocol::kBroadcast.data(), protocol::kBroadcast.size());
    broadcast.channel = 0;
    broadcast.ifidx = WIFI_IF_STA;
    broadcast.encrypt = false;
    err = esp_now_add_peer(&broadcast);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(kTag, "Broadcast-Peer: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_now_register_recv_cb(&SlaveLink::recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "recv_cb: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(&SlaveLink::worker_entry, "link_slave", 4096, this, 5, &worker_) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(&SlaveLink::announce_entry, "link_announce", 3072, this, 4, &announcer_) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(kTag, "ESP-NOW-Slave bereit, warte auf Master");
    return ESP_OK;
}

void SlaveLink::recv_cb(const esp_now_recv_info_t* info, const std::uint8_t* data, int len) {
    SlaveLink* self = s_instance;
    if (self == nullptr || info == nullptr || data == nullptr ||
        len != static_cast<int>(sizeof(protocol::Frame))) {
        return;
    }

    RxFrame rx{};
    std::memcpy(&rx.frame, data, sizeof(protocol::Frame));
    std::memcpy(rx.src.data(), info->src_addr, rx.src.size());
    xQueueSend(self->rx_queue_, &rx, 0);
}

void SlaveLink::worker_entry(void* arg) {
    static_cast<SlaveLink*>(arg)->worker_loop();
}

void SlaveLink::announce_entry(void* arg) {
    static_cast<SlaveLink*>(arg)->announce_loop();
}

void SlaveLink::worker_loop() {
    RxFrame rx{};
    for (;;) {
        if (xQueueReceive(rx_queue_, &rx, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const protocol::Frame& frame = rx.frame;
        if (frame.magic != protocol::kMagic || frame.version != protocol::kVersion) {
            continue;
        }

        if (frame.type == protocol::MsgType::HelloAck) {
            const bool was_paired = paired_.exchange(true);
            {
                std::lock_guard<std::mutex> lock(mac_mutex_);
                master_mac_ = rx.src;
            }
            if (!esp_now_is_peer_exist(rx.src.data())) {
                esp_now_peer_info_t peer{};
                std::memcpy(peer.peer_addr, rx.src.data(), rx.src.size());
                peer.channel = 0;
                peer.ifidx = WIFI_IF_STA;
                peer.encrypt = false;
                esp_now_add_peer(&peer);
            }
            if (!was_paired) {
                ESP_LOGI(kTag, "Mit Master verbunden: %02X:%02X:%02X:%02X:%02X:%02X", rx.src[0],
                         rx.src[1], rx.src[2], rx.src[3], rx.src[4], rx.src[5]);
            }
            continue;
        }

        // Kommandos nur vom verbundenen Master annehmen.
        const auto master = master_mac();
        if (!paired_.load() || master != rx.src) {
            continue;
        }

        switch (frame.type) {
        case protocol::MsgType::IdleRequest:
            port_.allInputsPullDown();
            vTaskDelay(pdMS_TO_TICKS(protocol::kSettleMs));
            send_frame(master.data(), protocol::MsgType::IdleReply, frame.seq, frame.line,
                       port_.sample());
            break;
        case protocol::MsgType::MeasureRequest:
            port_.allInputsPullDown();
            vTaskDelay(pdMS_TO_TICKS(protocol::kSettleMs));
            send_frame(master.data(), protocol::MsgType::MeasureReply, frame.seq, frame.line,
                       port_.sample());
            break;
        case protocol::MsgType::FloatRequest:
            port_.allFloating();
            send_frame(master.data(), protocol::MsgType::Ack, frame.seq, frame.line, 0);
            break;
        case protocol::MsgType::ReleaseRequest:
            port_.allInputsPullDown();
            send_frame(master.data(), protocol::MsgType::Ack, frame.seq, frame.line, 0);
            break;
        case protocol::MsgType::Ping:
            send_frame(master.data(), protocol::MsgType::Pong, frame.seq, frame.line, 0);
            break;
        default:
            break;
        }
    }
}

void SlaveLink::announce_loop() {
    for (;;) {
        send_frame(protocol::kBroadcast.data(), protocol::MsgType::Hello, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(kAnnounceIntervalMs));
    }
}

void SlaveLink::send_frame(const std::uint8_t* mac, protocol::MsgType type, std::uint8_t seq,
                           std::uint8_t line, std::uint8_t mask) {
    protocol::Frame frame{};
    frame.magic = protocol::kMagic;
    frame.version = protocol::kVersion;
    frame.type = type;
    frame.seq = seq;
    frame.line = line;
    frame.mask = mask;
    frame.fw_version = cable_test::kFirmwareVersionNumber;
    esp_now_send(mac, reinterpret_cast<const std::uint8_t*>(&frame), sizeof(frame));
}

std::array<std::uint8_t, 6> SlaveLink::master_mac() const {
    std::lock_guard<std::mutex> lock(mac_mutex_);
    return master_mac_;
}

} // namespace cable_tester
