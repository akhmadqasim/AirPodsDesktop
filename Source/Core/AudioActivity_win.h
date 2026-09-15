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

#if !defined APD_OS_WIN
    #error "This file shouldn't be compiled."
#endif

#include "AudioActivity.h"
#include "OS/Windows.h"

#include <QString>

#include <endpointvolume.h>
#include <mmdeviceapi.h>

namespace Core::AudioActivity {

// Reads the peak meter of the default render endpoint, whichever device that is at the moment.
// Everything here runs on the monitor's polling thread.
//
class WindowsBackend final : public Backend
{
public:
    void OnPollingStarted() override;
    void OnPollingStopped() override;
    std::optional<float> SamplePeak() override;

private:
    bool _comInitialized{false};
    OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> _enumerator;
    OS::Windows::Com::UniquePtr<IAudioMeterInformation> _meter;
    QString _meterEndpointId;

    bool ResolveMeter();
};

} // namespace Core::AudioActivity
