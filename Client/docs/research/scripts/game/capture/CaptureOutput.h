#pragma once

#include <cstdio>
#include <utility>

namespace Client::Capture
{
class CaptureOutput
{
public:
    CaptureOutput() = default;
    CaptureOutput(const CaptureOutput&) = delete;
    CaptureOutput& operator=(const CaptureOutput&) = delete;

    ~CaptureOutput() noexcept
    {
        if (m_owned && m_stream != nullptr)
            std::fclose(m_stream);
    }

    [[nodiscard]] bool Redirect(const char* path) noexcept
    {
        m_stream = std::freopen(path, "w", stdout);
        m_owned = m_stream != nullptr;
        return m_owned;
    }

    // fclose también puede fallar después de un fflush exitoso.
    [[nodiscard]] bool Finish() noexcept
    {
        if (m_stream == nullptr)
            return true;
        FILE* stream = std::exchange(m_stream, nullptr);
        const bool flushed = std::fflush(stream) == 0;
        const bool written = std::ferror(stream) == 0;
        const bool closed = !m_owned || std::fclose(stream) == 0;
        return flushed && written && closed;
    }

private:
    FILE* m_stream = stdout;
    bool m_owned = false;
};
} // namespace Client::Capture
