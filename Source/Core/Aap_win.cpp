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

#include "Aap_win.h"

#include <array>
#include <cstring>

#include <cfgmgr32.h>

#include "../Logger.h"

#pragma comment(lib, "Cfgmgr32.lib")

namespace Core::Aap {

namespace {

// Device interface class registered by the AapL2cap driver (see E:\AapWin).
constexpr GUID kInterfaceGuid = {
    0x0907fade, 0xe50e, 0x4873, {0xb1, 0x32, 0xd1, 0xbe, 0x9b, 0xdd, 0xac, 0x55}};

// Largest SDU the driver accepts; reads are given twice that to be safe.
constexpr size_t kMaxSdu = 672;
constexpr DWORD kWriteTimeoutMs = 3000;

struct HandleCloser {
    void operator()(HANDLE handle) const
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

std::string LastErrorText(DWORD error)
{
    char message[256]{};
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, message,
        sizeof(message), nullptr);
    std::string text{message};
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}
} // namespace

WindowsBackend::WindowsBackend()
{
    _wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WindowsBackend::~WindowsBackend()
{
    Disconnect();
    CloseHandle(_wakeEvent);
}

void WindowsBackend::SetSink(Sink *sink)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _sink = sink;
}

std::optional<std::wstring> WindowsBackend::FindInterfacePath()
{
    GUID guid = kInterfaceGuid;
    ULONG length = 0;
    if (CM_Get_Device_Interface_List_SizeW(
            &length, &guid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS ||
        length < 2)
    {
        return std::nullopt;
    }

    std::wstring buffer(length, L'\0');
    if (CM_Get_Device_Interface_ListW(
            &guid, nullptr, buffer.data(), length, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
        CR_SUCCESS)
    {
        return std::nullopt;
    }

    // Multi-sz; the first entry is enough, a PC has one pair of AirPods bound at a time.
    std::wstring first{buffer.c_str()};
    return first.empty() ? std::nullopt : std::optional{std::move(first)};
}

bool WindowsBackend::IsAvailable()
{
    return FindInterfacePath().has_value();
}

void WindowsBackend::Connect()
{
    if (_thread.joinable()) {
        if (_workerRunning) {
            return;
        }
        // The previous session ended on its own (remote close); reap it before starting anew.
        _thread.join();
    }
    _workerRunning = true;
    _thread = std::jthread{[this](std::stop_token stopToken) {
        Worker(stopToken);
        _workerRunning = false;
    }};
}

void WindowsBackend::Disconnect()
{
    if (!_thread.joinable()) {
        return;
    }
    _thread.request_stop();
    // `CreateFile` blocks until the L2CAP open completes; try not to wait for its timeout.
    CancelSynchronousIo(_thread.native_handle());
    SetEvent(_wakeEvent);
    _thread.join();
}

bool WindowsBackend::Send(const Bytes &packet)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_open) {
            return false;
        }
        _outbox.push_back(packet);
    }
    SetEvent(_wakeEvent);
    return true;
}

void WindowsBackend::NotifyLink(bool up)
{
    Sink *sink;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        sink = _sink;
    }
    if (sink != nullptr) {
        sink->OnLinkChanged(up);
    }
}

void WindowsBackend::NotifyPacket(Bytes packet)
{
    Sink *sink;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        sink = _sink;
    }
    if (sink != nullptr) {
        sink->OnPacketReceived(std::move(packet));
    }
}

bool WindowsBackend::WritePacket(HANDLE handle, HANDLE event, const Bytes &packet)
{
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    ResetEvent(event);

    DWORD written = 0;
    if (WriteFile(handle, packet.data(), static_cast<DWORD>(packet.size()), &written, &overlapped))
    {
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        LOG(Warn, "AAP: WriteFile failed: {}", LastErrorText(GetLastError()));
        return false;
    }
    if (WaitForSingleObject(event, kWriteTimeoutMs) != WAIT_OBJECT_0) {
        CancelIoEx(handle, &overlapped);
        GetOverlappedResult(handle, &overlapped, &written, TRUE);
        LOG(Warn, "AAP: WriteFile timed out.");
        return false;
    }
    if (!GetOverlappedResult(handle, &overlapped, &written, FALSE)) {
        LOG(Warn, "AAP: WriteFile completion failed: {}", LastErrorText(GetLastError()));
        return false;
    }
    return true;
}

void WindowsBackend::Worker(std::stop_token stopToken)
{
    auto path = FindInterfacePath();
    if (!path.has_value()) {
        LOG(Info, "AAP: No AapL2cap device interface present (driver not installed?).");
        NotifyLink(false);
        return;
    }

    LOG(Info, "AAP: Opening L2CAP channel.");
    UniqueHandle handle{CreateFileW(
        path->c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr)};
    if (handle.get() == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        handle.release();
        LOG(Warn, "AAP: CreateFile failed: {} (0x{:08X})", LastErrorText(error), error);
        NotifyLink(false);
        return;
    }

    UniqueHandle readEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    UniqueHandle writeEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};

    {
        std::lock_guard<std::mutex> lock{_mutex};
        _open = true;
        _outbox.clear();
    }
    NotifyLink(true);

    std::array<uint8_t, kMaxSdu * 2> buffer{};
    OVERLAPPED readOverlapped{};
    bool readPending = false;

    auto finishRead = [&]() -> std::optional<Bytes> {
        DWORD read = 0;
        readPending = false;
        if (!GetOverlappedResult(handle.get(), &readOverlapped, &read, FALSE)) {
            const auto error = GetLastError();
            if (error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_NOT_READY) {
                LOG(Info, "AAP: Channel closed by the remote side.");
            }
            else if (error != ERROR_OPERATION_ABORTED) {
                LOG(Warn, "AAP: ReadFile failed: {} (0x{:08X})", LastErrorText(error), error);
            }
            return std::nullopt;
        }
        return Bytes(buffer.begin(), buffer.begin() + read);
    };

    bool alive = true;
    while (alive && !stopToken.stop_requested()) {
        if (!readPending) {
            readOverlapped = {};
            readOverlapped.hEvent = readEvent.get();
            ResetEvent(readEvent.get());
            DWORD read = 0;
            if (ReadFile(
                    handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                    &readOverlapped))
            {
                NotifyPacket(Bytes(buffer.begin(), buffer.begin() + read));
                continue;
            }
            if (GetLastError() != ERROR_IO_PENDING) {
                LOG(Warn, "AAP: ReadFile failed: {}", LastErrorText(GetLastError()));
                break;
            }
            readPending = true;
        }

        const HANDLE waits[] = {readEvent.get(), _wakeEvent};
        const auto signaled = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (signaled == WAIT_OBJECT_0) {
            if (auto packet = finishRead()) {
                NotifyPacket(std::move(*packet));
            }
            else {
                alive = false;
            }
        }
        else if (signaled == WAIT_OBJECT_0 + 1) {
            std::deque<Bytes> outbox;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                outbox.swap(_outbox);
            }
            for (const auto &packet : outbox) {
                if (!WritePacket(handle.get(), writeEvent.get(), packet)) {
                    alive = false;
                    break;
                }
            }
        }
        else {
            LOG(Warn, "AAP: Wait failed: {}", LastErrorText(GetLastError()));
            alive = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock{_mutex};
        _open = false;
        _outbox.clear();
    }

    if (readPending) {
        CancelIoEx(handle.get(), &readOverlapped);
        DWORD read = 0;
        GetOverlappedResult(handle.get(), &readOverlapped, &read, TRUE);
    }
    handle.reset();

    LOG(Info, "AAP: L2CAP channel closed.");
    NotifyLink(false);
}

} // namespace Core::Aap
