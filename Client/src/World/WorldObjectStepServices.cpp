#include "World/WorldObjectStepServices.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainLight.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace Client::World
{
namespace
{
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);
constexpr float LampGreen = std::bit_cast<float>(0x3f4ccccdu);
constexpr float WarmGreen = std::bit_cast<float>(0x3f19999au);
constexpr float FireBlue = std::bit_cast<float>(0x3ecccccdu);
constexpr float CandleBlue = std::bit_cast<float>(0x3e4ccccdu);
constexpr std::int32_t LampRadius = 3;
constexpr std::int32_t FireRadius = 4;
constexpr std::int32_t JitterMask = 15;
constexpr std::int32_t JitterCenter = 8;
constexpr double DoorDistance = 200.0;
constexpr float DoorTurn = 10.0f;
constexpr float DoorApproach = std::bit_cast<float>(0x3e4ccccdu);
constexpr std::int32_t SlidingDoorSound = 0x12;
constexpr std::int32_t HingedDoorSound = 0x11;

// fst conserva el rojo retenido: sus productos no parten del canal ya redondeado a float.
glm::vec3 WarmLight(std::int32_t intensity, float green, float blue)
{
    const double red = static_cast<double>(intensity) * static_cast<double>(Tenth);
    return {static_cast<float>(red), static_cast<float>(red * static_cast<double>(green)),
            static_cast<float>(red * static_cast<double>(blue))};
}

Core::Expected<void, std::string> AddObjectLight(const ObjectStepGlobals& globals, const glm::vec3& position,
                                                 const glm::vec3& color, std::int32_t radius, std::string_view address)
{
    if (globals.dynamicLighting == nullptr)
    {
        return Core::Unexpected(std::format("{}: falta el buffer dinámico 0x7b4ccc4 para 0x5f9780", address));
    }
    // El kernel requiere un centro y límites int32 representables; no inventar una celda para NaN.
    constexpr float CellScale = std::bit_cast<float>(0x3c23d70au);
    for (int axis = 0; axis < 2; ++axis)
    {
        const float scaled = static_cast<float>(static_cast<double>(position[axis]) * static_cast<double>(CellScale));
        const double center = std::trunc(static_cast<double>(scaled));
        if (!std::isfinite(center) || center < static_cast<double>(std::numeric_limits<std::int32_t>::min()) + radius ||
            center > static_cast<double>(std::numeric_limits<std::int32_t>::max()) - radius - 1)
        {
            return Core::Unexpected(std::format("{}: centro de luz fuera del dominio int32 de 0x5f9780", address));
        }
    }
    const Data::Terrain::TerrainLightColor channels{color.x, color.y, color.z};
    AddTerrainLight(*globals.dynamicLighting, position.x, position.y, channels, radius);
    return {};
}

Core::Expected<void, std::string> EmitParticle(const ObjectStepGlobals& globals, const WorldObject& object,
                                               const glm::vec3& position, const glm::vec3& light, std::int32_t type,
                                               std::int32_t subtype, Render::Models::CrtRandom& random)
{
    if (globals.particles == nullptr)
    {
        return Core::Unexpected(std::string("0x53fe70: falta el pool de partículas 0x743a198"));
    }
    const auto emitted = globals.particles->CreateChecked(type, position, object.transform.angles, light, subtype, 1.0f,
                                                          globals.worldClock, random, 0);
    if (!emitted.HasValue())
    {
        return Core::Unexpected(std::format("0x53fe70: creación de partícula {:#x}/{} rechazada ({})", type, subtype,
                                            static_cast<int>(emitted.Error())));
    }
    // Sin índice es pool lleno: no interrumpe el caller ni su luz posterior.
    return {};
}

bool HasAngleBits(float angle, float degrees)
{
    return std::bit_cast<std::uint32_t>(angle) == std::bit_cast<std::uint32_t>(degrees);
}

void OpenSlidingDoor(WorldObject& object, double opening)
{
    const float yaw = object.transform.angles.z;
    if (HasAngleBits(yaw, 90.0f))
    {
        object.transform.position.y = static_cast<float>(static_cast<double>(object.copiedPosition.y) + opening * 2.0);
    }
    if (HasAngleBits(yaw, 270.0f))
    {
        object.transform.position.y = static_cast<float>(static_cast<double>(object.copiedPosition.y) - opening * 2.0);
    }
    // fcomp/test C3 admite cero con ambos signos y unordered.
    if (yaw == 0.0f || std::isnan(yaw))
    {
        object.transform.position.x = static_cast<float>(static_cast<double>(object.copiedPosition.x) + opening * 2.0);
    }
    if (HasAngleBits(yaw, 180.0f))
    {
        object.transform.position.x = static_cast<float>(static_cast<double>(object.copiedPosition.x) - opening * 2.0);
    }
}

void OpenHingedDoor(WorldObject& object, double opening)
{
    const float yaw = object.copiedAngles.z;
    if (HasAngleBits(yaw, 90.0f))
    {
        object.transform.angles.z = static_cast<float>(30.0 - opening * 0.5);
    }
    if (HasAngleBits(yaw, 270.0f))
    {
        object.transform.angles.z = static_cast<float>(330.0 + opening * 0.5);
    }
    if (yaw == 0.0f || std::isnan(yaw))
    {
        object.transform.angles.z = static_cast<float>(300.0 - opening * 0.5);
    }
    if (HasAngleBits(yaw, 180.0f))
    {
        object.transform.angles.z = static_cast<float>(240.0 + opening * 0.5);
    }
}
} // namespace

Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass, const WorldObject& object,
                                                 const glm::vec3& offset, Render::Models::CrtRandom& random,
                                                 const ObjectStepGlobals& globals)
{
    const auto matrix = Render::Models::OriginalAngleMatrix(object.transform.angles);
    glm::vec3 position = Render::Models::OriginalVectorRotate(offset, matrix);
    for (int axis = 0; axis < 3; ++axis)
    {
        position[axis] = static_cast<float>(static_cast<double>(position[axis]) + object.transform.position[axis]);
    }
    // Tres stores separados tras las tres sumas anteriores, en X/Y/Z.
    for (int axis = 0; axis < 3; ++axis)
    {
        const int jitter = (random.Next() & JitterMask) - JitterCenter;
        position[axis] = static_cast<float>(static_cast<double>(jitter) + position[axis]);
    }
    if (fireClass == 0)
    {
        const glm::vec3 light = WarmLight(random.Next() % 6 + 6, WarmGreen, FireBlue);
        if ((random.Next() & 1) == 0)
        {
            const int subtype = random.Next() & 3;
            const auto emitted = EmitParticle(globals, object, position, light, FireParticle, subtype, random);
            if (!emitted.HasValue())
            {
                return emitted;
            }
        }
        return AddObjectLight(globals, position, light, FireRadius, "0x52f534");
    }
    if ((fireClass == 1 || fireClass == 2) && (random.Next() & 1) == 0)
    {
        return EmitParticle(globals, object, position, object.light, BreathParticle,
                            fireClass == 1 ? 0 : WorldSmokeSubtype, random);
    }
    return {};
}

Core::Expected<void, std::string> StepObjectLamp(const WorldObject& object, Render::Models::CrtRandom& random,
                                                 const ObjectStepGlobals& globals)
{
    const bool candle = object.type == 0x96;
    const int intensity = (random.Next() & (candle ? 3 : 1)) + (candle ? 3 : 6);
    const glm::vec3 light = WarmLight(intensity, candle ? WarmGreen : LampGreen, candle ? CandleBlue : WarmGreen);
    return AddObjectLight(globals, object.transform.position, light, LampRadius, "0x605718");
}

Core::Expected<void, std::string> StepDeviasObjectDoor(WorldObject& object, const ObjectStepGlobals& globals)
{
    if (globals.editorMode)
    {
        return {};
    }
    if (globals.heroPosition == nullptr)
    {
        return Core::Unexpected(std::string("0x6050d4: falta la posición del héroe para la puerta del mapa 2"));
    }
    const double dx = static_cast<double>(globals.heroPosition->x) - object.copiedPosition.x;
    const double dy = static_cast<double>(globals.heroPosition->y) - object.copiedPosition.y;
    const double distance = std::sqrt(dy * dy + dx * dx);
    const float storedDistance = static_cast<float>(distance);
    if (!(distance >= DoorDistance))
    {
        const double opening = DoorDistance - static_cast<double>(storedDistance);
        const bool sliding = object.type == 0x56;
        if (sliding)
        {
            OpenSlidingDoor(object, opening);
        }
        else
        {
            OpenHingedDoor(object, opening);
        }
        if (!globals.sound)
        {
            return Core::Unexpected(std::string("0x41ed10: falta el emisor sonoro de la puerta del mapa 2"));
        }
        globals.sound(sliding ? SlidingDoorSound : HingedDoorSound);
        return {};
    }
    object.transform.angles.z =
        static_cast<float>(Render::Models::TurnAngle(object.transform.angles.z, object.copiedAngles.z, DoorTurn));
    for (int axis = 0; axis < 2; ++axis)
    {
        const double current = object.transform.position[axis];
        object.transform.position[axis] = static_cast<float>(
            (static_cast<double>(object.copiedPosition[axis]) - current) * static_cast<double>(DoorApproach) + current);
    }
    return {};
}

Core::Expected<void, std::string> StepAidaObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                 const ObjectStepGlobals& globals)
{
    // 0x6b8a24 (0,015f), 0x6b88d4 (0,01f), 0x6b87b4/0x6b8a20 (0,4f y 0,6f), 0x6b8a1c/0x6b8854 (0,9f y 0,2f).
    constexpr double Scroll = static_cast<double>(std::bit_cast<float>(0x3c75c28fu));
    constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    constexpr double GreenLow = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
    constexpr double GreenHigh = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr double RedHigh = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    constexpr double RedLow = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    constexpr float HalfAlpha = std::bit_cast<float>(0x3f000000u);
    constexpr float SlowSpeed = std::bit_cast<float>(0x3d4ccccdu);
    constexpr std::int32_t AidaRadius = 2;
    constexpr std::int32_t LightSpan = 5;
    constexpr std::int32_t AmbientSound = 0x246;
    switch (object.type)
    {
    case 0x19:
    case 0x1c:
        object.state.uv.y = static_cast<float>(static_cast<double>(object.state.uv.y) - Scroll);
        break;
    case 0x1e:
    case 0x47:
    {
        // 0x425137/0x425186: el sorteo por 0,01 queda en la x87; cada canal se guarda de su suma.
        const double base = static_cast<double>(random.Next() % LightSpan) * Hundredth;
        const bool green = object.type == 0x1e;
        const float first = static_cast<float>(base + (green ? GreenLow : RedHigh));
        const float second = static_cast<float>(base + (green ? GreenHigh : RedLow));
        const glm::vec3 light = green ? glm::vec3(first, second, first) : glm::vec3(first, second, second);
        auto added =
            AddObjectLight(globals, object.transform.position, light, AidaRadius, green ? "0x42517c" : "0x4251c9");
        if (!added.HasValue())
        {
            return added;
        }
        break;
    }
    case 0x29:
        object.alpha = HalfAlpha;
        break;
    case 0x38:
    case 0x39:
    case 0x3a:
    case 0x3b:
    case 0x3e:
    case 0x3f:
    case 0x43:
    case 0x46:
        object.state.hiddenMesh = HiddenObject;
        break;
    case 0x40:
        object.speed = SlowSpeed;
        break;
    default:
        break;
    }
    // 0x4251fb: 0x41ed10(0x246, 0, 0) con cada objeto que llega, de cualquier tipo.
    if (!globals.sound)
    {
        return Core::Unexpected(std::string("0x4251fb: falta el emisor sonoro 0x41ed10 del 0x246"));
    }
    globals.sound(AmbientSound);
    return {};
}
} // namespace Client::World
