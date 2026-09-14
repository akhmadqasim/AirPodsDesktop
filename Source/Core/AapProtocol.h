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

#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

// AAP = Apple Accessory Protocol, spoken over L2CAP PSM 0x1001 once the AirPods are connected.
// Packet layout (little endian): `04 00 04 00 <opcode u16> <body...>`; the handshake is the only
// packet with a different header. Reference: LibrePods "AAP Definitions".
//
namespace Core::AapProtocol {

using Bytes = std::vector<uint8_t>;

enum class EarState : uint8_t {
    InEar = 0x00,
    Out = 0x01,
    InCase = 0x02,
    Disconnected = 0x03,
};

enum class ListeningMode : uint8_t {
    Off = 0x01,
    Anc = 0x02,
    Transparency = 0x03,
    Adaptive = 0x04,
};

struct HandshakeAck {
    bool operator==(const HandshakeAck &) const = default;
};

// The primary bud is whichever one currently owns the link; the battery report lists it first.
struct EarStatus {
    EarState primary{EarState::Disconnected};
    EarState secondary{EarState::Disconnected};

    bool IsBothInEar() const
    {
        return primary == EarState::InEar && secondary == EarState::InEar;
    }

    bool operator==(const EarStatus &) const = default;
};

struct ListeningModeStatus {
    ListeningMode mode;

    bool operator==(const ListeningModeStatus &) const = default;
};

enum class BatteryComponent : uint8_t {
    Headset = 0x01,
    Right = 0x02,
    Left = 0x04,
    Case = 0x08,
};

enum class ChargeStatus : uint8_t {
    Unknown = 0x00,
    Charging = 0x01,
    Discharging = 0x02,
    Disconnected = 0x04,
};

struct BatteryReading {
    uint8_t level{0}; // percent
    ChargeStatus status{ChargeStatus::Unknown};

    bool operator==(const BatteryReading &) const = default;
};

// A component that is not reachable right now (the case while the pods are out, a pod that is
// off) is reported as `Disconnected` by the AirPods; it is left empty here.
struct BatteryStatus {
    std::optional<BatteryReading> left, right, caseBox;
    // The pod listed first is the primary one, which is what `EarStatus::primary` refers to.
    std::optional<BatteryComponent> primary;

    bool operator==(const BatteryStatus &) const = default;
};

using Event = std::variant<HandshakeAck, EarStatus, ListeningModeStatus, BatteryStatus>;

// Packets the host sends. The sequence after `Handshake` mirrors what iOS does before the
// AirPods start streaming notifications.
Bytes Handshake();
Bytes RequestFeatureCapabilities();
Bytes EnableNotifications();
Bytes AllowOffListeningMode();
Bytes SetListeningMode(ListeningMode mode);

// Returns `nullopt` for packets we do not decode (metadata, battery, other control ids).
std::optional<Event> Parse(const Bytes &packet);

} // namespace Core::AapProtocol
