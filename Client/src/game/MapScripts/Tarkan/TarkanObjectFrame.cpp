#include "MapScripts/Tarkan/TarkanObjectFrame.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/RoundedMath.h"
#include "WorldObjects/ObjectFrameSprites.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Math/Ftol.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::MapScripts::Tarkan
{
namespace
{
using Math::D;
using Math::F32;

// 0x6b89bc (0.002f), 0x6b89dc (0.3f), 0x6b8a5c (0.7f), 0x6ba73c (0.5882353f), 0x6ba738 (0.32f), 0x6b87f8 (5),
// 0x6b8b94 (1.5) and 0x6b882c (0.5).
constexpr double PulseRate = static_cast<double>(std::bit_cast<float>(0x3b03126fu));
constexpr double PulseAmplitude = static_cast<double>(std::bit_cast<float>(0x3e99999au));
constexpr double PulseBase = static_cast<double>(std::bit_cast<float>(0x3f333333u));
constexpr double SparkRed = static_cast<double>(std::bit_cast<float>(0x3f169696u));
constexpr double ImpactShade = static_cast<double>(std::bit_cast<float>(0x3ea3d70au));
constexpr double SparkScale = 5.0;
constexpr double ImpactScale = 1.5;
constexpr double PhasePerDegree = 5.0;
constexpr double HalfScale = 0.5;
constexpr std::uint16_t SparkTexture = 0x5c6;
constexpr std::uint16_t ImpactTexture = 0x716;
// 0x68b193c and 0x68b187c: bones 6 and 2 from 0x68b181c, in steps of 0x30.
constexpr std::size_t SparkBone = 6;
constexpr std::size_t ImpactBone = 2;

double Pulse(double phase)
{
    return (Math::RoundedSin(phase * PulseRate) * PulseAmplitude) + PulseBase;
}

Core::Expected<void, std::string> Sprite(const WorldObjects::GameObjectPass& pass, std::uint16_t texture,
                                         std::size_t bone, float scale, const glm::vec3& light,
                                         std::string_view address)
{
    // 0x4ec110(bone, local_38, out, 0) with local_38 at zero since 0x602fce.
    const auto point = WorldObjects::ObjectBonePoint(pass, bone, glm::vec3(0.0f));
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de sprites (0x5491e0)", address));
    // 0x5491e0(texture, point, scale, light, object, 0, 0): MuRet does not keep the sprite's owner (Q-C303).
    static_cast<void>(pass.sprites->Create(texture, point.Value(), scale, light, 0.0f, 0));
    return {};
}

// 0x53fe70(0x5f2, point, angles, object light, subtype, scale, 0) with the borrowed world clock.
Core::Expected<void, std::string> Smoke(const WorldObjects::GameObjectPass& pass,
                                        const WorldObjects::WorldObject& object, const glm::vec3& position,
                                        std::int32_t subtype, float scale, std::string_view address)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de partículas (0x53fe70)", address));
    Particles::PortableParticleTrigonometry trigonometry;
    const auto created =
        pass.particles->CreateChecked(Particles::BreathParticle, position, object.transform.angles, object.light,
                                      subtype, scale, std::cref(pass.worldClock), *pass.random, 0, trigonometry);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("{}: el 5f2/{} falla con error {}", address, subtype, static_cast<int>(created.Error())));
    return {};
}

Core::Expected<void, std::string> SmokeAndStone(WorldObjects::WorldObject& object,
                                                const WorldObjects::GameObjectPass& pass)
{
    // 0x603c9d..0x603cda: +0x58 before the two ftol; the angle's first, times 10 in 32 bits.
    object.state.hiddenMesh = WorldObjects::HiddenObject;
    const std::optional<std::int32_t> angle = Math::OriginalFtolLow(D(object.transform.angles.z));
    if (!angle)
        return Core::Unexpected(std::string("0x603ca7: ftol del ángulo z fuera de lo acreditado"));
    const std::optional<std::int32_t> clock = Math::OriginalFtolLow(D(pass.worldClock));
    if (!clock)
        return Core::Unexpected(std::string("0x603cb7: ftol del reloj del mundo fuera de lo acreditado"));
    constexpr std::int32_t Period = 10000;
    constexpr std::uint32_t WindowStart = 3500;
    constexpr std::uint32_t WindowEnd = 4000;
    constexpr std::uint32_t Tenfold = 10;
    const std::uint32_t base = static_cast<std::uint32_t>(*angle) * Tenfold;
    const std::int32_t phase = *clock % Period;
    if (phase <= static_cast<std::int32_t>(base + WindowStart) || phase >= static_cast<std::int32_t>(base + WindowEnd))
        return {};
    const glm::vec3 position = object.transform.position;
    if (auto smoked = Smoke(pass, object, position, 8, object.transform.scale, "0x603d14"); !smoked.HasValue())
        return smoked;
    constexpr int StoneChance = 3;
    if (pass.random->Next() % StoneChance != 0)
        return {};
    // 0x603d31..0x603d7c: (rand() & 127) − 64 in x and in y, added on the x87 and stored; the object's z.
    constexpr int Spread = 0x7f;
    constexpr int Center = 0x40;
    const int dx = (pass.random->Next() & Spread) - Center;
    const float x = F32(static_cast<double>(dx) + D(position.x));
    const int dy = (pass.random->Next() & Spread) - Center;
    const float y = F32(static_cast<double>(dy) + D(position.y));
    if (auto smoked =
            Smoke(pass, object, glm::vec3(x, y, position.z), 4, F32(D(object.transform.scale) * HalfScale), "0x603d95");
        !smoked.HasValue())
        return smoked;
    // 0x603db1..0x603dc8: the type's rand() before 0x518cf0(200 or 201, …, 0, 0, −1, 0, 0, 0, 0).
    constexpr std::int16_t FirstChip = Effects::FirstStoneChipEffect;
    const auto chip = static_cast<std::int16_t>(FirstChip + (pass.random->Next() & 1));
    if (pass.effects == nullptr)
        return Core::Unexpected(std::string("0x603dc8: la piedrita del 0x53 pide la tabla de efectos (0x518cf0)"));
    static_cast<void>(
        pass.effects->CreateStoneChip(chip, position, object.transform.angles, object.light, *pass.random));
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTarkanObjectFrame(WorldObjects::WorldObject& object,
                                                        const WorldObjects::GameObjectPass& pass)
{
    const double clock = D(pass.worldClock);
    switch (object.type)
    {
    case 0:
    {
        // 0x603a5a..0x603abd: the light from the x87's s; the scale from the stored L.
        const double pulse = Pulse(clock);
        const float stored = F32(pulse);
        const glm::vec3 light{F32(pulse * SparkRed), stored, stored};
        return Sprite(pass, SparkTexture, SparkBone, F32(D(stored) * SparkScale), light, "0x603abd");
    }
    case 0x3c:
    {
        // 0x603acc: twenty smokes the first time, with the object's position, angles, light and scale.
        if (object.state.hiddenMesh != WorldObjects::HiddenObject)
        {
            constexpr int Smokes = 20;
            for (int smoke = 0; smoke < Smokes; ++smoke)
            {
                if (auto smoked = Smoke(pass, object, object.transform.position, 6, object.transform.scale, "0x603af3");
                    !smoked.HasValue())
                    return smoked;
            }
        }
        object.state.hiddenMesh = WorldObjects::HiddenObject;
        return {};
    }
    case 0x3f:
    case 0x40:
    {
        // 0x603b0c/0x603b87: the phase with the z angle times 5. The 0x3f takes the light from the x87's s; the 0x40
        // stores it first and takes it from there.
        const double pulse = Pulse((D(object.transform.angles.z) * PhasePerDegree) + clock);
        const float stored = F32(pulse);
        const glm::vec3 light = object.type == 0x3f
                                    ? glm::vec3(F32(pulse * SparkRed), stored, stored)
                                    : glm::vec3(stored, F32(D(stored) * ImpactShade), F32(D(stored) * ImpactShade));
        return Sprite(pass, ImpactTexture, ImpactBone, F32(D(stored) * ImpactScale), light,
                      object.type == 0x3f ? "0x603b78" : "0x603bf7");
    }
    case 0x46:
    {
        // 0x603c06: +0x58 before the rand().
        object.state.hiddenMesh = WorldObjects::HiddenObject;
        constexpr int SmokeChance = 5;
        if (pass.random->Next() % SmokeChance != 0)
            return {};
        return Smoke(pass, object, object.transform.position, 7, object.transform.scale, "0x603c3d");
    }
    case 0x4c:
    {
        // 0x603c4c: +0x58 before the ftol; the signed remainder against 4500.
        object.state.hiddenMesh = WorldObjects::HiddenObject;
        const std::optional<std::int32_t> low = Math::OriginalFtolLow(clock);
        if (!low)
            return Core::Unexpected(std::string("0x603c59: ftol del reloj del mundo fuera de lo acreditado"));
        constexpr std::int32_t Period = 5000;
        constexpr std::int32_t Threshold = 4500;
        if (*low % Period <= Threshold)
            return {};
        return Smoke(pass, object, object.transform.position, 4, object.transform.scale, "0x603c8e");
    }
    case 0x53:
        return SmokeAndStone(object, pass);
    default:
        return {};
    }
}
} // namespace Client::MapScripts::Tarkan
