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

#include "Aap.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

#include <Windows.h>

namespace Core::Aap {

// Talks to the AapL2cap KMDF driver: one handle on its device interface is one L2CAP channel to
// PSM 0x1001, `ReadFile`/`WriteFile` move one SDU each. The reader thread owns the handle.
//
class WindowsBackend final : public Backend
{
public:
    WindowsBackend();
    ~WindowsBackend() override;

    void SetSink(Sink *sink) override;
    bool IsAvailable() override;
    void Connect() override;
    void Disconnect() override;
    bool Send(const Bytes &packet) override;

    static std::optional<std::wstring> FindInterfacePath();

private:
    std::mutex _mutex;
    Sink *_sink{};
    bool _open{false};
    std::deque<Bytes> _outbox;
    HANDLE _wakeEvent{};
    std::atomic<bool> _workerRunning{false};
    std::jthread _thread;

    void Worker(std::stop_token stopToken);
    void NotifyLink(bool up);
    void NotifyPacket(Bytes packet);
    bool WritePacket(HANDLE handle, HANDLE event, const Bytes &packet);
};

} // namespace Core::Aap
