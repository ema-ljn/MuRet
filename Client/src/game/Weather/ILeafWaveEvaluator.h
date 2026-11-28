#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <system_error>

namespace Client::Weather
{
enum class LeafWave : std::uint8_t
{
    First,
    Second,
};

// Numeric boundary of 0x530490/0x69fd3c (wave-order-v1). Evaluate retains the intermediate until Truncate, which hands
// over the low 32 bits of its truncated signed64 conversion, with no f32 or double spill between the two callbacks.
// With an invalid conversion they are those of the masked indefinite integer, 0x8000000000000000 (low32 = 0), as in
// ADR0012's profile and in FtolLow. The binding follows ADR0014's retained profile (FirstLeafSine, SecondLeafSine and
// TruncateLeafSine of OriginalRetainedMath): the sine of the R53 argument rounded once to 64 bits, to nearest. It is a
// documented portable model, not the x87's FSIN: ADR0014 records at which values they differ. The interface does not
// use std::sin or provide a clock, RNG or persistent weather state.
class ILeafWaveEvaluator
{
public:
    virtual ~ILeafWaveEvaluator() = default;
    [[nodiscard]] virtual Core::Expected<void, std::error_code> Evaluate(LeafWave wave, float time) = 0;
    [[nodiscard]] virtual Core::Expected<std::int32_t, std::error_code> Truncate() = 0;
};
} // namespace Client::Weather
