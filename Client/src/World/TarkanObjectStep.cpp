#include "World/TarkanObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/ObjectState.h"
#include "World/ObjectModelStepState.h"
#include "World/OriginalFtol.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// 0x6b89c8 (0,001f), 0x6b8a94 (0,0001f), 0x6b8a64 (0,0002f) y 0x6b8a28 (0,00005f).
constexpr double Thousandth = static_cast<double>(std::bit_cast<float>(0x3a83126fu));
constexpr double TenThousandth = static_cast<double>(std::bit_cast<float>(0x38d1b717u));
constexpr double TwoTenThousandths = static_cast<double>(std::bit_cast<float>(0x3951b717u));
constexpr double FiveHundredThousandths = static_cast<double>(std::bit_cast<float>(0x3851b717u));
// 0x6b89bc (0,002f), 0x6b8bc4 (0,35f), 0x6b8bc0 (0,65f), 0x6b86b4 (100), 0x6b8a20 (0,6f) y 0x6b8854 (0,2f).
constexpr double PulseRate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
constexpr double PulseAmplitude = static_cast<double>(std::bit_cast<float>(0x3eb33333u));
constexpr double PulseBase = static_cast<double>(std::bit_cast<float>(0x3f266666u));
constexpr double PhasePerDegree = 100.0;
constexpr double WarmGreen = static_cast<double>(std::bit_cast<float>(0x3f19999au));
constexpr double WarmBlue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
constexpr std::int32_t WideLight = 3;
constexpr std::int32_t NarrowLight = 2;

// fld del reloj, ftol, neg en 32 bits (envuelve en −2^31), cdq, idiv por `period`, fild del resto y fmul
// por `factor`, guardado en float.
std::optional<float> Scroll(float worldClock, std::int32_t period, double factor)
{
    const std::optional<std::int32_t> low = OriginalFtolLow(D(worldClock));
    if (!low)
        return std::nullopt;
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*low));
    return F32(static_cast<double>(negated % period) * factor);
}

// fsin de (fase) · 0,002f, por 0,35f y más 0,65f, sin redondear en la x87.
double Pulse(double phase)
{
    return (std::sin(phase * PulseRate) * PulseAmplitude) + PulseBase;
}

glm::vec3 WarmLight(double pulse)
{
    return {F32(pulse), F32(pulse * WarmGreen), F32(pulse * WarmBlue)};
}

Core::Unexpected<std::string> ClockOutOfDomain(std::string_view address)
{
    return Core::Unexpected(std::format("{}: ftol del reloj del mundo fuera de lo acreditado", address));
}

Core::Expected<void, std::string> ScrollV(WorldObject& object, float worldClock, std::int32_t period, double factor,
                                          std::string_view address)
{
    const std::optional<float> v = Scroll(worldClock, period, factor);
    if (!v)
        return ClockOutOfDomain(address);
    object.state.uv.y = *v;
    return {};
}
} // namespace

void SeedTarkanModelState(std::uint8_t map, ObjectModelStepState& state)
{
    constexpr std::uint8_t TarkanMap = 8;
    if (map != TarkanMap)
        return;
    // 0x61a150..0x61a18f: los bytes 11 · 0xe0 + 0x88 a 79 · 0xe0 + 0x88 de la tabla 0x5769ad8.
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

Core::Expected<void, std::string> StepTarkanObject(WorldObject& object, const ObjectStepGlobals& globals)
{
    constexpr std::int32_t ShortPeriod = 1000;
    constexpr std::int32_t LongPeriod = 10000;
    constexpr std::int32_t SlowPeriod = 50000;
    const float clock = globals.worldClock;
    switch (object.type)
    {
    case 2:
    {
        // 0x605683: el blendMesh antes del ftol.
        object.state.blendMesh = 0;
        const std::optional<float> u = Scroll(clock, ShortPeriod, Thousandth);
        if (!u)
            return ClockOutOfDomain("0x605690");
        object.state.uv.x = *u;
        return {};
    }
    case 4:
    {
        // 0x6056b5..0x6056d6: la luz del blendMesh es la L guardada; la x87 la conserva para la luz dinámica.
        object.state.blendMesh = 0;
        const double pulse = Pulse(D(clock));
        object.state.light = F32(pulse);
        if (auto scrolled = ScrollV(object, clock, LongPeriod, TenThousandth, "0x6056df"); !scrolled.HasValue())
            return scrolled;
        const float light = F32(pulse);
        return AddDynamicLight(globals.dynamicLighting, object.transform.position, glm::vec3(light), WideLight,
                               "0x605718");
    }
    case 7:
    {
        // 0x605727..0x60577d: la fase suma el ángulo z por 100 al reloj antes del fmul.
        object.state.blendMesh = 0;
        const double pulse = Pulse((D(object.transform.angles.z) * PhasePerDegree) + D(clock));
        object.state.light = F32(pulse);
        return AddDynamicLight(globals.dynamicLighting, object.transform.position, WarmLight(pulse), WideLight,
                               "0x60577d");
    }
    case 0xb:
        return ScrollV(object, clock, LongPeriod, TwoTenThousandths, "0x605792");
    case 0xc:
    {
        // 0x6057b7..0x6057fd: dos ftol del mismo reloj, la U y después la V.
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
        // 0x605831/0x605855..0x605718: blendMesh 1, la V y la luz de radio 2; la del blendMesh no se toca.
        object.state.blendMesh = 1;
        if (auto scrolled =
                ScrollV(object, clock, ShortPeriod, Thousandth, object.type == 0x3d ? "0x60583e" : "0x605862");
            !scrolled.HasValue())
            return scrolled;
        return AddDynamicLight(globals.dynamicLighting, object.transform.position, WarmLight(Pulse(D(clock))),
                               NarrowLight, "0x605718");
    }
    case 0x3f:
    case 0x40:
        object.state.hiddenMesh = HiddenObject;
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
        // 0x605970: el blendMesh en 0 (EDX, todavía en 0 desde 0x604d50) y la luz del objeto en blanco.
        object.state.blendMesh = 0;
        object.light = glm::vec3(1.0f);
        return {};
    default:
        return {};
    }
}
} // namespace Client::World
