#include "Core/Crash/MemoryTotals.h"

#include "Core/Crash/CrashReport.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Client::Core::Crash
{
namespace
{
std::uint32_t Saturated32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::uint32_t>::max()));
}
} // namespace

MemoryStatus32 ToMemoryStatus32(const MemoryTotals& totals, std::uint32_t memoryLoad)
{
    return MemoryStatus32{.memoryLoad = memoryLoad,
                          .totalPhysical = Saturated32(totals.totalPhysical),
                          .availablePhysical = Saturated32(totals.availablePhysical),
                          .totalPageFile = Saturated32(totals.totalPageFile),
                          .availablePageFile = Saturated32(totals.availablePageFile),
                          .totalVirtual = Saturated32(totals.totalVirtual),
                          .availableVirtual = Saturated32(totals.availableVirtual)};
}

std::uint32_t MemoryLoadPercent(std::uint64_t total, std::uint64_t available)
{
    constexpr std::uint64_t Percent = 100;
    if (total == 0)
    {
        return 0;
    }
    const std::uint64_t used = total - std::min(available, total);
    if (used > std::numeric_limits<std::uint64_t>::max() / Percent)
    {
        // used * 100 desbordaría; con un total así de grande, total / 100 no pierde nada que se vea.
        return static_cast<std::uint32_t>(used / (total / Percent));
    }
    return static_cast<std::uint32_t>(used * Percent / total);
}
} // namespace Client::Core::Crash
