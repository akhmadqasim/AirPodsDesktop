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

#include "AirPods.h"

#include <algorithm>
#include <utility>

#include "GlobalMedia.h"
#include "../Error.h"
#include "../Helper.h"
#include "../Logger.h"

namespace Core::AirPods {

QString Manager::BoundDeviceContainerId()
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (!_boundDevice.has_value()) {
        return {};
    }
    return QString::fromWCharArray(_boundDevice->GetAepId().c_str());
}

QString Details::ResolveDisplayName(QString deviceName, Model model)
{
    deviceName.remove(" - Find My");

    const bool isDefaultUsbCMaxName =
        model == Model::AirPods_Max_USB_C && deviceName == "AirPods Max";
    return deviceName.isEmpty() || isDefaultUsbCMaxName ? Helper::ToString(model) : deviceName;
}

std::optional<bool> Details::EarDetectionTracker::Update(const State &state)
{
    const bool bothInEar = state.pods.left.isInEar && state.pods.right.isInEar;
    const bool changed = _lastBothInEar.has_value() && _lastBothInEar != bothInEar;
    _lastBothInEar = bothInEar;
    return changed ? std::optional{bothInEar} : std::nullopt;
}

void Details::EarDetectionTracker::Reset()
{
    _lastBothInEar.reset();
}

std::optional<State> Details::BuildAapState(Model model, const AapSnapshot &snapshot)
{
    using AapProtocol::BatteryComponent;
    using AapProtocol::ChargeStatus;
    using AapProtocol::EarState;

    if (!snapshot.battery.has_value()) {
        return std::nullopt;
    }
    const auto &battery = *snapshot.battery;

    auto fill = [](BasicState &target, const std::optional<AapProtocol::BatteryReading> &reading) {
        if (reading.has_value()) {
            target.battery = Battery{reading->level};
            target.isCharging = reading->status == ChargeStatus::Charging;
        }
    };

    State state;
    state.model = model;
    fill(state.pods.left, battery.left);
    fill(state.pods.right, battery.right);
    fill(state.caseBox, battery.caseBox);

    if (snapshot.ear.has_value()) {
        const auto &ear = *snapshot.ear;
        const bool primaryIsLeft = battery.primary == BatteryComponent::Left;
        const auto leftEar = primaryIsLeft ? ear.primary : ear.secondary;
        const auto rightEar = primaryIsLeft ? ear.secondary : ear.primary;
        state.pods.left.isInEar = leftEar == EarState::InEar;
        state.pods.right.isInEar = rightEar == EarState::InEar;
        state.caseBox.isBothPodsInCase =
            leftEar == EarState::InCase && rightEar == EarState::InCase;
        state.caseBox.isLidOpened = state.caseBox.isBothPodsInCase;
    }
    return state;
}

Manager::Manager(QObject *parent) : QObject{parent}
{
    _stateMgr.SetOnDiscardState([this] {
        // Stopping the scanner for AAP lets the lost timer fire; that is not a disconnect.
        if (_aapLinkUp) {
            return;
        }
        QMetaObject::invokeMethod(this, [this] { emit Disconnected(); }, Qt::QueuedConnection);
    });

    _adWatcher.CbReceived() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvertisementReceived(std::forward<decltype(args)>(args)...);
    };

    _adWatcher.CbStateChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvWatcherStateChanged(std::forward<decltype(args)>(args)...);
    };
}

Manager::~Manager()
{
    StopScanner();

    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }
}

void Manager::StartScanner()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_boundDevice.has_value()) {
            LOG(Info, "Skip Bluetooth AdvWatcher start because no device is bound.");
            return;
        }
        if (_aapLinkUp) {
            LOG(Info, "Skip Bluetooth AdvWatcher start because AAP owns the state.");
            return;
        }
        if (_scannerWanted) {
            return;
        }
        _scannerWanted = true;
    }

    if (!_adWatcher.Start()) {
        std::lock_guard<std::mutex> lock{_mutex};
        _scannerWanted = false;
        LOG(Warn, "Bluetooth AdvWatcher start failed.");
    }
    else {
        LOG(Info, "Bluetooth AdvWatcher start succeeded.");
    }
}

void Manager::StopScanner()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_scannerWanted) {
            return;
        }
        _scannerWanted = false;
    }

    if (!_adWatcher.Stop()) {
        LOG(Warn, "AsyncScanner::Stop() failed.");
    }
    else {
        LOG(Info, "AsyncScanner::Stop() succeeded.");
    }
}

void Manager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _stateMgr.OnRssiMinChanged(rssiMin);
}

void Manager::OnAutomaticEarDetectionChanged(bool enable)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _automaticEarDetection = enable;
}

void Manager::OnAapLinkChanged(bool up)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_aapLinkUp == up) {
        return;
    }
    _aapLinkUp = up;
    _aapSnapshot = {};
    _aapState.reset();

    if (up) {
        LOG(Info, "AAP link up: handing the state over from BLE.");
        _stateMgr.Disconnect();
        QMetaObject::invokeMethod(this, [this] { StopScanner(); }, Qt::QueuedConnection);
    }
    else {
        LOG(Info, "AAP link down: handing the state back to BLE.");
        QMetaObject::invokeMethod(this, [this] { StartScanner(); }, Qt::QueuedConnection);
    }
}

void Manager::OnAapEarStatusChanged(const Core::AapProtocol::EarStatus &status)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (!_aapLinkUp) {
        return;
    }
    _aapSnapshot.ear = status;
    PublishAapState();
}

void Manager::OnAapBatteryStatusChanged(const Core::AapProtocol::BatteryStatus &status)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (!_aapLinkUp) {
        return;
    }
    _aapSnapshot.battery = status;
    PublishAapState();
}

void Manager::PublishAapState()
{
    auto newState = Details::BuildAapState(_boundModel, _aapSnapshot);
    if (!newState.has_value() || newState == _aapState) {
        return;
    }
    Details::StateManager::UpdateEvent event{_aapState, *newState};
    _aapState = std::move(newState);
    _aapStatePublished = true;
    OnStateChanged(std::move(event));
}

void Manager::OnBoundDeviceAddressChanged(uint64_t address)
{
    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }

    std::lock_guard<std::mutex> lock{_mutex};

    _requestedDeviceAddress = address;
    _boundDevice.reset();
    _boundModel = Model::Unknown;
    _deviceConnected = false;
    _stateMgr.Disconnect();
    _earDetection.Reset();
    emit DeviceConnectionChanged(false);
    QMetaObject::invokeMethod(this, [this] { StopScanner(); }, Qt::QueuedConnection);

    if (address == 0) {
        LOG(Info, "Unbind device.");
        return;
    }

    LOG(Info, "Bind a new device.");

    _deviceLookupThread = std::jthread{[this, address](std::stop_token stopToken) {
        OS::Windows::Winrt::Initialize();
        auto device = Bluetooth::DeviceManager::FindDevice(address);
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, address, device = std::move(device)]() mutable {
                CompleteBoundDeviceLookup(address, std::move(device));
            },
            Qt::QueuedConnection);
    }};
}

void Manager::CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device)
{
    std::lock_guard<std::mutex> lock{_mutex};

    if (address != _requestedDeviceAddress) {
        LOG(Info, "Ignore a stale bound-device lookup result.");
        return;
    }

    if (!device.has_value()) {
        LOG(Warn, "The bound device address is no longer available.");
        emit BoundDeviceUnavailable();
        return;
    }

    _boundDevice = std::move(device);
    _boundModel = AppleCP::AirPods::GetModel(_boundDevice->GetProductId());

    _deviceName = QString::fromStdString([&] {
        auto name = _boundDevice->GetName();
        return name.find("Bluetooth") != std::string::npos ? std::string{} : name;
    }());

    _boundDevice->CbConnectionStatusChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnBoundDeviceConnectionStateChanged(std::forward<decltype(args)>(args)...);
    };

    OnBoundDeviceConnectionStateChanged(_boundDevice->GetConnectionState());
}

void Manager::OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state)
{
    const bool oldDeviceConnected = _deviceConnected;
    bool newDeviceConnected = state == Bluetooth::DeviceState::Connected;
    bool doDisconnect = oldDeviceConnected && !newDeviceConnected;
    _deviceConnected = newDeviceConnected;
    emit DeviceConnectionChanged(newDeviceConnected);

    // The scanner keeps running across link changes: with the pods on another device the popup
    // still shows their state and offers to connect. AAP (when up) stops it instead.
    QMetaObject::invokeMethod(this, [this] { StartScanner(); }, Qt::QueuedConnection);

    if (doDisconnect) {
        _earDetection.Reset();
        if (_aapStatePublished) {
            // The AAP-built state dies with the link; BLE rebuilds one within a few seconds.
            _aapStatePublished = false;
            QMetaObject::invokeMethod(this, [this] { emit Disconnected(); }, Qt::QueuedConnection);
        }
    }

    if (oldDeviceConnected != newDeviceConnected) {
        // Re-announce the current state so the popup's connect button follows the link.
        auto current = _aapState.has_value() ? _aapState : _stateMgr.GetCurrentState();
        if (current.has_value()) {
            OnStateChanged({current, *current});
        }
    }

    LOG(Info, "The device we bound is updated. current: {}, new: {}", oldDeviceConnected,
        newDeviceConnected);
}

void Manager::OnStateChanged(Details::StateManager::UpdateEvent updateEvent)
{
    const auto &oldState = updateEvent.oldState;
    auto &newState = updateEvent.newState;

    newState.displayName = Details::ResolveDisplayName(_deviceName, newState.model);
    newState.isConnected = _deviceConnected;

    emit StateUpdated(newState);

    // The lid is only observable while both pods are inside. Report "opened" on that edge and
    // "closed" only when actually seen closed; taking the pods out is neither.
    const auto lidOpened = [](const State &state) {
        return state.caseBox.isBothPodsInCase && state.caseBox.isLidOpened;
    };
    const auto lidClosed = [](const State &state) {
        return state.caseBox.isBothPodsInCase && !state.caseBox.isLidOpened;
    };
    const bool oldLidOpened = oldState.has_value() && lidOpened(*oldState);
    const bool oldLidClosed = oldState.has_value() && lidClosed(*oldState);
    if (lidOpened(newState) && !oldLidOpened) {
        OnLidOpened(true);
    }
    else if (lidClosed(newState) && !oldLidClosed) {
        OnLidOpened(false);
    }

    // Compared against the tracker rather than `oldState`: `StateManager` drops its cached state
    // when the device is lost, and the next update would otherwise start over without a baseline.
    if (auto changed = _earDetection.Update(newState)) {
        OnBothInEar(*changed);
    }
}

void Manager::OnLidOpened(bool opened)
{
    emit LidToggled(opened);
}

void Manager::OnBothInEar(bool isBothInEar)
{
    if (!_automaticEarDetection) {
        LOG(Info, "automatic_ear_detection: Do nothing because it is disabled. ({})", isBothInEar);
        return;
    }
    if (!_deviceConnected) {
        LOG(Info, "automatic_ear_detection: Do nothing because the pods are on another device.");
        return;
    }

    LOG(Info, "automatic_ear_detection: bothInEar changed to {}", isBothInEar);

    if (isBothInEar) {
        GlobalMedia::Play();
    }
    else {
        GlobalMedia::Pause();
    }
}

bool Manager::OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    if (!Details::Advertisement::IsDesiredAdv(data)) {
        return false;
    }

    Details::Advertisement adv{data};

    if (_aapLinkUp) {
        LOG(Trace, "Ignore advertisement while AAP owns the state.");
        return false;
    }

    if (_boundModel != Model::Unknown && adv.GetAdvState().model != _boundModel) {
        LOG(Trace, "Ignore advertisement for a model other than the bound device.");
        return false;
    }

    LOG(Trace, "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
        Helper::ToString(adv.GetDesensitizedData()), Helper::Hash(data.address), data.rssi);

    auto optUpdateEvent = _stateMgr.OnAdvReceived(std::move(adv));
    if (optUpdateEvent.has_value()) {
        _aapStatePublished = false;
        OnStateChanged(std::move(optUpdateEvent.value()));
    }
    return true;
}

void Manager::OnAdvWatcherStateChanged(
    Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError)
{
    switch (state) {
    case Bluetooth::AdvertisementWatcher::State::Started:
        emit ScannerAvailabilityChanged(true);
        LOG(Info, "Bluetooth AdvWatcher started.");
        break;

    case Bluetooth::AdvertisementWatcher::State::Stopped:
        if (_scannerWanted) {
            emit ScannerAvailabilityChanged(false);
            LOG(Warn, "Bluetooth AdvWatcher stopped. Error: '{}'.", optError.value_or("nullopt"));
        }
        else {
            LOG(Info, "Bluetooth AdvWatcher stopped intentionally.");
        }
        break;

    default:
        FatalError("Unhandled adv watcher state: '{}'", Helper::ToUnderlying(state));
    }
}

std::vector<Bluetooth::Device> GetDevices()
{
    std::vector<Bluetooth::Device> devices =
        Bluetooth::DeviceManager::GetDevicesByState(Bluetooth::DeviceState::Paired);

    LOG(Info, "Paired devices count: {}", devices.size());

    devices.erase(
        std::remove_if(
            devices.begin(), devices.end(),
            [](const auto &device) {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();
                const auto doErase = vendorId != AppleCP::VendorId ||
                                     AppleCP::AirPods::GetModel(productId) == Model::Unknown;

                LOG(Trace, "Device VendorId: '{}', ProductId: '{}', doErase: {}", vendorId,
                    productId, doErase);
                return doErase;
            }),
        devices.end());

    LOG(Info, "AirPods devices count: {} (filtered)", devices.size());
    return devices;
}

} // namespace Core::AirPods
