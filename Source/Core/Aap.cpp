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

#include "Aap.h"

#include "../Helper.h"
#include "../Logger.h"

namespace Core::Aap {

Client::Client(std::shared_ptr<Backend> backend, Intervals intervals)
    : _backend{std::move(backend)}, _intervals{intervals}
{
    qRegisterMetaType<AapProtocol::EarStatus>();
    qRegisterMetaType<AapProtocol::BatteryStatus>();
    qRegisterMetaType<AapProtocol::ListeningMode>();

    _setupTimer.setSingleShot(true);
    _retryTimer.setSingleShot(true);

    connect(&_setupTimer, &QTimer::timeout, this, &Client::AdvanceSetup);
    connect(&_retryTimer, &QTimer::timeout, this, &Client::RequestConnect);

    _backend->SetSink(this);
}

Client::~Client()
{
    _backend->Disconnect();
    _backend->SetSink(nullptr);
}

bool Client::IsReady() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _setup == Setup::Ready;
}

std::optional<ListeningMode> Client::CurrentListeningMode() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _listeningMode;
}

std::optional<EarStatus> Client::CurrentEarStatus() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _earStatus;
}

std::optional<BatteryStatus> Client::CurrentBatteryStatus() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _batteryStatus;
}

bool Client::SetListeningMode(ListeningMode mode)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_linkUp) {
            LOG(Warn, "AAP: Ignore listening mode request, link is down.");
            return false;
        }
    }

    LOG(Info, "AAP: Set listening mode to {}.", Helper::ToUnderlying(mode));
    return _backend->Send(AapProtocol::SetListeningMode(mode));
}

void Client::SetDeviceConnected(bool connected)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_deviceConnected == connected) {
            return;
        }
        _deviceConnected = connected;
    }

    LOG(Info, "AAP: Bound device connected: {}.", connected);

    if (connected) {
        if (!_backend->IsAvailable()) {
            LOG(Info, "AAP: Transport unavailable, staying idle until the device reconnects.");
            return;
        }
        RequestConnect();
    }
    else {
        _retryTimer.stop();
        DropLink();
    }
}

void Client::OnLinkChanged(bool up)
{
    QMetaObject::invokeMethod(this, [this, up] { HandleLinkChanged(up); }, Qt::QueuedConnection);
}

void Client::OnPacketReceived(Bytes packet)
{
    auto event = AapProtocol::Parse(packet);
    if (!event.has_value()) {
        LOG(Trace, "AAP: Undecoded packet: {}", Helper::ToString(packet));
        return;
    }

    std::visit(
        Helper::Overloaded{
            [this](const AapProtocol::HandshakeAck &) {
                QMetaObject::invokeMethod(this, [this] { HandleHandshakeAck(); },
                                          Qt::QueuedConnection);
            },
            [this](const AapProtocol::EarStatus &status) {
                {
                    std::lock_guard<std::mutex> lock{_mutex};
                    _earStatus = status;
                }
                LOG(Info, "AAP: Ear status primary: {}, secondary: {}.",
                    Helper::ToUnderlying(status.primary), Helper::ToUnderlying(status.secondary));
                emit EarStatusChanged(status);
            },
            [this](const AapProtocol::BatteryStatus &status) {
                {
                    std::lock_guard<std::mutex> lock{_mutex};
                    if (_batteryStatus == status) {
                        return;
                    }
                    _batteryStatus = status;
                }
                LOG(Info, "AAP: Battery status changed.");
                emit BatteryStatusChanged(status);
            },
            [this](const AapProtocol::ListeningModeStatus &status) {
                {
                    std::lock_guard<std::mutex> lock{_mutex};
                    if (_listeningMode == status.mode) {
                        return;
                    }
                    _listeningMode = status.mode;
                }
                LOG(Info, "AAP: Listening mode: {}.", Helper::ToUnderlying(status.mode));
                emit ListeningModeChanged(status.mode);
            },
        },
        *event);
}

void Client::HandleLinkChanged(bool up)
{
    bool wasReady;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_linkUp == up) {
            // A connect attempt that failed before the link ever came up lands here.
            if (!up && _deviceConnected && !_retryTimer.isActive() && _backend->IsAvailable()) {
                _retryTimer.start(_intervals.retry);
            }
            return;
        }
        _linkUp = up;
        wasReady = _setup == Setup::Ready;
        _setup = up ? Setup::AwaitingHandshake : Setup::Idle;
        if (!up) {
            _listeningMode.reset();
            _earStatus.reset();
            _batteryStatus.reset();
        }
    }

    LOG(Info, "AAP: Link {}.", up ? "up" : "down");
    emit LinkChanged(up);

    if (up) {
        _backend->Send(AapProtocol::Handshake());
        _setupTimer.start(_intervals.handshakeTimeout);
        return;
    }

    _setupTimer.stop();
    if (wasReady) {
        emit ReadyChanged(false);
    }

    bool retry;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        retry = _deviceConnected;
    }
    if (retry && _backend->IsAvailable()) {
        _retryTimer.start(_intervals.retry);
    }
}

void Client::HandleHandshakeAck()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_setup != Setup::AwaitingHandshake) {
            return;
        }
        _setup = Setup::Handshaken;
    }
    _setupTimer.start(_intervals.setupStep);
}

void Client::AdvanceSetup()
{
    Setup setup;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_linkUp) {
            return;
        }
        switch (_setup) {
        case Setup::AwaitingHandshake:
            LOG(Warn, "AAP: No handshake ack, continuing anyway.");
            [[fallthrough]];
        case Setup::Handshaken:
            _setup = Setup::CapabilitiesSent;
            break;
        case Setup::CapabilitiesSent:
            _setup = Setup::NotificationsSent;
            break;
        case Setup::NotificationsSent:
            _setup = Setup::Ready;
            break;
        default:
            return;
        }
        setup = _setup;
    }

    switch (setup) {
    case Setup::CapabilitiesSent:
        _backend->Send(AapProtocol::RequestFeatureCapabilities());
        _setupTimer.start(_intervals.setupStep);
        break;
    case Setup::NotificationsSent:
        _backend->Send(AapProtocol::EnableNotifications());
        _setupTimer.start(_intervals.setupStep);
        break;
    case Setup::Ready:
        _backend->Send(AapProtocol::AllowOffListeningMode());
        LOG(Info, "AAP: Session ready.");
        emit ReadyChanged(true);
        break;
    default:
        break;
    }
}

void Client::RequestConnect()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_deviceConnected || _linkUp) {
            return;
        }
    }
    LOG(Info, "AAP: Opening link.");
    _backend->Connect();
}

void Client::DropLink()
{
    _backend->Disconnect();
    // The backend guarantees silence after `Disconnect()`, so the queued "link down" that a
    // failing reader may already have posted is harmless: `HandleLinkChanged` deduplicates.
    HandleLinkChanged(false);
}

} // namespace Core::Aap
