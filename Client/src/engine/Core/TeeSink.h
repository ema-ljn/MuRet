#pragma once

#include "Core/ILogSink.h"
#include "EngineApi.h"

#include <mutex>
#include <string_view>

// Its std::mutex makes both sinks get the lines in the same order even from two threads; each sink's own mutex orders
// only its own writes. The locks are always taken in the same order (the Tee's, then each sink's), so they cannot
// deadlock.
namespace Client::Core
{
class ENGINE_API TeeSink final : public ILogSink
{
public:
    // Both sinks must outlive the Tee.
    TeeSink(ILogSink& first, ILogSink& second);

    void Write(LogLevel level, std::string_view line) override;

private:
    ILogSink& m_first;
    ILogSink& m_second;
    std::mutex m_mutex;
};
} // namespace Client::Core
