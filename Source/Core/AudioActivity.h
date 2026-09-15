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
#include <thread>

#include <QObject>

namespace Core::AudioActivity {

// Turns a stream of peak-meter samples into "audio started" / "audio stopped" decisions. A few
// consecutive loud samples mean playback started; a longer run of silence means it stopped, so
// the gaps between tracks do not count as a fresh start.
//
class Detector
{
public:
    struct Options {
        float threshold{0.001f};
        int onSamples{4};   // ~2 s at 500 ms
        int offSamples{10}; // ~5 s at 500 ms
    };

    // `assumePlaying` starts the detector as if audio were already on: the first "started" then
    // needs a real pause first, so watching that begins mid-playback does not fire at once.
    explicit Detector(Options options = {}, bool assumePlaying = false);

    // The new activity when this sample flips it, nothing otherwise.
    std::optional<bool> Feed(float peak);
    bool IsActive() const;

private:
    Options _options;
    bool _active;
    int _run{0};
};

class Backend
{
public:
    virtual ~Backend() = default;

    // Called on the polling thread around its lifetime, for per-thread setup such as COM.
    virtual void OnPollingStarted() {}
    virtual void OnPollingStopped() {}
    // Peak level (0..1) of whatever the system is rendering right now; nothing when unknown.
    virtual std::optional<float> SamplePeak() = 0;
};

class NullBackend final : public Backend
{
public:
    std::optional<float> SamplePeak() override
    {
        return std::nullopt;
    }
};

// Polls the backend on its own thread while asked to watch, and reports on the GUI thread when
// audio starts or stops. Idle (no thread, no polling) otherwise.
//
class Monitor : public QObject
{
    Q_OBJECT

public:
    explicit Monitor(
        std::shared_ptr<Backend> backend,
        std::chrono::milliseconds interval = std::chrono::milliseconds{500},
        Detector::Options options = {});
    ~Monitor() override;

    void Watch(bool assumePlaying);
    void Stop();
    bool IsWatching() const;

Q_SIGNALS:
    void ActivityChanged(bool active);

private:
    struct Link {
        std::mutex mutex;
        Monitor *monitor{};
    };

    std::shared_ptr<Backend> _backend;
    std::chrono::milliseconds _interval;
    Detector::Options _options;
    std::shared_ptr<Link> _link{std::make_shared<Link>()};
    std::jthread _worker;
};

} // namespace Core::AudioActivity
