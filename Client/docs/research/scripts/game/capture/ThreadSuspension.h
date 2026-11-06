#pragma once

#include "CaptureHandle.h"

#include <windows.h>

#include <optional>

namespace Client::Capture
{
class ThreadSuspension
{
public:
    struct CaptureResult
    {
        DWORD prior = 0;
        DWORD resumed = MAXDWORD;
        DWORD readError = 0;
        DWORD resumeError = 0;
        BOOL read = FALSE;
    };

    static constexpr DWORD RecoveryMs = 250;

    explicit ThreadSuspension(HANDLE thread) noexcept : m_thread(thread) {}
    ThreadSuspension(const ThreadSuspension&) = delete;
    ThreadSuspension& operator=(const ThreadSuspension&) = delete;

    ~ThreadSuspension() noexcept
    {
        // Solo fallback: el flujo normal recupera explícitamente antes de cerrar.
        // No abandonar el incremento propio, tampoco ante una salida imprevista.
        while (m_owed && !ConfirmTargetExit())
        {
            Sleep(RecoveryMs);
            static_cast<void>(TryResume());
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept
    {
        return m_thread.Get() != nullptr;
    }

    [[nodiscard]] std::optional<DWORD> SuspendAndCapture(CONTEXT& context) noexcept
    {
        m_capture.prior = SuspendThread(m_thread.Get());
        if (m_capture.prior == MAXDWORD)
            return GetLastError();
        m_owed = true;
        // Sin I/O ni asignación dinámica entre Suspend y el primer Resume.
        // Nunca SetThreadContext; GetLastError debe anteceder a cualquier otra API.
        m_capture.read = GetThreadContext(m_thread.Get(), &context);
        m_capture.readError = m_capture.read ? 0 : GetLastError();
        static_cast<void>(TryResume());
        return std::nullopt;
    }

    [[nodiscard]] const CaptureResult& Result() const noexcept
    {
        return m_capture;
    }

    [[nodiscard]] bool IsPending() const noexcept
    {
        return m_owed;
    }

    [[nodiscard]] bool TryResume() noexcept
    {
        if (!m_owed)
            return true;
        m_capture.resumed = ResumeThread(m_thread.Get());
        m_capture.resumeError = m_capture.resumed == MAXDWORD ? GetLastError() : 0;
        if (m_capture.resumed == MAXDWORD)
            return false;
        // Un decremento exitoso satisface nuestro incremento, aunque queden otros.
        m_owed = false;
        return true;
    }

    [[nodiscard]] bool ConfirmTargetExit() noexcept
    {
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeThread(m_thread.Get(), &exitCode) || exitCode == STILL_ACTIVE)
            return false;
        m_owed = false;
        return true;
    }

    [[nodiscard]] std::optional<DWORD> Close() noexcept
    {
        // No entregar el handle mientras quede una obligación de recuperación.
        if (m_owed)
            return ERROR_BUSY;
        return m_thread.Close();
    }

private:
    CaptureHandle m_thread;
    CaptureResult m_capture;
    bool m_owed = false;
};
} // namespace Client::Capture
