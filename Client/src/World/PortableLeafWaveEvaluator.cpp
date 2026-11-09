#include "World/PortableLeafWaveEvaluator.h"

#include "Core/Expected.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/ILeafWaveEvaluator.h"

#include <bit>
#include <cstdint>
#include <system_error>

namespace Client::World
{
Core::Expected<void, std::error_code> PortableLeafWaveEvaluator::Evaluate(LeafWave wave, float time)
{
    if (m_pending.has_value())
    {
        m_pending.reset();
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    switch (wave)
    {
    case LeafWave::First:
        m_pending = Render::Models::Detail::FirstLeafSine(time).value;
        return {};
    case LeafWave::Second:
        m_pending = Render::Models::Detail::SecondLeafSine(time).value;
        return {};
    }
    return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
}

Core::Expected<std::int32_t, std::error_code> PortableLeafWaveEvaluator::Truncate()
{
    if (!m_pending.has_value())
    {
        return Core::Unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    const auto retained = *m_pending;
    m_pending.reset();
    const auto conversion = Render::Models::Detail::TruncateLeafSine(retained);
    if (!conversion.valid)
    {
        return std::int32_t{0};
    }
    const auto lowBits = static_cast<std::uint32_t>(conversion.integer);
    return std::bit_cast<std::int32_t>(lowBits);
}
} // namespace Client::World
