#include "World/WorldObjectMapStep.h"

#include "World/WorldObjectSecondaryStep.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainLight.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "World/Ftol.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/IWorldObjectStepServices.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectFire.h"
#include "World/WorldObjectParticleRequest.h"
#include "World/WorldObjectStepContext.h"
#include "World/WorldObjectStepModel.h"
#include "World/WorldObjectStepError.h"
#include "World/WorldObjectStepHook.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace Client::World
{
namespace
{
using Result = Core::Expected<void, WorldObjectStepError>;
// 604ab0: constantes f32 de las tablas de evidencia. Los productos retienen double hasta cada store.
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);
constexpr float TwentyPercent = std::bit_cast<float>(0x3e4ccccdu);
constexpr float ThirtyPercent = std::bit_cast<float>(0x3e99999au);
constexpr float SixtyPercent = std::bit_cast<float>(0x3f19999au);
constexpr float EightyPercent = std::bit_cast<float>(0x3f4ccccdu);
constexpr float Thousandth = std::bit_cast<float>(0x3a83126fu);
constexpr float TwoThousandths = std::bit_cast<float>(0x3b03126fu);
constexpr float FourThousandths = std::bit_cast<float>(0x3b83126fu);
constexpr float HalfThousandth = std::bit_cast<float>(0x3a03126fu);
constexpr float QuarterThousandth = std::bit_cast<float>(0x3983126fu);
constexpr float TenthThousandth = std::bit_cast<float>(0x38d1b717u);
constexpr float TwoTenthsThousandth = std::bit_cast<float>(0x3951b717u);
constexpr float HalfTenthThousandth = std::bit_cast<float>(0x3851b717u);
constexpr float WaveRange = std::bit_cast<float>(0x3eb33333u);
constexpr float WaveBase = std::bit_cast<float>(0x3f266666u);
constexpr float SceneFadeFactor = std::bit_cast<float>(0x3ca3d70au);

std::int32_t ClockInteger(float clock)
{
    const auto converted = FtolLowChecked(clock);
    if (!converted)
    {
        std::fputs("WorldObjectStep: reloj fuera del dominio portable de ftol\n", stderr);
        std::abort();
    }
    return *converted;
}

float ClockPhase(float clock, std::int32_t period, float factor, bool negate = false)
{
    std::int32_t value = ClockInteger(clock);
    if (negate)
    {
        value = std::bit_cast<std::int32_t>(0u - std::bit_cast<std::uint32_t>(value));
    }
    return static_cast<float>(static_cast<double>(value % period) * static_cast<double>(factor));
}

float MapClockUv(float clock)
{
    return ClockPhase(clock, 1000, Thousandth, true);
}

void AddRgb(WorldObjectStepContext& context, float x, float y, double intensity, float green, float blue,
            std::int32_t radius)
{
    const Data::Terrain::TerrainLightColor color{static_cast<float>(intensity),
                                                 static_cast<float>(intensity * static_cast<double>(green)),
                                                 static_cast<float>(intensity * static_cast<double>(blue))};
    AddTerrainLight(context.terrainLight, x, y, color, radius);
}

void FlickerTerrain(WorldObject& object, WorldObjectStepContext& context, bool candle)
{
    const int value = context.random.Next();
    const double light = static_cast<double>(candle ? value % 4 + 3 : value % 2 + 6) * static_cast<double>(Tenth);
    AddRgb(context, object.transform.position.x, object.transform.position.y, light,
           candle ? SixtyPercent : EightyPercent, candle ? TwentyPercent : SixtyPercent, 3);
}

Result EmitParticle(std::int32_t type, glm::vec3& position, const glm::vec3& angles, glm::vec3& color,
                    std::int32_t subtype, float scale, WorldObjectStepContext& context)
{
    const WorldObjectParticleRequest request{type, position, angles, color, subtype, scale, 0};
    return context.services.CreateParticle(request, context.random);
}

Result EmitModel(std::int16_t type, WorldObject& object, WorldObjectStepContext& context)
{
    const WorldObjectModelEffectRequest request{type, object.transform.position, 0.0f, 0};
    return context.services.CreateModelEffect(request, object, context.random);
}

Core::Expected<void, WorldObjectStepError> StepLorencia(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 50:
        return EmitWorldObjectFire(0, object, {0.0f, 0.0f, 200.0f}, context);
    case 51:
        return EmitWorldObjectFire(0, object, {0.0f, -30.0f, 60.0f}, context);
    case 52:
    {
        const auto fire = EmitWorldObjectFire(0, object, {0.0f, 0.0f, 60.0f}, context);
        if (!fire.HasValue())
        {
            return Core::Unexpected(fire.Error());
        }
        object.state.light =
            static_cast<float>(static_cast<double>(context.random.Next() % 6 + 4) * static_cast<double>(Tenth));
        return {};
    }
    case 55:
    case 80:
    {
        const bool bridge = object.type == 80;
        const auto first = EmitWorldObjectFire(
            0, object, bridge ? glm::vec3(90.0f, -200.0f, 30.0f) : glm::vec3(-150.0f, -150.0f, 140.0f), context);
        if (!first.HasValue())
        {
            return Core::Unexpected(first.Error());
        }
        return EmitWorldObjectFire(
            0, object, bridge ? glm::vec3(90.0f, 200.0f, 30.0f) : glm::vec3(150.0f, -150.0f, 140.0f), context);
    }
    case 130:
    case 131:
    case 132:
    {
        const auto fire = EmitWorldObjectFire(object.type - 130, object, {}, context);
        if (!fire.HasValue())
        {
            return Core::Unexpected(fire.Error());
        }
        object.state.hiddenMesh = HiddenObject;
        return {};
    }
    case 90:
        FlickerTerrain(object, context, false);
        return {};
    case 150:
        FlickerTerrain(object, context, true);
        return {};
    case 117:
    case 122:
        object.state.light =
            static_cast<float>(static_cast<double>(context.random.Next() % 4 + 4) * static_cast<double>(Tenth));
        return {};
    case 118:
    case 119:
        object.state.uv.y = MapClockUv(context.worldTime);
        return {};
    default:
        return {};
    }
}

Result StepMapOne(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 22:
    case 23:
    case 24:
        context.models.get().Step(object.type).flag88 = 1;
        object.state.uv.y = ClockPhase(context.worldTime, 1000, -Thousandth);
        return {};
    case 39:
    case 40:
    case 51:
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 41:
        return EmitWorldObjectFire(0, object, {0.0f, -30.0f, 240.0f}, context);
    case 42:
        return EmitWorldObjectFire(0, object, {0.0f, 0.0f, 190.0f}, context);
    case 52:
        if (context.random.Next() % 3 == 0)
        {
            const auto emitted = EmitModel(0xda, object, context);
            if (!emitted.HasValue())
                return Core::Unexpected(emitted.Error());
        }
        object.state.hiddenMesh = HiddenObject;
        return {};
    default:
        return {};
    }
}

bool IsBits(float value, std::uint32_t bits)
{
    return std::bit_cast<std::uint32_t>(value) == bits;
}

void MoveMapTwoNear(WorldObject& object, double distance, std::int16_t type)
{
    const double amount = 200.0 - static_cast<double>(static_cast<float>(distance));
    if (type == 86)
    {
        if (IsBits(object.transform.angles.z, 0x42b40000u))
            object.transform.position.y = static_cast<float>((amount + amount) + object.copiedPosition.y);
        if (IsBits(object.transform.angles.z, 0x43870000u))
            object.transform.position.y = static_cast<float>(object.copiedPosition.y - (amount + amount));
        if (object.transform.angles.z == 0.0f || std::isnan(object.transform.angles.z))
            object.transform.position.x = static_cast<float>((amount + amount) + object.copiedPosition.x);
        if (IsBits(object.transform.angles.z, 0x43340000u))
            object.transform.position.x = static_cast<float>(object.copiedPosition.x - (amount + amount));
        return;
    }
    if (IsBits(object.copiedAngles.z, 0x42b40000u))
        object.transform.angles.z = static_cast<float>(30.0 - amount * 0.5);
    if (IsBits(object.copiedAngles.z, 0x43870000u))
        object.transform.angles.z = static_cast<float>(amount * 0.5 + 330.0);
    if (object.copiedAngles.z == 0.0f || std::isnan(object.copiedAngles.z))
        object.transform.angles.z = static_cast<float>(300.0 - amount * 0.5);
    if (IsBits(object.copiedAngles.z, 0x43340000u))
        object.transform.angles.z = static_cast<float>(amount * 0.5 + 240.0);
}

Result MoveMapTwo(WorldObject& object, WorldObjectStepContext& context)
{
    if (context.editorState != 0)
        return {};
    const std::int16_t type = object.type;
    const double x = static_cast<double>(context.heroPosition.x) - object.copiedPosition.x;
    const double y = static_cast<double>(context.heroPosition.y) - object.copiedPosition.y;
    const double distance = std::sqrt(x * x + y * y);
    if (distance >= 200.0)
    {
        object.transform.angles.z =
            static_cast<float>(Render::Models::TurnAngle(object.transform.angles.z, object.copiedAngles.z, 10.0f));
        object.transform.position.x =
            static_cast<float>((static_cast<double>(object.copiedPosition.x) - object.transform.position.x) *
                                   static_cast<double>(TwentyPercent) +
                               object.transform.position.x);
        object.transform.position.y =
            static_cast<float>((static_cast<double>(object.copiedPosition.y) - object.transform.position.y) *
                                   static_cast<double>(TwentyPercent) +
                               object.transform.position.y);
        return {};
    }
    // Menor O unordered toma este brazo; la distancia se relee del store f32 en MoveMapTwoNear.
    MoveMapTwoNear(object, distance, type);
    return context.services.PlaySound(type == 86 ? 0x12 : 0x11, 0, 0);
}

Result StepMapTwo(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 20:
    case 65:
    case 86:
    case 88:
        return MoveMapTwo(object, context);
    case 30:
    case 66:
        return EmitWorldObjectFire(0, object, {0.0f, 0.0f, 50.0f}, context);
    case 78:
        object.state.light = static_cast<float>(static_cast<double>(context.random.Next() % 4 + 4) * Tenth);
        return {};
    default:
        return {};
    }
}

Result StepMapThree(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 39:
        object.state.blendMesh = 1;
        return {};
    case 41:
        object.state.blendMesh = 0;
        object.state.uv.y = ClockPhase(context.worldTime, 2000, HalfThousandth);
        return {};
    case 42:
    case 43:
        context.models.get().Step(object.type == 42 ? std::int16_t{42} : std::int16_t{43}).flag88 = 0;
        object.state.uv.x = ClockPhase(context.worldTime, 500, object.type == 42 ? -TwoThousandths : TwoThousandths);
        return {};
    case 322:
    {
        glm::vec3 position{object.transform.position.x,
                           static_cast<float>(static_cast<double>(object.transform.position.y) - 50.0),
                           static_cast<float>(static_cast<double>(object.transform.position.z) + 350.0)};
        glm::vec3 color(0.5f);
        const auto emitted = EmitParticle(0x5c6, position, object.transform.angles, color, 9,
                                          std::bit_cast<float>(0x3fb33333u), context);
        if (!emitted.HasValue())
            return Core::Unexpected(emitted.Error());
        // La cola relee el tiempo tras el productor; no cambia al tipo que tomó el switch.
        object.state.uv.y = ClockPhase(context.worldTime, 1000, Thousandth);
        return {};
    }
    case 18:
        object.state.uv.y = ClockPhase(context.worldTime, 1000, Thousandth);
        return {};
    default:
        return {};
    }
}

Result StepMapFour(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 3:
    case 4:
        object.state.uv.x = MapClockUv(context.worldTime);
        return {};
    case 18:
    case 23:
        object.state.blendMesh = 1;
        return {};
    case 19:
    case 20:
        object.state.blendMesh = 4;
        object.state.uv.x = MapClockUv(context.worldTime);
        return {};
    case 24:
        object.state.hiddenMesh = HiddenObject;
        if (context.random.Next() % 64 == 0)
            return EmitModel(0x5de, object, context);
        return {};
    case 25:
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 38:
    case 39:
        return StepWorldObjectSecondary(object, context);
    default:
        return {};
    }
}

Result StepMapFive(WorldObject& object, WorldObjectStepContext& context)
{
    if (object.type == 2 || object.type == 3)
        object.state.blendMesh = 0;
    if (object.type == 3)
        object.state.light = static_cast<float>(static_cast<double>(context.random.Next() % 4 + 6) * Tenth);
    return {};
}

Result StepMapSix(WorldObject& object, WorldObjectStepContext& context)
{
    if (object.type == 38)
        object.state.hiddenMesh = HiddenObject;
    if (object.type == 21)
    {
        object.state.blendMesh = 3;
        object.state.uv.y = MapClockUv(context.worldTime);
    }
    return {};
}

double Wave(float time, float factor, float range, double base)
{
    // Proyección PC53/RN/libm existente del port; no promete sin87 idéntico para todo f32/CW.
    return std::sin(static_cast<double>(time) * static_cast<double>(factor)) * static_cast<double>(range) + base;
}

Result StepMapSeven(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 22:
    {
        const double accumulated = static_cast<double>(object.field84) + Tenth;
        object.state.hiddenMesh = HiddenObject;
        object.field84 = static_cast<float>(accumulated);
        if (accumulated > 10.0)
            object.field84 = 0.0f;
        // Segunda guarda toma el f32 releído;5.000000096 no implica que el store sea >5.
        if (object.field84 > 5.0f)
            return EmitParticle(0x607, object.transform.position, object.transform.angles, object.light, 0, 1.0f,
                                context);
        return {};
    }
    case 23:
    case 40:
    {
        object.state.blendMesh = 0;
        const double phase =
            static_cast<double>(context.worldTime) * (object.type == 40 ? FourThousandths : TwoThousandths);
        if (object.type == 40)
            object.speed = std::bit_cast<float>(0x3d4ccccdu);
        object.state.light = static_cast<float>(std::sin(phase) * ThirtyPercent + 0.5);
        return {};
    }
    case 32:
    case 34:
        object.state.blendMesh = 1;
        object.state.light =
            static_cast<float>((std::sin(static_cast<double>(context.worldTime) * FourThousandths) + 1.0) * 0.5);
        return {};
    case 38:
        object.state.blendMesh = 0;
        return {};
    default:
        return {};
    }
}

Result StepMapEight(WorldObject& object, WorldObjectStepContext& context)
{
    switch (object.type)
    {
    case 2:
        object.state.blendMesh = 0;
        object.state.uv.x = MapClockUv(context.worldTime);
        return {};
    case 4:
    {
        const double phase = static_cast<double>(context.worldTime) * TwoThousandths;
        object.state.blendMesh = 0;
        const double light = std::sin(phase) * WaveRange + WaveBase;
        object.state.light = static_cast<float>(light);
        object.state.uv.y = ClockPhase(context.worldTime, 10000, TenthThousandth, true);
        // 69fd3c FISTP consume sólo tiempo y preserva v inferior: tres stores desde ese mismo v.
        AddRgb(context, object.transform.position.x, object.transform.position.y, light, 1, 1, 3);
        return {};
    }
    case 7:
    {
        const double phase =
            (static_cast<double>(object.transform.angles.z) * 100.0 + context.worldTime) * TwoThousandths;
        const float y = object.transform.position.y;
        const float x = object.transform.position.x;
        object.state.blendMesh = 0;
        const double light = std::sin(phase) * WaveRange + WaveBase;
        object.state.light = static_cast<float>(light);
        AddRgb(context, x, y, light, SixtyPercent, TwentyPercent, 3);
        return {};
    }
    case 11:
    case 13:
    case 72:
    case 73:
    case 75:
    case 79:
        if (object.type == 72)
            object.state.blendMesh = 0;
        object.state.uv.y = ClockPhase(context.worldTime, 10000, TwoTenthsThousandth, true);
        return {};
    case 12:
        object.state.uv.x = ClockPhase(context.worldTime, 50000, HalfTenthThousandth, true);
        object.state.uv.y = ClockPhase(context.worldTime, 50000, HalfTenthThousandth, true);
        return {};
    case 61:
    case 65:
    case 66:
    {
        object.state.blendMesh = 1;
        object.state.uv.y = MapClockUv(context.worldTime);
        const double light = Wave(context.worldTime, TwoThousandths, WaveRange, WaveBase);
        AddRgb(context, object.transform.position.x, object.transform.position.y, light, SixtyPercent, TwentyPercent,
               2);
        return {};
    }
    case 63:
    case 64:
        object.state.hiddenMesh = HiddenObject;
        return {};
    case 82:
        object.state.blendMesh = 0;
        object.light.x = 1.0f;
        object.light.y = 1.0f;
        object.light.z = 1.0f;
        return {};
    default:
        return {};
    }
}

Result StepDefault(WorldObject& object, WorldObjectStepContext& context)
{
    constexpr std::array Handlers{WorldObjectStepHook::Family438a90, WorldObjectStepHook::Family42c850,
                                  WorldObjectStepHook::Family43ffc0, WorldObjectStepHook::Family42ee40,
                                  WorldObjectStepHook::Family4250f0, WorldObjectStepHook::Family4303e0};
    for (std::size_t i = 0; i < Handlers.size(); ++i)
    {
        const auto handled = context.services.HandleMap(Handlers[i], object, context.random);
        if (!handled.HasValue())
            return Core::Unexpected(handled.Error());
        const auto al = static_cast<std::uint8_t>(handled.Value());
        if ((i < 2 && al == 1) || (i >= 2 && i < 5 && al != 0))
            return {};
    }
    return {};
}

Result PrepareMapNine(WorldObjectStepContext& context)
{
    const bool activeTime = ClockInteger(context.worldTime) % 4000 < 1000;
    if (activeTime && context.random.Next() % 100 == 0)
    {
        const double intensity = static_cast<double>(context.random.Next() % 12 + 4) * static_cast<double>(Tenth);
        const Data::Terrain::TerrainLightColor color{static_cast<float>(intensity * static_cast<double>(TwentyPercent)),
                                                     static_cast<float>(intensity * static_cast<double>(ThirtyPercent)),
                                                     static_cast<float>(intensity * 0.5)};
        const auto yOffset = context.random.Next() % 1200;
        const float y = static_cast<float>((static_cast<double>(yOffset) + context.heroPosition.y) - 600.0);
        const auto xOffset = context.random.Next() % 1200;
        const float x = static_cast<float>((static_cast<double>(xOffset) + context.heroPosition.x) - 600.0);
        AddTerrainLight(context.terrainLight, x, y, color, 12);
    }
    return context.services.PlaySound(1, 0, 1);
}

} // namespace

Core::Expected<void, WorldObjectStepError> PrepareWorldObjectStep(WorldObject& object, WorldObjectStepContext& context)
{
    if (context.map == 9)
    {
        const auto prepared = PrepareMapNine(context);
        if (!prepared.HasValue())
            return Core::Unexpected(prepared.Error());
    }
    if (context.map == 0 && (object.type == 125 || object.type == 126))
        object.targetAlpha = context.currentHeroTile1 == 4 ? 0.0f : 1.0f;
    if (context.map == 2 &&
        (object.type == 81 || object.type == 82 || object.type == 96 || object.type == 98 || object.type == 99))
        object.targetAlpha = context.currentHeroTile1 == 3 || context.currentHeroTile1 >= 10 ? 0.0f : 1.0f;
    return {};
}

void StepWorldObjectSceneCases(WorldObject& object, WorldObjectStepContext& context)
{
    if (context.scene != ObjectScene::Login && context.scene != ObjectScene::CharacterSelect)
    {
        return;
    }
    if (object.type == 160 || object.type == 161)
    {
        const std::int32_t remainder = ClockInteger(context.worldTime) % 4000;
        object.state.uv.y =
            static_cast<float>(-(static_cast<double>(remainder) * static_cast<double>(QuarterThousandth)));
    }
    else if (object.type == 162)
    {
        const double light = context.state7d3a0cc != 0
                                 ? 1.5
                                 : static_cast<double>(context.value6d5e30) * static_cast<double>(SceneFadeFactor);
        object.light.x = static_cast<float>(light);
        object.light.y = static_cast<float>(light);
        object.light.z = static_cast<float>(light);
        object.state.light = static_cast<float>(light);
    }
}

Core::Expected<void, WorldObjectStepError> StepWorldObjectMapCases(WorldObject& object, WorldObjectStepContext& context)
{
    switch (context.map)
    {
    case 0:
        return StepLorencia(object, context);
    case 1:
        return StepMapOne(object, context);
    case 2:
        return StepMapTwo(object, context);
    case 3:
        return StepMapThree(object, context);
    case 4:
        return StepMapFour(object, context);
    case 5:
        return StepMapFive(object, context);
    case 6:
        return StepMapSix(object, context);
    case 7:
        return StepMapSeven(object, context);
    case 8:
        return StepMapEight(object, context);
    case 10:
        return {};
    default:
        if (context.map >= 11 && context.map <= 17)
        {
            if ((object.type == 9 || object.type == 10) && object.field8a != 4)
                object.state.hiddenMesh = HiddenObject;
            return {};
        }
        return StepDefault(object, context);
    }
}
} // namespace Client::World
