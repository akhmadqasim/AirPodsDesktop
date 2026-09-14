#include <deque>
#include <memory>
#include <vector>

#include <QtTest>

#include "Source/Core/Aap.h"
#include "Source/Core/AapProtocol.h"

using namespace std::chrono_literals;
using Core::AapProtocol::Bytes;
using Core::AapProtocol::EarState;
using Core::AapProtocol::EarStatus;
using Core::AapProtocol::ListeningMode;

namespace {

Bytes Packet(std::initializer_list<uint8_t> bytes)
{
    return Bytes{bytes};
}

// Records what the client sends and lets a test play the AirPods' side.
class FakeBackend final : public Core::Aap::Backend
{
public:
    Core::Aap::Sink *sink{};
    int connectCalls{0};
    int disconnectCalls{0};
    std::vector<Bytes> sent;
    bool open{false};
    bool available{true};

    void SetSink(Core::Aap::Sink *value) override
    {
        sink = value;
    }

    bool IsAvailable() override
    {
        return available;
    }

    void Connect() override
    {
        ++connectCalls;
    }

    void Disconnect() override
    {
        ++disconnectCalls;
        open = false;
    }

    bool Send(const Bytes &packet) override
    {
        if (!open) {
            return false;
        }
        sent.push_back(packet);
        return true;
    }

    void LinkUp()
    {
        open = true;
        sink->OnLinkChanged(true);
    }

    void LinkDown()
    {
        open = false;
        sink->OnLinkChanged(false);
    }

    void Receive(Bytes packet)
    {
        sink->OnPacketReceived(std::move(packet));
    }
};

const Core::Aap::Client::Intervals kFastIntervals{
    .handshakeTimeout = 40ms,
    .setupStep = 10ms,
    .retry = 30ms,
};
} // namespace

class AapTests : public QObject
{
    Q_OBJECT

private slots:
    void ParsesHandshakeAck()
    {
        auto event = Core::AapProtocol::Parse(
            Packet({0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00}));
        QVERIFY(event.has_value());
        QVERIFY(std::holds_alternative<Core::AapProtocol::HandshakeAck>(*event));
    }

    void ParsesEarStatus()
    {
        auto event = Core::AapProtocol::Parse(Packet({0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x01}));
        QVERIFY(event.has_value());
        auto *status = std::get_if<EarStatus>(&*event);
        QVERIFY(status != nullptr);
        QCOMPARE(status->primary, EarState::InEar);
        QCOMPARE(status->secondary, EarState::Out);
        QVERIFY(!status->IsBothInEar());

        const EarStatus bothIn{EarState::InEar, EarState::InEar};
        const EarStatus bothInCase{EarState::InCase, EarState::InCase};
        QVERIFY(bothIn.IsBothInEar());
        QVERIFY(!bothInCase.IsBothInEar());
    }

    void ParsesListeningModeAndIgnoresOtherControls()
    {
        auto mode = Core::AapProtocol::Parse(
            Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x03, 0x00, 0x00, 0x00}));
        QVERIFY(mode.has_value());
        QCOMPARE(std::get<Core::AapProtocol::ListeningModeStatus>(*mode).mode,
                 ListeningMode::Transparency);

        QVERIFY(!Core::AapProtocol::Parse(
                     Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x28, 0x02, 0x00, 0x00, 0x00}))
                     .has_value());
        QVERIFY(!Core::AapProtocol::Parse(
                     Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x09, 0x00, 0x00, 0x00}))
                     .has_value());
    }

    void ParsesBatteryReport()
    {
        using namespace Core::AapProtocol;
        // Right (primary) 100 % discharging, Left 100 % discharging, Case disconnected.
        auto event = Core::AapProtocol::Parse(Packet({0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03,
                                                      0x02, 0x01, 0x64, 0x02, 0x01,
                                                      0x04, 0x01, 0x5A, 0x01, 0x01,
                                                      0x08, 0x01, 0x00, 0x04, 0x01}));
        QVERIFY(event.has_value());
        auto *battery = std::get_if<BatteryStatus>(&*event);
        QVERIFY(battery != nullptr);
        QCOMPARE(battery->primary, std::optional{BatteryComponent::Right});
        const std::optional<BatteryReading> right{BatteryReading{100, ChargeStatus::Discharging}};
        const std::optional<BatteryReading> left{BatteryReading{90, ChargeStatus::Charging}};
        QCOMPARE(battery->right, right);
        QCOMPARE(battery->left, left);
        QVERIFY(!battery->caseBox.has_value());

        // Truncated entry list.
        QVERIFY(!Core::AapProtocol::Parse(
                     Packet({0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x02, 0x02, 0x01, 0x64, 0x02, 0x01}))
                     .has_value());
    }

    void RejectsMalformedPackets()
    {
        QVERIFY(!Core::AapProtocol::Parse({}).has_value());
        QVERIFY(!Core::AapProtocol::Parse(Packet({0x04, 0x00, 0x04, 0x00, 0x06})).has_value());
        QVERIFY(!Core::AapProtocol::Parse(Packet({0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00}))
                     .has_value());
        QVERIFY(!Core::AapProtocol::Parse(Packet({0x04, 0x00, 0x04, 0x00, 0x04, 0x00}))
                     .has_value());
        QVERIFY(!Core::AapProtocol::Parse(Packet({0x05, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00}))
                     .has_value());
    }

    void BuildsControlPackets()
    {
        QCOMPARE(Core::AapProtocol::SetListeningMode(ListeningMode::Anc),
                 Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x02, 0x00, 0x00, 0x00}));
        QCOMPARE(Core::AapProtocol::AllowOffListeningMode(),
                 Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x34, 0x01, 0x00, 0x00, 0x00}));
        QCOMPARE(Core::AapProtocol::Handshake().size(), size_t{16});
        QCOMPARE(Core::AapProtocol::Handshake()[4], uint8_t{0x01});
    }

    void RunsSetupSequenceAfterHandshakeAck()
    {
        auto backend = std::make_shared<FakeBackend>();
        Core::Aap::Client client{backend, kFastIntervals};
        QSignalSpy ready{&client, &Core::Aap::Client::ReadyChanged};
        QSignalSpy link{&client, &Core::Aap::Client::LinkChanged};

        client.SetDeviceConnected(true);
        QCOMPARE(backend->connectCalls, 1);

        backend->LinkUp();
        QTRY_COMPARE(backend->sent.size(), size_t{1});
        QCOMPARE(backend->sent[0], Core::AapProtocol::Handshake());
        QCOMPARE(link.count(), 1);
        QCOMPARE(link[0][0].toBool(), true);

        backend->Receive(Packet({0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00}));
        QTRY_COMPARE(backend->sent.size(), size_t{4});
        QCOMPARE(backend->sent[1], Core::AapProtocol::RequestFeatureCapabilities());
        QCOMPARE(backend->sent[2], Core::AapProtocol::EnableNotifications());
        QCOMPARE(backend->sent[3], Core::AapProtocol::AllowOffListeningMode());
        QVERIFY(client.IsReady());
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ready[0][0].toBool(), true);
    }

    void ContinuesSetupWithoutHandshakeAck()
    {
        auto backend = std::make_shared<FakeBackend>();
        Core::Aap::Client client{backend, kFastIntervals};

        client.SetDeviceConnected(true);
        backend->LinkUp();
        QTRY_VERIFY(client.IsReady());
        QCOMPARE(backend->sent.size(), size_t{4});
    }

    void ForwardsEarAndModeEventsAndCachesThem()
    {
        auto backend = std::make_shared<FakeBackend>();
        Core::Aap::Client client{backend, kFastIntervals};
        QSignalSpy ears{&client, &Core::Aap::Client::EarStatusChanged};
        QSignalSpy modes{&client, &Core::Aap::Client::ListeningModeChanged};

        client.SetDeviceConnected(true);
        backend->LinkUp();
        QTRY_VERIFY(client.IsReady());

        backend->Receive(Packet({0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00}));
        backend->Receive(Packet({0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x01, 0x00}));
        QCOMPARE(ears.count(), 2);
        QCOMPARE(client.CurrentEarStatus(), (EarStatus{EarState::Out, EarState::InEar}));

        backend->Receive(Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x02, 0x00, 0x00, 0x00}));
        backend->Receive(Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x02, 0x00, 0x00, 0x00}));
        backend->Receive(Packet({0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x04, 0x00, 0x00, 0x00}));
        QCOMPARE(modes.count(), 2);
        QCOMPARE(client.CurrentListeningMode(), std::optional{ListeningMode::Adaptive});

        QVERIFY(client.SetListeningMode(ListeningMode::Off));
        QCOMPARE(backend->sent.back(), Core::AapProtocol::SetListeningMode(ListeningMode::Off));
    }

    void RetriesWhileDeviceStaysConnected()
    {
        auto backend = std::make_shared<FakeBackend>();
        Core::Aap::Client client{backend, kFastIntervals};
        QSignalSpy ready{&client, &Core::Aap::Client::ReadyChanged};

        client.SetDeviceConnected(true);
        QCOMPARE(backend->connectCalls, 1);

        // Attempt failed before the link ever came up.
        backend->LinkDown();
        QTRY_COMPARE(backend->connectCalls, 2);

        backend->LinkUp();
        QTRY_VERIFY(client.IsReady());

        // Remote closed an established link.
        backend->LinkDown();
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(ready[1][0].toBool(), false);
        QVERIFY(!client.IsReady());
        QVERIFY(!client.CurrentListeningMode().has_value());
        QTRY_COMPARE(backend->connectCalls, 3);
    }

    void DisconnectsAndStopsRetryingWhenDeviceGoesAway()
    {
        auto backend = std::make_shared<FakeBackend>();
        Core::Aap::Client client{backend, kFastIntervals};
        QSignalSpy link{&client, &Core::Aap::Client::LinkChanged};

        client.SetDeviceConnected(true);
        backend->LinkUp();
        QTRY_VERIFY(client.IsReady());

        client.SetDeviceConnected(false);
        QCOMPARE(backend->disconnectCalls, 1);
        QCOMPARE(link.count(), 2);
        QCOMPARE(link[1][0].toBool(), false);
        QVERIFY(!client.SetListeningMode(ListeningMode::Anc));

        QTest::qWait(100);
        QCOMPARE(backend->connectCalls, 1);
    }

    void StaysIdleWithoutTransport()
    {
        auto backend = std::make_shared<FakeBackend>();
        backend->available = false;
        Core::Aap::Client client{backend, kFastIntervals};

        client.SetDeviceConnected(true);
        QTest::qWait(100);
        QCOMPARE(backend->connectCalls, 0);

        // The driver got installed in the meantime; the next device connection picks it up.
        backend->available = true;
        client.SetDeviceConnected(false);
        client.SetDeviceConnected(true);
        QCOMPARE(backend->connectCalls, 1);
    }

    void IgnoresConnectWhileDisconnected()
    {
        auto backend = std::make_shared<FakeBackend>();
        Core::Aap::Client client{backend, kFastIntervals};

        client.SetDeviceConnected(false);
        QTest::qWait(50);
        QCOMPARE(backend->connectCalls, 0);
    }
};

QTEST_GUILESS_MAIN(AapTests)
#include "AapTests.moc"
