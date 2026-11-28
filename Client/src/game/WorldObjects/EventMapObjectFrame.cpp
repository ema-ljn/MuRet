#include "WorldObjects/EventMapObjectFrame.h"

#include "Core/Expected.h"
#include "Kernel/ObjectPass.h"
#include "Kernel/WorldObject.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "WorldObjects/ObjectFrameSprites.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Particles/BloodCastleParticleKernels.h"
#include "Math/Ftol.h"
#include "Kernel/JointRequest.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::WorldObjects
{
namespace
{
using Math::F32;

// 0x6b89c8 (0.001f), 0x6b86d0 (20), 0x6b89dc (0.3f), 0x6b8a5c (0.7f), 0x6b882c (0.5), 0x6b8818 (2) and 0x6b87d4 (1).
constexpr double Thousandth = Math::ThousandthAsDouble;
constexpr double AngleRate = 20.0;
constexpr float PulseSwing = std::bit_cast<float>(0x3e99999au);
constexpr double PulseBase = static_cast<double>(std::bit_cast<float>(0x3f333333u));
constexpr double Half = Math::HalfAsDouble;
constexpr double TorchLead = 2.0;
constexpr double TorchStep = 1.0;

constexpr std::int32_t RainParticle = 0x68;
constexpr std::int32_t GlowSpriteTexture = 0x5ac;
constexpr std::int32_t OrbTexture = 0x60f;
constexpr std::int32_t TorchOrbSubtype = 4;
// 0x6042a5..0x604285 (the 0x60f/4)
constexpr glm::vec3 TorchOrbLight{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f4ccccdu)};
constexpr float TorchOrbScale = std::bit_cast<float>(0x3e428f5cu);

constexpr std::int16_t DevilSquareRainObject = 2;
constexpr std::int16_t GlowingStatue = 0xb;
constexpr std::int16_t PulsingOrb = 0xd;
constexpr std::int16_t Torch = 0x25;
// 0x603df9 and 0x60433a..0x604361
constexpr std::size_t FirstRainBone = 23;
constexpr std::size_t SecondRainBone = 31;
constexpr glm::vec3 RainOffset{-15.0f, 0.0f, 0.0f};
constexpr std::array<std::size_t, 7> StatueBones{1, 2, 4, 6, 9, 10, 11};
constexpr glm::vec3 StatueOffset{0.0f, 0.0f, 2.0f};
constexpr float StatueSprite = 0.5f;
constexpr std::size_t OrbBone = 3;

// 0x4059d0: the Chaos Castle types and their clouds' lights (0x3d4ccccd, 0x3dcccccd and 0x3e99999a).
constexpr std::int16_t LastTrapObject = 3;
constexpr std::int16_t FirstCloudObject = 6;
constexpr std::int16_t LastManyCloudObject = 8;
constexpr std::int16_t BrightCloudObject = 0xc;
constexpr int ManyClouds = 10;
constexpr int FewClouds = 5;
constexpr int BrightClouds = 7;
constexpr std::int32_t BrightCloudSubtype = 7;
constexpr glm::vec3 CloudLight{std::bit_cast<float>(0x3d4ccccdu), std::bit_cast<float>(0x3d4ccccdu),
                               std::bit_cast<float>(0x3dcccccdu)};
constexpr glm::vec3 BrightCloudLight{std::bit_cast<float>(0x3e99999au)};
constexpr std::uint8_t NoArea = 0xff;
// 0x405cea..0x405d69: bone 1 (0x68b184c), +0x60's count, the joint and the trap's sound.
constexpr std::size_t TrapBone = 1;
constexpr std::int32_t TrapStart = 10;
constexpr std::int32_t TrapEnd = 5;
constexpr int TrapScaleSpan = 10;
constexpr double TrapScaleBase = 60.0;
constexpr std::int32_t TrapJoint = 0x615;
constexpr std::int32_t TrapJointSubtype = 2;
constexpr std::int32_t TrapSound = 0x9c;

Core::Expected<void, std::string> Emit(const Kernel::GameObjectPass& pass, std::int32_t type, const glm::vec3& position,
                                       const glm::vec3& angles, const glm::vec3& light, std::int32_t subtype,
                                       float scale, std::string_view address)
{
    if (pass.particles == nullptr || pass.random == nullptr)
        return Core::Unexpected(
            std::format("{}: falta el pool de partículas 0x743a198 para {:#x}/{}", address, type, subtype));
    const auto created =
        pass.particles->CreateChecked(type, position, angles, light, subtype, scale, pass.worldClock, *pass.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: el alta de {:#x}/{} falla con error {}", address, type, subtype,
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x6042a5..0x6042ac: 0x53fe70(0x60f, position, angles, (1; 0.8f; 0.8f), 4, 0.19f, 0), with the world clock and the
// orbit's trigonometry borrowed.
Core::Expected<void, std::string> Orbit(const Kernel::GameObjectPass& pass, const glm::vec3& position,
                                        const glm::vec3& angles)
{
    if (pass.particles == nullptr)
        return Core::Unexpected(std::string("0x6042ac: falta el pool de partículas 0x743a198 para 0x60f/4"));
    Particles::PortableParticleTrigonometry trigonometry;
    const auto created =
        pass.particles->CreateChecked(Particles::OrbitParticle, position, angles, TorchOrbLight, TorchOrbSubtype,
                                      TorchOrbScale, std::cref(pass.worldClock), *pass.random, 0, trigonometry);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x6042ac: el alta de 0x60f/4 falla con error {}", static_cast<unsigned>(created.Error())));
    return {};
}

// 0x5491e0(texture, point, scale, light, object, 0, 0): MuRet does not keep the sprite's owner (Q-C303).
Core::Expected<void, std::string> Sprite(const Kernel::GameObjectPass& pass, std::int32_t texture,
                                         const glm::vec3& position, float scale, const glm::vec3& light,
                                         std::string_view address)
{
    if (pass.sprites == nullptr)
        return Core::Unexpected(std::format("{}: falta el pool de sprites (0x5491e0)", address));
    static_cast<void>(pass.sprites->Create(static_cast<std::uint16_t>(texture), position, scale, light, 0.0f, 0));
    return {};
}

Core::Expected<void, std::string> RainAt(const Kernel::WorldObject& object, const Kernel::GameObjectPass& pass,
                                         std::size_t bone, std::string_view address)
{
    const auto point = Kernel::ObjectBonePoint(pass, bone, RainOffset);
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    return Emit(pass, RainParticle, point.Value(), object.transform.angles, glm::vec3(1.0f), 0, 1.0f, address);
}

// 0x60433a..0x6043e9.
Core::Expected<void, std::string> StatueGlows(const Kernel::WorldObject& object, const Kernel::GameObjectPass& pass)
{
    const double phase =
        (static_cast<double>(object.transform.angles.z) * AngleRate) + static_cast<double>(pass.worldClock);
    const std::optional<double> wave = Math::Detail::SineSumPC53(phase * Thousandth, 1.0);
    if (!wave)
        return Core::Unexpected(std::string("0x604374: el seno del reloj no cabe en el perfil PC53"));
    const double level = *wave * Half;
    const glm::vec3 light{F32(level), F32(level * Half), 0.0f};
    for (const std::size_t bone : StatueBones)
    {
        const auto point = Kernel::ObjectBonePoint(pass, bone, StatueOffset);
        if (!point.HasValue())
            return Core::Unexpected(point.Error());
        if (auto created = Sprite(pass, GlowSpriteTexture, point.Value(), StatueSprite, light, "0x6043dd");
            !created.HasValue())
            return created;
    }
    return {};
}

// 0x6042bb..0x60432b.
Core::Expected<void, std::string> PulsingOrbSprite(const Kernel::GameObjectPass& pass)
{
    const std::optional<double> wave =
        Math::Detail::SineProductPC53(static_cast<double>(pass.worldClock) * Thousandth, PulseSwing);
    if (!wave)
        return Core::Unexpected(std::string("0x6042d0: el seno del reloj no cabe en el perfil PC53"));
    const float level = F32(*wave + PulseBase);
    const auto point = Kernel::ObjectBonePoint(pass, OrbBone, glm::vec3(0.0f));
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    return Sprite(pass, OrbTexture, point.Value(), F32(static_cast<double>(level) + Half), glm::vec3(level),
                  "0x60432b");
}

// 0x604199..0x6041c2 and 0x60421c..0x604245: ftol of the phase (plus `lead`) % 4 and the phase plus 1, stored.
Core::Expected<bool, std::string> TorchTurn(Kernel::WorldObject& object, double lead, std::string_view address)
{
    const std::optional<std::int32_t> turn = Math::OriginalFtolLow(static_cast<double>(object.field84) + lead);
    if (!turn)
        return Core::Unexpected(std::format("{}: la fase del 0x25 no tiene un ftol acreditado", address));
    constexpr std::int32_t TorchPeriod = 4;
    object.field84 = F32(static_cast<double>(object.field84) + TorchStep);
    return *turn % TorchPeriod == 0;
}

// 0x604171..0x6042ac.
Core::Expected<void, std::string> TorchFrame(Kernel::WorldObject& object, const Kernel::GameObjectPass& pass)
{
    const glm::vec3& position = object.transform.position;
    const glm::vec3& angles = object.transform.angles;
    if (pass.random->Next() % 2 == 0)
    {
        const auto turn = TorchTurn(object, TorchLead, "0x6041a5");
        if (!turn.HasValue())
            return Core::Unexpected(turn.Error());
        if (turn.Value())
        {
            if (auto flame =
                    Emit(pass, Particles::TorchFlameParticle, position, angles, glm::vec3(1.0f), 0, 1.0f, "0x6041e4");
                !flame.HasValue())
                return flame;
            if (auto spark =
                    Emit(pass, Particles::TorchSparkParticle, position, angles, glm::vec3(1.0f), 0, 1.0f, "0x6041fd");
                !spark.HasValue())
                return spark;
        }
    }
    if (pass.random->Next() % 2 != 0)
        return {};
    const auto turn = TorchTurn(object, 0.0, "0x604222");
    if (!turn.HasValue())
        return Core::Unexpected(turn.Error());
    if (!turn.Value())
        return {};
    if (auto smoke = Emit(pass, Particles::CloudParticle, position, angles, glm::vec3(1.0f),
                          Particles::TorchSmokeSubtype, 1.0f, "0x604265");
        !smoke.HasValue())
        return smoke;
    if (auto spark = Emit(pass, Particles::TorchSparkParticle, position, angles, glm::vec3(1.0f), 1, 1.0f, "0x60427e");
        !spark.HasValue())
        return spark;
    return Orbit(pass, position, angles);
}
// 0x405abb..0x405c5c: the clouds of 6 to 0xc, owned by the object, the first time.
Core::Expected<void, std::string> ChaosCastleClouds(Kernel::WorldObject& object, const Kernel::GameObjectPass& pass)
{
    if (object.state.hiddenMesh != Kernel::HiddenObject)
    {
        const std::int16_t type = object.type;
        const bool bright = type == BrightCloudObject;
        const int count = bright ? BrightClouds : (type <= LastManyCloudObject ? ManyClouds : FewClouds);
        const std::int32_t subtype = bright ? BrightCloudSubtype : type - FirstCloudObject;
        const glm::vec3 light = bright ? BrightCloudLight : CloudLight;
        const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::WorldObject, object.identity};
        for (int cloud = 0; cloud < count; ++cloud)
        {
            if (pass.particles == nullptr || pass.random == nullptr)
                return Core::Unexpected(
                    std::format("0x405b02: falta el pool de partículas 0x743a198 para 0x622/{}", subtype));
            const auto created = pass.particles->CreateOwnedChecked(
                Particles::CloudParticle, object.transform.position, object.transform.angles, light, subtype,
                object.transform.scale, pass.worldClock, *pass.random, pass.scene, owner);
            if (!created.HasValue())
                return Core::Unexpected(std::format("0x405b02: el alta de 0x622/{} falla con error {}", subtype,
                                                    static_cast<unsigned>(created.Error())));
        }
    }
    object.state.hiddenMesh = Kernel::HiddenObject;
    return {};
}

// 0x405cc5..0x405d82: the 0..3 with +0x8a and not hidden take the point of bone 1 of the global pose (0x4ec110 with
// 0x68b184c and zero offset). With +0x60 at 10 it creates joint 0x615/2 there, from that point to that point, with the
// object's angles and scale rand() % 10 + 60 (0x6b86cc), and asks for sound 0x9c + (rand() & 1) with 0x41ed10(id, 0,
// 0). Then, +0x60 below 5 turns +0x8a off and, otherwise, drops by one.
Core::Expected<void, std::string> ChaosCastleTrap(Kernel::WorldObject& object, const Kernel::GameObjectPass& pass)
{
    const auto point = Kernel::ObjectBonePoint(pass, TrapBone, glm::vec3(0.0f));
    if (!point.HasValue())
        return Core::Unexpected(point.Error());
    if (object.field60 == TrapStart)
    {
        if (pass.random == nullptr)
            return Core::Unexpected(std::string("0x405d17: la trampa de Chaos Castle saca rand(), y el pase no lo "
                                                "tiene"));
        const float scale = F32(static_cast<double>(pass.random->Next() % TrapScaleSpan) + TrapScaleBase);
        if (!pass.createJoint)
            return Core::Unexpected(std::string("0x405d49: falta quien cree el joint 0x615/2 de Chaos Castle"));
        glm::vec3 bone = point.Value();
        const Kernel::JointRequest request{.type = TrapJoint,
                                           .firstPosition = bone,
                                           .secondPosition = bone,
                                           .angles = object.transform.angles,
                                           .subtype = TrapJointSubtype,
                                           .ownerContext = nullptr,
                                           .scale = scale,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = pass.createJoint(request); !created.HasValue())
            return created;
        if (!pass.requestSound)
            return Core::Unexpected(std::string("0x405d69: falta el sonido 0x9c de la trampa de Chaos Castle"));
        pass.requestSound(Audio::SoundId{TrapSound + (pass.random->Next() & 1)});
    }
    if (object.field60 < TrapEnd)
        object.field8a = 0;
    else
        --object.field60;
    return {};
}

// 0x405c7f and 0x405d95: −1 draws it again and −2 hides it.
void ShowChaosCastleObject(Kernel::WorldObject& object, bool shown)
{
    object.state.hiddenMesh = shown ? Models::NoHiddenMesh : Kernel::HiddenObject;
}
} // namespace

Core::Expected<void, std::string> StepDevilSquareObjectFrame(const Kernel::WorldObject& object,
                                                             const Kernel::GameObjectPass& pass)
{
    if (object.type != DevilSquareRainObject)
        return {};
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x603e0e: el 2 del mapa 9 saca rand(), y el pase no lo tiene"));
    if (pass.random->Next() % 4 == 0)
    {
        if (auto rain = RainAt(object, pass, FirstRainBone, "0x603e4e"); !rain.HasValue())
            return rain;
    }
    if (pass.random->Next() % 4 == 0)
    {
        if (auto rain = RainAt(object, pass, SecondRainBone, "0x603e96"); !rain.HasValue())
            return rain;
    }
    return RainAt(object, pass, FirstRainBone, "0x603ee0");
}

Core::Expected<void, std::string> StepBloodCastleObjectFrame(Kernel::WorldObject& object,
                                                             const Kernel::GameObjectPass& pass)
{
    if (object.type == GlowingStatue)
        return StatueGlows(object, pass);
    if (object.type == PulsingOrb)
        return PulsingOrbSprite(pass);
    if (object.type != Torch)
        return {};
    if (pass.random == nullptr)
        return Core::Unexpected(std::string("0x604186: el 0x25 de los mapas 11 a 17 saca rand(), y el pase no lo "
                                            "tiene"));
    return TorchFrame(object, pass);
}

Core::Expected<void, std::string> StepChaosCastleObjectFrame(Kernel::WorldObject& object,
                                                             const Kernel::GameObjectPass& pass)
{
    const std::int16_t type = object.type;
    const std::uint8_t area = pass.chaosCastleArea;
    // 0x4055b0 sets +0x8a in the step (the trap armed or the chosen one's count); then it falls to the area.
    if (type >= 0 && type <= LastTrapObject && object.field8a != 0 && object.state.hiddenMesh != Kernel::HiddenObject)
        if (auto trap = ChaosCastleTrap(object, pass); !trap.HasValue())
            return trap;
    constexpr std::uint8_t OpenAreas = 2;
    if ((type >= 0 && type <= 5) || (type >= 0xd && type <= 0x11))
    {
        if (area >= OpenAreas && area != NoArea)
            ShowChaosCastleObject(object, false);
        return {};
    }
    if (type >= FirstCloudObject && type <= BrightCloudObject)
        return ChaosCastleClouds(object, pass);
    if (type >= 0x12 && type <= 0x15)
    {
        ShowChaosCastleObject(object, area == 7 || area == 8);
        return {};
    }
    if (type >= 0x18 && type <= 0x1d)
    {
        if (area == 4 || area == 5)
            ShowChaosCastleObject(object, true);
        else if (area >= 8)
            ShowChaosCastleObject(object, false);
        return {};
    }
    if (type >= 0x1e && type <= 0x23)
    {
        if (area == 1 || area == 2)
            ShowChaosCastleObject(object, true);
        else if (area >= 5)
            ShowChaosCastleObject(object, false);
    }
    return {};
}
} // namespace Client::WorldObjects
