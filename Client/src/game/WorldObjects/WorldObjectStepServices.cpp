#include "WorldObjects/WorldObjectStepServices.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/WorldObject.h"
#include "Math/FloatConstants.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Effects/ModelEffects.h"
#include "Particles/ObjectFire.h"
#include "Particles/Particles.h"
#include "Terrain/DynamicWorldLight.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::WorldObjects
{
namespace
{
using Math::Tenth;
constexpr float LampGreen = std::bit_cast<float>(0x3f4ccccdu);
constexpr float WarmGreen = std::bit_cast<float>(0x3f19999au);
constexpr float CandleBlue = std::bit_cast<float>(0x3e4ccccdu);
constexpr std::int32_t LampRadius = 3;
constexpr double DoorDistance = 200.0;
constexpr float DoorTurn = 10.0f;
constexpr float DoorApproach = std::bit_cast<float>(0x3e4ccccdu);
constexpr std::int32_t SlidingDoorSound = 0x12;
constexpr std::int32_t HingedDoorSound = 0x11;

Core::Expected<void, std::string> AddObjectLight(const Kernel::ObjectStepGlobals& globals, const glm::vec3& position,
                                                 const glm::vec3& color, std::int32_t radius, std::string_view address)
{
    return Terrain::AddDynamicLight(globals.dynamicLighting, position, color, radius, address);
}
} // namespace

namespace
{
bool HasAngleBits(float angle, float degrees)
{
    return std::bit_cast<std::uint32_t>(angle) == std::bit_cast<std::uint32_t>(degrees);
}

void OpenSlidingDoor(Kernel::WorldObject& object, double opening)
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
    // fcomp/test C3 accepts zero with both signs and unordered.
    if (yaw == 0.0f || std::isnan(yaw))
    {
        object.transform.position.x = static_cast<float>(static_cast<double>(object.copiedPosition.x) + opening * 2.0);
    }
    if (HasAngleBits(yaw, 180.0f))
    {
        object.transform.position.x = static_cast<float>(static_cast<double>(object.copiedPosition.x) - opening * 2.0);
    }
}

void OpenHingedDoor(Kernel::WorldObject& object, double opening)
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

Core::Expected<void, std::string> StepObjectLamp(const Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                 const Kernel::ObjectStepGlobals& globals)
{
    const bool candle = object.type == 0x96;
    const int intensity = (random.Next() & (candle ? 3 : 1)) + (candle ? 3 : 6);
    const glm::vec3 light =
        Particles::WarmLight(intensity, candle ? WarmGreen : LampGreen, candle ? CandleBlue : WarmGreen);
    return AddObjectLight(globals, object.transform.position, light, LampRadius, "0x605718");
}

Core::Expected<void, std::string> StepDeviasObjectDoor(Kernel::WorldObject& object,
                                                       const Kernel::ObjectStepGlobals& globals)
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
        globals.sound(Audio::SoundId{sliding ? SlidingDoorSound : HingedDoorSound});
        return {};
    }
    object.transform.angles.z =
        static_cast<float>(Math::TurnAngle(object.transform.angles.z, object.copiedAngles.z, DoorTurn));
    for (int axis = 0; axis < 2; ++axis)
    {
        const double current = object.transform.position[axis];
        object.transform.position[axis] = static_cast<float>(
            (static_cast<double>(object.copiedPosition[axis]) - current) * static_cast<double>(DoorApproach) + current);
    }
    return {};
}

Core::Expected<void, std::string> StepAidaObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                 const Kernel::ObjectStepGlobals& globals)
{
    // 0x6b8a24 (0.015f), 0x6b88d4 (0.01f), 0x6b87b4/0x6b8a20 (0.4f and 0.6f), 0x6b8a1c/0x6b8854 (0.9f and 0.2f).
    constexpr double Scroll = static_cast<double>(std::bit_cast<float>(0x3c75c28fu));
    constexpr double Hundredth = Math::HundredthAsDouble;
    constexpr double GreenLow = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));
    constexpr double GreenHigh = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    constexpr double RedHigh = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    constexpr double RedLow = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    constexpr float HalfAlpha = std::bit_cast<float>(0x3f000000u);
    constexpr float SlowSpeed = std::bit_cast<float>(0x3d4ccccdu);
    constexpr std::int32_t AidaRadius = 2;
    constexpr std::int32_t LightSpan = 5;
    constexpr Audio::SoundId AmbientSound{0x246};
    switch (object.type)
    {
    case 0x19:
    case 0x1c:
        object.state.uv.y = static_cast<float>(static_cast<double>(object.state.uv.y) - Scroll);
        break;
    case 0x1e:
    case 0x47:
    {
        // 0x425137/0x425186: the draw times 0.01 stays in the x87; each channel is stored from its sum.
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
        object.state.hiddenMesh = Kernel::HiddenObject;
        break;
    case 0x40:
        object.speed = SlowSpeed;
        break;
    default:
        break;
    }
    // 0x4251fb: 0x41ed10(0x246, 0, 0) with each arriving object, of any type.
    if (!globals.sound)
    {
        return Core::Unexpected(std::string("0x4251fb: falta el emisor sonoro 0x41ed10 del 0x246"));
    }
    globals.sound(AmbientSound);
    return {};
}

Core::Expected<void, std::string> StepLandOfTrialsObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                         const Kernel::ObjectStepGlobals& globals)
{
    constexpr std::uint32_t TimeMask = 0x3ff;
    constexpr std::uint32_t ButterflyWindow = 10;
    constexpr std::uint32_t AmbientPeriod = 300000; // 0x440159: 0x493e0, unsigned (jbe).
    constexpr Audio::SoundId AmbientSound{0x1ef};
    // 0x6b8bbc (0.0024f) and 0x6b89dc (0.3f): the 0x1b's sway.
    constexpr double SwayRate = static_cast<double>(std::bit_cast<float>(0x3b1d4952u));
    constexpr float SwayHeight = std::bit_cast<float>(0x3e99999au);
    // 0x6b87b8 (0.1f), 0x6b8a1c (0.9f) and 0x6b8854 (0.2f): the 0x2a's light.
    constexpr double TorchStep = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    constexpr double TorchRed = static_cast<double>(std::bit_cast<float>(0x3f666666u));
    constexpr double TorchGreen = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    constexpr int TorchMask = 3, TorchBase = 3;
    constexpr std::int32_t TorchRadius = 3;
    // 0x43ffd8 leaves timeGetTime in EDI; each call asks for the borrowed clock where the ASM makes it.
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
        // 0x43fffd: the and leaves a non-negative value, and the following jl never jumps.
        if ((time.Value() & TimeMask) < ButterflyWindow)
        {
            if (globals.effects == nullptr)
                return Core::Unexpected(
                    std::string("0x440044: falta la tabla de efectos de modelo para la mariposa 0xae"));
            // 0x518cf0(0xae, position, angles, (1, 1, 1), subtype, object, −1, 0, 0, 0, 0): the step runs in the game
            // scene (5), so the butterfly is not born hidden.
            static_cast<void>(globals.effects->CreateButterflyEffect(object.transform.position, object.transform.angles,
                                                                     glm::vec3(1.0f), subtype, object.transform.scale,
                                                                     false, random));
        }
        object.state.hiddenMesh = Kernel::HiddenObject;
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
        // 0x44014a: those that emit in the frame (0x4401e0) are not drawn.
        object.state.hiddenMesh = Kernel::HiddenObject;
        break;
    case 0x1b:
    {
        // 0x4400ce..0x4400eb: z += sin(clock · 0.0024f + phase) · 0.3f, each step over the previous z; without +0x58's
        // −2.
        const double argument =
            static_cast<double>(globals.worldClock) * SwayRate + static_cast<double>(object.field84);
        const auto sway = Math::Detail::SineProductPC53(argument, SwayHeight);
        if (!sway)
            return Core::Unexpected(std::string("0x4400e0: el vaivén del 0x1b del 31 no cabe en el perfil PC53"));
        object.transform.position.z = static_cast<float>(*sway + static_cast<double>(object.transform.position.z));
        break;
    }
    case 0x2a:
    {
        // 0x4400f0..0x44013f: ((rand() & 3) + 3) · 0.1f stays in the x87 for the three channels.
        const double level = static_cast<double>((random.Next() & TorchMask) + TorchBase) * TorchStep;
        const glm::vec3 light(static_cast<float>(level * TorchRed), static_cast<float>(level * TorchGreen),
                              static_cast<float>(level * TorchStep));
        if (auto added = AddObjectLight(globals, object.transform.position, light, TorchRadius, "0x440142");
            !added.HasValue())
            return added;
        object.state.hiddenMesh = Kernel::HiddenObject;
        break;
    }
    default:
        break;
    }
    // 0x440151..0x440170: with any type, the 0x1ef ambient every more than five system minutes; the time it stores is a
    // second call's.
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
        // 0x44016b stores the time before the sound request.
        *globals.trialsAmbientTime = stored.Value();
        if (!globals.sound)
            return Core::Unexpected(std::string("0x440170: falta el emisor sonoro 0x41ed10 del 0x1ef"));
        globals.sound(AmbientSound);
    }
    return {};
}

Core::Expected<void, std::string> StepCrywolfObject(Kernel::WorldObject& object, Crt::CrtRandom& random,
                                                    const Kernel::ObjectStepGlobals& globals)
{
    // 0x430429..0x430437: (0.2f; 0.7f; 0.5f), in the ASM's bits.
    constexpr glm::vec3 GlowLight(std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3f333333u),
                                  std::bit_cast<float>(0x3f000000u));
    // 0x6b8a20 (0.6f) and 0x6b8854 (0.2f)
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
        // 0x43044d..0x430494: fst of the red; the green and the blue come from the unrounded product.
        const std::int32_t intensity = (random.Next() & TorchMask) + TorchBase;
        return AddObjectLight(globals, object.transform.position,
                              Particles::WarmLight(intensity, TorchGreen, TorchBlue), TorchRadius, "0x430497");
    }
    case 0x51:
        object.alpha = FaintAlpha; // 0x4304a6
        return {};
    case 0x52:
    case 0x53:
    case 0x54:
        object.state.hiddenMesh = Kernel::HiddenObject; // 0x4304b7
        return {};
    default:
        return {};
    }
}
} // namespace Client::WorldObjects
