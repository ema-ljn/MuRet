#include "World/DungeonObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/ModelEffects.h"
#include "World/ObjectModelStepState.h"
#include "World/OriginalFtol.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::World
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

constexpr std::int32_t ScrollPeriod = 1000;
// 0x6ba1a4 (−0,001f) del mapa 1 y 0x6b89c8 (0,001f) del 4.
constexpr double NegativeThousandth = static_cast<double>(std::bit_cast<float>(0xba83126fu));
constexpr double Thousandth = static_cast<double>(std::bit_cast<float>(0x3a83126fu));

// 0x60500b..0x60502b: fld del reloj, ftol, idiv por 1.000 y fild del resto por −0,001f.
std::optional<float> DungeonScroll(float worldClock)
{
    const std::optional<std::int32_t> low = OriginalFtolLow(static_cast<double>(worldClock));
    if (!low)
        return std::nullopt;
    return F32(static_cast<double>(*low % ScrollPeriod) * NegativeThousandth);
}

// 0x605404..0x605426: el ftol negado en 32 bits (envuelve en −2^31), su resto firmado y 0,001f.
std::optional<float> LostTowerScroll(float worldClock)
{
    const std::optional<std::int32_t> low = OriginalFtolLow(static_cast<double>(worldClock));
    if (!low)
        return std::nullopt;
    const auto negated = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(*low));
    return F32(static_cast<double>(negated % ScrollPeriod) * Thousandth);
}

Core::Expected<void, std::string> Fire(const WorldObject& object, const glm::vec3& offset,
                                       Render::Models::CrtRandom& random, const ObjectStepGlobals& globals,
                                       const char* address)
{
    if (auto emitted = EmitObjectFire(0, object, offset, random, globals); !emitted.HasValue())
        return Core::Unexpected(std::format("{}: {}", address, emitted.Error()));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepDungeonObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                    const ObjectStepGlobals& globals)
{
    switch (object.type)
    {
    case 0x16:
    case 0x17:
    case 0x18:
    {
        // 0x605003: el byte del modelo, antes del ftol.
        if (globals.modelState == nullptr)
            return Core::Unexpected(std::string("0x605003: falta el model+0x88 de los tipos del mapa 1"));
        globals.modelState->unlitMeshes[object.type] = 1;
        const std::optional<float> v = DungeonScroll(globals.worldClock);
        if (!v)
            return Core::Unexpected(std::string("0x605011: ftol del reloj del mundo fuera de lo acreditado"));
        object.state.uv.y = *v;
        return {};
    }
    case 0x27:
    case 0x28:
    case 0x33:
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 0x29:
        return Fire(object, glm::vec3(0.0f, -30.0f, 240.0f), random, globals, "0x605043");
    case 0x2a:
        return Fire(object, glm::vec3(0.0f, 0.0f, 190.0f), random, globals, "0x60505e");
    case 0x34:
    {
        constexpr int StoneChance = 3;
        if (random.Next() % StoneChance == 0)
        {
            if (globals.effects == nullptr)
                return Core::Unexpected(std::string("0x604fdb: la piedra 0xda del 0x34 pide la tabla de efectos "
                                                    "(0x518cf0)"));
            static_cast<void>(globals.effects->CreateFallingStone(object.transform.position, object.transform.angles,
                                                                  object.light, random));
        }
        object.state.hiddenMesh = HiddenObject;
        return {};
    }
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepLostTowerObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                      const ObjectStepGlobals& globals)
{
    switch (object.type)
    {
    case 3:
    case 4:
    case 0x13:
    case 0x14:
    {
        constexpr std::int16_t ChromeBlendMesh = 4;
        if (object.type == 0x13 || object.type == 0x14)
            object.state.blendMesh = ChromeBlendMesh;
        const std::optional<float> u = LostTowerScroll(globals.worldClock);
        if (!u)
            return Core::Unexpected(std::format("{}: ftol del reloj del mundo fuera de lo acreditado",
                                                object.type == 0x13 || object.type == 0x14 ? "0x60543c" : "0x60540a"));
        object.state.uv.x = *u;
        return {};
    }
    case 0x12:
    case 0x17:
        object.state.blendMesh = 1;
        return {};
    case 0x18:
    {
        object.state.hiddenMesh = HiddenObject;
        constexpr int JetMask = 63;
        if ((random.Next() & JetMask) != 0)
            return {};
        if (globals.effects == nullptr || globals.ownRecordDepth == nullptr)
            return Core::Unexpected(std::string("0x6054a9: el chorro 0x5de del 0x18 pide la tabla de efectos "
                                                "(0x518cf0) y el registro propio (0x7681eb4)"));
        static_cast<void>(globals.effects->CreateFlameJet(object.transform.position, object.transform.angles,
                                                          object.light, *globals.ownRecordDepth, random));
        return {};
    }
    case 0x19:
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 0x26:
    case 0x27:
        return KickObject(object, globals);
    default:
        return {};
    }
}

Core::Expected<void, std::string> StepArenaObject(WorldObject& object, const ObjectStepGlobals& globals)
{
    constexpr std::int16_t ScrollingObject = 0x15;
    constexpr std::int16_t HiddenType = 0x26;
    if (object.type == HiddenType)
    {
        object.state.hiddenMesh = HiddenObject; // 0x60550f.
        return {};
    }
    if (object.type != ScrollingObject)
        return {};
    // 0x60551d: el blendMesh antes del ftol; 0x605524..0x605546: la V con el ftol negado.
    constexpr std::int16_t ScrollBlendMesh = 3;
    object.state.blendMesh = ScrollBlendMesh;
    const std::optional<float> v = LostTowerScroll(globals.worldClock);
    if (!v)
        return Core::Unexpected(std::string("0x60552a: ftol del reloj del mundo fuera de lo acreditado"));
    object.state.uv.y = *v;
    return {};
}

Core::Expected<void, std::string> KickObject(WorldObject& object, const ObjectStepGlobals& globals)
{
    if (globals.heroPosition == nullptr || globals.heroAction == nullptr)
        return Core::Unexpected(std::string("0x52f556: la patada 0x52f550 lee la posición y la acción del héroe "
                                            "(0x743a170)"));
    // 0x6b87b8 (0,1f), 0x6b86b0 (50), 0x6ba18c (−0,4f), 0x6ba2bc (−4) y 0x6b8a20 (0,6f).
    constexpr std::uint8_t FirstKickAction = 0xd;
    constexpr std::uint8_t LastKickAction = 0x21;
    constexpr float StillSpeed = std::bit_cast<float>(0x3dcccccdu);
    constexpr double KickReach = 50.0;
    constexpr double Push = static_cast<double>(std::bit_cast<float>(0xbecccccdu));
    constexpr double Spin = -4.0;
    constexpr double Decay = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr std::int32_t KickSound = 0x7c;
    const std::uint8_t action = *globals.heroAction;
    if (action >= FirstKickAction && action <= LastKickAction && !(object.velocity.x >= StillSpeed))
    {
        const double dx =
            static_cast<double>(globals.heroPosition->x) - static_cast<double>(object.transform.position.x);
        const double dy =
            static_cast<double>(globals.heroPosition->y) - static_cast<double>(object.transform.position.y);
        // 0x52f5a0: fst de dy en float y fmul por esa copia; dx al cuadrado desde la pila.
        const float dyStored = F32(dy);
        const double distance = std::sqrt((dy * static_cast<double>(dyStored)) + (dx * dx));
        if (!(distance >= KickReach))
        {
            object.velocity.z = 0.0f;
            object.velocity.x = F32(dx * Push);
            object.velocity.y = F32(static_cast<double>(dyStored) * Push);
            object.copiedAngles.y = F32(dx * Spin);
            object.copiedAngles.x = F32(static_cast<double>(dyStored) * Spin);
            if (!globals.sound)
                return Core::Unexpected(std::string("0x52f5fc: la patada pide el emisor sonoro 0x41ed10 del 0x7c"));
            globals.sound(KickSound);
        }
    }
    // 0x52f608..0x52f6a1: la x de la velocidad se suma desde la pila (fst), los demás desde su float.
    const double pushX = static_cast<double>(object.velocity.x) * Decay;
    object.velocity.x = F32(pushX);
    object.velocity.y = F32(static_cast<double>(object.velocity.y) * Decay);
    object.velocity.z = F32(static_cast<double>(object.velocity.z) * Decay);
    for (int axis = 0; axis < 3; ++axis)
        object.copiedAngles[axis] = F32(static_cast<double>(object.copiedAngles[axis]) * Decay);
    glm::vec3& position = object.transform.position;
    position.x = F32(pushX + static_cast<double>(position.x));
    position.y = F32(static_cast<double>(object.velocity.y) + static_cast<double>(position.y));
    position.z = F32(static_cast<double>(object.velocity.z) + static_cast<double>(position.z));
    for (int axis = 0; axis < 3; ++axis)
        object.transform.angles[axis] =
            F32(static_cast<double>(object.copiedAngles[axis]) + static_cast<double>(object.transform.angles[axis]));
    return {};
}
} // namespace Client::World
