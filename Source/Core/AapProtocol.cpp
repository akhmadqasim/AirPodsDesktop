//
// AirPodsDesktop - AirPods Desktop User Experience Enhancement Program.
// Copyright (C) 2021-2022 SpriteOvO
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "AapProtocol.h"

#include <algorithm>
#include <array>

namespace Core::AapProtocol {

namespace {

constexpr std::array<uint8_t, 4> kHeader{0x04, 0x00, 0x04, 0x00};
constexpr std::array<uint8_t, 4> kHandshakeAckHeader{0x01, 0x00, 0x04, 0x00};

constexpr uint16_t kOpcodeBattery = 0x0004;
constexpr uint16_t kOpcodeEar = 0x0006;
constexpr uint16_t kOpcodeControl = 0x0009;

constexpr uint8_t kControlListeningMode = 0x0D;
constexpr uint8_t kControlAllowOffOption = 0x34;

Bytes ControlPacket(uint8_t id, uint8_t value)
{
    return {0x04, 0x00, 0x04, 0x00, 0x09, 0x00, id, value, 0x00, 0x00, 0x00};
}

bool StartsWith(const Bytes &packet, const std::array<uint8_t, 4> &prefix)
{
    return packet.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), packet.begin());
}

template <class EnumT>
std::optional<EnumT> ToEnum(uint8_t value, EnumT min, EnumT max)
{
    if (value < static_cast<uint8_t>(min) || value > static_cast<uint8_t>(max)) {
        return std::nullopt;
    }
    return static_cast<EnumT>(value);
}
} // namespace

Bytes Handshake()
{
    return {0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

Bytes RequestFeatureCapabilities()
{
    return {0x04, 0x00, 0x04, 0x00, 0x4D, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

Bytes EnableNotifications()
{
    return {0x04, 0x00, 0x04, 0x00, 0x0F, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
}

Bytes AllowOffListeningMode()
{
    return ControlPacket(kControlAllowOffOption, 0x01);
}

Bytes SetListeningMode(ListeningMode mode)
{
    return ControlPacket(kControlListeningMode, static_cast<uint8_t>(mode));
}

std::optional<Event> Parse(const Bytes &packet)
{
    if (StartsWith(packet, kHandshakeAckHeader)) {
        return HandshakeAck{};
    }
    if (!StartsWith(packet, kHeader) || packet.size() < 6) {
        return std::nullopt;
    }

    const uint16_t opcode = static_cast<uint16_t>(packet[4] | (packet[5] << 8));
    const auto *body = packet.data() + 6;
    const size_t bodyLength = packet.size() - 6;

    switch (opcode) {
    case kOpcodeBattery: {
        // count, then per component: id, 0x01, level, status, 0x01
        constexpr size_t kEntrySize = 5;
        if (bodyLength < 1) {
            return std::nullopt;
        }
        const size_t count = body[0];
        if (bodyLength < 1 + count * kEntrySize) {
            return std::nullopt;
        }

        BatteryStatus status;
        for (size_t i = 0; i < count; ++i) {
            const auto *entry = body + 1 + i * kEntrySize;
            const auto component = static_cast<BatteryComponent>(entry[0]);
            const auto charge = static_cast<ChargeStatus>(entry[3]);
            const auto reading = charge == ChargeStatus::Disconnected
                                     ? std::nullopt
                                     : std::optional{BatteryReading{entry[2], charge}};
            switch (component) {
            case BatteryComponent::Left:
            case BatteryComponent::Right:
                if (!status.primary.has_value()) {
                    status.primary = component;
                }
                (component == BatteryComponent::Left ? status.left : status.right) = reading;
                break;
            case BatteryComponent::Case:
                status.caseBox = reading;
                break;
            default:
                break;
            }
        }
        return status;
    }

    case kOpcodeEar: {
        if (bodyLength < 2) {
            return std::nullopt;
        }
        auto primary = ToEnum(body[0], EarState::InEar, EarState::Disconnected);
        auto secondary = ToEnum(body[1], EarState::InEar, EarState::Disconnected);
        if (!primary || !secondary) {
            return std::nullopt;
        }
        return EarStatus{*primary, *secondary};
    }

    case kOpcodeControl: {
        if (bodyLength < 2 || body[0] != kControlListeningMode) {
            return std::nullopt;
        }
        auto mode = ToEnum(body[1], ListeningMode::Off, ListeningMode::Adaptive);
        if (!mode) {
            return std::nullopt;
        }
        return ListeningModeStatus{*mode};
    }

    default:
        return std::nullopt;
    }
}

} // namespace Core::AapProtocol
