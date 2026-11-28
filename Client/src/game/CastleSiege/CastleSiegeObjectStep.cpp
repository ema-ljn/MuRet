#include "CastleSiege/CastleSiegeObjectStep.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "CastleSiege/CastleSiegeEmission.h"
#include "Effects/ModelEffects.h"
#include "WorldObjects/WorldObjectMapCreation.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/WorldObject.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

namespace Client::CastleSiege
{
namespace
{
// 0x42c8d4..0x42c904: ((rand() & 3) + 3) · 0.1f (0x6b87b8) stays on the x87; red is stored with fst, green is that
// value times 0.6f (0x6b8a20) and blue times 0.2f (0x6b8854), each with its fstp. Radius 3.
Core::Expected<void, std::string> AddTheTorchLight(const Kernel::WorldObject& object, int draw,
                                                   const Kernel::ObjectStepGlobals& globals)
{
    constexpr int Mask = 3, Base = 3;
    constexpr std::int32_t Radius = 3;
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    const double level = static_cast<double>((draw & Mask) + Base) * Step;
    const glm::vec3 light(static_cast<float>(level), static_cast<float>(level * Green),
                          static_cast<float>(level * Blue));
    return Terrain::AddDynamicLight(globals.dynamicLighting, object.transform.position, light, Radius, "0x42c904");
}
} // namespace

bool InTheCastleSiegeFog(const glm::vec3& hero)
{
    constexpr float West = 16100.0f, East = 19000.0f, South = 18900.0f, North = 21700.0f;
    return hero.x >= West && hero.x <= East && hero.y >= South && hero.y <= North;
}

Core::Expected<void, std::string> StepCastleSiegePrelude(const CastleSiegePrelude& prelude)
{
    // 0x6b8a88 and 0x6b8a80, two doubles.
    constexpr double Milliseconds = 0.001;
    constexpr double Period = 1800.0;
    constexpr Audio::SoundId AmbientSound{0x1e2};
    if (!prelude.systemTime)
        return Core::Unexpected(std::string("0x42b557: falta timeGetTime() (0x6b8544) para la música del 30"));
    if (prelude.musicTime == nullptr)
        return Core::Unexpected(std::string("0x42b56a: falta la hora de la música del 30 (0x7239f0)"));
    // 0x42b565: fild QWORD with the high part at 0, the clock unsigned and exact on the x87.
    const double now = static_cast<double>(prelude.systemTime());
    const float last = *prelude.musicTime;
    const double elapsed = (now - static_cast<double>(last)) * Milliseconds;
    // 0x42b57e..0x42b594: the jae on C0|C3 lets only the greater and ordered through; the 0 compares C3, which is also
    // set with NaN.
    if (elapsed > Period || last == 0.0f || std::isnan(last))
    {
        *prelude.musicTime = static_cast<float>(now);
        // 0x42b5c3 (without siege) and 0x42b5a6 (with it): 0x4acf60 drops one music and 0x4acfd0 plays the other.
        if (prelude.updateMusic)
        {
            prelude.updateMusic(prelude.siegeActive);
        }
    }
    if (!prelude.sound)
        return Core::Unexpected(std::string("0x42b5e9: falta el emisor sonoro 0x41ed10 del 0x1e2"));
    prelude.sound(AmbientSound);
    if (prelude.siegeActive)
        return EmitTheCastleBattle(prelude);
    return {};
}

Core::Expected<void, std::string> StepCastleSiegeObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                        const Kernel::ObjectStepGlobals& globals)
{
    if (globals.siegeActive == nullptr)
        return Core::Unexpected(std::string("0x42c850: falta el byte del asedio (0x7239ec) para los objetos del 30"));
    const bool siege = *globals.siegeActive;
    // 0x6b8a94: 0.0001f, the U of the 4.
    constexpr double Scroll = static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    constexpr std::int16_t Spearman = 0x19f;
    constexpr std::int16_t NoHiddenMesh = -1;
    switch (object.type)
    {
    case 0:
        // 0x42c8b1: without siege, the light; with it, only the −2.
        if (!siege)
        {
            if (auto lit = AddTheTorchLight(object, random.Next(), globals); !lit.HasValue())
                return lit;
        }
        object.state.hiddenMesh = Kernel::HiddenObject;
        return {};
    case 4:
        // 0x42c894..0x42c8a9.
        object.state.hiddenMesh = NoHiddenMesh;
        object.state.uv.x = static_cast<float>(static_cast<double>(globals.worldClock) * Scroll);
        return {};
    case 0x35:
        // 0x42c91a: the reverse of the 0.
        if (siege)
        {
            if (auto lit = AddTheTorchLight(object, random.Next(), globals); !lit.HasValue())
                return lit;
        }
        object.state.hiddenMesh = Kernel::HiddenObject;
        return {};
    case 0x2a:
    case 0x34:
    case 0x36:
        object.state.hiddenMesh = Kernel::HiddenObject;
        return {};
    case 0x42:
        // 0x42c967: the −2 before checking the siege.
        object.state.hiddenMesh = Kernel::HiddenObject;
        if (siege)
        {
            if (globals.siegeCrownFlags == nullptr)
                return Core::Unexpected(std::string("0x42c977: faltan las banderas de la corona (0x7239e4)"));
            constexpr std::uint32_t OpenCrown = 0x20000;
            if (*globals.siegeCrownFlags != OpenCrown)
                object.state.hiddenMesh = NoHiddenMesh;
            constexpr double MarkerScroll = static_cast<double>(std::bit_cast<float>(0x3a83126fu));
            object.state.uv.y = static_cast<float>(static_cast<double>(globals.worldClock) * MarkerScroll);
        }
        return {};
    case Spearman:
        // 0x42c950.
        object.state.hiddenMesh = siege ? Kernel::HiddenObject : NoHiddenMesh;
        return {};
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepCastleSiegeSeenObject(Kernel::WorldObject& object, bool siegeActive,
                                                            float worldClock,
                                                            const CastleGateCollapseServices& services)
{
    // 0x6b8a64 (0.0002f) and 0x6b8a90 (−0.0004f): the V of the 0x51 and the 0x53.
    constexpr double SlowScroll = static_cast<double>(std::bit_cast<float>(0x3951b717u));
    constexpr double BackScroll = static_cast<double>(std::bit_cast<float>(0xb9d1b717u));
    constexpr std::int16_t FirstGate = 0x3d, LastGate = 0x40;
    if (object.type == 0x51 || object.type == 0x53)
    {
        // 0x42c533 and 0x42c55e: DWORD 1 at +0x64 and 1.0f at +0x68 before the clock's fld.
        object.state.blendMesh = 1;
        object.state.light = 1.0f;
        const double scroll = object.type == 0x51 ? SlowScroll : BackScroll;
        object.state.uv.y = static_cast<float>(static_cast<double>(worldClock) * scroll);
    }
    else if (object.type >= FirstGate && object.type <= LastGate && siegeActive)
    {
        if (object.field08 == 0)
        {
            object.state.hiddenMesh = -1;
            WorldObjects::ApplyCastleSiegeObjectState(object, 1);
            return {};
        }
        if (object.field08 == 99 && object.seen)
        {
            if (object.state.hiddenMesh == -1)
            {
                if (!services.emit)
                    return Core::Unexpected(std::string("0x42c651: falta el alta de la puerta derrumbada"));
                const Kernel::ModelEffectArguments effect{.type = object.type,
                                                          .position = object.transform.position,
                                                          .angles = object.transform.angles,
                                                          .light = object.light,
                                                          .subtype = 1};
                if (auto emitted = services.emit(effect); !emitted.HasValue())
                    return emitted;
                if (services.sound)
                    services.sound(Audio::SoundId{0x1ec});
            }
            // 0x42c667: zero hides only mesh 0; HiddenObject (−2) would hide the whole object.
            object.state.hiddenMesh = 0;
        }
    }
    WorldObjects::ApplyCastleSiegeObjectState(object, siegeActive ? 1 : 0);
    return {};
}
} // namespace Client::CastleSiege
