#include "World/WorldObjectStepServices.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainLight.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "World/ModelEffects.h"
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
    return AddDynamicLight(globals.dynamicLighting, position, color, radius, address);
}
} // namespace

Core::Expected<void, std::string> AddDynamicLight(TerrainLighting* destination, const glm::vec3& position,
                                                  const glm::vec3& color, std::int32_t radius, std::string_view address)
{
    if (destination == nullptr)
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
    AddTerrainLight(*destination, position.x, position.y, channels, radius);
    return {};
}

namespace
{
Core::Expected<void, std::string> EmitParticle(const ObjectStepGlobals& globals, const glm::vec3& angles,
                                               const glm::vec3& position, const glm::vec3& light, std::int32_t type,
                                               std::int32_t subtype, Render::Models::CrtRandom& random)
{
    if (globals.particles == nullptr)
    {
        return Core::Unexpected(std::string("0x53fe70: falta el pool de partículas 0x743a198"));
    }
    const auto emitted =
        globals.particles->CreateChecked(type, position, angles, light, subtype, 1.0f, globals.worldClock, random, 0);
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
    return EmitObjectFire(fireClass, object.transform.angles, object.transform.position, object.light, offset, random,
                          globals);
}

Core::Expected<void, std::string> EmitObjectFire(std::int32_t fireClass, const glm::vec3& angles,
                                                 const glm::vec3& origin, const glm::vec3& objectLight,
                                                 const glm::vec3& offset, Render::Models::CrtRandom& random,
                                                 const ObjectStepGlobals& globals)
{
    const auto matrix = Render::Models::OriginalAngleMatrix(angles);
    glm::vec3 position = Render::Models::OriginalVectorRotate(offset, matrix);
    for (int axis = 0; axis < 3; ++axis)
    {
        position[axis] = static_cast<float>(static_cast<double>(position[axis]) + origin[axis]);
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
            const auto emitted = EmitParticle(globals, angles, position, light, FireParticle, subtype, random);
            if (!emitted.HasValue())
            {
                return emitted;
            }
        }
        return AddObjectLight(globals, position, light, FireRadius, "0x52f534");
    }
    if ((fireClass == 1 || fireClass == 2) && (random.Next() & 1) == 0)
    {
        return EmitParticle(globals, angles, position, objectLight, BreathParticle,
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

Core::Expected<void, std::string> StepLandOfTrialsObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                         const ObjectStepGlobals& globals)
{
    constexpr std::uint32_t TimeMask = 0x3ff;
    constexpr std::uint32_t ButterflyWindow = 10;
    constexpr std::uint32_t AmbientPeriod = 300000; // 0x440159: 0x493e0, sin signo (jbe).
    constexpr std::int32_t AmbientSound = 0x1ef;
    // 0x6b8bbc (0,0024f) y 0x6b89dc (0,3f): el vaivén del 0x1b.
    constexpr double SwayRate = static_cast<double>(std::bit_cast<float>(0x3b1d4952u));
    constexpr float SwayHeight = std::bit_cast<float>(0x3e99999au);
    // 0x6b87b8 (0,1f), 0x6b8a1c (0,9f) y 0x6b8854 (0,2f): la luz del 0x2a.
    constexpr double TorchStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double TorchRed = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    constexpr double TorchGreen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    constexpr int TorchMask = 3, TorchBase = 3;
    constexpr std::int32_t TorchRadius = 3;
    // 0x43ffd8 deja timeGetTime en EDI; cada llamada pide el reloj prestado donde la hace el ASM.
    const auto now = [&](std::string_view address) -> Core::Expected<std::uint32_t, std::string>
    {
        if (!globals.systemTime)
            return Core::Unexpected(std::format("{}: falta timeGetTime() (0x6b8544) para los objetos del 31", address));
        return globals.systemTime();
    };
    const auto butterfly = [&](std::int32_t subtype, std::string_view address) -> Core::Expected<void, std::string>
    {
        const auto time = now(address);
        if (!time.HasValue())
            return Core::Unexpected(time.Error());
        // 0x43fffd: el and deja un valor no negativo, y el jl que sigue nunca salta.
        if ((time.Value() & TimeMask) < ButterflyWindow)
        {
            if (globals.effects == nullptr)
                return Core::Unexpected(
                    std::string("0x440044: falta la tabla de efectos de modelo para la mariposa 0xae"));
            // 0x518cf0(0xae, posición, ángulos, (1, 1, 1), subtipo, objeto, −1, 0, 0, 0, 0): el paso corre en
            // la escena del juego (5), así que la mariposa no nace oculta.
            static_cast<void>(globals.effects->CreateButterflyEffect(object.transform.position, object.transform.angles,
                                                                     glm::vec3(1.0f), subtype, object.transform.scale,
                                                                     false, random));
        }
        object.state.hiddenMesh = HiddenObject;
        return {};
    };
    switch (object.type)
    {
    case 1:
        if (auto created = butterfly(0, "0x43fffb"); !created.HasValue())
            return created;
        break;
    case 0x2c:
        if (auto created = butterfly(1, "0x440051"); !created.HasValue())
            return created;
        break;
    case 0x2d:
        if (auto created = butterfly(2, "0x44008e"); !created.HasValue())
            return created;
        break;
    case 3:
    case 0x35:
        // 0x44014a: los que emiten en el frame (0x4401e0) no se dibujan.
        object.state.hiddenMesh = HiddenObject;
        break;
    case 0x1b:
    {
        // 0x4400ce..0x4400eb: z += sin(reloj · 0,0024f + fase) · 0,3f, cada paso sobre la z anterior; sin el
        // −2 de +0x58.
        const double argument =
            static_cast<double>(globals.worldClock) * SwayRate + static_cast<double>(object.field84);
        const auto sway = Render::Models::Detail::SineProductPC53(argument, SwayHeight);
        if (!sway)
            return Core::Unexpected(std::string("0x4400e0: el vaivén del 0x1b del 31 no cabe en el perfil PC53"));
        object.transform.position.z = static_cast<float>(*sway + static_cast<double>(object.transform.position.z));
        break;
    }
    case 0x2a:
    {
        // 0x4400f0..0x44013f: ((rand() & 3) + 3) · 0,1f queda en la x87 para los tres canales.
        const double level = static_cast<double>((random.Next() & TorchMask) + TorchBase) * TorchStep;
        const glm::vec3 light(static_cast<float>(level * TorchRed), static_cast<float>(level * TorchGreen),
                              static_cast<float>(level * TorchStep));
        if (auto added = AddObjectLight(globals, object.transform.position, light, TorchRadius, "0x440142");
            !added.HasValue())
            return added;
        object.state.hiddenMesh = HiddenObject;
        break;
    }
    default:
        break;
    }
    // 0x440151..0x440170: con cualquier tipo, el ambiente 0x1ef cada más de cinco minutos del sistema; la
    // hora que guarda es la de una segunda llamada.
    const auto time = now("0x440151");
    if (!time.HasValue())
        return Core::Unexpected(time.Error());
    if (globals.trialsAmbientTime == nullptr)
        return Core::Unexpected(std::string("0x440153: falta la hora del último ambiente del 31 (0x723c4c)"));
    if (time.Value() - *globals.trialsAmbientTime > AmbientPeriod)
    {
        const auto stored = now("0x440160");
        if (!stored.HasValue())
            return Core::Unexpected(stored.Error());
        // 0x44016b guarda la hora antes del pedido sonoro.
        *globals.trialsAmbientTime = stored.Value();
        if (!globals.sound)
            return Core::Unexpected(std::string("0x440170: falta el emisor sonoro 0x41ed10 del 0x1ef"));
        globals.sound(AmbientSound);
    }
    return {};
}

Core::Expected<void, std::string> StepCrywolfObject(WorldObject& object, Render::Models::CrtRandom& random,
                                                    const ObjectStepGlobals& globals)
{
    // 0x430429..0x430437: (0,2f; 0,7f; 0,5f), en los bits del ASM.
    constexpr glm::vec3 GlowLight(std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3f333333u),
                                  std::bit_cast<float>(0x3f000000u));
    // 0x6b8a20 (0,6f) y 0x6b8854 (0,2f): el verde y el azul de la antorcha.
    constexpr float TorchGreen = std::bit_cast<float>(0x3f19999au);
    constexpr float TorchBlue = std::bit_cast<float>(0x3e4ccccdu);
    constexpr std::int32_t GlowRadius = 2;
    constexpr std::int32_t TorchRadius = 3;
    constexpr int TorchMask = 3, TorchBase = 3;
    constexpr float FaintAlpha = std::bit_cast<float>(0x3e4ccccdu);
    switch (object.type)
    {
    case 0x29:
        return AddObjectLight(globals, object.transform.position, GlowLight, GlowRadius, "0x43043e");
    case 0x39:
    case 0x47:
    {
        // 0x43044d..0x430494: fst del rojo; el verde y el azul salen del producto sin redondear.
        const std::int32_t intensity = (random.Next() & TorchMask) + TorchBase;
        return AddObjectLight(globals, object.transform.position, WarmLight(intensity, TorchGreen, TorchBlue),
                              TorchRadius, "0x430497");
    }
    case 0x51:
        object.alpha = FaintAlpha; // 0x4304a6
        return {};
    case 0x52:
    case 0x53:
    case 0x54:
        object.state.hiddenMesh = HiddenObject; // 0x4304b7
        return {};
    default:
        return {};
    }
}
} // namespace Client::World
