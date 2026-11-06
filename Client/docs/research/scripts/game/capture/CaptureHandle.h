#pragma once

#include <windows.h>

#include <optional>

namespace Client::Capture
{
class CaptureHandle
{
public:
    explicit CaptureHandle(HANDLE handle) noexcept : m_handle(handle) {}
    CaptureHandle(const CaptureHandle&) = delete;
    CaptureHandle& operator=(const CaptureHandle&) = delete;

    ~CaptureHandle() noexcept
    {
        if (m_handle != nullptr)
            CloseHandle(m_handle);
    }

    [[nodiscard]] HANDLE Get() const noexcept
    {
        return m_handle;
    }

    // El cierre normal se comprueba; el destructor solo cubre una salida imprevista.
    [[nodiscard]] std::optional<DWORD> Close() noexcept
    {
        if (m_handle == nullptr)
            return std::nullopt;
        if (!CloseHandle(m_handle))
            return GetLastError();
        m_handle = nullptr;
        return std::nullopt;
    }

private:
    HANDLE m_handle;
};
} // namespace Client::Capture
