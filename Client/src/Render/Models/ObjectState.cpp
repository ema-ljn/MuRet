#include "Render/Models/ObjectState.h"

#include "Core/IClock.h"

#include <glm/ext/vector_float2.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ratio>

namespace Client::Render::Models
{
namespace
{
double MillisecondsOf(Core::Duration clock)
{
    return std::chrono::duration<double, std::milli>(clock).count();
}

// sin(T · speed) · amplitude + offset, con los floats del original y el seno en la FPU.
float PulseAt(Core::Duration clock, float speed, float amplitude, float offset)
{
    return static_cast<float>(std::sin(MillisecondsOf(clock) * static_cast<double>(speed)) *
                                  static_cast<double>(amplitude) +
                              static_cast<double>(offset));
}

// Las luces que late 0x607b60: 0,004 (0x6b89e8) con 0,3 y 0,7 (0x6b89dc, 0x6b8a5c), con 0,2 y 0,8
// (0x6b8854, 0x6b8a38) o con 0,2 y 0,9 (0x6b8a1c); y 0,001 (0x6b89c8) con 0,4 y 0,6 (0x6b87b4,
// 0x6b8a20), la de los FShoots_R.
float FastPulse(Core::Duration clock)
{
    return PulseAt(clock, 0.004f, 0.3f, 0.7f);
}
float SoftPulse(Core::Duration clock)
{
    return PulseAt(clock, 0.004f, 0.2f, 0.8f);
}
float BrightPulse(Core::Duration clock)
{
    return PulseAt(clock, 0.004f, 0.2f, 0.9f);
}
float ShootsPulse(Core::Duration clock)
{
    return PulseAt(clock, 0.001f, 0.4f, 0.6f);
}

// rand() % count · 0,1 (0x6b87b8).
float RandomTenth(int random, int count)
{
    return static_cast<float>(random % count) * 0.1f;
}

// La V de Spear10: (T truncado % 2000) · −0,0005 (0x6ba19c), que la corre una vez cada 2 s.
float ScrollingV(Core::Duration clock)
{
    constexpr long long Period = 2000;
    const auto milliseconds = static_cast<long long>(MillisecondsOf(clock));
    return static_cast<float>(milliseconds % Period) * -0.0005f;
}

// 0x607b60: el objeto de siempre y, por tipo, lo que cambia. Los nombres son los de 0x60ff50.
ObjectState InitialObjectState(std::uint16_t type, Core::Duration clock, ObjectRandoms randoms)
{
    ObjectState state;
    switch (type)
    {
    case 0x217:  // Sword06
    case 0x21f:  // Sword14
    case 0xc1b:  // Staff10
    case 0x1c12: // Helper01
        state.blendMesh = 1;
        break;
    case 0x21c: // Sword11
    case 0x618: // Mace07
    case 0x812: // Spear01
    case 0xe1d: // Shield12
    case 0xe1e: // Shield13
    case 0xe1f: // Shield14
        state.blendMesh = 1;
        state.light = FastPulse(clock);
        break;
    case 0x220: // Sword15
        state.blendMesh = 1;
        state.uv.x = RandomTenth(randoms[0], 10);
        state.light = FastPulse(clock);
        break;
    case 0x616: // Mace05
        state.blendMesh = 1;
        state.light = SoftPulse(clock);
        break;
    case 0xc19: // Staff08
        state.blendMesh = 1;
        state.light = RandomTenth(randoms[0], 11);
        break;
    case 0xe20: // Shield15
        state.blendMesh = 1;
        state.uv = {RandomTenth(randoms[0], 10), RandomTenth(randoms[1], 10)};
        break;
    case 0xc12: // Staff01
        state.blendMesh = 2;
        break;
    case 0xc17: // Staff06
        state.blendMesh = 2;
        state.uv = {RandomTenth(randoms[0], 10), RandomTenth(randoms[1], 10)};
        break;
    case 0xc1d: // Staff12
        state.blendMesh = 2;
        state.light = FastPulse(clock);
        break;
    case 0xa18: // Bow07
    case 0xa1f: // CrossBow06
    case 0xa20: // CrossBow07
    case 0xc18: // Staff07
        state.blendMesh = EveryMeshBlends;
        state.light = FastPulse(clock);
        break;
    case 0xa22: // Bow17
    case 0xc1a: // Staff09
        state.blendMesh = EveryMeshBlends;
        state.light = BrightPulse(clock);
        break;
    case 0x617:  // Mace06
    case 0x1a12: // Wing01
    case 0x1a15: // Wing04
    case 0x1a1a:
    case 0x1a1b:
    case 0x1a1c:
    case 0x1a1d:
    case 0x1a26:
        state.blendMesh = 0;
        break;
    case 0x231: // Sword32
    case 0x619: // Mace08
    case 0xe22: // Shield17
        state.hiddenMesh = 2;
        break;
    case 0x81c: // Spear11
        state.hiddenMesh = 1;
        break;
    case 0x81b: // Spear10
        state.uv.y = ScrollingV(clock);
        break;
    case 0x102a: // HelmMale25
    case 0x142a: // PantMale25
        state.light = ShootsPulse(clock);
        break;
    default:
        break;
    }
    return state;
}

// La luz que 0x60b3d0 escribe por el tipo del objeto (0x60c2c7..0x60c343 → 0x60c5c0).
std::optional<float> TypeLightOf(std::uint16_t type, Core::Duration clock)
{
    switch (type)
    {
    case 0x1a16: // Wing05: sin(0,001 · T) + 1,1 (0x6b8830)
        return PulseAt(clock, 0.001f, 1.0f, 1.1f);
    case 0x1a17: // Wing06: (sin(0,001 · T) + 1) · 0,25 (0x6b87d4, 0x6b89e0)
        return static_cast<float>(
            (std::sin(MillisecondsOf(clock) * static_cast<double>(0.001f)) + static_cast<double>(1.0f)) *
            static_cast<double>(0.25f));
    case 0xc1d: // Staff12: la misma de 0x607b60
        return FastPulse(clock);
    default:
        return std::nullopt;
    }
}
} // namespace

ObjectState HangingObjectState(std::uint16_t type, Core::Duration clock, ObjectRandoms randoms)
{
    ObjectState state = InitialObjectState(type, clock, randoms);
    if (const std::optional<float> light = TypeLightOf(type, clock))
    {
        state.light = *light;
    }
    if (const std::optional<float> light = ModelLightOf(type, clock))
    {
        state.light = *light;
    }
    return state;
}

std::optional<float> ModelLightOf(std::uint16_t model, Core::Duration clock)
{
    switch (model)
    {
    case 0x102a: // HelmMale25
    case 0x142a: // PantMale25
        return ShootsPulse(clock);
    case 0x1025: // HelmMaleTest20
    case 0x1625: // GloveMaleTest20
    case 0x1825: // BootMaleTest20
        return 1.0f;
    default:
        return std::nullopt;
    }
}

float MaskedClassLightAt(Core::Duration clock)
{
    return PulseAt(clock, 0.001f, 0.1f, 0.7f);
}
} // namespace Client::Render::Models
