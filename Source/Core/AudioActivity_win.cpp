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

#include "AudioActivity_win.h"

#include "../Logger.h"

namespace Core::AudioActivity {

void WindowsBackend::OnPollingStarted()
{
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    _comInitialized = SUCCEEDED(result);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        LOG(Warn, "AudioActivity: CoInitializeEx failed. HRESULT: {:#x}", result);
    }
}

void WindowsBackend::OnPollingStopped()
{
    _meter = {};
    _meterEndpointId.clear();
    _enumerator = {};
    if (_comInitialized) {
        CoUninitialize();
        _comInitialized = false;
    }
}

bool WindowsBackend::ResolveMeter()
{
    if (!_enumerator) {
        const HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, _enumerator.GetIID(),
            reinterpret_cast<void **>(_enumerator.ReleaseAndAddressOf()));
        if (FAILED(result)) {
            LOG(Warn, "AudioActivity: create MMDeviceEnumerator failed. HRESULT: {:#x}", result);
            return false;
        }
    }

    OS::Windows::Com::UniquePtr<IMMDevice> endpoint;
    HRESULT result =
        _enumerator->GetDefaultAudioEndpoint(eRender, eConsole, endpoint.ReleaseAndAddressOf());
    if (FAILED(result)) {
        _meter = {};
        _meterEndpointId.clear();
        return false;
    }

    LPWSTR rawId{};
    result = endpoint->GetId(&rawId);
    if (FAILED(result)) {
        return _meter ? true : false;
    }
    const QString endpointId = QString::fromWCharArray(rawId);
    CoTaskMemFree(rawId);

    if (_meter && endpointId == _meterEndpointId) {
        return true;
    }

    // The default endpoint changed (or this is the first sample): meter the new one.
    _meter = {};
    result = endpoint->Activate(
        __uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr,
        reinterpret_cast<void **>(_meter.ReleaseAndAddressOf()));
    if (FAILED(result)) {
        LOG(Warn, "AudioActivity: activate IAudioMeterInformation failed. HRESULT: {:#x}", result);
        _meterEndpointId.clear();
        return false;
    }
    _meterEndpointId = endpointId;
    LOG(Info, "AudioActivity: metering the default render endpoint");
    return true;
}

std::optional<float> WindowsBackend::SamplePeak()
{
    if (!ResolveMeter()) {
        return std::nullopt;
    }

    float peak{};
    const HRESULT result = _meter->GetPeakValue(&peak);
    if (FAILED(result)) {
        // The endpoint probably went away; the next sample re-resolves.
        _meter = {};
        _meterEndpointId.clear();
        return std::nullopt;
    }
    return peak;
}

} // namespace Core::AudioActivity
