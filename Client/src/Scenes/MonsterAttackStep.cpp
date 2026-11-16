#include "Scenes/MonsterAttackStep.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// 0x4f5b70 con sus ayudantes 0x43a7a0, 0x4436f0, 0x42dcf0, 0x429110, 0x42fce0 y 0x4324c0, cotejados con el PE c32bbd5f:
// 06, "Los casos de los monstruos (0x4f5b70)".
namespace Client::Scenes
{
namespace
{
using Render::Models::CrtRandom;
using Render::Models::LogicalAccess;

// 0x517630 y los accesos en línea a +0x38e: una entrada de un byte, un rand() al cerrarla en 0.
constexpr std::uint16_t SkillCloseRandoms = 1;
// 0x4f80e0..0x4f80f4 y 0x43b055..0x43b069: los lugares del pool que se aceptan como blanco.
constexpr std::int16_t PoolSlots = 400;
// '2' en +0x38e: la habilidad con que sueltan su efecto los casos de 0x4f5b70 y el 0x17c de 0x4324c0.
constexpr std::uint8_t CaseSkill = 0x32;
// 0x4f811b y 0x4f8a2c: las habilidades de los dos switch de la cola.
constexpr std::uint8_t KindSkill = 0x11;
constexpr std::uint8_t BoneSkill = 3;
constexpr std::uint16_t ThreeAquagoldArrowsKind = 0x39;
// Los valores de +0x381 que miran los casos.
constexpr std::uint8_t FirstStep = 1;
constexpr std::uint8_t FifthStep = 5;
constexpr std::uint8_t EighthStep = 8;
constexpr std::uint8_t NinthStep = 9;
constexpr std::uint8_t TenthStep = 10;
constexpr std::uint8_t ThirteenthStep = 13;
constexpr std::uint8_t FourteenthStep = 14;
constexpr std::uint8_t FifteenthStep = 15;
// Las acciones de pegar de un monstruo (+0x111).
constexpr std::uint8_t FirstAttack = 3;
constexpr std::uint8_t SecondAttack = 4;
constexpr std::int32_t RoarSound = 0x75;
constexpr std::int32_t HowlSound = 0x50;
constexpr std::int32_t HitSound = 0x42;
constexpr std::int32_t AngryRoarSound = 0x77;
constexpr std::int32_t TailFireParticle = World::FireParticle;
constexpr std::int32_t TailThunderParticle = World::ThunderParticle;
constexpr float TailParticleScale = 1.0f;
constexpr glm::vec3 InitialTailLight(1.0f);
constexpr glm::vec3 TrailAttackLight(1.0f, 0.5f, 0.0f);
constexpr glm::vec3 Kind31TailLight(std::bit_cast<float>(0x3ed70a3eu), std::bit_cast<float>(0x3f570a3eu),
                                    std::bit_cast<float>(0x3fb33333u));

// Un caso de un switch de 0x4f5b70 o de sus ayudantes: el +0x376 (o el tipo) y dónde arranca.
struct KindCase
{
    std::uint16_t kind = 0;
    std::uint32_t address = 0;
};

// 0x43a7a0 (la tabla de bytes de 0x43b8fc sobre +0x376 − 0x90 y sus saltos de 0x43b8d4): los monstruos de Kalima; los
// demás van a 0x43b8cb (AL en 0).
constexpr std::array KalimaCases{
    KindCase{0x90, 0x43a7fe},  KindCase{0xae, 0x43a7fe},  KindCase{0xb6, 0x43a7fe},  KindCase{0xbe, 0x43a7fe},
    KindCase{0x104, 0x43a7fe}, KindCase{0x10c, 0x43a7fe}, KindCase{0x114, 0x43a7fe}, KindCase{0x91, 0x43a872},
    KindCase{0xaf, 0x43a872},  KindCase{0xb7, 0x43a872},  KindCase{0xbf, 0x43a872},  KindCase{0x105, 0x43a872},
    KindCase{0x10d, 0x43a872}, KindCase{0x115, 0x43a872}, KindCase{0x92, 0x43ab7a},  KindCase{0xb0, 0x43ab7a},
    KindCase{0xb8, 0x43ab7a},  KindCase{0xc0, 0x43ab7a},  KindCase{0x106, 0x43ab7a}, KindCase{0x10e, 0x43ab7a},
    KindCase{0x116, 0x43ab7a}, KindCase{0x93, 0x43abea},  KindCase{0xb1, 0x43abea},  KindCase{0xb9, 0x43abea},
    KindCase{0xc1, 0x43abea},  KindCase{0x107, 0x43abea}, KindCase{0x10f, 0x43abea}, KindCase{0x117, 0x43abea},
    KindCase{0x94, 0x43ac3a},  KindCase{0xb2, 0x43ac3a},  KindCase{0xba, 0x43ac3a},  KindCase{0xc2, 0x43ac3a},
    KindCase{0x108, 0x43ac3a}, KindCase{0x110, 0x43ac3a}, KindCase{0x118, 0x43ac3a}, KindCase{0x95, 0x43ae56},
    KindCase{0xb3, 0x43ae56},  KindCase{0xbb, 0x43ae56},  KindCase{0xc3, 0x43ae56},  KindCase{0x109, 0x43ae56},
    KindCase{0x111, 0x43ae56}, KindCase{0x119, 0x43ae56}, KindCase{0xa0, 0x43b208},  KindCase{0xb4, 0x43b208},
    KindCase{0xbc, 0x43b208},  KindCase{0xc4, 0x43b208},  KindCase{0x10a, 0x43b208}, KindCase{0x112, 0x43b208},
    KindCase{0xa1, 0x43b279},  KindCase{0xb5, 0x43b279},  KindCase{0xbd, 0x43b279},  KindCase{0xc5, 0x43b279},
    KindCase{0x10b, 0x43b279}, KindCase{0x113, 0x43b5e4}};
// 0x4436f0 (la tabla de 0x443734 sobre +0x376 − 0x123): los que devuelven 1 (0x443721) sin hacer nada más.
constexpr std::array<std::uint16_t, 5> TrialsQuietKinds{0x123, 0x124, 0x125, 0x127, 0x12f};
// 0x42dcf0 (0x42dd11..0x42dd1b) y 0x429110 (0x429131..0x429142).
constexpr std::array SiegeCases{KindCase{0x68, 0x42dd56}, KindCase{0x120, 0x42dd23}};
constexpr std::array DoppelgangerCases{KindCase{0x130, 0x429211}, KindCase{0x134, 0x4291b1}, KindCase{0x135, 0x42914b}};
// 0x42fce0 (0x42fcf3..0x42fd02): por el tipo (+2).
constexpr std::array FortressCases{KindCase{0x1ad, 0x42fd43}, KindCase{0x1b2, 0x42fd04}};
// 0x4324c0 (la tabla de 0x432b8c sobre el tipo − 0x17c).
constexpr std::array CrywolfCases{KindCase{0x1b2, 0x4324fb}, KindCase{0x1ad, 0x432543}, KindCase{0x17c, 0x43258b},
                                  KindCase{0x17e, 0x432a0c}, KindCase{0x1b5, 0x432b30}};
// 0x4f5c7e..0x4f75ae: los casos por +0x376 (las tablas de 0x4f9074 sobre +0x376 − 0x23 y de 0x4f90b8 sobre
// +0x376 − 0x49, y el 0x48 de 0x4f5c95); los demás, y el 0xffff de los jugadores, van a 0x4f80e0.
constexpr std::array AttackKindCases{
    KindCase{0x23, 0x4f6fea}, KindCase{0x26, 0x4f7074}, KindCase{0x2a, 0x4f6b78}, KindCase{0x2d, 0x4f715a},
    KindCase{0x31, 0x4f67ad}, KindCase{0x35, 0x4f6442}, KindCase{0x36, 0x4f7f67}, KindCase{0x39, 0x4f7f67},
    KindCase{0x3a, 0x4f6442}, KindCase{0x3b, 0x4f6442}, KindCase{0x3d, 0x4f5d23}, KindCase{0x3f, 0x4f5d23},
    KindCase{0x42, 0x4f6152}, KindCase{0x43, 0x4f7074}, KindCase{0x46, 0x4f5cb6}, KindCase{0x47, 0x4f793e},
    KindCase{0x48, 0x4f7223}, KindCase{0x49, 0x4f798d}, KindCase{0x4a, 0x4f793e}, KindCase{0x4b, 0x4f798d},
    KindCase{0x4d, 0x4f7bc8}, KindCase{0x59, 0x4f75b5}, KindCase{0x5f, 0x4f75b5}, KindCase{0x67, 0x4f804e},
    KindCase{0x70, 0x4f75b5}, KindCase{0x76, 0x4f75b5}, KindCase{0x7c, 0x4f75b5}, KindCase{0x82, 0x4f75b5},
    KindCase{0x8f, 0x4f75b5}, KindCase{0x97, 0x4f7f67}, KindCase{0x12d, 0x4f793e}};
// 0x4f8147: los casos con la habilidad 0x11 (la tabla de 0x4f91cc sobre +0x376 − 0x25); los demás salen (0x4f903c).
constexpr std::array KindSkillCases{
    KindCase{0x25, 0x4f8837}, KindCase{0x2e, 0x4f876e}, KindCase{0x3d, 0x4f85ca}, KindCase{0x42, 0x4f890b},
    KindCase{0x45, 0x4f8562}, KindCase{0x49, 0x4f8493}, KindCase{0x4b, 0x4f8493}, KindCase{0x4d, 0x4f83f4},
    KindCase{0x57, 0x4f835b}, KindCase{0x5d, 0x4f835b}, KindCase{0x63, 0x4f835b}, KindCase{0x74, 0x4f835b},
    KindCase{0x7a, 0x4f835b}, KindCase{0x80, 0x4f835b}, KindCase{0x8d, 0x4f835b}, KindCase{0x59, 0x4f82b4},
    KindCase{0x5f, 0x4f82b4}, KindCase{0x70, 0x4f82b4}, KindCase{0x76, 0x4f82b4}, KindCase{0x7c, 0x4f82b4},
    KindCase{0x82, 0x4f82b4}, KindCase{0x8f, 0x4f82b4}, KindCase{0xa3, 0x4f814e}, KindCase{0xa5, 0x4f814e},
    KindCase{0xa7, 0x4f814e}, KindCase{0xa9, 0x4f814e}, KindCase{0xab, 0x4f814e}, KindCase{0xad, 0x4f814e}};
// 0x4f8a56: los casos con la habilidad 3 (la tabla de 0x4f9274 sobre +0x376 − 0x22); los demás van a 0x4f8f5b.
constexpr std::array BoneSkillCases{KindCase{0x22, 0x4f8ce1}, KindCase{0x25, 0x4f8c0d}, KindCase{0x27, 0x4f8e85},
                                    KindCase{0x30, 0x4f8dc4}, KindCase{0x4d, 0x4f8b78}, KindCase{0x59, 0x4f8a5d},
                                    KindCase{0x5f, 0x4f8a5d}, KindCase{0x70, 0x4f8a5d}, KindCase{0x76, 0x4f8a5d},
                                    KindCase{0x7c, 0x4f8a5d}, KindCase{0x82, 0x4f8a5d}, KindCase{0x8f, 0x4f8a5d}};

// Lo que devuelve un ayudante de 0x4f5b70: AL en 0 sigue al switch por +0x376; AL en 1 sale (0x4f903c).
enum class Helped : std::uint8_t
{
    Continue,
    Exit,
};

template <typename T, std::size_t N> bool IsOneOf(const std::array<T, N>& values, T value)
{
    return std::ranges::find(values, value) != values.end();
}

template <std::size_t N> const KindCase* CaseOf(const std::array<KindCase, N>& cases, std::uint16_t kind)
{
    const auto found = std::ranges::find(cases, kind, &KindCase::kind);
    return found != cases.end() ? &*found : nullptr;
}

// 0x517630 (thiscall sobre 0x57630c8) y sus copias en línea: abre +0x38e, lo lee y lo cierra.
std::uint8_t ReadTheSkill(ActorState& actor, CrtRandom& random)
{
    const LogicalAccess access(actor.byte38eAccesses, random, SkillCloseRandoms);
    return actor.byte38e;
}

// 0x41ed10(sonido, 0, 0): la CL10 decide cómo suena.
void RequestTheSound(const CharacterStepContext& context, std::int32_t sound)
{
    if (context.soundRequest)
        context.soundRequest(sound);
}

bool HasFreeModelEffect(const CharacterStepContext& context)
{
    const auto effects = context.world.Map().effects.Effects();
    return std::ranges::any_of(effects, [](const World::ModelEffect& effect) { return !effect.alive; });
}

bool IsJointPoolFull(const CharacterStepContext& context)
{
    return !context.joints.empty() &&
           std::ranges::none_of(context.joints, [](const JointState& joint) { return !joint.alive; });
}

bool HasFreeParticle(const CharacterStepContext& context)
{
    if (context.particles == nullptr)
        return false;
    const auto particles = context.particles->All();
    return std::ranges::any_of(particles, [](const World::Particle& particle) { return !particle.alive; });
}

std::optional<std::size_t> CreateKalimaEarthquake(ActorState& actor, std::uint8_t state,
                                                  const CharacterStepContext& context)
{
    return context.world.Map().effects.CreateEarthquake(
        actor.position, actor.angles, actor.light, state,
        World::ModelEffectOwner{.type = &actor.type, .effect = nullptr, .angles = &actor.angles},
        context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random);
}

// Un efecto, joint o partícula sin portar en un caso de 0x4f5b70 o de sus ayudantes, con lo que hizo antes y sus
// rand().
Core::Unexpected<std::string> CutAt(std::uint32_t address, std::string_view what, const ActorState& actor,
                                    std::size_t slot)
{
    return Core::Unexpected(
        std::format("{:#x}: {} del lugar {} (tipo {:#x}, +0x376 en {:#x}, +0x381 en {}), sin portar", address, what,
                    slot, actor.type, actor.word376, actor.byte381));
}

// 0x4ec110(modelo, huesos propios + hueso · 0x30, offset, punto, 1) con la escala y la posición que 0x4fd69b copió
// del monstruo al modelo antes del paso. Sin huesos propios (antes del primer dibujo), la matriz en cero (Q-C318),
// como OwnBonePoint de CharacterStep.cpp.
glm::vec3 BonePoint(const ActorState& actor, std::size_t bone, const glm::vec3& offset,
                    const CharacterStepContext& context)
{
    const Render::Models::BoneTransform matrix =
        bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Render::Models::BoneTransform(0.0f);
    const SharedModelPlacement placement = context.world.ModelPlacement(static_cast<std::uint16_t>(actor.type));
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), offset, placement.scale,
                                                     placement.origin);
}

Core::Expected<void, std::string> AttemptJoint(ActorState& actor, std::size_t slot, const CharacterStepContext& context,
                                               std::uint32_t address, std::string_view description)
{
    if (!IsJointPoolFull(context))
        return CutAt(address, description, actor, slot);
    return {};
}

Core::Expected<void, std::string> CreateTailParticle(ActorState& actor, std::size_t slot,
                                                     const CharacterStepContext& context, std::int32_t type,
                                                     const glm::vec3& position, const glm::vec3& light,
                                                     std::uint32_t address)
{
    if (context.particles == nullptr)
    {
        return Core::Unexpected(
            std::format("{:#x}: falta el pool de partículas para {:#x}/0 del lugar {}", address, type, slot));
    }
    const auto created = context.particles->CreateChecked(type, position, actor.angles, light, 0, TailParticleScale,
                                                          context.worldClock, context.random);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{:#x}: la partícula {:#x}/0 del lugar {} falla con error {}", address,
                                            type, slot, static_cast<unsigned>(created.Error())));
    }
    return {};
}

Core::Expected<void, std::string> CreateTailFire(ActorState& actor, std::size_t slot,
                                                 const CharacterStepContext& context, std::size_t bone,
                                                 std::uint32_t address)
{
    return CreateTailParticle(actor, slot, context, TailFireParticle, BonePoint(actor, bone, glm::vec3(0.0f), context),
                              actor.light, address);
}

Core::Expected<void, std::string> CreateTailThunder(ActorState& actor, std::size_t slot,
                                                    const CharacterStepContext& context, std::size_t bone,
                                                    const glm::vec3& offset, const glm::vec3& light,
                                                    std::uint32_t address)
{
    return CreateTailParticle(actor, slot, context, TailThunderParticle, BonePoint(actor, bone, offset, context), light,
                              address);
}

// 0x4f7a6b..0x4f7a8a: cinco puntos desde la posición del actor, con dos rand() por escritura.
void SetTheFifthTrailPoint(ActorState& actor, CrtRandom& random)
{
    constexpr int Iterations = 5;
    constexpr int OffsetSpan = 1001;
    constexpr int OffsetCenter = 500;
    constexpr float Rise = 500.0f;
    for (int iteration = 0; iteration < Iterations; ++iteration)
    {
        const auto dx = static_cast<std::int32_t>(random.Next() % OffsetSpan) - OffsetCenter;
        const auto dy = static_cast<std::int32_t>(random.Next() % OffsetSpan) - OffsetCenter;
        actor.trailOrigin = {
            static_cast<float>(static_cast<double>(actor.position.x) + static_cast<double>(dx)),
            static_cast<float>(static_cast<double>(actor.position.y) + static_cast<double>(dy)),
            static_cast<float>(static_cast<double>(actor.position.z) + static_cast<double>(Rise)),
        };
    }
}

// 0x43aa46..0x43ab01 y sus copias de 0x43b47c y 0x43b7e7: el switch por la habilidad leída (las tablas de bytes de
// 0x43b9a0, 0x43ba84 y 0x43bb68; los demás no hacen nada), todos con +0x381 desde 13 (JL o JC, el byte sin signo).
Core::Expected<void, std::string> StepTheKalimaSkill(ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                     const CharacterStepContext& context)
{
    constexpr std::uint8_t BurstSkill = 1;
    constexpr std::uint8_t RingSkill = 0x37;
    constexpr std::uint8_t ShieldSkill = 0xc8;
    constexpr std::uint8_t WardSkill = 0xc9;
    constexpr std::uint8_t GuardSkill = 0xca;
    constexpr std::uint32_t WardFlag = 0x100;
    constexpr std::uint32_t GuardFlag = 0x8;
    constexpr std::int32_t GuardSound = 0x1e1;
    constexpr std::int32_t BurstSound = 0x1e0;
    constexpr std::int32_t RingSound = 0x1df;
    if (actor.byte381 >= ThirteenthStep && !HasFreeModelEffect(context) &&
        (skill == BurstSkill || skill == RingSkill || skill == ShieldSkill))
    {
        if (skill == BurstSkill)
        {
            static_cast<void>(context.random.Next());
            RequestTheSound(context, BurstSound);
        }
        else if (skill == RingSkill)
        {
            RequestTheSound(context, RingSound);
        }
        return {};
    }
    if (actor.byte381 < ThirteenthStep)
        return {};
    switch (skill)
    {
    case BurstSkill:
        // 0x439bd0(c, +0x381, 13): rand() % 360, el giro de los cinco 0x518cf0(0xc2/8) alrededor de c y
        // 0x41ed10(0x1e0).
        static_cast<void>(context.random.Next());
        return CutAt(0x439ca6, "el primero de los cinco efectos 0xc2/8 de 0x439bd0 (CL11)", actor, slot);
    case RingSkill:
        // 0x439b20(c, +0x381, 13, 200,0f): tres 0x518cf0(0x11b/1) sobre c y 0x41ed10(0x1df), sin rand() antes.
        return CutAt(0x439ba5, "el primero de los tres efectos 0x11b/1 de 0x439b20 (CL11)", actor, slot);
    case ShieldSkill:
        // 0x439cd0(c, +0x381, 13): 0x518cf0(0xcb/3) y 0x518cf0(0xcc/4) sobre c.
        return CutAt(0x439d09, "los efectos 0xcb/3 y 0xcc/4 de 0x439cd0 (CL11)", actor, slot);
    case WardSkill:
    case GuardSkill:
        // 0x43aabf..0x43aaf7: +0x78 de c con 0x100 (0xc9) o 0x8 (0xca), +0x381 en 15 y 0x41ed10(0x1e1, 0, 0).
        actor.flags78 |= skill == WardSkill ? WardFlag : GuardFlag;
        actor.byte381 = FifteenthStep;
        RequestTheSound(context, GuardSound);
        return {};
    default:
        return {};
    }
}

// 0x43a7a0(c, blanco, c, blanco, modelo), con el blanco de +0x3a4 en [0, 400) o 0 (0x4f5bdc..0x4f5c08): los monstruos
// de Kalima. Todos sus casos devuelven 1.
Core::Expected<Helped, std::string> StepKalima(ActorState& actor, const KindCase& found, std::size_t slot,
                                               const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    switch (found.address)
    {
    case 0x43a7fe:
    case 0x43ab7a:
        // En 14, 0x53fe70(0x600, el blanco o el héroe ([0x743a170]) + 0x10 con la z + 0x6b87ac, c + 0x1c, (1, 1, 1),
        // 1, 1,0f, 0).
        if (actor.byte381 == FourteenthStep)
        {
            if (context.particles != nullptr && !HasFreeParticle(context))
                return Helped::Exit;
            return CutAt(found.address == 0x43a7fe ? 0x43a861 : 0x43abd9,
                         "la partícula 0x600/1 sobre el blanco o el héroe (CL11)", actor, slot);
        }
        return Helped::Exit;
    case 0x43a872:
    case 0x43b279:
    case 0x43b5e4:
    {
        const std::uint8_t skill = ReadTheSkill(actor, context.random);
        if (auto stepped = StepTheKalimaSkill(actor, skill, slot, context); !stepped.HasValue())
            return Core::Unexpected(stepped.Error());
        // 0x43ab04..0x43ab42: en la 4 y en 14 (+0x381 después del switch), el terremoto 0xfa y el 0x615 sobre c.
        if (found.address == 0x43a872 && action == SecondAttack && actor.byte381 == FourteenthStep)
        {
            static_cast<void>(CreateKalimaEarthquake(actor, 1, context));
            if (HasFreeModelEffect(context))
                return CutAt(0x43ab5f, "el efecto 0x615 tras el terremoto 0xfa (CL11)", actor, slot);
            actor.byte381 = FifteenthStep;
            return Helped::Exit;
        }
        // 0x43b538..0x43b5d4: en la 3, en 7 el terremoto 0xfa; desde 13, 0xf6/0 y 0x5de/1 y +0x381 en 15.
        if (found.address == 0x43b279 && action == FirstAttack)
        {
            constexpr std::uint8_t QuakeStep = 7;
            if (actor.byte381 == QuakeStep)
            {
                static_cast<void>(CreateKalimaEarthquake(actor, 0, context));
                return Helped::Exit;
            }
            if (actor.byte381 >= ThirteenthStep)
            {
                if (!HasFreeModelEffect(context))
                {
                    actor.byte381 = FifteenthStep;
                    return Helped::Exit;
                }
                return CutAt(0x43b5ac, "los efectos 0xf6/0 y 0x5de/1 (CL11)", actor, slot);
            }
        }
        return Helped::Exit;
    }
    case 0x43abea:
        // En la 4 y en 14, 0x518cf0(0x11a) sobre c.
        if (action == SecondAttack && actor.byte381 == FourteenthStep)
        {
            if (HasFreeModelEffect(context))
                return CutAt(0x43ac29, "el efecto 0x11a (CL11)", actor, slot);
        }
        return Helped::Exit;
    case 0x43ac3a:
        // Con 0x11 en +0x38e y en 14, el terremoto 0xfa/1.
        if (ReadTheSkill(actor, context.random) == KindSkill && actor.byte381 == FourteenthStep)
            static_cast<void>(CreateKalimaEarthquake(actor, 1, context));
        return Helped::Exit;
    case 0x43ae56:
    {
        constexpr std::uint8_t BurstSkill = 1;
        const std::uint8_t skill = ReadTheSkill(actor, context.random);
        if (actor.byte381 != FourteenthStep)
            return Helped::Exit;
        if (skill == BurstSkill)
        {
            // 0x43b18f..0x43b1f4: tres veces, rand() % 200 − 100 para la x y la y alrededor del héroe y
            // 0x518cf0(0xc2/7); corta en el primero, después de sus dos rand().
            constexpr int Iterations = 3;
            constexpr int RandomsPerIteration = 2;
            if (!HasFreeModelEffect(context))
            {
                for (int iteration = 0; iteration < Iterations; ++iteration)
                    for (int draw = 0; draw < RandomsPerIteration; ++draw)
                        static_cast<void>(context.random.Next());
                return Helped::Exit;
            }
            for (int draw = 0; draw < RandomsPerIteration; ++draw)
                static_cast<void>(context.random.Next());
            return CutAt(0x43b1f4, "el primero de los tres efectos 0xc2/7 alrededor del héroe (CL11)", actor, slot);
        }
        // 0x43b045..0x43b0ed: con 0x11 y el blanco de +0x3a4 de c en [0, 400), 0x4ec110 con el hueso 60 y tres joints
        // 0x610/6 hacia el blanco.
        const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
        if (skill == KindSkill && target >= 0 && target < PoolSlots)
        {
            if (IsJointPoolFull(context))
                return Helped::Exit;
            return CutAt(0x43b0ed, "los tres joints 0x610/6 hacia el blanco (CL11)", actor, slot);
        }
        return Helped::Exit;
    }
    case 0x43b208:
        // En la 3 y desde 13, 0xf6/5 y 0x5de/2 y +0x381 en 15.
        if (action == FirstAttack && actor.byte381 >= ThirteenthStep)
        {
            if (!HasFreeModelEffect(context))
            {
                actor.byte381 = FifteenthStep;
                return Helped::Exit;
            }
            return CutAt(0x43b241, "los efectos 0xf6/5 y 0x5de/2 (CL11)", actor, slot);
        }
        return Helped::Exit;
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x43a7a0 sin rama", found.address));
    }
}

// 0x42dcf0 en el 30: el 0x68 sale (en 5, 0x518cf0(0x19f) sobre el que pega con la z + 0x6b8a98) y el 0x120 suelta
// 0x518cf0(0x61c) siempre.
Core::Expected<Helped, std::string> StepTheSiege(const ActorState& actor, const KindCase& found, std::size_t slot,
                                                 const CharacterStepContext& context)
{
    if (found.address == 0x42dd23)
    {
        if (!HasFreeModelEffect(context))
            return Helped::Exit;
        return CutAt(0x42dd48, "el efecto 0x61c (CL11)", actor, slot);
    }
    if (actor.byte381 == FifthStep)
    {
        if (!HasFreeModelEffect(context))
            return Helped::Exit;
        return CutAt(0x42dd9c, "el efecto 0x19f (CL11)", actor, slot);
    }
    return Helped::Exit;
}

// 0x429110 en el 33 y el 54: en 10, el 0x135 en la 3 y el 0x134 y el 0x130 en la 4 sueltan su efecto; todos salen.
Core::Expected<Helped, std::string> StepTheDoppelganger(const ActorState& actor, const KindCase& found,
                                                        std::size_t slot, const CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    if (actor.byte381 != TenthStep)
        return Helped::Exit;
    switch (found.address)
    {
    case 0x42914b:
        // Cinco veces la misma pregunta (0x429152..0x4291a6): corta en la primera.
        if (action == FirstAttack)
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(0x42919a, "el primero de los cinco efectos 0xcf/3 (CL11)", actor, slot);
        }
        return Helped::Exit;
    case 0x4291b1:
        if (action == SecondAttack)
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(0x429202, "el efecto 0x141 (CL11)", actor, slot);
        }
        return Helped::Exit;
    default:
        if (action == SecondAttack)
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(0x42925a, "el efecto 0x61c/1 (CL11)", actor, slot);
        }
        return Helped::Exit;
    }
}

Core::Expected<void, std::string> ScatterTheBurst(const ActorState& actor, std::size_t slot,
                                                  const CharacterStepContext& context);

// 0x4324c0 en Crywolf (el 34), por el tipo. Salvo los efectos de los que pegan en 14 y 15, devuelve 0.
Core::Expected<Helped, std::string> StepCrywolf(ActorState& actor, const KindCase& found, std::size_t slot,
                                                const CharacterStepContext& context)
{
    switch (found.address)
    {
    case 0x4324fb:
        if (actor.byte381 == FourteenthStep)
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(0x432532, "el efecto 0xe1/1 con la key de +0x8a (CL11)", actor, slot);
        }
        return Helped::Continue;
    case 0x432543:
        if (actor.byte381 == FourteenthStep)
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(0x43257a, "el efecto 0xf8/1 con la key de +0x8a (CL11)", actor, slot);
        }
        return Helped::Continue;
    case 0x43258b:
    {
        // En 1, 0x51fff0(c + 0x10, 0): las piedras (0x51fe10, con dos rand() por cada 0x5c5) y ocho 0xc8 o 0xc9.
        if (actor.byte381 == FirstStep)
        {
            if (auto burst = ScatterTheBurst(actor, slot, context); !burst.HasValue())
                return Core::Unexpected(burst.Error());
            return Helped::Continue;
        }
        // 0x4325a6..0x4329db: en 14 y con el +0x376 0x3b, +0x38e; con '2', dieciocho 0x518cf0(0xc1a) girando.
        constexpr std::uint16_t StoneKind = 0x3b;
        if (actor.byte381 == FourteenthStep && actor.word376 == StoneKind &&
            ReadTheSkill(actor, context.random) == CaseSkill)
        {
            if (HasFreeModelEffect(context))
                return CutAt(0x4329db, "el primero de los dieciocho efectos 0xc1a (CL11)", actor, slot);
        }
        return Helped::Continue;
    }
    case 0x432a0c:
    {
        // 0x432a12..0x432a79: seis veces, 0x4ec110 con el hueso de la mano (+0x2bd, la derecha las tres primeras) y el
        // offset de [EBP − 0x30], que nadie escribe, y rand() % 360; el punto no lo lee nadie.
        constexpr int Turns = 6;
        for (int turn = 0; turn < Turns; ++turn)
            static_cast<void>(context.random.Next());
        // 0x432a7b..0x432a8a: en 1, 0x41ed10(0x75, 0, 0).
        if (actor.byte381 == FirstStep)
            RequestTheSound(context, RoarSound);
        // 0x432acc: el mismo offset sin escribir, ahora para el punto de las cuatro 0x5d9/0 (Q-C1050).
        return CutAt(0x432acc,
                     "0x4ec110 con el offset sin escribir de [EBP − 0x30], antes de las 0x5d9/0 (CL9 con el offset que "
                     "mida la VM, Q-C1050)",
                     actor, slot);
    }
    default:
        if (actor.byte381 == FifteenthStep)
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(0x432b63, "el efecto 0xed/1 con la key de +0x8a (CL11)", actor, slot);
        }
        return Helped::Continue;
    }
}

// 0x4f5ba3..0x4f5c78: los seis ayudantes, cada uno en sus mapas (excluyentes); el que devuelve 1 hace salir.
Core::Expected<Helped, std::string> StepTheMapHelpers(ActorState& actor, std::size_t slot,
                                                      const CharacterStepContext& context)
{
    const std::int32_t map = context.world.Runtime().map;
    // 0x4f5ba3..0x4f5c0f: 0x43a7a0, solo en los mapas 24 a 29 y 36.
    if ((map >= 0x18 && map <= 0x1d) || map == 0x24)
    {
        if (const KindCase* found = CaseOf(KalimaCases, actor.word376))
            return StepKalima(actor, *found, slot, context);
    }
    // 0x4436f0 (0x4f5c22): en el 31 (0x43ff10).
    if (map == 0x1f && IsOneOf(TrialsQuietKinds, actor.word376))
        return Helped::Exit;
    // 0x42dcf0 (0x4f5c35): en el 30.
    if (map == 0x1e)
    {
        if (const KindCase* found = CaseOf(SiegeCases, actor.word376))
            return StepTheSiege(actor, *found, slot, context);
    }
    // 0x429110 (0x4f5c48): en el 33 y el 54 (0x425070).
    if (map == 0x21 || map == 0x36)
    {
        if (const KindCase* found = CaseOf(DoppelgangerCases, actor.word376))
            return StepTheDoppelganger(actor, *found, slot, context);
    }
    // 0x42fce0 (0x4f5c5b): en el 35 (0x42ee20), el tipo 0x1ad (0x42fd43) o 0x1b2 (0x42fd04) en 14; si no, AL en 0
    // (0x42fd82).
    if (map == 0x23 && actor.byte381 == FourteenthStep)
    {
        if (const KindCase* found = CaseOf(FortressCases, actor.type))
        {
            if (!HasFreeModelEffect(context))
                return Helped::Exit;
            return CutAt(found->address == 0x42fd04 ? 0x42fd37 : 0x42fd76,
                         found->address == 0x42fd04 ? "el efecto 0xe1/1 con la key de +0x8a (CL11)"
                                                    : "el efecto 0xf8/1 con la key de +0x8a (CL11)",
                         actor, slot);
        }
    }
    // 0x4324c0 (0x4f5c6e): en el 34 (0x4303c0).
    if (map == 0x22)
    {
        if (const KindCase* found = CaseOf(CrywolfCases, actor.type))
            return StepCrywolf(actor, *found, slot, context);
    }
    return Helped::Continue;
}

// Dos rand() & 0x3ff antes del 0xc2: con un slot de modelo libre conserva el corte; pool lleno omite ese intento.
Core::Expected<void, std::string> ScatterAndCut(std::uint32_t address, const ActorState& actor, std::size_t slot,
                                                const CharacterStepContext& context)
{
    static_cast<void>(context.random.Next());
    static_cast<void>(context.random.Next());
    if (!HasFreeModelEffect(context))
        return {};
    return CutAt(address, "el efecto 0xc2 alrededor del que pega (CL11)", actor, slot);
}

// 0x51fff0: ocho estallidos alrededor del monstruo. Con un modelo disponible, la primera piedra
// posterior al selector sigue sin portar; el pool lleno deja que corran los ocho grupos.
Core::Expected<void, std::string> ScatterTheBurst(const ActorState& actor, std::size_t slot,
                                                  const CharacterStepContext& context)
{
    constexpr int Groups = 8;
    constexpr int YawStep = 45;
    constexpr int SelectorSpan = 2;
    constexpr std::uint32_t StoneCut = 0x520119;
    constexpr glm::vec3 Offset{0.0f, -220.0f, 0.0f};
    for (int group = 0; group < Groups; ++group)
    {
        const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(group * YawStep)};
        const glm::vec3 rotated =
            Render::Models::OriginalVectorRotate(Offset, Render::Models::OriginalAngleMatrix(angles));
        glm::vec3 point{};
        for (int axis = 0; axis < 3; ++axis)
            point[axis] =
                static_cast<float>(static_cast<double>(rotated[axis]) + static_cast<double>(actor.position[axis]));
        if (auto burst = CreateCombatBurstAt(point, context); !burst.HasValue())
            return Core::Unexpected(burst.Error());
        static_cast<void>(context.random.Next() % SelectorSpan);
        const auto& effects = context.world.Map().effects.Effects();
        const auto freeEffect =
            std::ranges::find_if(effects, [](const World::ModelEffect& effect) { return !effect.alive; });
        if (freeEffect != effects.end())
        {
            return CutAt(StoneCut, "el efecto de piedra del grupo disperso", actor, slot);
        }
        // El segundo tipo de piedra también sólo consume el selector cuando el pool de modelos está lleno.
        static_cast<void>(context.random.Next() % SelectorSpan);
    }
    return {};
}

// 0x4f715a..0x4f721e (+0x376 0x2d): cuatro veces, tres rand() & 31 − 16 (el offset, con el FILD exacto), 0x4ec110
// con el hueso 2 (+0x60) de los huesos propios y 0x53fe70(0x607 y después 0x5e4, punto, o + 0x1c, (1, 1, 1) de
// 0x4f5bbb, 0, 1,0f, 0). Con el pool lleno las partículas no sacan sus rand() y las del offset salen igual.
Core::Expected<void, std::string> DripTheBlood(const ActorState& actor, std::size_t slot,
                                               const CharacterStepContext& context)
{
    constexpr int Drips = 4;
    constexpr std::size_t DripBone = 2;
    constexpr int Spread = 31;
    constexpr int Center = 16;
    const auto offset = [&context]
    { return static_cast<float>(static_cast<std::int32_t>(context.random.Next() & Spread) - Center); };
    for (int drip = 0; drip < Drips; ++drip)
    {
        const float x = offset();
        const float y = offset();
        const float z = offset();
        const glm::vec3 point = BonePoint(actor, DripBone, glm::vec3(x, y, z), context);
        for (const auto& [type, address] :
             {std::pair{World::DropParticle, 0x4f71ed}, std::pair{World::BloodParticle, 0x4f7209}})
        {
            if (context.particles == nullptr)
            {
                return Core::Unexpected(std::format("{:#x}: falta el pool de partículas de la gota {:#x} del lugar {}",
                                                    address, type, slot));
            }
            const auto created = context.particles->CreateChecked(type, point, actor.angles, glm::vec3(1.0f), 0, 1.0f,
                                                                  context.worldClock, context.random, 0);
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("{:#x}: la gota {:#x}/0 del lugar {} falla con error {}", address,
                                                    type, slot, static_cast<unsigned>(created.Error())));
            }
        }
    }
    return {};
}

// 0x4f5c7e..0x4f80dd: el switch por +0x376. Todos siguen a la cola (0x4f80e0) salvo los que cortan.
Core::Expected<void, std::string> StepTheKindCase(ActorState& actor, const KindCase& found, std::size_t slot,
                                                  const CharacterStepContext& context, glm::vec3& localLight)
{
    const std::uint8_t step = actor.byte381;
    const std::uint8_t action = actor.animation.action;
    switch (found.address)
    {
    case 0x4f5cb6:
        // En 5 y con +0x3a4 distinto de 0xffff, veinte 0x625; el primer slot libre conserva el corte.
        if (step == FifthStep && actor.attackTarget != 0xffff && HasFreeModelEffect(context))
            return CutAt(0x4f5d13, "el primero de los veinte efectos 0x625 sobre el blanco (CL11)", actor, slot);
        return {};
    case 0x4f5d23:
    {
        constexpr std::uint16_t RockKind = 0x3f;
        if (actor.word376 != RockKind)
        {
            // 0x4f6121 (el 0x3d): en 1, 0xf6 sobre c; pool lleno, sigue a la cola.
            if (step == FirstStep && HasFreeModelEffect(context))
                return CutAt(0x4f6145, "el efecto 0xf6 (CL11)", actor, slot);
            return {};
        }
        // El 0x3f: en 1, 0x51fff0(c + 0x10, 0) y 0xf6; +0x38e; con '2', dos rand() % 800 y 0x518cf0(0xf5).
        if (step == FirstStep)
        {
            if (auto burst = ScatterTheBurst(actor, slot, context); !burst.HasValue())
                return burst;
        }
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        static_cast<void>(context.random.Next());
        static_cast<void>(context.random.Next());
        if (!HasFreeModelEffect(context))
            return {};
        return CutAt(0x4f609d, "el efecto 0xf5 (CL11)", actor, slot);
    }
    case 0x4f6152:
        // +0x38e; con '2' y en 1, 0xf6/1; pool lleno, sigue a la cola.
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FirstStep &&
            HasFreeModelEffect(context))
            return CutAt(0x4f6422, "el efecto 0xf6/1 (CL11)", actor, slot);
        return {};
    case 0x4f6442:
    {
        // En 1, 0x51fff0(c + 0x10, 0); en 14 y con el 0x3b, +0x38e y con '2' dieciocho 0xc1a.
        constexpr std::uint16_t StoneKind = 0x3b;
        if (step == FirstStep)
            return ScatterTheBurst(actor, slot, context);
        if (step == FourteenthStep && actor.word376 == StoneKind && ReadTheSkill(actor, context.random) == CaseSkill &&
            HasFreeModelEffect(context))
            return CutAt(0x4f6780, "el primero de los dieciocho efectos 0xc1a (CL11)", actor, slot);
        return {};
    }
    case 0x4f67ad:
    {
        // +0x381 % 5 en 1: 0x5e9 del hueso 63; luego +0x38e y, con '2' en 1, 0x5e8. Cada slot se intenta aparte.
        constexpr std::uint8_t Period = 5;
        if (step % Period == FirstStep && HasFreeModelEffect(context))
            return CutAt(0x4f67fb, "el efecto 0x5e9 del hueso 63 (CL11)", actor, slot);
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FirstStep)
        {
            localLight = Kind31TailLight;
            if (HasFreeModelEffect(context))
                return CutAt(0x4f6b51, "el efecto 0x5e8 (CL11)", actor, slot);
        }
        return {};
    }
    case 0x4f6b78:
        // +0x38e; con '2': en 1, tres 0xc2/2 y sonido 0x42; después, dos rand() y 0xc2.
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        if (actor.byte381 == FirstStep)
        {
            if (!HasFreeModelEffect(context))
            {
                RequestTheSound(context, HitSound);
                static_cast<void>(context.random.Next());
                static_cast<void>(context.random.Next());
                RequestTheSound(context, HitSound);
                return {};
            }
            return CutAt(0x4f6ec5, "el primero de los tres efectos 0xc2/2 del hueso 11 (CL11)", actor, slot);
        }
        if (auto effect = ScatterAndCut(0x4f6fc5, actor, slot, context); !effect.HasValue())
            return effect;
        RequestTheSound(context, HitSound);
        return {};
    case 0x4f6fea:
        // 0x517630; con '2' y en 1, dieciocho 0xc2/1 y sonido 0x42; pool lleno, conserva el sonido.
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FirstStep &&
            HasFreeModelEffect(context))
            return CutAt(0x4f7050, "el primero de los dieciocho efectos 0xc2/1 (CL11)", actor, slot);
        if (actor.byte38e == CaseSkill && actor.byte381 == FirstStep)
            RequestTheSound(context, HitSound);
        return {};
    case 0x4f7074:
        // 0x517630; con '2': en 1, 0xcb, 0xcc y 0x77; después, dos rand() y 0xc2.
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        if (actor.byte381 == FirstStep)
        {
            if (!HasFreeModelEffect(context))
            {
                RequestTheSound(context, AngryRoarSound);
                static_cast<void>(context.random.Next());
                static_cast<void>(context.random.Next());
                RequestTheSound(context, HitSound);
                return {};
            }
            return CutAt(0x4f70b8, "los efectos 0xcb y 0xcc (CL11)", actor, slot);
        }
        if (auto effect = ScatterAndCut(0x4f80cf, actor, slot, context); !effect.HasValue())
            return effect;
        RequestTheSound(context, HitSound);
        return {};
    case 0x4f715a:
        return DripTheBlood(actor, slot, context);
    case 0x4f7223:
        // +0x38e; con '2' y en 14, treinta y seis joints 0x613/1, cada intento precedido por tres rand() % 360.
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FourteenthStep)
        {
            constexpr int JointCount = 36;
            for (int joint = 0; joint < JointCount; ++joint)
            {
                for (int angle = 0; angle < 3; ++angle)
                    static_cast<void>(context.random.Next());
                if (!IsJointPoolFull(context))
                    return CutAt(0x4f756a, "el primero de los treinta y seis joints 0x613/1 (CL11)", actor, slot);
            }
        }
        return {};
    case 0x4f75b5:
    {
        // +0x38e; con '2', rand() & 1: con 0 y en 1, 0xf6/1; con 1, dos rand() y 0xc2.
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        if ((context.random.Next() & 1) != 0)
        {
            if (auto effect = ScatterAndCut(0x4f7930, actor, slot, context); !effect.HasValue())
                return effect;
            RequestTheSound(context, HitSound);
        }
        else if (actor.byte381 == FirstStep && HasFreeModelEffect(context))
            return CutAt(0x4f789b, "el efecto 0xf6/1 (CL11)", actor, slot);
        return {};
    }
    case 0x4f793e:
        // En la 3 o la 4 y en 5, 0x51fff0(c + 0x10, 0) y 0xf6.
        if ((action == FirstAttack || action == SecondAttack) && step == FifthStep)
            return ScatterTheBurst(actor, slot, context);
        return {};
    case 0x4f798d:
    {
        constexpr std::uint8_t RockStep = 11;
        constexpr std::size_t TrailBone = 11;
        // En la 3 y en 11, 0x51fff0(c + 0x10, 0), 0xf6 y cinco 0x106/1 con dos rand() % 1001 cada uno.
        if (action == FirstAttack)
        {
            if (step == RockStep)
            {
                localLight = TrailAttackLight;
                if (auto burst = ScatterTheBurst(actor, slot, context); !burst.HasValue())
                    return burst;
                if (!HasFreeModelEffect(context))
                    SetTheFifthTrailPoint(actor, context.random);
            }
            return {};
        }
        // 0x4f7ab9..0x4f7b56: en 13, el punto (−50, 100, 0) del hueso 11 va a +0x17c..+0x184 y 0x518cf0(0x106/1,
        // dueño c, la luz (1, 0,5, 0)); en 9, el mismo 0x4ec110 con (0, 0, 0), que no lee nadie.
        if (step == ThirteenthStep)
        {
            constexpr glm::vec3 TrailOffset{-50.0f, 100.0f, 0.0f};
            localLight = TrailAttackLight;
            actor.trailOrigin = BonePoint(actor, TrailBone, TrailOffset, context);
            if (HasFreeModelEffect(context))
                return CutAt(0x4f80cf, "el efecto 0x106/1 desde +0x17c (CL11)", actor, slot);
            RequestTheSound(context, HitSound);
        }
        else if (step == NinthStep)
        {
            localLight = TrailAttackLight;
        }
        return {};
    }
    case 0x4f7bc8:
    {
        // +0x38e; con '2' y en 2 o 6, cuarenta joints 0x613/3, cada intento precedido por tres rand() % 360.
        constexpr std::uint8_t SecondStep = 2;
        constexpr std::uint8_t SixthStep = 6;
        if (ReadTheSkill(actor, context.random) == CaseSkill &&
            (actor.byte381 == SecondStep || actor.byte381 == SixthStep))
        {
            constexpr int JointCount = 40;
            for (int joint = 0; joint < JointCount; ++joint)
            {
                for (int angle = 0; angle < 3; ++angle)
                    static_cast<void>(context.random.Next());
                if (!IsJointPoolFull(context))
                    return CutAt(0x4f7f3c, "el primero de los cuarenta joints 0x613/3 (CL11)", actor, slot);
            }
        }
        return {};
    }
    case 0x4f7f67:
        // En 1, 0x4ec110 con el hueso de la derecha (+0x2bd) y (60, −110, 0) y 0x518cf0(0xe2, dueño c); el 0x39, dos
        // 0xe2 más.
        if (step == FirstStep)
        {
            const float originalYaw = actor.angles.z;
            std::array<glm::vec3, 3> arrowAngles{actor.angles, actor.angles, actor.angles};
            const std::size_t arrowCount = actor.word376 == ThreeAquagoldArrowsKind ? 3 : 1;
            if (arrowCount == 3)
            {
                arrowAngles[1].z = static_cast<float>(static_cast<double>(originalYaw) + 20.0);
                arrowAngles[2].z = static_cast<float>(static_cast<double>(arrowAngles[1].z) - 40.0);
            }
            for (std::size_t arrow = 0; arrow < arrowCount; ++arrow)
            {
                if (auto created = CreateAquagoldArrow(actor, arrowAngles[arrow], context); !created.HasValue())
                    return created;
            }
            return {};
        }
        return {};
    case 0x4f804e:
        // 0x517630; con '2', dos rand() & 0x3ff, 0xc2 y 0x41ed10(0x42).
        if (ReadTheSkill(actor, context.random) == CaseSkill)
        {
            if (auto effect = ScatterAndCut(0x4f80cf, actor, slot, context); !effect.HasValue())
                return effect;
            RequestTheSound(context, HitSound);
        }
        return {};
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x4f5b70 sin rama", found.address));
    }
}

// 0x4f8147: con 0x11 en +0x38e, el switch por +0x376 − 0x25. Todos sus casos salen (RET o 0x4f903c).
Core::Expected<void, std::string> StepTheKindSkill(ActorState& actor, const KindCase& found, std::size_t slot,
                                                   const CharacterStepContext& context)
{
    const std::uint8_t step = actor.byte381;
    const std::uint8_t action = actor.animation.action;
    switch (found.address)
    {
    case 0x4f814e:
    {
        // Con el arco 0xa25 en la derecha (+0x2b8) y en 8, 0x4f816f: 0x53e2b0(c, c, c, 0, 0, 0), la flecha (sin skill
        // ni bandera: 0x53d9a0 con las manos de c), y sale.
        constexpr std::uint16_t MonsterBow = 0xa25;
        if (actor.hands[0].model == MonsterBow)
        {
            if (step == EighthStep)
                return FireTheArrow(actor, &actor, slot, 0, 0, 0, context);
            return {};
        }
        // En la 3 y en 15, 0x4f5b10 (el punto (−20, −90, 100) de c) y 0x518cf0(0x5e8, dueño c).
        if (action == FirstAttack)
        {
            if (step == FifteenthStep && HasFreeModelEffect(context))
                return CutAt(0x4f81cc, "el efecto 0x5e8 delante del que pega (CL11)", actor, slot);
            return {};
        }
        // En la 4 y en 8, rand() & 1: con 0 dos 0xf5 sobre el blanco (dueño c); con 1 dos 0xc2/6 sobre el blanco.
        if (action == SecondAttack && step == EighthStep)
        {
            if ((context.random.Next() & 1) == 0)
            {
                if (HasFreeModelEffect(context))
                    return CutAt(0x4f8227, "los dos efectos 0xf5 sobre el blanco (CL11)", actor, slot);
            }
            else if (HasFreeModelEffect(context))
            {
                return CutAt(0x4f8287, "los dos efectos 0xc2/6 sobre el blanco (CL11)", actor, slot);
            }
        }
        return {};
    }
    case 0x4f82b4:
        // En 14, 0x4ec110 con el hueso 33, 0x518cf0(0x106/1) y el joint 0x614/2 hacia el blanco.
        if (step == FourteenthStep)
        {
            if (HasFreeModelEffect(context))
                return CutAt(0x4f8326, "el efecto 0x106/1 del hueso 33 (CL11)", actor, slot);
            return AttemptJoint(actor, slot, context, 0x4f834c, "el joint 0x614/2 del hueso 33 (CL11)");
        }
        return {};
    case 0x4f835b:
        // En 13, 0x4ec110 con el hueso 6 y (60, 30, 0) y 0x518cf0(0xc2/5).
        if (step == ThirteenthStep && HasFreeModelEffect(context))
            return CutAt(0x4f83e5, "el efecto 0xc2/5 del hueso 6 (CL11)", actor, slot);
        return {};
    case 0x4f83f4:
        // En 14, 0x4ec110 con la matriz de 0x74374ec, 0x106/1 y el joint 0x614/2 hacia el blanco.
        if (step == FourteenthStep)
        {
            if (HasFreeModelEffect(context))
                return CutAt(0x4f845e, "el efecto 0x106/1 (CL11)", actor, slot);
            return AttemptJoint(actor, slot, context, 0x4f8484, "el joint 0x614/2 (CL11)");
        }
        return {};
    case 0x4f8493:
        // En la 4 y en 13, 0x4ec110 con el hueso 11 y (−50, 100, 0), 0x106/1 y el joint 0x614/2 hacia el blanco.
        if (action == SecondAttack && step == ThirteenthStep)
        {
            if (HasFreeModelEffect(context))
                return CutAt(0x4f852d, "el efecto 0x106/1 del hueso 11 (CL11)", actor, slot);
            return AttemptJoint(actor, slot, context, 0x4f8553, "el joint 0x614/2 del hueso 11 (CL11)");
        }
        return {};
    case 0x4f8562:
        // En 1, cuatro veces dos joints 0x60f/7 hacia el blanco.
        if (step == FirstStep)
        {
            constexpr int Attempts = 4;
            for (int attempt = 0; attempt < Attempts; ++attempt)
            {
                if (auto joint = AttemptJoint(actor, slot, context, 0x4f8592, "uno de los ocho joints 0x60f/7 (CL11)");
                    !joint.HasValue())
                    return joint;
            }
        }
        return {};
    case 0x4f85ca:
    {
        // Seis joints 0x614/2 con un rand() cada uno, y después cuatro partículas 5d9/0.
        constexpr int JointAttempts = 6;
        constexpr int ParticleAttempts = 4;
        for (int attempt = 0; attempt < JointAttempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint =
                    AttemptJoint(actor, slot, context, 0x4f8654, "uno de los doce joints 0x614/2 de las manos (CL11)");
                !joint.HasValue())
                return joint;
        }
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        for (int attempt = 0; attempt < ParticleAttempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint = AttemptJoint(actor, slot, context, 0x4f872d, "uno de los cuatro joints 0x61b/0 (CL11)");
                !joint.HasValue())
                return joint;
            const std::size_t bone = attempt < 2 ? actor.hands[0].bone : actor.hands[1].bone;
            if (auto particle = CreateTailFire(actor, slot, context, bone, 0x4f874f); !particle.HasValue())
                return particle;
        }
        return {};
    }
    case 0x4f876e:
    {
        // En 1, sonido 0x75; cuatro veces dos joints 0x5bc/1.
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        constexpr int Attempts = 4;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            if (auto joint =
                    AttemptJoint(actor, slot, context, 0x4f87f8, "uno de los ocho joints 0x5bc/1 de las manos (CL11)");
                !joint.HasValue())
                return joint;
        }
        return {};
    }
    case 0x4f8837:
    {
        // En 1, sonido 0x75; cuatro veces un rand(), joint 0x61b/0 y partícula 0x5d9/0.
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        constexpr int Attempts = 4;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint = AttemptJoint(actor, slot, context, 0x4f88d0,
                                          "uno de los cuatro joints 0x61b/0 de las manos (CL11)");
                !joint.HasValue())
                return joint;
            const std::size_t bone = attempt < 2 ? actor.hands[0].bone : actor.hands[1].bone;
            if (auto particle = CreateTailFire(actor, slot, context, bone, 0x4f88f2); !particle.HasValue())
                return particle;
        }
        return {};
    }
    case 0x4f890b:
    {
        // En 1, sonido 0x50; cuatro veces joint 0x61b/1 y partícula 0x5d9/0, sin rand() caller.
        if (step == FirstStep)
            RequestTheSound(context, HowlSound);
        constexpr int Attempts = 4;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            if (auto joint = AttemptJoint(actor, slot, context, 0x4f89df,
                                          "uno de los cuatro joints 0x61b/1 de las manos (CL11)");
                !joint.HasValue())
                return joint;
            const std::size_t bone = attempt % 2 == 0 ? actor.hands[0].bone : actor.hands[1].bone;
            if (auto particle = CreateTailFire(actor, slot, context, bone, 0x4f8a01); !particle.HasValue())
                return particle;
        }
        return {};
    }
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x4f8147 sin rama", found.address));
    }
}

// 0x4f8a56: con 3 en +0x38e, el switch por +0x376 − 0x22. Todos sus casos salen.
Core::Expected<void, std::string> StepTheBoneSkill(ActorState& actor, const KindCase& found, std::size_t slot,
                                                   const CharacterStepContext& context, const glm::vec3& localLight)
{
    const std::uint8_t step = actor.byte381;
    switch (found.address)
    {
    case 0x4f8a5d:
    {
        // En 1, sonido 0x50; cuatro joints 0x61b/1 y partículas 0x5d9/0, alternando manos y sin rand().
        if (step == FirstStep)
            RequestTheSound(context, HowlSound);
        constexpr int Attempts = 4;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            if (auto joint = AttemptJoint(actor, slot, context, 0x4f8b31,
                                          "uno de los cuatro joints 0x61b/1 de las manos (CL11)");
                !joint.HasValue())
                return joint;
            const std::size_t bone = attempt % 2 == 0 ? actor.hands[0].bone : actor.hands[1].bone;
            if (auto particle = CreateTailFire(actor, slot, context, bone, 0x4f8b53); !particle.HasValue())
                return particle;
        }
        return {};
    }
    case 0x4f8b78:
    {
        // Desde 8 (JC), cuatro joints 0x614/0 precedidos cada uno por rand() % 360.
        if (step < EighthStep)
            return {};
        constexpr int Attempts = 4;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint = AttemptJoint(actor, slot, context, 0x4f8bfb, "uno de los cuatro joints 0x614/0 (CL11)");
                !joint.HasValue())
                return joint;
        }
        return {};
    }
    case 0x4f8c0d:
    {
        // En 1, sonido 0x75; cuatro veces un rand(), joint 0x61b/0 y partícula 0x5d9/0.
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        constexpr int Attempts = 4;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint = AttemptJoint(actor, slot, context, 0x4f8ca6,
                                          "uno de los cuatro joints 0x61b/0 de las manos (CL11)");
                !joint.HasValue())
                return joint;
            const std::size_t bone = attempt < 2 ? actor.hands[0].bone : actor.hands[1].bone;
            if (auto particle = CreateTailFire(actor, slot, context, bone, 0x4f8cc8); !particle.HasValue())
                return particle;
        }
        return {};
    }
    case 0x4f8ce1:
    {
        // Cuatro veces un rand(), dos joints 0x614/0 y la partícula 0x5ca/0 con la luz local.
        constexpr int Attempts = 4;
        constexpr glm::vec3 NoOffset(0.0f);
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint =
                    AttemptJoint(actor, slot, context, 0x4f8d63, "uno de los ocho joints 0x614/0 de las manos (CL11)");
                !joint.HasValue())
                return joint;
            const std::size_t bone = attempt < 2 ? actor.hands[0].bone : actor.hands[1].bone;
            if (auto particle = CreateTailThunder(actor, slot, context, bone, NoOffset, localLight, 0x4f8dab);
                !particle.HasValue())
                return particle;
        }
        return {};
    }
    case 0x4f8dc4:
    {
        // Seis veces un rand() y dos joints 0x614/2.
        constexpr int Attempts = 6;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            static_cast<void>(context.random.Next());
            if (auto joint =
                    AttemptJoint(actor, slot, context, 0x4f8e46, "uno de los doce joints 0x614/2 de las manos (CL11)");
                !joint.HasValue())
                return joint;
        }
        return {};
    }
    case 0x4f8e85:
    {
        // Dos joints 0x614/0 y una partícula 0x5ca/0, sin rand().
        constexpr glm::vec3 Offset20A(0.0f);
        constexpr glm::vec3 OtherOffset(0.0f, -130.0f, 0.0f);
        const auto model = static_cast<std::uint16_t>(actor.type);
        const glm::vec3& offset = model == Data::Models::PlayerSkeletonModel ? Offset20A : OtherOffset;
        if (auto joint = AttemptJoint(actor, slot, context, 0x4f8f07, "los joints 0x614/0 del caso 0x27 (CL11)");
            !joint.HasValue())
            return joint;
        if (auto joint = AttemptJoint(actor, slot, context, 0x4f8f2a, "el segundo joint 0x614/0 del caso 0x27 (CL11)");
            !joint.HasValue())
            return joint;
        return CreateTailThunder(actor, slot, context, actor.hands[0].bone, offset, localLight, 0x4f8f4c);
    }
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x4f8a56 sin rama", found.address));
    }
}

// 0x4f80e0..0x4f903c: la cola, con el blanco de +0x3a4 (MOVSX) en [0, 400).
Core::Expected<void, std::string> StepTheTail(ActorState& actor, std::size_t slot, const CharacterStepContext& context,
                                              const glm::vec3& localLight)
{
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    if (target < 0 || target >= PoolSlots)
        return {};
    // 0x4f811b: con 0x11, el switch por +0x376 − 0x25 (0x4f8147); los que no tienen caso, y el 0xffff, salen.
    if (ReadTheSkill(actor, context.random) == KindSkill)
    {
        if (const KindCase* found = CaseOf(KindSkillCases, actor.word376))
            return StepTheKindSkill(actor, *found, slot, context);
        return {};
    }
    // 0x4f8a2c: con 3, el switch por +0x376 − 0x22 (0x4f8a56); sin caso, y el 0xffff, 0x4f8f5b (el hueso de +0x2bd con
    // 0x4ec110, dos joints 0x614 y la partícula 0x5ca).
    if (ReadTheSkill(actor, context.random) == BoneSkill)
    {
        if (const KindCase* found = CaseOf(BoneSkillCases, actor.word376))
            return StepTheBoneSkill(actor, *found, slot, context, localLight);
        std::size_t boneCount = 0;
        const auto model = static_cast<std::uint16_t>(actor.type);
        const Render::Models::Skeleton* skeleton = context.world.FaunaResources().SkeletonOf(model);
        if (skeleton == nullptr && context.models != nullptr)
            skeleton = context.models->SkeletonOf(model);
        if (skeleton != nullptr)
            boneCount = skeleton->bones.size();
        if (boneCount < actor.hands[0].bone)
            return {};
        constexpr glm::vec3 Offset20A(0.0f);
        constexpr glm::vec3 OtherOffset(0.0f, -130.0f, 0.0f);
        const glm::vec3& offset = model == Data::Models::PlayerSkeletonModel ? Offset20A : OtherOffset;
        if (auto joint = AttemptJoint(actor, slot, context, 0x4f8fef, "el primer joint del caso sin tabla (CL11)");
            !joint.HasValue())
            return joint;
        if (auto joint = AttemptJoint(actor, slot, context, 0x4f9012, "el segundo joint del caso sin tabla (CL11)");
            !joint.HasValue())
            return joint;
        return CreateTailThunder(actor, slot, context, actor.hands[0].bone, offset, localLight, 0x4f9034);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheAttackCases(ActorState& actor, std::size_t slot,
                                                     const CharacterStepContext& context)
{
    auto helped = StepTheMapHelpers(actor, slot, context);
    if (!helped.HasValue())
        return Core::Unexpected(helped.Error());
    if (helped.Value() == Helped::Exit)
        return {};
    glm::vec3 localLight = InitialTailLight;
    // 0x4f5c7e..0x4f75ae: los casos por +0x376.
    if (const KindCase* found = CaseOf(AttackKindCases, actor.word376))
    {
        if (auto cased = StepTheKindCase(actor, *found, slot, context, localLight); !cased.HasValue())
            return cased;
    }
    return StepTheTail(actor, slot, context, localLight);
}
} // namespace Client::Scenes
