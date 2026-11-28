#include "MapScripts/Tarkan/TarkanObjectStep.h"

#include "Core/Expected.h"
#include "Maps/MapId.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/Ftol.h"
#include "Kernel/WorldObject.h"
#include "Terrain/DynamicWorldLight.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::MapScripts::Tarkan
{
namespace
{
using Math::D;
using Math::F32;

// 0x6b89c8 (0.001f), 0x6b8a94 (0.0001f), 0x6b8a64 (0.0002f) and 0x6b8a28 (0.00005f).
constexpr double Thousandth = Math::ThousandthAsDouble;
constexpr double TenThousandth = static_cast<double>(std::bit_cast<float>(0x38d1b717u));
constexpr double TwoTenThousandths = static_cast<double>(std::bit_cast<float>(0x3951b717u));
constexpr double FiveHundredThousandths = static_cast<double>(std::bit_cast<float>(0x3851b717u));
// 0x6b89bc (0.002f), 0x6b8bc4 (0.35f), 0x6b8bc0 (0.65f), 0x6b86b4 (100), 0x6b8a20 (0.6f) and 0x6b8854 (0.2f).
constexpr double PulseRate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
constexpr float PulseAmplitude = std::bit_cast<float>(0x3eb33333u);
constexpr double PulseBase = static_cast<double>(std::bit_cast<float>(0x3f266666u));
constexpr double PhasePerDegree = 100.0;
constexpr double WarmGreen = static_cast<double>(std::bit_cast<float>(0x3f19999au));
constexpr double WarmBlue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr std::int32_t WideLight = 3;
constexpr std::int32_t NarrowLight = 2;

// fld of the clock, ftol, neg in 32 bits (wraps at −2^31), cdq, idiv by `period`, fild of the remainder and fmul
// by `factor`, stored as float.
std::optional<float> Scroll(float worldClock, std::int32_t period, double factor)
{
    const std::optional<std::int32_t> low = Math::OriginalFtolLow(D(worldClock));
    if (!low)
        return std::nullopt;
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*low));
    return F32(static_cast<double>(negated % period) * factor);
}

// fsin of (phase) · 0.002f, with its 64-bit result, fmul by 0.35f with a single R53 and fadd of 0.65f, without
// storing, on the x87.
std::optional<double> Pulse(double phase)
{
    const std::optional<double> wave = Math::Detail::SineProductPC53(phase * PulseRate, PulseAmplitude);
    if (!wave)
        return std::nullopt;
    return *wave + PulseBase;
}

glm::vec3 WarmLight(double pulse)
{
    return {F32(pulse), F32(pulse * WarmGreen), F32(pulse * WarmBlue)};
}

Core::Unexpected<std::string> ClockOutOfDomain(std::string_view address)
{
    return Core::Unexpected(std::format("{}: ftol del reloj del mundo fuera de lo acreditado", address));
}

Core::Unexpected<std::string> SineOutOfDomain(std::string_view address)
{
    return Core::Unexpected(std::format("{}: el seno del reloj no cabe en el perfil PC53", address));
}

Core::Expected<void, std::string> ScrollV(Kernel::WorldObject& object, float worldClock, std::int32_t period,
                                          double factor, std::string_view address)
{
    const std::optional<float> v = Scroll(worldClock, period, factor);
    if (!v)
        return ClockOutOfDomain(address);
    object.state.uv.y = *v;
    return {};
}
} // namespace

void SeedTarkanModelState(std::uint8_t map, Kernel::ObjectModelStepState& state)
{
    constexpr std::uint8_t TarkanMap = Maps::TarkanMap;
    if (map != TarkanMap)
        return;
    // 0x61a150..0x61a18f: bytes 11 · 0xe0 + 0x88 to 79 · 0xe0 + 0x88 of table 0x5769ad8.
    constexpr std::array<std::int16_t, 6> Scrolled{0xb, 0xc, 0xd, 0x49, 0x4b, 0x4f};
    for (const std::int16_t type : Scrolled)
        state.unlitMeshes[type] = 0;
}

float TarkanAnimationSpeed(std::int16_t type, float speed)
{
    constexpr std::int16_t FastObject = 8;
    constexpr float Quadruple = 4.0f;
    return type == FastObject ? speed * Quadruple : speed;
}

Core::Expected<void, std::string> StepTarkanObject(Kernel::WorldObject& object,
                                                   const Kernel::ObjectStepGlobals& globals)
{
    constexpr std::int32_t ShortPeriod = 1000;
    constexpr std::int32_t LongPeriod = 10000;
    constexpr std::int32_t SlowPeriod = 50000;
    const float clock = globals.worldClock;
    switch (object.type)
    {
    case 2:
    {
        // 0x605683: the blendMesh before the ftol.
        object.state.blendMesh = 0;
        const std::optional<float> u = Scroll(clock, ShortPeriod, Thousandth);
        if (!u)
            return ClockOutOfDomain("0x605690");
        object.state.uv.x = *u;
        return {};
    }
    case 4:
    {
        // 0x6056b5..0x6056d6: the blendMesh light is the stored L; the x87 keeps it for the dynamic light.
        object.state.blendMesh = 0;
        const std::optional<double> pulse = Pulse(D(clock));
        if (!pulse)
            return SineOutOfDomain("0x6056c8");
        object.state.light = F32(*pulse);
        if (auto scrolled = ScrollV(object, clock, LongPeriod, TenThousandth, "0x6056df"); !scrolled.HasValue())
            return scrolled;
        const float light = F32(*pulse);
        return Terrain::AddDynamicLight(globals.dynamicLighting, object.transform.position, glm::vec3(light), WideLight,
                                        "0x605718");
    }
    case 7:
    {
        // 0x605727..0x60577d: the phase adds the z angle times 100 to the clock before the fmul.
        object.state.blendMesh = 0;
        const std::optional<double> pulse = Pulse((D(object.transform.angles.z) * PhasePerDegree) + D(clock));
        if (!pulse)
            return SineOutOfDomain("0x605755");
        object.state.light = F32(*pulse);
        return Terrain::AddDynamicLight(globals.dynamicLighting, object.transform.position, WarmLight(*pulse),
                                        WideLight, "0x60577d");
    }
    case 0xb:
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x605792");
    case 0xc:
    {
        // 0x6057b7..0x6057fd: two ftol of the same clock, the U and then the V.
        const std::optional<float> u = Scroll(clock, SlowPeriod, FiveHundredThousandths);
        if (!u)
            return ClockOutOfDomain("0x6057bd");
        object.state.uv.x = *u;
        return ScrollV(object, clock, SlowPeriod, FiveHundredThousandths, "0x6057e1");
    }
    case 0xd:
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x60580c");
    case 0x3d:
    case 0x41:
    case 0x42:
    {
        // 0x605831/0x605855..0x605718: blendMesh 1, the V and the light of radius 2; the blendMesh light is not
        // touched.
        object.state.blendMesh = 1;
        if (auto scrolled =
                ScrollV(object, clock, ShortPeriod, Thousandth, object.type == 0x3d ? "0x60583e" : "0x605862");
            !scrolled.HasValue())
            return scrolled;
        const std::optional<double> pulse = Pulse(D(clock));
        if (!pulse)
            return SineOutOfDomain("0x605893");
        return Terrain::AddDynamicLight(globals.dynamicLighting, object.transform.position, WarmLight(*pulse),
                                        NarrowLight, "0x605718");
    }
    case 0x3f:
    case 0x40:
        object.state.hiddenMesh = Kernel::HiddenObject;
        return {};
    case 0x48:
        object.state.blendMesh = 0;
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x6058ca");
    case 0x49:
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x6058f5");
    case 0x4b:
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x605920");
    case 0x4f:
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x60594b");
    case 0x52:
        // 0x605970: the blendMesh at 0 (EDX, still 0 since 0x604d50) and the object's light white.
        object.state.blendMesh = 0;
        object.light = glm::vec3(1.0f);
        return {};
    default:
        return {};
    }
}
} // namespace Client::MapScripts::Tarkan
