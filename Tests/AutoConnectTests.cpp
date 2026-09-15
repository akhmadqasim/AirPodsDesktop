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

#include <QtTest>

#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

#include "Source/Core/AudioActivity.h"
#include "Source/Core/AutoConnect.h"

using namespace std::chrono_literals;
using Core::AudioActivity::Detector;
using Core::AudioActivity::Monitor;
using Core::AutoConnect::Coordinator;
using Core::AutoConnect::Policy;

namespace {

// Hands out a scripted sequence of peak samples, then repeats the last one.
class ScriptedAudio final : public Core::AudioActivity::Backend
{
public:
    std::mutex mutex;
    std::deque<float> samples;
    float last{0.0f};
    std::atomic<int> started{};
    std::atomic<int> stopped{};

    void OnPollingStarted() override
    {
        ++started;
    }

    void OnPollingStopped() override
    {
        ++stopped;
    }

    std::optional<float> SamplePeak() override
    {
        std::lock_guard<std::mutex> lock{mutex};
        if (!samples.empty()) {
            last = samples.front();
            samples.pop_front();
        }
        return last;
    }

    void Play(float level, int count)
    {
        std::lock_guard<std::mutex> lock{mutex};
        for (int i = 0; i < count; ++i) {
            samples.push_back(level);
        }
    }
};

class RecordingQuickConnect final : public Core::QuickConnect::Backend
{
public:
    std::atomic<int> reconnectCalls{};
    QString lastId;
    std::mutex mutex;

    std::vector<Core::QuickConnect::Device> ListDevices(std::stop_token) override
    {
        return {{"{AIRPODS}", "AirPods", false}};
    }

    bool RequestReconnect(const QString &id, std::stop_token) override
    {
        std::lock_guard<std::mutex> lock{mutex};
        ++reconnectCalls;
        lastId = id;
        return true;
    }
};

Core::AirPods::State InEarState(bool left, bool right)
{
    Core::AirPods::State state;
    state.model = Core::AirPods::Model::AirPods_Pro_2;
    state.pods.left.isInEar = left;
    state.pods.right.isInEar = right;
    return state;
}

} // namespace

class AutoConnectTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void DetectorNeedsARunToFlip();
    void DetectorAssumingPlaybackWaitsForAPause();
    void PolicyGates();
    void MonitorReportsEdgesOnTheGuiThread();
    void CoordinatorWatchesOnlyWhileWorthIt();
    void CoordinatorConnectsWhenAudioStarts();
    void CoordinatorIgnoresAudioAlreadyPlayingAfterThePodsLeft();
};

void AutoConnectTests::DetectorNeedsARunToFlip()
{
    Detector detector{{.threshold = 0.01f, .onSamples = 3, .offSamples = 4}};
    QVERIFY(!detector.IsActive());

    QVERIFY(!detector.Feed(0.5f).has_value());
    QVERIFY(!detector.Feed(0.5f).has_value());
    QVERIFY(!detector.Feed(0.0f).has_value()); // the run breaks
    QVERIFY(!detector.Feed(0.5f).has_value());
    QVERIFY(!detector.Feed(0.5f).has_value());
    const auto started = detector.Feed(0.5f);
    QVERIFY(started.has_value() && *started);
    QVERIFY(detector.IsActive());

    // A short gap between tracks is not a stop.
    for (int i = 0; i < 3; ++i) {
        QVERIFY(!detector.Feed(0.0f).has_value());
    }
    QVERIFY(!detector.Feed(0.5f).has_value());
    for (int i = 0; i < 3; ++i) {
        QVERIFY(!detector.Feed(0.0f).has_value());
    }
    const auto stopped = detector.Feed(0.0f);
    QVERIFY(stopped.has_value() && !*stopped);
}

void AutoConnectTests::DetectorAssumingPlaybackWaitsForAPause()
{
    Detector detector{{.threshold = 0.01f, .onSamples = 2, .offSamples = 2}, true};
    QVERIFY(detector.IsActive());
    for (int i = 0; i < 10; ++i) {
        QVERIFY(!detector.Feed(0.5f).has_value());
    }
    QVERIFY(!detector.Feed(0.0f).has_value());
    const auto stopped = detector.Feed(0.0f);
    QVERIFY(stopped.has_value() && !*stopped);
    QVERIFY(!detector.Feed(0.5f).has_value());
    const auto started = detector.Feed(0.5f);
    QVERIFY(started.has_value() && *started);
}

void AutoConnectTests::PolicyGates()
{
    Policy policy;
    policy.options = {.earStaleAfter = 10s, .cooldownAfterLinkDrop = 30s, .cooldownAfterFailure = 60s};
    const auto now = Policy::Clock::now();

    QVERIFY(!policy.ShouldWatch(now));
    policy.enabled = true;
    QVERIFY(!policy.ShouldWatch(now)); // nothing in an ear
    policy.lastInEar = now - 5s;
    QVERIFY(policy.ShouldWatch(now));
    QVERIFY(policy.ShouldConnect(now));
    policy.lastInEar = now - 11s;
    QVERIFY(!policy.ShouldWatch(now)); // stale
    policy.lastInEar = now;

    policy.deviceConnected = true;
    QVERIFY(!policy.ShouldWatch(now)); // already here
    policy.deviceConnected = false;

    policy.linkDroppedAt = now - 10s;
    QVERIFY(policy.ShouldWatch(now));
    QVERIFY(!policy.ShouldConnect(now)); // they only just left
    policy.linkDroppedAt = now - 31s;
    QVERIFY(policy.ShouldConnect(now));

    policy.failedAt = now - 30s;
    QVERIFY(!policy.ShouldConnect(now));
    policy.failedAt = now - 61s;
    QVERIFY(policy.ShouldConnect(now));
}

void AutoConnectTests::MonitorReportsEdgesOnTheGuiThread()
{
    auto audio = std::make_shared<ScriptedAudio>();
    Monitor monitor{audio, 5ms, {.threshold = 0.01f, .onSamples = 2, .offSamples = 2}};
    QSignalSpy spy{&monitor, &Monitor::ActivityChanged};

    QVERIFY(!monitor.IsWatching());
    audio->Play(0.0f, 2);
    audio->Play(0.5f, 6);
    audio->Play(0.0f, 6);
    monitor.Watch(false);
    QVERIFY(monitor.IsWatching());

    QTRY_COMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QCOMPARE(spy.at(1).at(0).toBool(), false);

    monitor.Stop();
    QVERIFY(!monitor.IsWatching());
    QCOMPARE(audio->started.load(), 1);
    QCOMPARE(audio->stopped.load(), 1);
}

void AutoConnectTests::CoordinatorWatchesOnlyWhileWorthIt()
{
    auto audio = std::make_shared<ScriptedAudio>();
    Monitor monitor{audio, 5ms};
    auto backend = std::make_shared<RecordingQuickConnect>();
    Core::QuickConnect::Controller quickConnect{backend};
    Coordinator coordinator{
        monitor, quickConnect, [] { return QString{"{AIRPODS}"}; }, {.earStaleAfter = 100ms}};

    coordinator.SetEnabled(true);
    QVERIFY(!monitor.IsWatching()); // nothing in an ear yet

    coordinator.OnStateUpdated(InEarState(true, false));
    QVERIFY(monitor.IsWatching());

    coordinator.OnDeviceConnectionChanged(true);
    QVERIFY(!monitor.IsWatching()); // the pods are here already
    coordinator.OnDeviceConnectionChanged(false);
    QVERIFY(monitor.IsWatching());

    coordinator.OnStateUpdated(InEarState(false, false));
    QVERIFY(!monitor.IsWatching());

    coordinator.OnStateUpdated(InEarState(false, true));
    QVERIFY(monitor.IsWatching());
    // No more reports: the ear information goes stale and polling stops.
    QTRY_VERIFY_WITH_TIMEOUT(!monitor.IsWatching(), 2000);

    coordinator.OnStateUpdated(InEarState(true, true));
    QVERIFY(monitor.IsWatching());
    coordinator.SetEnabled(false);
    QVERIFY(!monitor.IsWatching());
}

void AutoConnectTests::CoordinatorConnectsWhenAudioStarts()
{
    auto audio = std::make_shared<ScriptedAudio>();
    Monitor monitor{audio, 5ms, {.threshold = 0.01f, .onSamples = 2, .offSamples = 2}};
    auto backend = std::make_shared<RecordingQuickConnect>();
    Core::QuickConnect::Controller quickConnect{backend};
    Coordinator coordinator{monitor, quickConnect, [] { return QString{"{AIRPODS}"}; }};
    QSignalSpy attempts{&coordinator, &Coordinator::AttemptStarted};
    QSignalSpy loud{&quickConnect, &Core::QuickConnect::Controller::OutcomeChanged};
    QSignalSpy quiet{&quickConnect, &Core::QuickConnect::Controller::QuietOutcomeChanged};

    coordinator.SetEnabled(true);
    coordinator.OnStateUpdated(InEarState(true, false));
    QVERIFY(monitor.IsWatching());

    audio->Play(0.6f, 100);
    QTRY_COMPARE(attempts.count(), 1);
    QTRY_COMPARE(backend->reconnectCalls.load(), 1);
    {
        std::lock_guard<std::mutex> lock{backend->mutex};
        QCOMPARE(backend->lastId, QString{"{AIRPODS}"});
    }
    // Automation stays out of the tray balloons.
    QTRY_VERIFY(quiet.count() >= 1);
    QCOMPARE(loud.count(), 0);

    coordinator.OnDeviceConnectionChanged(true);
    QVERIFY(!monitor.IsWatching());
}

void AutoConnectTests::CoordinatorIgnoresAudioAlreadyPlayingAfterThePodsLeft()
{
    auto audio = std::make_shared<ScriptedAudio>();
    Monitor monitor{audio, 5ms, {.threshold = 0.01f, .onSamples = 2, .offSamples = 2}};
    auto backend = std::make_shared<RecordingQuickConnect>();
    Core::QuickConnect::Controller quickConnect{backend};
    Coordinator coordinator{
        monitor, quickConnect, [] { return QString{"{AIRPODS}"}; },
        {.cooldownAfterLinkDrop = 0s}};
    QSignalSpy attempts{&coordinator, &Coordinator::AttemptStarted};

    coordinator.SetEnabled(true);
    coordinator.OnStateUpdated(InEarState(true, true));
    coordinator.OnDeviceConnectionChanged(true);
    QVERIFY(!monitor.IsWatching());

    // Music is playing here as the pods leave for the phone: that must not pull them back.
    audio->Play(0.6f, 40);
    coordinator.OnDeviceLinkChanged(false);
    QVERIFY(monitor.IsWatching());
    QTest::qWait(300);
    QCOMPARE(attempts.count(), 0);

    // A pause and a fresh start on this PC does.
    audio->Play(0.0f, 4);
    audio->Play(0.6f, 100);
    QTRY_COMPARE(attempts.count(), 1);
}

QTEST_MAIN(AutoConnectTests)
#include "AutoConnectTests.moc"
