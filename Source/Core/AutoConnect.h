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
#include <functional>
#include <optional>

#include <QObject>
#include <QTimer>

#include "AirPods.h"
#include "AudioActivity.h"
#include "QuickConnect.h"

namespace Core::AutoConnect {

// When to pull the pods over from another device: audio just started on this PC, an AirPod is in
// an ear, the pods are not already here, and nothing recent says "leave them alone" (they just
// left for the other device, or the last attempt failed).
//
struct Policy {
    using Clock = std::chrono::steady_clock;

    struct Options {
        // Zero: the last report stands until the next one (state reports only arrive on change;
        // a lost signal comes as its own event).
        std::chrono::milliseconds earStaleAfter{0};
        std::chrono::milliseconds cooldownAfterLinkDrop{std::chrono::seconds{5}};
        std::chrono::milliseconds cooldownAfterFailure{std::chrono::seconds{60}};
    };

    Options options;
    bool enabled{false};
    bool deviceConnected{false};
    std::optional<Clock::time_point> lastInEar;
    std::optional<Clock::time_point> linkDroppedAt;
    std::optional<Clock::time_point> failedAt;

    bool AnyInEar(Clock::time_point now) const;
    // Worth polling the audio meter at all (otherwise the app stays idle).
    bool ShouldWatch(Clock::time_point now) const;
    // An audio start right now should bring the pods here.
    bool ShouldConnect(Clock::time_point now) const;
};

class Coordinator : public QObject
{
    Q_OBJECT

public:
    // `containerId` names the bound AirPods for the reconnect request, resolved at attempt time.
    Coordinator(
        AudioActivity::Monitor &monitor, QuickConnect::Controller &quickConnect,
        std::function<QString()> containerId, Policy::Options options = {});

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

public Q_SLOTS:
    void OnStateUpdated(const Core::AirPods::State &state);
    void OnStateLost();
    void OnDeviceConnectionChanged(bool connected);
    void OnDeviceLinkChanged(bool connected);
    void OnAudioActivity(bool active);
    void OnQuickConnectOutcome(Core::QuickConnect::Outcome outcome, QString deviceName);

Q_SIGNALS:
    void AttemptStarted();

private:
    AudioActivity::Monitor &_monitor;
    QuickConnect::Controller &_quickConnect;
    std::function<QString()> _containerId;
    Policy _policy;
    bool _assumePlaying{false};
    QTimer _earStaleTimer{this};

    void Reevaluate();
};

} // namespace Core::AutoConnect
