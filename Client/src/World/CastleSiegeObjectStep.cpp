#include "World/CastleSiegeObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/WorldObjectMapCreation.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

namespace Client::World
{
namespace
{
// 0x42c8d4..0x42c904: ((rand() & 3) + 3) · 0,1f (0x6b87b8) queda en la x87; el rojo se guarda con fst, el verde
// es ese valor por 0,6f (0x6b8a20) y el azul por 0,2f (0x6b8854), cada uno con su fstp. Radio 3.
Core::Expected<void, std::string> AddTheTorchLight(const WorldObject& object, int draw,
                                                   const ObjectStepGlobals& globals)
{
    constexpr int Mask = 3, Base = 3;
    constexpr std::int32_t Radius = 3;
    constexpr double Step = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double Green = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr double Blue = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    const double level = static_cast<double>((draw & Mask) + Base) * Step;
    const glm::vec3 light(static_cast<float>(level), static_cast<float>(level * Green),
                          static_cast<float>(level * Blue));
    return AddDynamicLight(globals.dynamicLighting, object.transform.position, light, Radius, "0x42c904");
}
} // namespace

bool InTheCastleSiegeFog(const glm::vec3& hero)
{
    constexpr float West = 16100.0f, East = 19000.0f, South = 18900.0f, North = 21700.0f;
    return hero.x >= West && hero.x <= East && hero.y >= South && hero.y <= North;
}

Core::Expected<void, std::string> StepCastleSiegePrelude(const CastleSiegePrelude& prelude)
{
    // 0x6b8a88 y 0x6b8a80, dos double: 0,001 y 1.800.
    constexpr double Milliseconds = 0.001;
    constexpr double Period = 1800.0;
    constexpr std::int32_t AmbientSound = 0x1e2;
    if (!prelude.systemTime)
        return Core::Unexpected(std::string("0x42b557: falta timeGetTime() (0x6b8544) para la música del 30"));
    if (prelude.musicTime == nullptr)
        return Core::Unexpected(std::string("0x42b56a: falta la hora de la música del 30 (0x7239f0)"));
    // 0x42b565: fild QWORD con la parte alta en 0, el reloj sin signo y exacto en la x87.
    const double now = static_cast<double>(prelude.systemTime());
    const float last = *prelude.musicTime;
    const double elapsed = (now - static_cast<double>(last)) * Milliseconds;
    // 0x42b57e..0x42b594: el jae sobre C0|C3 deja pasar solo lo mayor y ordenado; el 0 compara C3, que también
    // queda prendido con NaN.
    if (elapsed > Period || last == 0.0f || std::isnan(last))
    {
        *prelude.musicTime = static_cast<float>(now);
        // 0x42b5c3 (sin asedio) y 0x42b5a6 (con él): 0x4acf60 deja una música y 0x4acfd0 toca la otra. MuRet
        // todavía no tiene audio (CL10).
    }
    if (!prelude.sound)
        return Core::Unexpected(std::string("0x42b5e9: falta el emisor sonoro 0x41ed10 del 0x1e2"));
    prelude.sound(AmbientSound);
    if (prelude.siegeActive)
        return Core::Unexpected(std::string("0x42b5f8: el preludio del asedio prendido (los sonidos 0x1e3 a 0x1e7 y "
                                            "las piedras sobre el héroe) no está portado"));
    return {};
}

Core::Expected<void, std::string> StepCastleSiegeObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                        const ObjectStepGlobals& globals)
{
    if (globals.siegeActive == nullptr)
        return Core::Unexpected(std::string("0x42c850: falta el byte del asedio (0x7239ec) para los objetos del 30"));
    const bool siege = *globals.siegeActive;
    // 0x6b8a94: 0,0001f, la U del 4.
    constexpr double Scroll = static_cast<double>(std::bit_cast<float>(0x38d1b717u));
    constexpr std::int16_t Spearman = 0x19f;
    constexpr std::int16_t NoHiddenMesh = -1;
    switch (object.type)
    {
    case 0:
        // 0x42c8b1: sin asedio, la luz; con él, solo el −2.
        if (!siege)
        {
            if (auto lit = AddTheTorchLight(object, random.Next(), globals); !lit.HasValue())
                return lit;
        }
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 4:
        // 0x42c894..0x42c8a9.
        object.state.hiddenMesh = NoHiddenMesh;
        object.state.uv.x = static_cast<float>(static_cast<double>(globals.worldClock) * Scroll);
        return {};
    case 0x35:
        // 0x42c91a: al revés que el 0.
        if (siege)
        {
            if (auto lit = AddTheTorchLight(object, random.Next(), globals); !lit.HasValue())
                return lit;
        }
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 0x2a:
    case 0x34:
    case 0x36:
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 0x42:
        // 0x42c967: el −2 antes de mirar el asedio.
        object.state.hiddenMesh = HiddenObject;
        if (siege)
            return Core::Unexpected(std::string("0x42c977: el 0x42 del 30 con el asedio lee 0x7239e4, que no está "
                                                "portado"));
        return {};
    case Spearman:
        // 0x42c950.
        object.state.hiddenMesh = siege ? HiddenObject : NoHiddenMesh;
        return {};
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepCastleSiegeSeenObject(WorldObject& object, bool siegeActive, float worldClock)
{
    // 0x6b8a64 (0,0002f) y 0x6b8a90 (−0,0004f): la V del 0x51 y del 0x53.
    constexpr double SlowScroll = static_cast<double>(std::bit_cast<float>(0x3951b717u));
    constexpr double BackScroll = static_cast<double>(std::bit_cast<float>(0xb9d1b717u));
    constexpr std::int16_t FirstGate = 0x3d, LastGate = 0x40;
    if (object.type == 0x51 || object.type == 0x53)
    {
        // 0x42c533 y 0x42c55e: el DWORD 1 en +0x64 y 1,0f en +0x68 antes del fld del reloj.
        object.state.blendMesh = 1;
        object.state.light = 1.0f;
        const double scroll = object.type == 0x51 ? SlowScroll : BackScroll;
        object.state.uv.y = static_cast<float>(static_cast<double>(worldClock) * scroll);
    }
    else if (object.type >= FirstGate && object.type <= LastGate && siegeActive)
    {
        return Core::Unexpected(std::string("0x42c596: las puertas 0x3d a 0x40 del 30 con el asedio no están "
                                            "portadas"));
    }
    ApplyCastleSiegeObjectState(object, siegeActive ? 1 : 0);
    return {};
}
} // namespace Client::World
