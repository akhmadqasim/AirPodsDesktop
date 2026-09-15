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

#include "AutoConnect.h"

#include "../Logger.h"

namespace Core::AutoConnect {

bool Policy::AnyInEar(Clock::time_point now) const
{
    return lastInEar.has_value() && now - *lastInEar <= options.earStaleAfter;
}

bool Policy::ShouldWatch(Clock::time_point now) const
{
    return enabled && !deviceConnected && AnyInEar(now);
}

bool Policy::ShouldConnect(Clock::time_point now) const
{
    if (!ShouldWatch(now)) {
        return false;
    }
    if (linkDroppedAt.has_value() && now - *linkDroppedAt < options.cooldownAfterLinkDrop) {
        return false;
    }
    if (failedAt.has_value() && now - *failedAt < options.cooldownAfterFailure) {
        return false;
    }
    return true;
}

Coordinator::Coordinator(
    AudioActivity::Monitor &monitor, QuickConnect::Controller &quickConnect,
    std::function<QString()> containerId, Policy::Options options)
    : _monitor{monitor}, _quickConnect{quickConnect}, _containerId{std::move(containerId)}
{
    _policy.options = options;
    _earStaleTimer.setSingleShot(true);
    connect(&_earStaleTimer, &QTimer::timeout, this, &Coordinator::Reevaluate);
    connect(&_monitor, &AudioActivity::Monitor::ActivityChanged, this, &Coordinator::OnAudioActivity);
    connect(
        &_quickConnect, &QuickConnect::Controller::QuietOutcomeChanged, this,
        &Coordinator::OnQuickConnectOutcome);
}

void Coordinator::SetEnabled(bool enabled)
{
    _policy.enabled = enabled;
    Reevaluate();
}

bool Coordinator::IsEnabled() const
{
    return _policy.enabled;
}

void Coordinator::OnStateUpdated(const Core::AirPods::State &state)
{
    const bool anyInEar = state.pods.left.isInEar || state.pods.right.isInEar;
    const bool wasInEar = _policy.AnyInEar(Policy::Clock::now());
    if (anyInEar) {
        _policy.lastInEar = Policy::Clock::now();
        _earStaleTimer.start(_policy.options.earStaleAfter);
    }
    else {
        _policy.lastInEar.reset();
        _earStaleTimer.stop();
    }
    if (anyInEar != wasInEar) {
        // Pods just went in: audio that is already playing here counts as a reason to switch.
        _assumePlaying = false;
        Reevaluate();
    }
}

void Coordinator::OnStateLost()
{
    _policy.lastInEar.reset();
    _earStaleTimer.stop();
    Reevaluate();
}

void Coordinator::OnDeviceConnectionChanged(bool connected)
{
    _policy.deviceConnected = connected;
    Reevaluate();
}

void Coordinator::OnDeviceLinkChanged(bool connected)
{
    if (!connected) {
        // They just left for the other device: whatever is playing here now must stop and start
        // again before it counts, or the two hosts would fight over the pods.
        _policy.linkDroppedAt = Policy::Clock::now();
        _assumePlaying = true;
    }
    _policy.deviceConnected = connected;
    Reevaluate();
}

void Coordinator::OnAudioActivity(bool active)
{
    if (!active) {
        return;
    }
    const auto now = Policy::Clock::now();
    if (!_policy.ShouldConnect(now)) {
        LOG(Info, "AutoConnect: audio started, but not switching (cooldown or gate)");
        return;
    }
    const auto containerId = _containerId ? _containerId() : QString{};
    if (containerId.isEmpty()) {
        LOG(Warn, "AutoConnect: no bound device to connect");
        return;
    }
    LOG(Info, "AutoConnect: audio started with an AirPod in ear, connecting");
    _quickConnect.RequestDevice(containerId, true);
    emit AttemptStarted();
}

void Coordinator::OnQuickConnectOutcome(Core::QuickConnect::Outcome outcome, QString)
{
    switch (outcome) {
    case QuickConnect::Outcome::Failed:
    case QuickConnect::Outcome::TimedOut:
    case QuickConnect::Outcome::DeviceUnavailable:
        _policy.failedAt = Policy::Clock::now();
        break;
    default:
        break;
    }
}

void Coordinator::Reevaluate()
{
    const bool want = _policy.ShouldWatch(Policy::Clock::now());
    if (want && !_monitor.IsWatching()) {
        _monitor.Watch(_assumePlaying);
    }
    else if (!want && _monitor.IsWatching()) {
        _monitor.Stop();
    }
}

} // namespace Core::AutoConnect
