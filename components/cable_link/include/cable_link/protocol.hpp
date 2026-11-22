#pragma once

#include <array>
#include <cstdint>

namespace cable_tester::protocol {

inline constexpr std::uint8_t kMagic = 0xC7;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint8_t kChannel = 1;
// Wartezeit beider Seiten, nachdem eine Leitung getrieben wurde.
inline constexpr std::uint32_t kSettleMs = 2;
// Timeout für eine einzelne ESP-NOW-Anfrage inkl. Antwort.
inline constexpr std::uint32_t kRpcTimeoutMs = 300;
inline constexpr int kRpcAttempts = 3;

inline constexpr std::array<std::uint8_t, 6> kBroadcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum class MsgType : std::uint8_t {
    Hello = 1,       // Slave -> Master (Broadcast), danach periodisch
    HelloAck = 2,    // Master -> Slave
    IdleRequest = 3, // Master -> Slave: alle Leitungen hochohmig lesen
    IdleReply = 4,
    MeasureRequest = 5, // Master -> Slave: Pegel lesen, Master treibt Leitung
    MeasureReply = 6,
    FloatRequest = 7, // Master -> Slave: alle Leitungen hochohmig
    ReleaseRequest = 8,
    Ack = 9,
    Ping = 10,
    Pong = 11,
};

struct __attribute__((packed)) Frame {
    std::uint8_t magic;
    std::uint8_t version;
    MsgType type;
    std::uint8_t seq;
    std::uint8_t line;
    std::uint8_t mask;
    std::uint16_t fw_version;
};
static_assert(sizeof(Frame) == 8, "Frame muss kompakt bleiben");

[[nodiscard]] inline bool is_reply(MsgType type) noexcept {
    switch (type) {
    case MsgType::HelloAck:
    case MsgType::IdleReply:
    case MsgType::MeasureReply:
    case MsgType::Ack:
    case MsgType::Pong:
        return true;
    default:
        return false;
    }
}

} // namespace cable_tester::protocol
