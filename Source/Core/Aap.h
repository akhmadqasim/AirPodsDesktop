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

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>

#include <QMetaType>
#include <QObject>
#include <QTimer>

#include "AapProtocol.h"

namespace Core::Aap {

using AapProtocol::BatteryStatus;
using AapProtocol::Bytes;
using AapProtocol::EarStatus;
using AapProtocol::ListeningMode;

// Receives what the backend produces. Both callbacks may come from any thread.
class Sink
{
public:
    virtual ~Sink() = default;

    virtual void OnLinkChanged(bool up) = 0;
    virtual void OnPacketReceived(Bytes packet) = 0;
};

// Owns the transport (on Windows: a handle on the AapL2cap driver interface) and its reader.
class Backend
{
public:
    virtual ~Backend() = default;

    virtual void SetSink(Sink *sink) = 0;
    // Whether the transport exists at all on this machine (on Windows: the AapL2cap driver is
    // installed for the bound AirPods). Cheap; the client asks before every connect attempt.
    virtual bool IsAvailable() = 0;
    // Asynchronous; the outcome is reported through `Sink::OnLinkChanged`.
    virtual void Connect() = 0;
    // Synchronous; no `Sink` callback runs after it returns.
    virtual void Disconnect() = 0;
    virtual bool Send(const Bytes &packet) = 0;
};

class NullBackend final : public Backend
{
public:
    void SetSink(Sink *) override {}
    bool IsAvailable() override
    {
        return false;
    }
    void Connect() override {}
    void Disconnect() override {}
    bool Send(const Bytes &) override
    {
        return false;
    }
};

// Drives the session: opens the link while the bound AirPods are connected, runs the setup
// handshake, decodes notifications and exposes listening-mode control. Without the transport
// (no driver) it stays idle: one availability check per device connection, no timers.
//
// Threading: the object lives on the GUI thread (its timers need the event loop), but
// `EarStatusChanged` / `ListeningModeChanged` are emitted on the backend's thread so that a
// direct-connected receiver can do blocking work (media control) without stalling the GUI.
//
class Client final : public QObject, private Sink
{
    Q_OBJECT

public:
    struct Intervals {
        std::chrono::milliseconds handshakeTimeout{1500};
        std::chrono::milliseconds setupStep{300};
        std::chrono::milliseconds retry{std::chrono::seconds{5}};
    };

    explicit Client(std::shared_ptr<Backend> backend, Intervals intervals = {});
    ~Client() override;

    bool IsReady() const;
    std::optional<ListeningMode> CurrentListeningMode() const;
    std::optional<EarStatus> CurrentEarStatus() const;
    std::optional<BatteryStatus> CurrentBatteryStatus() const;

    bool SetListeningMode(ListeningMode mode);

public Q_SLOTS:
    void SetDeviceConnected(bool connected);

Q_SIGNALS:
    // Emitted on the GUI thread. `LinkChanged` follows the transport; `ReadyChanged(true)` waits
    // for the setup sequence to complete.
    void LinkChanged(bool up);
    void ReadyChanged(bool ready);
    // Emitted on the backend thread.
    void EarStatusChanged(Core::AapProtocol::EarStatus status);
    void BatteryStatusChanged(Core::AapProtocol::BatteryStatus status);
    void ListeningModeChanged(Core::AapProtocol::ListeningMode mode);

private:
    enum class Setup {
        Idle,
        AwaitingHandshake,
        Handshaken,
        CapabilitiesSent,
        NotificationsSent,
        Ready,
    };

    std::shared_ptr<Backend> _backend;
    Intervals _intervals;
    mutable std::mutex _mutex;
    bool _deviceConnected{false};
    bool _linkUp{false};
    Setup _setup{Setup::Idle};
    std::optional<ListeningMode> _listeningMode;
    std::optional<EarStatus> _earStatus;
    std::optional<BatteryStatus> _batteryStatus;
    QTimer _setupTimer{this};
    QTimer _retryTimer{this};

    void OnLinkChanged(bool up) override;
    void OnPacketReceived(Bytes packet) override;

    void HandleLinkChanged(bool up);
    void HandleHandshakeAck();
    void AdvanceSetup();
    void RequestConnect();
    void DropLink();
};

} // namespace Core::Aap

Q_DECLARE_METATYPE(Core::AapProtocol::EarStatus)
Q_DECLARE_METATYPE(Core::AapProtocol::BatteryStatus)
Q_DECLARE_METATYPE(Core::AapProtocol::ListeningMode)
