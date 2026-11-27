#include "Scenes/MonsterAttackStep.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Math/Angles.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/BlastEffectCalls.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HighModelEffects.h"
#include "Scenes/RingEffects.h"
#include "Scenes/JointFamilies.h"
#include "Scenes/JointKernels.h"
#include "Scenes/KalimaMonsterEffects.h"
#include "Scenes/WizardEffects.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/WizardModelEffects.h"
#include "Assets/FaunaModels.h"

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
using Crt::CrtRandom;
using Models::LogicalAccess;

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
std::uint8_t ReadTheSkill(Kernel::ActorState& actor, CrtRandom& random)
{
    const LogicalAccess access(actor.byte38eAccesses, random, SkillCloseRandoms);
    return actor.byte38e;
}

// 0x41ed10(sonido, 0, 0): la CL10 decide cómo suena.
void RequestTheSound(const Actors::CharacterStepContext& context, std::int32_t sound)
{
    if (context.soundRequest)
        context.soundRequest(sound);
}

bool HasFreeModelEffect(const Actors::CharacterStepContext& context)
{
    const auto effects = context.world.Map().effects.Effects();
    return std::ranges::any_of(effects, [](const World::ModelEffect& effect) { return !effect.alive; });
}

bool IsJointPoolFull(const Actors::CharacterStepContext& context)
{
    return !context.joints.empty() &&
           std::ranges::none_of(context.joints, [](const JointState& joint) { return !joint.alive; });
}

std::optional<std::size_t> CreateKalimaEarthquake(Kernel::ActorState& actor, std::uint8_t state,
                                                  const Actors::CharacterStepContext& context)
{
    const auto created = context.world.Map().effects.CreateEarthquake(
        actor.position, actor.angles, actor.light, state,
        World::ModelEffectOwner{.type = &actor.type, .effect = nullptr, .angles = &actor.angles},
        context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random);
    if (created)
    {
        auto& quake = context.world.Map().effects.Effects()[*created];
        quake.ownerContext = &actor;
        quake.itemType = &actor.word8c;
        quake.itemLevel = &actor.byte8e;
    }
    return created;
}

// 0x518cf0 con un efecto del lote switch de la CL11 (Scenes/BlastEffectCalls): en `position`, con `angles` y `light`,
// el subtipo, `owner` en +0x108 (o nadie), +0x8a en −1, los bytes en 0 y la escala 0.
Core::Expected<void, std::string> CreateTheBlastEffect(std::int16_t type, std::int32_t subtype,
                                                       const glm::vec3& position, const glm::vec3& angles,
                                                       const glm::vec3& light, Kernel::ActorState* owner,
                                                       const Actors::CharacterStepContext& context)
{
    const World::ModelEffectArguments arguments{.type = type,
                                                .position = position,
                                                .angles = angles,
                                                .light = light,
                                                .subtype = subtype,
                                                .owner = owner,
                                                .key = 0xffff,
                                                .byte89 = 0,
                                                .byte88 = 0,
                                                .byte1bd = 0,
                                                .scale = 0.0f};
    const auto made = CreateBlastEffect(arguments, owner, context);
    if (!made.HasValue())
        return Core::Unexpected(made.Error());
    return {};
}

// 0x518cf0 sobre c, con sus ángulos y su luz (los argumentos de 0x43b21d..0x43b25e, 0x4f6129..0x4f6145, …).
Core::Expected<void, std::string> CreateOnTheActor(std::int16_t type, std::int32_t subtype, Kernel::ActorState& actor,
                                                   Kernel::ActorState* owner,
                                                   const Actors::CharacterStepContext& context)
{
    return CreateTheBlastEffect(type, subtype, actor.position, actor.angles, actor.light, owner, context);
}

// Un efecto, joint o partícula sin portar en un caso de 0x4f5b70 o de sus ayudantes, con lo que hizo antes y sus
// rand().
Core::Unexpected<std::string> CutAt(std::uint32_t address, std::string_view what, const Kernel::ActorState& actor,
                                    std::size_t slot)
{
    return Core::Unexpected(
        std::format("{:#x}: {} del lugar {} (tipo {:#x}, +0x376 en {:#x}, +0x381 en {}), sin portar", address, what,
                    slot, actor.type, actor.word376, actor.byte381));
}

// La luz (0,8f; 0,5; 0,1f) de los fuegos 0xc2/6, /7 y /8 (0x439c1f, 0x43b172 y 0x4f8272).
constexpr glm::vec3 MeteorLight{std::bit_cast<float>(0x3f4ccccdu), 0.5f, std::bit_cast<float>(0x3dcccccdu)};

// El alta de un efecto de los magos (Scenes::CreateWizardEffect). Con la tabla llena, nada y sin rand(); lo que el
// efecto pide y MuRet no tiene corta con la dirección de la llamada.
Core::Expected<void, std::string> CreateWizard(std::uint32_t address, const World::ModelEffectArguments& arguments,
                                               const Kernel::ActorState& actor, std::size_t slot,
                                               const Actors::CharacterStepContext& context)
{
    const auto created = CreateWizardEffect(context, arguments);
    if (!created.HasValue())
    {
        return Core::Unexpected(std::format("{:#x}: el efecto {:#x}/{} del lugar {} (tipo {:#x}, +0x376 en {:#x}): {}",
                                            address, arguments.type, arguments.subtype, slot, actor.type, actor.word376,
                                            created.Error()));
    }
    return {};
}

// 0x4ec110(modelo, huesos propios + hueso · 0x30, offset, punto, 1) con la escala y la posición que 0x4fd69b copió
// del monstruo al modelo antes del paso. Sin huesos propios (antes del primer dibujo), la matriz en cero (Q-C318),
// como OwnBonePoint de CharacterStep.cpp.
glm::vec3 BonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                    const Actors::CharacterStepContext& context)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    const Kernel::SharedModelPlacement placement = context.world.ModelPlacement(static_cast<std::uint16_t>(actor.type));
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
}

// 0x4ec110(modelo, matriz, offset, punto, 1) con una matriz que no es un hueso del actor: las del fénix que guardó el
// último dibujo (WorldRuntime::phoenixBones, 0x743748c..0x74374ec), con la escala y la posición del modelo del tipo.
glm::vec3 MatrixPoint(const Kernel::ActorState& actor, const Math::BoneTransform& matrix, const glm::vec3& offset,
                      const Actors::CharacterStepContext& context)
{
    const Kernel::SharedModelPlacement placement = context.world.ModelPlacement(static_cast<std::uint16_t>(actor.type));
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
}

// Lo que el alta de un joint le pide al paso: el hueso 33 del jugador, el sonido de la CL10 y el registro propio.
JointKernels::CreationServices JointServices(const Actors::CharacterStepContext& context)
{
    return {.playerBonePoint =
                [&context](const Kernel::ActorState& bones, std::size_t bone, const glm::vec3& offset)
            {
                const Math::BoneTransform matrix =
                    bone < bones.bodyPose.size() ? bones.bodyPose[bone] : Math::BoneTransform(0.0f);
                const Kernel::SharedModelPlacement placement =
                    context.world.ModelPlacement(Tables::PlayerSkeletonModel);
                return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale,
                                                       placement.origin);
            },
            .soundRequest = [&context](std::int32_t sound) { RequestTheSound(context, sound); },
            .ownRecordDepth = &context.world.Runtime().ownRecordDepth,
            .ownVolley = &context.world.OwnCharacter().bytef04};
}

// 0x4f74ee..0x4f7573 y 0x4f7ec0..0x4f7f45: un joint 0x613 sin dueño desde 100 más arriba del monstruo, con los
// ángulos (rand() % 360, rand() % 360, rand() % 360) y la key, la skill y el serial en 0. Con el pool lleno, los
// rand() de los ángulos y nada más.
Core::Expected<void, std::string> ThrowTheSpirit(const Kernel::ActorState& actor,
                                                 const Actors::CharacterStepContext& context, std::uint32_t address,
                                                 std::int32_t subtype, float width)
{
    using Math::FullTurn;
    glm::vec3 angles{};
    angles.x = static_cast<float>(context.random.Next() % FullTurn);
    angles.y = static_cast<float>(context.random.Next() % FullTurn);
    angles.z = static_cast<float>(context.random.Next() % FullTurn);
    glm::vec3 point{actor.position.x, actor.position.y,
                    static_cast<float>(static_cast<double>(actor.position.z) + 100.0)};
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", address));
    const JointKernels::CreationServices services = JointServices(context);
    const World::FishEffectRequest request{.type = JointFamilies::SpiritJoint,
                                           .firstPosition = point,
                                           .secondPosition = point,
                                           .angles = angles,
                                           .subtype = subtype,
                                           .ownerContext = nullptr,
                                           .scale = width,
                                           .key = 0,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    const auto created = CreateFaunaJoint(context.joints, request, context.random, JointOwnerKind::ByType, &services);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    return {};
}

// 0x530860(tipo, desde, hasta, ángulos, subtipo, dueño, ancho, key, 0, 0, −1) desde la cola de 0x4f5b70 (key −1) y
// desde 0x43b0ed (key 50): el dueño es el blanco (ESI). El 0x614/2 lee el hueso 33 del blanco con el modelo del jugador
// (0x5313fe: 0x5769ad8 + 0x20a · 0xe0). Con el pool lleno, nada y sin rand().
Core::Expected<void, std::string> CreateTheJoint(Kernel::ActorState& owner, const Actors::CharacterStepContext& context,
                                                 std::uint32_t address, std::int32_t type, std::int32_t subtype,
                                                 glm::vec3 from, glm::vec3 to, glm::vec3 angles, float width,
                                                 std::int32_t key = -1)
{
    if (context.joints.empty())
        return Core::Unexpected(std::format("{:#x}: faltan los joints del frame (0x530860)", address));
    const JointKernels::CreationServices services = JointServices(context);
    const World::FishEffectRequest request{.type = type,
                                           .firstPosition = from,
                                           .secondPosition = to,
                                           .angles = angles,
                                           .subtype = subtype,
                                           .ownerContext = &owner,
                                           .scale = width,
                                           .key = key,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
    const auto created = CreateFaunaJoint(context.joints, request, context.random, JointOwnerKind::Actor, &services);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    return {};
}

// rand() % 360 en la z de los ángulos (0x4f8620..0x4f8651 y sus copias): la x y la y en 0.
glm::vec3 RandomTurn(CrtRandom& random)
{
    using Math::FullTurn;
    return {0.0f, 0.0f, static_cast<float>(random.Next() % FullTurn)};
}

Core::Expected<void, std::string> CreateTailParticle(Kernel::ActorState& actor, std::size_t slot,
                                                     const Actors::CharacterStepContext& context, std::int32_t type,
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

// 0x4f8c0d, 0x4f8837 y 0x4f86a6..0x4f8761: cuatro veces, con la mano 0 en las dos primeras, el punto del hueso,
// rand() % 360, el joint 0x61b/0 de ancho 50 hacia el blanco y la partícula 0x5d9/0 con los ángulos y la luz del actor.
Core::Expected<void, std::string> ThrowTheHandBolts(Kernel::ActorState& actor, Kernel::ActorState& target,
                                                    std::size_t slot, const Actors::CharacterStepContext& context,
                                                    std::uint32_t jointAddress, std::uint32_t particleAddress)
{
    constexpr int Bolts = 4;
    constexpr int FirstHandBolts = 2;
    constexpr float Width = 50.0f; // 0x42480000
    for (int bolt = 0; bolt < Bolts; ++bolt)
    {
        const std::size_t bone = actor.hands[bolt < FirstHandBolts ? 0 : 1].bone;
        const glm::vec3 point = BonePoint(actor, bone, glm::vec3(0.0f), context);
        const glm::vec3 angles = RandomTurn(context.random);
        if (auto made = CreateTheJoint(target, context, jointAddress, 0x61b, 0, point, target.position, angles, Width);
            !made.HasValue())
            return made;
        if (auto particle =
                CreateTailParticle(actor, slot, context, TailFireParticle, point, actor.light, particleAddress);
            !particle.HasValue())
            return particle;
    }
    return {};
}

// 0x4f8a5d y 0x4f890b: el giro sale del reloj, (45 − (ftol(reloj) / 10 + +0x381 · 3) % 90) + 180 (0x6b8848 y
// 0x6b86bc) y suma 270 (0x6ba10c) por joint; cuatro veces, alternando manos, el joint 0x61b/1 de ancho 50 y la
// partícula 0x5d9/0, sin rand() propio.
Core::Expected<void, std::string> TurnTheHandBolts(Kernel::ActorState& actor, Kernel::ActorState& target,
                                                   std::size_t slot, const Actors::CharacterStepContext& context,
                                                   std::uint32_t clockAddress, std::uint32_t jointAddress,
                                                   std::uint32_t particleAddress)
{
    constexpr std::int32_t ClockDivisor = 10;
    constexpr std::int32_t StepTurn = 3;
    constexpr std::int32_t Swing = 90;
    constexpr double SwingCenter = 45.0;
    constexpr double HalfTurn = Math::HalfTurnAsDouble;
    constexpr double NextTurn = 270.0;
    constexpr int Bolts = 4;
    constexpr float Width = 50.0f;
    const auto clock = Math::OriginalFtolLow(context.worldClock);
    if (!clock)
        return Core::Unexpected(std::format("{:#x}: el reloj del giro excede ftol (0x69fd3c)", clockAddress));
    const std::int32_t phase = (*clock / ClockDivisor + static_cast<std::int32_t>(actor.byte381) * StepTurn) % Swing;
    auto angle = static_cast<float>((SwingCenter - static_cast<double>(phase)) + HalfTurn);
    for (int bolt = 0; bolt < Bolts; ++bolt)
    {
        const std::size_t bone = actor.hands[static_cast<std::size_t>(bolt % 2)].bone;
        const glm::vec3 point = BonePoint(actor, bone, glm::vec3(0.0f), context);
        const glm::vec3 angles{0.0f, 0.0f, angle};
        if (auto made = CreateTheJoint(target, context, jointAddress, 0x61b, 1, point, target.position, angles, Width);
            !made.HasValue())
            return made;
        if (auto particle =
                CreateTailParticle(actor, slot, context, TailFireParticle, point, actor.light, particleAddress);
            !particle.HasValue())
            return particle;
        angle = static_cast<float>(static_cast<double>(angle) + NextTurn);
    }
    return {};
}

// Los rayos de las manos: por cada uno, el punto del hueso (la mano 0 en la primera mitad), rand() % 360 y dos joints
// 0x614 hacia el blanco, de ancho 50 y 10; el 0x4f8ce1 suma la partícula 0x5ca/0 con la luz local.
struct HandLightning
{
    std::int32_t subtype = 0;
    int count = 0;
    std::uint32_t wideAddress = 0;
    std::uint32_t thinAddress = 0;
    std::uint32_t thunderAddress = 0; // 0: sin la partícula.
};

Core::Expected<void, std::string> ThrowTheHandLightning(Kernel::ActorState& actor, Kernel::ActorState& target,
                                                        std::size_t slot, const Actors::CharacterStepContext& context,
                                                        const HandLightning& cast, const glm::vec3& localLight)
{
    constexpr float WideWidth = 50.0f; // 0x42480000
    constexpr float ThinWidth = 10.0f; // 0x41200000
    for (int bolt = 0; bolt < cast.count; ++bolt)
    {
        const std::size_t bone = actor.hands[bolt < cast.count / 2 ? 0 : 1].bone;
        const glm::vec3 point = BonePoint(actor, bone, glm::vec3(0.0f), context);
        const glm::vec3 angles = RandomTurn(context.random);
        if (auto made = CreateTheJoint(target, context, cast.wideAddress, 0x614, cast.subtype, point, target.position,
                                       angles, WideWidth);
            !made.HasValue())
            return made;
        if (auto made = CreateTheJoint(target, context, cast.thinAddress, 0x614, cast.subtype, point, target.position,
                                       angles, ThinWidth);
            !made.HasValue())
            return made;
        if (cast.thunderAddress == 0)
            continue;
        if (auto particle =
                CreateTailParticle(actor, slot, context, TailThunderParticle, point, localLight, cast.thunderAddress);
            !particle.HasValue())
            return particle;
    }
    return {};
}

// 0x4f8e85 y 0x4f8f5b: el punto de la mano 0 (con (0, −130, 0) salvo el modelo 0x20a), los ángulos (−60, 0, z del
// actor), dos joints 0x614/0 de ancho 50 y 10 y la partícula 0x5ca/0 con la luz local, sin rand() propio.
Core::Expected<void, std::string> CastTheHandLightning(Kernel::ActorState& actor, Kernel::ActorState& target,
                                                       std::size_t slot, const Actors::CharacterStepContext& context,
                                                       const glm::vec3& localLight, std::uint32_t wideAddress)
{
    constexpr glm::vec3 Offset20A(0.0f);
    constexpr glm::vec3 OtherOffset(0.0f, -130.0f, 0.0f); // 0xc3020000
    constexpr float Tilt = -60.0f;                        // 0xc2700000
    constexpr float WideWidth = 50.0f;
    constexpr float ThinWidth = 10.0f;
    constexpr std::uint32_t ThinOffset = 0x23;    // 0x4f8f07 → 0x4f8f2a, 0x4f8fef → 0x4f9012
    constexpr std::uint32_t ThunderOffset = 0x45; // → 0x4f8f4c, → 0x4f9034
    const auto model = static_cast<std::uint16_t>(actor.type);
    const glm::vec3& offset = model == Tables::PlayerSkeletonModel ? Offset20A : OtherOffset;
    const glm::vec3 point = BonePoint(actor, actor.hands[0].bone, offset, context);
    const glm::vec3 angles{Tilt, 0.0f, actor.angles.z};
    if (auto made = CreateTheJoint(target, context, wideAddress, 0x614, 0, point, target.position, angles, WideWidth);
        !made.HasValue())
        return made;
    if (auto made = CreateTheJoint(target, context, wideAddress + ThinOffset, 0x614, 0, point, target.position, angles,
                                   ThinWidth);
        !made.HasValue())
        return made;
    return CreateTailParticle(actor, slot, context, TailThunderParticle, point, localLight,
                              wideAddress + ThunderOffset);
}

// 0x4f79db..0x4f7aae: cinco veces, un punto desde la posición de c con dos rand() % 1001 − 500 y 500 más arriba, que
// queda también en +0x17c..+0x184, y 0x518cf0(0x106/1) ahí, con los ángulos de c y 45 más en x (0x6b8848), la luz
// (1; 0,5; 0) y c de dueño.
Core::Expected<void, std::string> LaunchTheFiveStabs(Kernel::ActorState& actor,
                                                     const Actors::CharacterStepContext& context)
{
    constexpr int Iterations = 5;
    constexpr int OffsetSpan = 1001;
    constexpr int OffsetCenter = 500;
    constexpr double Rise = 500.0, Tilt = 45.0; // 0x6b8a98, 0x6b8848
    constexpr std::int16_t StabEffect = 0x106;
    for (int iteration = 0; iteration < Iterations; ++iteration)
    {
        const glm::vec3 angles{static_cast<float>(static_cast<double>(actor.angles.x) + Tilt), actor.angles.y,
                               actor.angles.z};
        const auto dx = static_cast<std::int32_t>(context.random.Next() % OffsetSpan) - OffsetCenter;
        const auto dy = static_cast<std::int32_t>(context.random.Next() % OffsetSpan) - OffsetCenter;
        actor.trailOrigin = {
            static_cast<float>(static_cast<double>(dx) + static_cast<double>(actor.position.x)),
            static_cast<float>(static_cast<double>(dy) + static_cast<double>(actor.position.y)),
            static_cast<float>(static_cast<double>(actor.position.z) + Rise),
        };
        if (auto made =
                CreateTheBlastEffect(StabEffect, 1, actor.trailOrigin, angles, TrailAttackLight, &actor, context);
            !made.HasValue())
            return made;
    }
    return {};
}

// 0x439bd0(c, +0x381, 13), la habilidad 1 de Kalima: rand() % 360 en la z de un giro (fild, guardado en float); cinco
// veces, la z 72 más (fadd y fstp), el punto (0, 300, 0) girado por él (0x5fc8f0 y 0x5fcbd0) más la posición de c,
// cada eje en la x87, y 0x518cf0(0xc2, punto, c+0x1c, (0,8f; 0,5; 0,1f), 8, sin dueño, 0, …); después el sonido 0x1e0.
Core::Expected<void, std::string> ThrowTheFireRing(const Kernel::ActorState& actor, std::size_t slot,
                                                   const Actors::CharacterStepContext& context)
{
    constexpr int Fires = 5;
    constexpr double FireTurn = 72.0; // 0x6b8ba0
    constexpr glm::vec3 FireReach{0.0f, 300.0f, 0.0f};
    constexpr std::int32_t FireSubtype = 8;
    constexpr std::uint16_t FireKey = 0;
    constexpr std::int32_t BurstSound = 0x1e0;
    constexpr int FullTurn = Math::FullTurn;
    glm::vec3 turn{0.0f, 0.0f, static_cast<float>(context.random.Next() % FullTurn)};
    for (int fire = 0; fire < Fires; ++fire)
    {
        turn.z = static_cast<float>(static_cast<double>(turn.z) + FireTurn);
        const glm::vec3 reach = Math::OriginalVectorRotate(FireReach, Math::OriginalAngleMatrix(turn));
        glm::vec3 point{};
        for (int axis = 0; axis < 3; ++axis)
            point[axis] =
                static_cast<float>(static_cast<double>(reach[axis]) + static_cast<double>(actor.position[axis]));
        if (auto created = CreateWizard(
                0x439ca6,
                WizardArguments(World::FireEffect, point, actor.angles, MeteorLight, FireSubtype, nullptr, FireKey),
                actor, slot, context);
            !created.HasValue())
            return created;
    }
    RequestTheSound(context, BurstSound);
    return {};
}

// 0x439b20(c, +0x381, 13, 200,0f), la habilidad 0x37 de Kalima: el punto de c con la z 200 más (fadd del float y su
// fstp); tres veces 0x518cf0(0x11b, punto, (0, 0, i · 120), luz 0, 1, c, −1, 0, 0, 0, 0) (0x439ba5), sin rand() antes
// (los del alta, sus joints 0x618/0xa y /0xb), y después el sonido 0x1df.
Core::Expected<void, std::string> RaiseTheRings(Kernel::ActorState& actor, std::size_t slot,
                                                const Actors::CharacterStepContext& context)
{
    constexpr float Hover = 200.0f;    // el cuarto argumento de 0x439b20
    constexpr float RingTurn = 120.0f; // 0x6b86c8
    constexpr int Rings = 3;
    constexpr std::int32_t RingSubtype = 1;
    constexpr std::int32_t RingSound = 0x1df;
    const glm::vec3 point{actor.position.x, actor.position.y,
                          static_cast<float>(static_cast<double>(actor.position.z) + static_cast<double>(Hover))};
    for (int ring = 0; ring < Rings; ++ring)
    {
        const glm::vec3 angles{0.0f, 0.0f,
                               static_cast<float>(static_cast<double>(ring) * static_cast<double>(RingTurn))};
        if (auto created = CreateHighModelEffect({.type = RingEffect,
                                                  .position = point,
                                                  .angles = angles,
                                                  .light = glm::vec3(0.0f),
                                                  .subtype = RingSubtype,
                                                  .owner = &actor},
                                                 HighContextOf(context));
            !created.HasValue())
            return Core::Unexpected(std::format("{} (0x439ba5, el lugar {})", created.Error(), slot));
    }
    RequestTheSound(context, RingSound);
    return {};
}

// 0x43aa46..0x43ab01 y sus copias de 0x43b47c y 0x43b7e7: el switch por la habilidad leída (las tablas de bytes de
// 0x43b9a0, 0x43ba84 y 0x43bb68; los demás no hacen nada), todos con +0x381 desde 13 (JL o JC, el byte sin signo).
Core::Expected<void, std::string> StepTheKalimaSkill(Kernel::ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                     const Actors::CharacterStepContext& context)
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
        return ThrowTheFireRing(actor, slot, context);
    case RingSkill:
        return RaiseTheRings(actor, slot, context);
    case ShieldSkill:
    {
        // 0x439cd0(c, +0x381, 13): 0x518cf0(0xcb, c+0x10, c+0x1c, c+0xf4, 3, c, −1, …) y (0xcc, …, 4, sin dueño, −1,
        // …), sin sonido.
        if (auto inner = CreateWizard(
                0x439d09,
                WizardArguments(World::InnerCircleEffect, actor.position, actor.angles, actor.light, 3, &actor), actor,
                slot, context);
            !inner.HasValue())
            return inner;
        return CreateWizard(0x439d24,
                            WizardArguments(World::OuterCircleEffect, actor.position, actor.angles, actor.light, 4),
                            actor, slot, context);
    }
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
Core::Expected<Helped, std::string> StepKalima(Kernel::ActorState& actor, const KindCase& found, std::size_t slot,
                                               const Actors::CharacterStepContext& context)
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
            constexpr std::int32_t RingParticle = 0x600;
            constexpr std::int32_t RingBurstSubtype = 1;
            constexpr double RingHeight = 150.0; // 0x6b87ac
            const std::uint32_t address = found.address == 0x43a7fe ? 0x43a861 : 0x43abd9;
            if (context.particles == nullptr)
                return Core::Unexpected(std::format("{:#x}: falta el pool de partículas para 0x600/1", address));
            const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
            const auto actors = context.world.Actors();
            const Kernel::ActorState& ringed =
                target >= 0 && target < PoolSlots ? actors[static_cast<std::size_t>(target)] : context.world.Hero();
            const glm::vec3 position{ringed.position.x, ringed.position.y,
                                     static_cast<float>(static_cast<double>(ringed.position.z) + RingHeight)};
            const auto created =
                context.particles->CreateChecked(RingParticle, position, actor.angles, glm::vec3(1.0f),
                                                 RingBurstSubtype, 1.0f, context.worldClock, context.random);
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("{:#x}: la partícula 0x600/1 del lugar {} falla con error {}",
                                                    address, slot, static_cast<unsigned>(created.Error())));
            }
        }
        return Helped::Exit;
    case 0x43a872:
    case 0x43b279:
    case 0x43b5e4:
    {
        const std::uint8_t skill = ReadTheSkill(actor, context.random);
        if (auto stepped = StepTheKalimaSkill(actor, skill, slot, context); !stepped.HasValue())
            return Core::Unexpected(stepped.Error());
        // 0x43ab04..0x43ab6b: en la 4 y en 14 (+0x381 después del switch), el terremoto 0xfa y 0x518cf0(0x615, c +
        // 0x10, c + 0x1c, c + 0xf4, 0, c, −1, 0, 1, 0, 0), el que suelta rayos sobre c (Scenes::KalimaMonsterEffects);
        // con la tabla llena, sin él. Después, +0x381 en 15.
        if (found.address == 0x43a872 && action == SecondAttack && actor.byte381 == FourteenthStep)
        {
            static_cast<void>(CreateKalimaEarthquake(actor, 1, context));
            if (auto created = CreateHighModelEffect({.type = ThunderEffect,
                                                      .position = actor.position,
                                                      .angles = actor.angles,
                                                      .light = actor.light,
                                                      .owner = &actor,
                                                      .byte88 = 1},
                                                     HighContextOf(context));
                !created.HasValue())
                return Core::Unexpected(std::format("0x43ab5f: {}", created.Error()));
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
                // 0x43b5ac y 0x43b5cc, con c de dueño.
                if (auto made = CreateOnTheActor(0xf6, 0, actor, &actor, context); !made.HasValue())
                    return Core::Unexpected(made.Error());
                if (auto made = CreateOnTheActor(World::FlameJetEffect, 1, actor, &actor, context); !made.HasValue())
                    return Core::Unexpected(made.Error());
                actor.byte381 = FifteenthStep;
            }
        }
        return Helped::Exit;
    }
    case 0x43abea:
        // En la 4 y en 14, 0x518cf0(0x11a, c + 0x10, c + 0x1c, c + 0xf4, 0, 0, −1, 0, 0, 0, 0) (0x43ac07..0x43ac29):
        // el seamanFX sobre c (Scenes::KalimaMonsterEffects; su alta pisa la luz). Con la tabla llena, nada.
        if (action == SecondAttack && actor.byte381 == FourteenthStep)
        {
            if (auto created = CreateHighModelEffect(
                    {.type = SeamanEffect, .position = actor.position, .angles = actor.angles, .light = actor.light},
                    HighContextOf(context));
                !created.HasValue())
                return Core::Unexpected(std::format("0x43ac29: {}", created.Error()));
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
            // 0x43b172..0x43b1fd: tres veces, rand() % 200 − 100 sumado (fild y fadd) a la x y a la y del héroe
            // ([0x743a170]), con su z, y 0x518cf0(0xc2, punto, c+0x1c, (0,8f; 0,5; 0,1f), 7, sin dueño, 0, …).
            constexpr int Fires = 3;
            constexpr int Spread = 200;
            constexpr int Centre = 100;
            constexpr std::int32_t FireSubtype = 7;
            constexpr std::uint16_t FireKey = 0;
            for (int fire = 0; fire < Fires; ++fire)
            {
                const auto around = [&context](float axis)
                {
                    const int offset = context.random.Next() % Spread - Centre;
                    return static_cast<float>(static_cast<double>(offset) + static_cast<double>(axis));
                };
                const Kernel::ActorState& hero = context.world.Hero();
                glm::vec3 point{};
                point.x = around(hero.position.x);
                point.y = around(hero.position.y);
                point.z = hero.position.z;
                if (auto created = CreateWizard(0x43b1f4,
                                                WizardArguments(World::FireEffect, point, actor.angles, MeteorLight,
                                                                FireSubtype, nullptr, FireKey),
                                                actor, slot, context);
                    !created.HasValue())
                    return Core::Unexpected(created.Error());
            }
            return Helped::Exit;
        }
        // 0x43b045..0x43b156: con 0x11 y el blanco de +0x3a4 de c en [0, 400), el punto del hueso 60 de c (0x4ec110 con
        // el offset en 0) y tres joints 0x610/6 de ahí a la posición del blanco, con él de dueño, ancho 30 y key 50
        // (0x43b0ed, 0x43b11c y 0x43b14e). Los ángulos son los de c: la z, después la z − 30 y después ésa + 15 (cada
        // una guardada). Sin rand(); con el pool lleno, nada.
        const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
        if (skill == KindSkill && target >= 0 && target < PoolSlots)
        {
            constexpr std::size_t BoltBone = 60;
            constexpr std::int32_t BoltJoint = 0x610, BoltSubtype = 6, BoltKey = 0x32;
            constexpr float BoltWidth = 30.0f;                       // 0x41f00000
            constexpr std::array<double, 3> Turns{0.0, -30.0, 15.0}; // 0x6b8858 y 0x6b87bc
            constexpr std::array<std::uint32_t, 3> Sites{0x43b0ed, 0x43b11c, 0x43b14e};
            const glm::vec3 from = BonePoint(actor, BoltBone, glm::vec3(0.0f), context);
            Kernel::ActorState& victim = context.world.Actors()[static_cast<std::size_t>(target)];
            glm::vec3 angles = actor.angles;
            for (std::size_t bolt = 0; bolt < Sites.size(); ++bolt)
            {
                if (bolt > 0)
                    angles.z = static_cast<float>(static_cast<double>(angles.z) + Turns[bolt]);
                if (auto made = CreateTheJoint(victim, context, Sites[bolt], BoltJoint, BoltSubtype, from,
                                               victim.position, angles, BoltWidth, BoltKey);
                    !made.HasValue())
                    return Core::Unexpected(made.Error());
            }
        }
        return Helped::Exit;
    }
    case 0x43b208:
        // En la 3 y desde 13, 0xf6/5 y 0x5de/2 y +0x381 en 15.
        if (action == FirstAttack && actor.byte381 >= ThirteenthStep)
        {
            // 0x43b241 y 0x43b25e, con c de dueño.
            if (auto made = CreateOnTheActor(0xf6, 5, actor, &actor, context); !made.HasValue())
                return Core::Unexpected(made.Error());
            if (auto made = CreateOnTheActor(World::FlameJetEffect, 2, actor, &actor, context); !made.HasValue())
                return Core::Unexpected(made.Error());
            actor.byte381 = FifteenthStep;
        }
        return Helped::Exit;
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x43a7a0 sin rama", found.address));
    }
}

// Lo que las altas de los tipos altos (Scenes/HighModelEffects) leen del paso de los personajes.
// 0x42dcf0 en el 30: el 0x120 (0x42dd23) suelta el pulso 0x61c/0 en cada paso y el 0x68 (0x42dd56), en el 5, el
// lancero 0x19f/0 con la z 500 más arriba; los dos sin dueño y con la clave en −1. Sale con AL en 1 también con la
// tabla llena.
Core::Expected<Helped, std::string> StepTheSiege(const Kernel::ActorState& actor, const KindCase& found,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t PulseEffect = 0x61c, SpearmanEffect = 0x19f;
    const bool pulse = found.address == 0x42dd23;
    if (!pulse && actor.byte381 != FifthStep)
        return Helped::Exit;
    World::ModelEffectArguments arguments{.type = pulse ? PulseEffect : SpearmanEffect,
                                          .position = actor.position,
                                          .angles = actor.angles,
                                          .light = actor.light};
    if (!pulse)
    {
        // 0x42dd6e..0x42dd92: la z más 500 (0x6b8a98) en la x87 y su FSTP, antes del alta.
        constexpr double SpearmanLift = 500.0;
        arguments.position.z = static_cast<float>(static_cast<double>(arguments.position.z) + SpearmanLift);
    }
    if (auto created = CreateHighModelEffect(arguments, HighContextOf(context)); !created.HasValue())
        return Core::Unexpected(created.Error());
    return Helped::Exit;
}

// 0x429110 en el 33 y el 54: en 10, el 0x135 en la 3 y el 0x134 y el 0x130 en la 4 sueltan su efecto; todos salen.
Core::Expected<Helped, std::string> StepTheDoppelganger(const Kernel::ActorState& actor, const KindCase& found,
                                                        std::size_t slot, const Actors::CharacterStepContext& context)
{
    const std::uint8_t action = actor.animation.action;
    if (actor.byte381 != TenthStep)
        return Helped::Exit;
    switch (found.address)
    {
    case 0x42914b:
        // 0x429152..0x4291a6: cinco veces, en la 3, 0x518cf0(0xcf, c+0x10, c+0x1c, (1, 1, 1), 3 + vuelta, sin dueño,
        // −1, …): los cinco remolinos chicos (0xcf/3 a /7).
        if (action == FirstAttack)
        {
            constexpr int Twisters = 5;
            constexpr std::int32_t FirstTwister = 3;
            for (int twister = 0; twister < Twisters; ++twister)
            {
                if (auto created = CreateWizard(0x42919a,
                                                WizardArguments(World::StormEffect, actor.position, actor.angles,
                                                                glm::vec3(1.0f), FirstTwister + twister),
                                                actor, slot, context);
                    !created.HasValue())
                    return Core::Unexpected(created.Error());
            }
        }
        return Helped::Exit;
    default:
        // 0x4291ce..0x429202 (el 0x134) y 0x429226..0x42925a (el 0x130): el 0x141 o el 0x61c/1 con la luz (1, 1, 1),
        // sin dueño y con la clave en −1.
        if (action == SecondAttack)
        {
            constexpr std::int16_t FlashEffect = 0x141, PulseEffect = 0x61c;
            const bool flash = found.address == 0x4291b1;
            const World::ModelEffectArguments arguments{.type = flash ? FlashEffect : PulseEffect,
                                                        .position = actor.position,
                                                        .angles = actor.angles,
                                                        .light = glm::vec3(1.0f),
                                                        .subtype = flash ? 0 : 1};
            if (auto created = CreateHighModelEffect(arguments, HighContextOf(context)); !created.HasValue())
                return Core::Unexpected(std::format("{} (lugar {})", created.Error(), slot));
        }
        return Helped::Exit;
    }
}

Core::Expected<void, std::string> ScatterTheBurst(const Kernel::ActorState& actor, std::size_t slot,
                                                  const Actors::CharacterStepContext& context);

// 0x43257a y 0x42fd76 comparten los argumentos y salen incluso si el pool está lleno.
Core::Expected<Helped, std::string> LaunchTheCrywolfArrow(Kernel::ActorState& actor,
                                                          const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t CelestialArrow = 0xf8;
    constexpr std::int32_t CelestialSubtype = 1;
    const auto created = CreateTheArrowEffect({.type = CelestialArrow,
                                               .position = actor.position,
                                               .angles = actor.angles,
                                               .light = actor.light,
                                               .subtype = CelestialSubtype,
                                               .owner = &actor,
                                               .key = static_cast<std::uint16_t>(actor.word8a)},
                                              context.world, context.random, &actor.angles);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    return Helped::Exit;
}

// 0x4329a4..0x4329ea y 0x4f6749..0x4f678f: dieciocho 0xc1a/0 desde el actor, sin dueño y con la clave en −1; la z de
// los ángulos es la del actor más i · 20 (FILD, FMUL y FADD).
Core::Expected<void, std::string> LaunchTheRadialVolley(const Kernel::ActorState& actor,
                                                        const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t ShardEffect = 0xc1a;
    constexpr int Shards = 18;
    constexpr double DegreesPerShard = 20.0; // 0x6b86d0
    glm::vec3 angles = actor.angles;
    for (int shard = 0; shard < Shards; ++shard)
    {
        angles.z =
            static_cast<float>(static_cast<double>(shard) * DegreesPerShard + static_cast<double>(actor.angles.z));
        const World::ModelEffectArguments arguments{
            .type = ShardEffect, .position = actor.position, .angles = angles, .light = actor.light};
        if (auto created = CreateHighModelEffect(arguments, HighContextOf(context)); !created.HasValue())
            return Core::Unexpected(created.Error());
    }
    return {};
}

// 0x4324c0 en Crywolf (el 34), por el tipo. Salvo los efectos de los que pegan en 14 y 15, devuelve 0.
Core::Expected<Helped, std::string> StepCrywolf(Kernel::ActorState& actor, const KindCase& found, std::size_t slot,
                                                const Actors::CharacterStepContext& context)
{
    switch (found.address)
    {
    case 0x4324fb:
        if (actor.byte381 == FourteenthStep)
        {
            const auto created = CreateTheArrowEffect({.type = 0xe1,
                                                       .position = actor.position,
                                                       .angles = actor.angles,
                                                       .light = actor.light,
                                                       .subtype = 1,
                                                       .owner = &actor,
                                                       .key = static_cast<std::uint16_t>(actor.word8a)},
                                                      context.world, context.random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return Helped::Exit;
        }
        return Helped::Continue;
    case 0x432543:
        if (actor.byte381 == FourteenthStep)
            return LaunchTheCrywolfArrow(actor, context);
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
            if (auto volley = LaunchTheRadialVolley(actor, context); !volley.HasValue())
                return Core::Unexpected(volley.Error());
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
        // 0x432acc: el punto sólo sale del marco si 0x53fe70 encuentra lugar. Lleno, quedan los cuatro sorteos de yaw.
        if (context.particles != nullptr &&
            std::ranges::all_of(context.particles->All(),
                                [](const World::Particle& particle) { return particle.alive; }))
        {
            constexpr int SuppressedFlames = 4;
            for (int flame = 0; flame < SuppressedFlames; ++flame)
                static_cast<void>(context.random.Next());
            return Helped::Continue;
        }
        return CutAt(0x432acc,
                     "0x4ec110 con el offset sin escribir de [EBP − 0x30], antes de las 0x5d9/0 (CL9 con el offset que "
                     "mida la VM, Q-C1050)",
                     actor, slot);
    }
    default:
        if (actor.byte381 == FifteenthStep)
        {
            // 432b30..432b63: el intento sale con AL1, incluso si518cf0 encuentra el pool lleno.
            const auto created = CreateTheArrowEffect({.type = 0xed,
                                                       .position = actor.position,
                                                       .angles = actor.angles,
                                                       .light = actor.light,
                                                       .subtype = 1,
                                                       .owner = &actor,
                                                       .key = static_cast<std::uint16_t>(actor.word8a)},
                                                      context.world, context.random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return Helped::Exit;
        }
        return Helped::Continue;
    }
}

// 0x4f5ba3..0x4f5c78: los seis ayudantes, cada uno en sus mapas (excluyentes); el que devuelve 1 hace salir.
Core::Expected<Helped, std::string> StepTheMapHelpers(Kernel::ActorState& actor, std::size_t slot,
                                                      const Actors::CharacterStepContext& context)
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
            return StepTheSiege(actor, *found, context);
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
            if (found->address == 0x42fd04)
            {
                // 42fd04..42fd37: mismos once argumentos que el E1/1 de43252d; ningún CRT propio.
                const auto created = CreateTheArrowEffect({.type = 0xe1,
                                                           .position = actor.position,
                                                           .angles = actor.angles,
                                                           .light = actor.light,
                                                           .subtype = 1,
                                                           .owner = &actor,
                                                           .key = static_cast<std::uint16_t>(actor.word8a)},
                                                          context.world, context.random);
                if (!created.HasValue())
                    return Core::Unexpected(created.Error());
                return Helped::Exit;
            }
            return LaunchTheCrywolfArrow(actor, context);
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

// 0x4f6f53..0x4f6fc5 (y 0x4f70ee, 0x4f78be y 0x4f8063): dos rand() & 0x3ff (con signo, 0x800003ff), el punto
// (rand + x − 512, rand + y − 512, z) de c, cada eje con fild, fadd y fsub en la x87, y 0x518cf0(0xc2, punto, c+0x1c,
// c+0xf4, 0, sin dueño, −1, …). El sonido 0x42 de después lo pide el caso.
Core::Expected<void, std::string> ScatterTheFire(std::uint32_t address, const Kernel::ActorState& actor,
                                                 std::size_t slot, const Actors::CharacterStepContext& context)
{
    constexpr int Spread = 0x3ff;
    constexpr double Centre = 512.0; // 0x6ba16c
    const auto around = [&context](float axis)
    {
        const int offset = static_cast<int>(context.random.Next() & Spread);
        return static_cast<float>((static_cast<double>(offset) + static_cast<double>(axis)) - Centre);
    };
    glm::vec3 point{};
    point.x = around(actor.position.x);
    point.y = around(actor.position.y);
    point.z = actor.position.z;
    return CreateWizard(address, WizardArguments(World::FireEffect, point, actor.angles, actor.light, 0), actor, slot,
                        context);
}

// 0x4f6e51..0x4f6f40: el punto del hueso 11 (0x4ec110 con (0, 0, 0)) y tres 0x518cf0(0xc2, punto, ángulos, c+0xf4, 2,
// sin dueño, −1, …), con los ángulos de c corridos (x − 20, z − 30), (x − 30) y (x − 20, z + 30), cada uno con fld,
// fsub o fadd y fstp.
Core::Expected<void, std::string> SpreadTheFires(const Kernel::ActorState& actor, std::size_t slot,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr std::size_t FireBone = 11;
    constexpr std::int32_t FallingSubtype = 2;
    constexpr double Tilt = 20.0;  // 0x6b86d0
    constexpr double Steep = 30.0; // 0x6b8858
    const auto shifted = [](float axis, double by) { return static_cast<float>(static_cast<double>(axis) + by); };
    const glm::vec3 point = BonePoint(actor, FireBone, glm::vec3(0.0f), context);
    const glm::vec3& angles = actor.angles;
    const std::array<std::pair<std::uint32_t, glm::vec3>, 3> fires{
        std::pair{0x4f6ec5u, glm::vec3(shifted(angles.x, -Tilt), angles.y, shifted(angles.z, -Steep))},
        std::pair{0x4f6efeu, glm::vec3(shifted(angles.x, -Steep), angles.y, angles.z)},
        std::pair{0x4f6f40u, glm::vec3(shifted(angles.x, -Tilt), angles.y, shifted(angles.z, Steep))}};
    for (const auto& [address, turned] : fires)
    {
        if (auto created =
                CreateWizard(address, WizardArguments(World::FireEffect, point, turned, actor.light, FallingSubtype),
                             actor, slot, context);
            !created.HasValue())
            return created;
    }
    return {};
}

// 0x51fff0: ocho estallidos alrededor del monstruo, cada uno con dos intentos de piedra.
Core::Expected<void, std::string> ScatterTheBurst(const Kernel::ActorState& actor, std::size_t slot,
                                                  const Actors::CharacterStepContext& context)
{
    const auto burst = ScatterCombatBurstAt(actor.position, context);
    if (!burst.HasValue())
        return Core::Unexpected(std::format("{} (lugar {})", burst.Error(), slot));
    return {};
}

// 0x4f715a..0x4f721e (+0x376 0x2d): cuatro veces, tres rand() & 31 − 16 (el offset, con el FILD exacto), 0x4ec110
// con el hueso 2 (+0x60) de los huesos propios y 0x53fe70(0x607 y después 0x5e4, punto, o + 0x1c, (1, 1, 1) de
// 0x4f5bbb, 0, 1,0f, 0). Con el pool lleno las partículas no sacan sus rand() y las del offset salen igual.
Core::Expected<void, std::string> DripTheBlood(const Kernel::ActorState& actor, std::size_t slot,
                                               const Actors::CharacterStepContext& context)
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
Core::Expected<void, std::string> StepTheKindCase(Kernel::ActorState& actor, const KindCase& found, std::size_t slot,
                                                  const Actors::CharacterStepContext& context, glm::vec3& localLight)
{
    const std::uint8_t step = actor.byte381;
    const std::uint8_t action = actor.animation.action;
    switch (found.address)
    {
    case 0x4f5cb6:
    {
        // 0x4f5cb6..0x4f5d1e: en 5 y con +0x3a4 distinto de 0xffff, veinte 0x518cf0(0x625, la posición del personaje
        // +0x3a4 (MOVSX, sin mirar si vive), sus ángulos, la luz local, 0, 0, −1, 0, 0, 0, 0). El original no acota el
        // índice: fuera de [0, 400) lee fuera de la tabla, y MuRet corta.
        if (step != FifthStep || actor.attackTarget == 0xffff)
            return {};
        const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
        if (target < 0 || target >= PoolSlots)
            return Core::Unexpected(
                std::format("0x4f5cd4: el blanco {} del lugar {} está fuera de la tabla de personajes", target, slot));
        const Kernel::ActorState& victim = context.world.Actors()[static_cast<std::size_t>(target)];
        constexpr int FallingStars = 20;
        constexpr std::int16_t FallingStar = 0x625;
        for (int star = 0; star < FallingStars; ++star)
        {
            if (auto created = CreateHighModelEffect(
                    {.type = FallingStar, .position = victim.position, .angles = victim.angles, .light = localLight},
                    HighContextOf(context));
                !created.HasValue())
                return Core::Unexpected("0x4f5d13: " + created.Error());
        }
        return {};
    }
    case 0x4f5d23:
    {
        constexpr std::uint16_t RockKind = 0x3f;
        if (actor.word376 != RockKind)
        {
            // 0x4f6121 (el 0x3d): en 1, 0xf6/0 sobre c, sin dueño (0x4f6145).
            if (step == FirstStep)
                return CreateOnTheActor(0xf6, 0, actor, nullptr, context);
            return {};
        }
        // El 0x3f: en 1, 0x51fff0(c + 0x10, 0) y 0xf6; +0x38e; con '2', dos rand() % 800 y 0x518cf0(0xf5).
        if (step == FirstStep)
        {
            if (auto burst = ScatterTheBurst(actor, slot, context); !burst.HasValue())
                return burst;
            // 0x4f5d5a: el 0xf6/0 en c + 0x10, sin dueño.
            if (auto made = CreateOnTheActor(0xf6, 0, actor, nullptr, context); !made.HasValue())
                return made;
        }
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        // 0x4f6030..0x4f609d: el 0xf5/0 sin dueño en (r % 800 + x − 400; r % 800 + y − 400; z) (0x6b87b0), y en 14
        // las dieciocho 0xc1a (0x4f60a5).
        constexpr int Spread = 800;
        constexpr double Center = 400.0;
        const auto dx = static_cast<double>(context.random.Next() % Spread);
        const auto dy = static_cast<double>(context.random.Next() % Spread);
        const glm::vec3 point{static_cast<float>(dx + static_cast<double>(actor.position.x) - Center),
                              static_cast<float>(dy + static_cast<double>(actor.position.y) - Center),
                              actor.position.z};
        if (auto made = CreateTheBlastEffect(0xf5, 0, point, actor.angles, actor.light, nullptr, context);
            !made.HasValue())
            return made;
        if (actor.byte381 == FourteenthStep)
            return LaunchTheRadialVolley(actor, context);
        return {};
    }
    case 0x4f6152:
        // +0x38e; con '2' y en 1, 0xf6/1; pool lleno, sigue a la cola.
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FirstStep)
            return CreateOnTheActor(0xf6, 1, actor, nullptr, context); // 0x4f6422
        return {};
    case 0x4f6442:
    {
        // En 1, 0x51fff0(c + 0x10, 0); en 14 y con el 0x3b, +0x38e y con '2' dieciocho 0xc1a.
        constexpr std::uint16_t StoneKind = 0x3b;
        if (step == FirstStep)
            return ScatterTheBurst(actor, slot, context);
        if (step == FourteenthStep && actor.word376 == StoneKind && ReadTheSkill(actor, context.random) == CaseSkill)
            return LaunchTheRadialVolley(actor, context);
        return {};
    }
    case 0x4f67ad:
    {
        // 0x4f67ad..0x4f6800: con +0x381 % 5 en 1, el 0x5e9 en el punto (0, 0, 0) del hueso 63 (0x4ec110), con los
        // ángulos y la luz de c, sin dueño. 0x4f6803..0x4f6b5c: +0x38e y, con '2' y +0x381 en 1, la luz local en
        // (0,42; 0,84; 1,4) y nueve 0x5e8 desde 50 sobre c, el ángulo z de c 20 más y 40 más antes de cada uno.
        constexpr std::uint8_t Period = 5;
        constexpr std::size_t RayBone = 63;
        constexpr std::int16_t OrangeRay = 0x5e9, BlueRay = 0x5e8;
        if (step % Period == FirstStep)
        {
            if (auto made = CreateHighModelEffect({.type = OrangeRay,
                                                   .position = BonePoint(actor, RayBone, glm::vec3(0.0f), context),
                                                   .angles = actor.angles,
                                                   .light = actor.light},
                                                  HighContextOf(context));
                !made.HasValue())
                return Core::Unexpected(std::format("{} (lugar {})", made.Error(), slot));
        }
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FirstStep)
        {
            localLight = Kind31TailLight;
            constexpr int Rays = 9;
            constexpr double RayLift = 50.0, RayTurn = 20.0, RaySpread = 40.0;
            const glm::vec3 point{actor.position.x, actor.position.y,
                                  static_cast<float>(static_cast<double>(actor.position.z) + RayLift)};
            glm::vec3 angles = actor.angles;
            angles.z = static_cast<float>(static_cast<double>(angles.z) + RayTurn);
            for (int ray = 0; ray < Rays; ++ray)
            {
                angles.z = static_cast<float>(static_cast<double>(angles.z) + RaySpread);
                if (auto made = CreateHighModelEffect(
                        {.type = BlueRay, .position = point, .angles = angles, .light = localLight},
                        HighContextOf(context));
                    !made.HasValue())
                    return Core::Unexpected(std::format("{} (lugar {})", made.Error(), slot));
            }
        }
        return {};
    }
    case 0x4f6b78:
        // +0x38e; con '2': en 1, tres 0xc2/2 del hueso 11 y el sonido 0x42; después, en todos, dos rand() y el 0xc2.
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        if (actor.byte381 == FirstStep)
        {
            if (auto spread = SpreadTheFires(actor, slot, context); !spread.HasValue())
                return spread;
            RequestTheSound(context, HitSound);
        }
        if (auto effect = ScatterTheFire(0x4f6fc5, actor, slot, context); !effect.HasValue())
            return effect;
        RequestTheSound(context, HitSound);
        return {};
    case 0x4f6fea:
    {
        // 0x517630; con '2' y en 1 (0x4f7010..0x4f706f), dieciocho 0x518cf0(0xc2, c+0x10, (0, 0, vuelta · 20f),
        // c+0xf4, 1, c de dueño, −1, …) y el sonido 0x42.
        if (ReadTheSkill(actor, context.random) != CaseSkill || actor.byte381 != FirstStep)
            return {};
        constexpr int Fires = 18;
        constexpr double FireTurn = 20.0; // 0x6b86d0
        for (int fire = 0; fire < Fires; ++fire)
        {
            const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(static_cast<double>(fire) * FireTurn)};
            if (auto created = CreateWizard(
                    0x4f7050, WizardArguments(World::FireEffect, actor.position, angles, actor.light, 1, &actor), actor,
                    slot, context);
                !created.HasValue())
                return created;
        }
        RequestTheSound(context, HitSound);
        return {};
    }
    case 0x4f7074:
        // 0x517630; con '2': en 1 (0x4f7096..0x4f70e9), 0x518cf0(0xcb, c+0x10, c+0x1c, c+0xf4, 0, sin dueño, −1, …), el
        // 0xcc/0 igual y el sonido 0x77; después, en todos, dos rand() y el 0xc2.
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        if (actor.byte381 == FirstStep)
        {
            if (auto inner = CreateWizard(
                    0x4f70b8, WizardArguments(World::InnerCircleEffect, actor.position, actor.angles, actor.light, 0),
                    actor, slot, context);
                !inner.HasValue())
                return inner;
            if (auto outer = CreateWizard(
                    0x4f70d6, WizardArguments(World::OuterCircleEffect, actor.position, actor.angles, actor.light, 0),
                    actor, slot, context);
                !outer.HasValue())
                return outer;
            RequestTheSound(context, AngryRoarSound);
        }
        if (auto effect = ScatterTheFire(0x4f80cf, actor, slot, context); !effect.HasValue())
            return effect;
        RequestTheSound(context, HitSound);
        return {};
    case 0x4f715a:
        return DripTheBlood(actor, slot, context);
    case 0x4f7223:
        // +0x38e; con '2' y en 14, treinta y seis joints 0x613/1 de ancho 60 (0x4f756a), cada uno con su sonido 0x3e.
        if (ReadTheSkill(actor, context.random) == CaseSkill && actor.byte381 == FourteenthStep)
        {
            constexpr int JointCount = 36;
            constexpr float Width = 60.0f; // 0x42700000
            for (int joint = 0; joint < JointCount; ++joint)
                if (auto thrown = ThrowTheSpirit(actor, context, 0x4f756a, 1, Width); !thrown.HasValue())
                    return thrown;
        }
        return {};
    case 0x4f75b5:
    {
        // +0x38e; con '2', rand() & 1: con 0 y en 1, 0xf6/1; con 1, dos rand() y 0xc2.
        if (ReadTheSkill(actor, context.random) != CaseSkill)
            return {};
        if ((context.random.Next() & 1) != 0)
        {
            if (auto effect = ScatterTheFire(0x4f7930, actor, slot, context); !effect.HasValue())
                return effect;
            RequestTheSound(context, HitSound);
        }
        else if (actor.byte381 == FirstStep)
            return CreateOnTheActor(0xf6, 1, actor, nullptr, context); // 0x4f789b
        return {};
    }
    case 0x4f793e:
        // En la 3 o la 4 y en 5, 0x51fff0(c + 0x10, 0) y 0xf6.
        if ((action == FirstAttack || action == SecondAttack) && step == FifthStep)
        {
            if (auto burst = ScatterTheBurst(actor, slot, context); !burst.HasValue())
                return burst;
            return CreateOnTheActor(0xf6, 0, actor, nullptr, context); // 0x4f7980
        }
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
                if (auto made = CreateOnTheActor(0xf6, 0, actor, nullptr, context); !made.HasValue()) // 0x4f79d3
                    return made;
                return LaunchTheFiveStabs(actor, context);
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
            // 0x4f7b56 → 0x4f80cf: el 0x106/1 desde +0x17c, con 45 más en la x de los ángulos y c de dueño.
            const glm::vec3 angles{static_cast<float>(static_cast<double>(actor.angles.x) + 45.0), actor.angles.y,
                                   actor.angles.z};
            if (auto made =
                    CreateTheBlastEffect(0x106, 1, actor.trailOrigin, angles, TrailAttackLight, &actor, context);
                !made.HasValue())
                return made;
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
        // +0x38e; con '2' y en 2 o 6, cuarenta joints 0x613/3 de ancho 50 (0x4f7f3c).
        constexpr std::uint8_t SecondStep = 2;
        constexpr std::uint8_t SixthStep = 6;
        if (ReadTheSkill(actor, context.random) == CaseSkill &&
            (actor.byte381 == SecondStep || actor.byte381 == SixthStep))
        {
            constexpr int JointCount = 40;
            constexpr float Width = 50.0f; // 0x42480000
            for (int joint = 0; joint < JointCount; ++joint)
                if (auto thrown = ThrowTheSpirit(actor, context, 0x4f7f3c, 3, Width); !thrown.HasValue())
                    return thrown;
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
            if (auto effect = ScatterTheFire(0x4f80cf, actor, slot, context); !effect.HasValue())
                return effect;
            RequestTheSound(context, HitSound);
        }
        return {};
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x4f5b70 sin rama", found.address));
    }
}

// 0x4f8147: con 0x11 en +0x38e, el switch por +0x376 − 0x25. Todos sus casos salen (RET o 0x4f903c).
Core::Expected<void, std::string> StepTheKindSkill(Kernel::ActorState& actor, Kernel::ActorState& target,
                                                   const KindCase& found, std::size_t slot,
                                                   const Actors::CharacterStepContext& context,
                                                   const glm::vec3& localLight)
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
        // En la 3 y en 15, 0x4f5b10 (el punto (−20, −90, 100) de c) y 0x518cf0(0x5e8, punto, c+0x1c, la luz local, 0,
        // c, −1, 0, 0, 0, 0) (0x4f81cc).
        if (action == FirstAttack)
        {
            if (step == FifteenthStep)
            {
                constexpr glm::vec3 RayOffset{-20.0f, -90.0f, 100.0f};
                if (auto made = CreateHighModelEffect({.type = 0x5e8,
                                                       .position = PointAround(actor, RayOffset),
                                                       .angles = actor.angles,
                                                       .light = localLight,
                                                       .owner = &actor},
                                                      HighContextOf(context));
                    !made.HasValue())
                    return Core::Unexpected(std::format("{} (lugar {})", made.Error(), slot));
            }
            return {};
        }
        // En la 4 y en 8, rand() & 1: con 0 dos 0xf5 sobre el blanco (dueño c); con 1 (0x4f8253..0x4f82aa) dos
        // 0x518cf0(0xc2, blanco+0x10, c+0x1c, (0,8f; 0,5; 0,1f), 6, sin dueño, −1, …).
        if (action == SecondAttack && step == EighthStep)
        {
            if ((context.random.Next() & 1) == 0)
            {
                // 0x4f8227 y 0x4f8244: en la posición del blanco (+0x3a4, ya en [0, 400)), con c de dueño.
                Kernel::ActorState& blasted =
                    context.world.Actors()[static_cast<std::size_t>(std::bit_cast<std::int16_t>(actor.attackTarget))];
                for (int blast = 0; blast < 2; ++blast)
                {
                    if (auto made =
                            CreateTheBlastEffect(0xf5, 0, blasted.position, actor.angles, actor.light, &actor, context);
                        !made.HasValue())
                        return made;
                }
                return {};
            }
            constexpr std::int32_t MeteorSubtype = 6;
            const auto targetSlot = static_cast<std::size_t>(std::bit_cast<std::int16_t>(actor.attackTarget));
            const Kernel::ActorState& victim = context.world.Actors()[targetSlot];
            for (const std::uint32_t address : {0x4f8287u, 0x4f82a5u})
            {
                if (auto created = CreateWizard(
                        address,
                        WizardArguments(World::FireEffect, victim.position, actor.angles, MeteorLight, MeteorSubtype),
                        actor, slot, context);
                    !created.HasValue())
                    return created;
            }
        }
        return {};
    }
    case 0x4f82b4:
        // En 14, 0x4ec110 con el hueso 33 (0x630), 0x518cf0(0x106/1) y el joint 0x614/2 desde y hasta ese punto, con
        // los ángulos del actor.
        if (step == FourteenthStep)
        {
            // 0x4f82c1..0x4f8326: el punto del hueso 33 y el 0x106/1 sin dueño, con la luz local.
            constexpr std::size_t ChestBone = 33;
            const glm::vec3 point = BonePoint(actor, ChestBone, glm::vec3(0.0f), context);
            if (auto made = CreateTheBlastEffect(0x106, 1, point, actor.angles, localLight, nullptr, context);
                !made.HasValue())
                return made;
            return CreateTheJoint(target, context, 0x4f834c, 0x614, 2, point, point, actor.angles, 50.0f);
        }
        return {};
    case 0x4f835b:
    {
        // En 13 (0x4f8368..0x4f83e5), 0x4ec110 con el hueso 6 y (60, 30, 0) y 0x518cf0(0xc2, punto, c+0x1c, c+0xf4, 5,
        // sin dueño, −1, …).
        constexpr std::size_t FireBone = 6;
        constexpr glm::vec3 FireOffset{60.0f, 30.0f, 0.0f};
        constexpr std::int32_t BouncingSubtype = 5;
        if (step != ThirteenthStep)
            return {};
        return CreateWizard(0x4f83e5,
                            WizardArguments(World::FireEffect, BonePoint(actor, FireBone, FireOffset, context),
                                            actor.angles, actor.light, BouncingSubtype),
                            actor, slot, context);
    }
    case 0x4f83f4:
        // En 14, 0x4ec110 con la matriz de 0x74374ec (la cola del fénix), 0x106/1 y el joint 0x614/2 desde y hasta ese
        // punto, con los ángulos del actor.
        if (step == FourteenthStep)
        {
            // 0x4f8401..0x4f845e: el punto de la matriz de 0x74374ec (WorldRuntime::phoenixBones[2]) y el 0x106/1 sin
            // dueño, con la luz local.
            constexpr std::size_t TailMatrix = 2;
            const glm::vec3 point =
                MatrixPoint(actor, context.world.Runtime().phoenixBones[TailMatrix], glm::vec3(0.0f), context);
            if (auto made = CreateTheBlastEffect(0x106, 1, point, actor.angles, localLight, nullptr, context);
                !made.HasValue())
                return made;
            return CreateTheJoint(target, context, 0x4f8484, 0x614, 2, point, point, actor.angles, 50.0f);
        }
        return {};
    case 0x4f8493:
        // En la 4 y en 13, los ángulos del actor con 45 más en la x, 0x4ec110 con el hueso 11 (0x210) y (−50, 100, 0),
        // 0x106/1 y el joint 0x614/2 hacia el blanco.
        if (action == SecondAttack && step == ThirteenthStep)
        {
            // 0x4f84ad..0x4f852d: el punto (−50, 100, 0) del hueso 11 y el 0x106/1 sin dueño, con 45 más en la x de los
            // ángulos y la luz (1; 0,5; 0).
            constexpr std::size_t ArmBone = 11;
            constexpr glm::vec3 ArmOffset{-50.0f, 100.0f, 0.0f}; // 0xc2480000 y 0x42c80000
            constexpr double Lift = 45.0;                        // 0x6b8848
            const glm::vec3 angles{static_cast<float>(static_cast<double>(actor.angles.x) + Lift), actor.angles.y,
                                   actor.angles.z};
            const glm::vec3 point = BonePoint(actor, ArmBone, ArmOffset, context);
            if (auto made = CreateTheBlastEffect(0x106, 1, point, angles, TrailAttackLight, nullptr, context);
                !made.HasValue())
                return made;
            return CreateTheJoint(target, context, 0x4f8553, 0x614, 2, point, target.position, angles, 50.0f);
        }
        return {};
    case 0x4f8562:
        // En 1, cuatro veces dos joints 0x60f/7: el primero desde la posición del actor con los ángulos de [EBP−0x20] y
        // el segundo desde [EBP−0x2c], dos locales que este camino (sin caso en el primer switch) no escribe. Con
        // lugar en el pool, corta; lleno, nada.
        if (step == FirstStep && !IsJointPoolFull(context))
            return CutAt(0x4f8592,
                         "el primero de los ocho joints 0x60f/7, con los ángulos de una local sin escribir (CL11)",
                         actor, slot);
        return {};
    case 0x4f85ca:
    {
        // Seis veces dos joints 0x614/2 de las manos; en 1, sonido 0x75; y los cuatro 0x61b/0 con su 0x5d9/0.
        constexpr HandLightning Lightning{
            .subtype = 2, .count = 6, .wideAddress = 0x4f8654, .thinAddress = 0x4f8677, .thunderAddress = 0};
        if (auto cast = ThrowTheHandLightning(actor, target, slot, context, Lightning, glm::vec3(0.0f));
            !cast.HasValue())
            return cast;
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        return ThrowTheHandBolts(actor, target, slot, context, 0x4f872d, 0x4f874f);
    }
    case 0x4f876e:
    {
        // En 1, sonido 0x75; cuatro veces (la mano 0 en las dos primeras) dos joints 0x5bc/1 de ancho 50 y 10 hacia el
        // blanco, con los ángulos en 0 y sin rand().
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        constexpr int Attempts = 4;
        constexpr int FirstHandAttempts = 2;
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            const std::size_t bone = actor.hands[attempt < FirstHandAttempts ? 0 : 1].bone;
            const glm::vec3 point = BonePoint(actor, bone, glm::vec3(0.0f), context);
            const glm::vec3 angles(0.0f);
            if (auto made = CreateTheJoint(target, context, 0x4f87f8, 0x5bc, 1, point, target.position, angles, 50.0f);
                !made.HasValue())
                return made;
            if (auto made = CreateTheJoint(target, context, 0x4f881e, 0x5bc, 1, point, target.position, angles, 10.0f);
                !made.HasValue())
                return made;
        }
        return {};
    }
    case 0x4f8837:
        // En 1, sonido 0x75; los cuatro joints 0x61b/0 de las manos con su 0x5d9/0.
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        return ThrowTheHandBolts(actor, target, slot, context, 0x4f88d0, 0x4f88f2);
    case 0x4f890b:
        // En 1, sonido 0x50; los cuatro joints 0x61b/1 girados por el reloj, con su 0x5d9/0.
        if (step == FirstStep)
            RequestTheSound(context, HowlSound);
        return TurnTheHandBolts(actor, target, slot, context, 0x4f8928, 0x4f89df, 0x4f8a01);
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x4f8147 sin rama", found.address));
    }
}

// 0x4f8a56: con 3 en +0x38e, el switch por +0x376 − 0x22. Todos sus casos salen.
Core::Expected<void, std::string> StepTheBoneSkill(Kernel::ActorState& actor, Kernel::ActorState& target,
                                                   const KindCase& found, std::size_t slot,
                                                   const Actors::CharacterStepContext& context,
                                                   const glm::vec3& localLight)
{
    const std::uint8_t step = actor.byte381;
    switch (found.address)
    {
    case 0x4f8a5d:
        // En 1, sonido 0x50; los cuatro joints 0x61b/1 girados por el reloj, con su 0x5d9/0.
        if (step == FirstStep)
            RequestTheSound(context, HowlSound);
        return TurnTheHandBolts(actor, target, slot, context, 0x4f8a7a, 0x4f8b31, 0x4f8b53);
    case 0x4f8b78:
    {
        // Desde 8 (JB), 0x4ec110 con la matriz de 0x743748c (el primer hueso del fénix) y cuatro veces rand() % 360 y
        // el joint 0x614/0 de ancho 80 hacia el blanco.
        if (step < EighthStep)
            return {};
        constexpr int Attempts = 4;
        constexpr float Width = 80.0f; // 0x42a00000
        const glm::vec3 point = MatrixPoint(actor, context.world.Runtime().phoenixBones[0], glm::vec3(0.0f), context);
        for (int attempt = 0; attempt < Attempts; ++attempt)
        {
            const glm::vec3 angles = RandomTurn(context.random);
            if (auto made = CreateTheJoint(target, context, 0x4f8bfb, 0x614, 0, point, target.position, angles, Width);
                !made.HasValue())
                return made;
        }
        return {};
    }
    case 0x4f8c0d:
        // En 1, sonido 0x75; los cuatro joints 0x61b/0 de las manos con su 0x5d9/0.
        if (step == FirstStep)
            RequestTheSound(context, RoarSound);
        return ThrowTheHandBolts(actor, target, slot, context, 0x4f8ca6, 0x4f8cc8);
    case 0x4f8ce1:
    {
        // Cuatro veces dos joints 0x614/0 de las manos y la partícula 0x5ca/0 con la luz local.
        constexpr HandLightning Lightning{
            .subtype = 0, .count = 4, .wideAddress = 0x4f8d63, .thinAddress = 0x4f8d89, .thunderAddress = 0x4f8dab};
        return ThrowTheHandLightning(actor, target, slot, context, Lightning, localLight);
    }
    case 0x4f8dc4:
    {
        // Seis veces dos joints 0x614/2 de las manos.
        constexpr HandLightning Lightning{
            .subtype = 2, .count = 6, .wideAddress = 0x4f8e46, .thinAddress = 0x4f8e6c, .thunderAddress = 0};
        return ThrowTheHandLightning(actor, target, slot, context, Lightning, localLight);
    }
    case 0x4f8e85:
        // Dos joints 0x614/0 desde la mano 0 y una partícula 0x5ca/0, sin rand().
        return CastTheHandLightning(actor, target, slot, context, localLight, 0x4f8f07);
    default:
        return Core::Unexpected(std::format("{:#x}: un caso de 0x4f8a56 sin rama", found.address));
    }
}

// 0x4f80e0..0x4f903c: la cola, con el blanco de +0x3a4 (MOVSX) en [0, 400).
Core::Expected<void, std::string> StepTheTail(Kernel::ActorState& actor, std::size_t slot,
                                              const Actors::CharacterStepContext& context, const glm::vec3& localLight)
{
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    if (target < 0 || target >= PoolSlots)
        return {};
    // ESI: el blanco, del que cuelgan los joints de la cola.
    Kernel::ActorState& victim = context.world.Actors()[static_cast<std::size_t>(target)];
    // 0x4f811b: con 0x11, el switch por +0x376 − 0x25 (0x4f8147); los que no tienen caso, y el 0xffff, salen.
    if (ReadTheSkill(actor, context.random) == KindSkill)
    {
        if (const KindCase* found = CaseOf(KindSkillCases, actor.word376))
            return StepTheKindSkill(actor, victim, *found, slot, context, localLight);
        return {};
    }
    // 0x4f8a2c: con 3, el switch por +0x376 − 0x22 (0x4f8a56); sin caso, y el 0xffff, 0x4f8f5b (el hueso de +0x2bd con
    // 0x4ec110, dos joints 0x614 y la partícula 0x5ca).
    if (ReadTheSkill(actor, context.random) == BoneSkill)
    {
        if (const KindCase* found = CaseOf(BoneSkillCases, actor.word376))
            return StepTheBoneSkill(actor, victim, *found, slot, context, localLight);
        std::size_t boneCount = 0;
        const auto model = static_cast<std::uint16_t>(actor.type);
        const Models::Skeleton* skeleton = Assets::FaunaModelsOrEmpty(context.faunaModels).SkeletonOf(model);
        if (model == Tables::PlayerSkeletonModel && context.playerSkeleton != nullptr)
            skeleton = context.playerSkeleton;
        if (skeleton == nullptr && context.models != nullptr)
            skeleton = context.models->SkeletonOf(model);
        if (skeleton != nullptr)
            boneCount = skeleton->bones.size();
        if (boneCount < actor.hands[0].bone)
            return {};
        return CastTheHandLightning(actor, victim, slot, context, localLight, 0x4f8fef);
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheAttackCases(Kernel::ActorState& actor, std::size_t slot,
                                                     const Actors::CharacterStepContext& context)
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
