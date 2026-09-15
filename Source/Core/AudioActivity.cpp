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

#include "AudioActivity.h"

#include <condition_variable>

#include <QMetaObject>

#include "../Logger.h"

namespace Core::AudioActivity {

Detector::Detector(Options options, bool assumePlaying)
    : _options{options}, _active{assumePlaying}
{
}

std::optional<bool> Detector::Feed(float peak)
{
    const bool loud = peak >= _options.threshold;
    if (loud == _active) {
        _run = 0;
        return std::nullopt;
    }

    ++_run;
    const int needed = loud ? _options.onSamples : _options.offSamples;
    if (_run < needed) {
        return std::nullopt;
    }

    _run = 0;
    _active = loud;
    return _active;
}

bool Detector::IsActive() const
{
    return _active;
}

Monitor::Monitor(
    std::shared_ptr<Backend> backend, std::chrono::milliseconds interval, Detector::Options options)
    : _backend{std::move(backend)}, _interval{interval}, _options{options}
{
    _link->monitor = this;
}

Monitor::~Monitor()
{
    {
        std::lock_guard<std::mutex> lock{_link->mutex};
        _link->monitor = nullptr;
    }
    Stop();
}

void Monitor::Watch(bool assumePlaying)
{
    if (_worker.joinable()) {
        return;
    }

    LOG(Info, "AudioActivity: watching (assumePlaying: {})", assumePlaying);

    _worker = std::jthread{[backend = _backend, link = _link, interval = _interval,
                            options = _options, assumePlaying](std::stop_token stopToken) {
        backend->OnPollingStarted();
        Detector detector{options, assumePlaying};
        std::mutex sleepMutex;
        std::condition_variable_any sleeper;

        while (!stopToken.stop_requested()) {
            const auto sample = backend->SamplePeak();
            if (const auto edge = detector.Feed(sample.value_or(0.0f)); edge.has_value()) {
                std::lock_guard<std::mutex> lock{link->mutex};
                if (link->monitor != nullptr) {
                    QMetaObject::invokeMethod(
                        link->monitor,
                        [monitor = link->monitor, active = *edge] {
                            LOG(Info, "AudioActivity: {}", active ? "started" : "stopped");
                            emit monitor->ActivityChanged(active);
                        },
                        Qt::QueuedConnection);
                }
            }

            std::unique_lock<std::mutex> lock{sleepMutex};
            sleeper.wait_for(lock, stopToken, interval, [] { return false; });
        }
        backend->OnPollingStopped();
    }};
}

void Monitor::Stop()
{
    if (!_worker.joinable()) {
        return;
    }
    LOG(Info, "AudioActivity: stopped watching");
    _worker.request_stop();
    _worker.join();
}

bool Monitor::IsWatching() const
{
    return _worker.joinable();
}

} // namespace Core::AudioActivity
