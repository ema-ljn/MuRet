#include "Skills/AttackFrameSkills.h"

#include "Core/Expected.h"
#include "Kernel/ModelEffectArguments.h"
#include "Tables/PlayerModels.h"
#include "Tables/PlayerActions.h"
#include "Math/Angles.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Skills/AttackSkillReads.h"
#include "Combat/BlastEffectCalls.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Actors/CharacterStep.h"
#include "Joints/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Hero/HeroAttack.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Effects/SkillBurstEffects.h"
#include "Effects/WizardEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>

// 0x4f9899..0x4fb0e6, los casos de 0x4f97b0, cotejados con el PE c32bbd5f: 06, "Las habilidades de los frames del
// ataque".
namespace Client::Skills
{
namespace
{
// 0x518cf0 de un tipo alto (Scenes/HighModelEffects) desde un caso, con la dirección del llamador delante del error.
Core::Expected<void, std::string> CreateHighEffect(const Kernel::ModelEffectArguments& arguments,
                                                   const Actors::CharacterStepContext& context, std::uint32_t address)
{
    if (auto made = Effects::CreateHighModelEffect(arguments, Effects::HighContextOf(context)); !made.HasValue())
        return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    return {};
}

constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
// 0x4fab0a, 0x4fab4a y 0x4fb0dc: +0x381 en 15, que la cabeza del paso (0x4fdbda) toma como habilidad terminada.
constexpr std::uint8_t AttackDone = 15;
// [0x6c88ac]: el 15 de 0x4f987d (y de 0x4fa47e, 0x4fa5c3, 0x4faa29 y 0x4faaba) y el 5 de la 0x34 (0x4f9ee2).
constexpr std::int32_t FullThreshold = 15;
constexpr std::int32_t ShortThreshold = 5;
// Los frames que miran los casos (FLD +0x114 y FCOMP): ≥ con TEST AH,1 y > con TEST AH,0x41; NaN no pasa ninguno.
constexpr float ThirdFrame = 3.0f;                               // 0x6b86b8
constexpr float ThirdAndHalfFrame = 3.5f;                        // 0x6b8ae4
constexpr float FifthFrame = 5.0f;                               // 0x6b87f8
constexpr float FifthAndHalfFrame = 5.5f;                        // 0x6b8a40
constexpr float FourteenthFrame = 14.0f;                         // 0x6b881c
constexpr float BoltOpening = std::bit_cast<float>(0x40133333u); // 0x6ba178, 2,3f
constexpr float BoltClosing = std::bit_cast<float>(0x40266666u); // 0x6ba174, 2,6f
constexpr double Degree = Math::DegreesToRadiansAsDouble;        // 0x6b8cf8, π/180
// 0x2b: la mano corrida 300 por el rumbo (0x6b8900).
constexpr float HandReach = 300.0f;

// La acción (+0x111) y el frame (+0x114) del objeto.
std::uint8_t ActionOf(const Kernel::ActorState& actor)
{
    return actor.animation.action;
}

float FrameOf(const Kernel::ActorState& actor)
{
    return actor.animation.frame;
}

bool IsPlayer(const Kernel::ActorState& actor)
{
    return actor.type == PlayerType;
}

// 0x4f9780(o): la mano que miran los casos, 1 (la izquierda, +0x2d8) con el 0x20a en la 0x25 o la 0x26; si no, 0.
std::size_t HandOf(const Kernel::ActorState& actor)
{
    constexpr std::uint8_t FirstLeftAction = 0x25;
    constexpr std::uint8_t SecondLeftAction = 0x26;
    const bool left = IsPlayer(actor) && (ActionOf(actor) == FirstLeftAction || ActionOf(actor) == SecondLeftAction);
    return left ? 1 : 0;
}

// 0x41ed10(sonido, ...): la CL10 decide cómo suena (WorldRuntime::lastSoundId en el juego).
void RequestTheSound(const Actors::CharacterStepContext& context, Audio::SoundId sound)
{
    if (context.soundRequest)
        context.soundRequest(sound);
}

// Un efecto, joint o partícula sin portar en un caso de 0x4f97b0, con lo que hizo antes y sus rand().
Core::Unexpected<std::string> CutAt(std::uint32_t address, std::string_view what, std::uint8_t skill,
                                    const Kernel::ActorState& actor, std::size_t slot)
{
    return Core::Unexpected(std::format("{:#x}: {} de la habilidad {:#x} del lugar {} (tipo {:#x}, +0x381 en {}), sin "
                                        "portar",
                                        address, what, skill, slot, actor.type, actor.byte381));
}

// El modelo del tipo de o (0x5769ad8 + MOVSX +2 · 0xe0): Player.bmd con el 0x20a, si no la biblioteca de modelos.
const Models::Skeleton* SkeletonOf(const Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    if (IsPlayer(actor) && context.playerSkeleton != nullptr)
        return context.playerSkeleton;
    if (context.models != nullptr)
        return context.models->SkeletonOf(Models::ModelId{actor.type});
    return nullptr;
}

// 0x4ec110(modelo, huesos propios + hueso · 0x30, offset, punto, 1) con la escala y la posición que 0x4fd69b copió del
// personaje al modelo antes del paso. Sin huesos propios (antes del primer dibujo), la matriz en cero (Q-C318), como
// OwnBonePoint de CharacterStep.cpp.
glm::vec3 BonePoint(const Kernel::ActorState& actor, std::size_t bone, const glm::vec3& offset,
                    const Kernel::WorldState& world)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    const auto placement = world.ModelPlacement(actor.type);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), offset, placement.scale, placement.origin);
}

// 0x4f98a0..0x4f9bcd: la 0x2b.
Core::Expected<void, std::string> StepTheSkill2b(Kernel::ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                 const Actors::CharacterStepContext& context)
{
    // 0x4f98a0..0x4f98f8: el hueso de la mano (+0x2bd de la de 0x4f9780) en el modelo del tipo; con el byte +0x22 de
    // su registro de 0x8c (un hueso que la pose no escribe) o fuera de la cuenta de huesos (+0x22 del modelo, JGE con
    // signo), sale. El original lee el byte antes de mirar la cuenta: fuera de ella lee de más y sale igual.
    const Models::Skeleton* skeleton = SkeletonOf(actor, context);
    if (skeleton == nullptr)
    {
        return Core::Unexpected(std::format(
            "0x4f98e4: falta el esqueleto del modelo {:#x} del lugar {} (la habilidad 0x2b)", actor.type, slot));
    }
    const std::size_t bone = actor.hands[HandOf(actor)].bone;
    if (bone >= skeleton->bones.size() || skeleton->bones[bone].dummy)
        return {};
    // 0x4f98fe..0x4f9915: en 8, 0x41ed10(0x71, 0, 0).
    constexpr std::uint8_t SoundStep = 8;
    constexpr Audio::SoundId ChargeSound{0x71};
    const std::uint8_t step = actor.byte381;
    if (step == SoundStep)
        RequestTheSound(context, ChargeSound);
    // 0x4f9918..0x4f9a07: de 2 a 8, tres veces la posición con z + 120 (0x6b86c8), tres rand() % 0x259 − 300 sumados
    // (x, y, z), menos el seno del rumbo · 1.400 en x y más el coseno en y (0x6ba188; FSIN y FCOS retenidos, un solo
    // redondeo con el FMUL), y 0x530860(0x10f, punto, punto, o+0x1c, 2, o, 40,0f, −1, 0, 0, −1): el joint va de la
    // mano (o+0x194) hacia el punto (AuraJoints.cpp). Con el pool lleno, los rand() igual.
    constexpr std::uint8_t FirstBoltStep = 2;
    constexpr std::uint8_t LastBoltStep = 8;
    if (step >= FirstBoltStep && step <= LastBoltStep)
    {
        constexpr int Bolts = 3;
        constexpr double Lift = 120.0;                // 0x6b86c8
        constexpr int Spread = 0x259, Center = 0x12c; // 0x4f995f, 0x4f9966
        constexpr float BoltReach = 1400.0f;          // 0x6ba188
        constexpr std::int32_t BoltJoint = 0x10f, BoltSubtype = 2;
        constexpr float BoltWidth = 40.0f; // 0x42200000
        for (int bolt = 0; bolt < Bolts; ++bolt)
        {
            glm::vec3 point{actor.position.x, actor.position.y,
                            static_cast<float>(static_cast<double>(actor.position.z) + Lift)};
            for (int axis = 0; axis < 3; ++axis)
            {
                const int jitter = context.random.Next() % Spread - Center;
                point[axis] = static_cast<float>(static_cast<double>(jitter) + static_cast<double>(point[axis]));
            }
            const double angle = static_cast<double>(actor.angles.z) * Degree;
            const auto sine = Math::Detail::SineProductPC53(angle, BoltReach);
            const auto cosine = Math::Detail::CosineProductPC53(angle, BoltReach);
            if (!sine || !cosine)
                return Core::Unexpected(std::format(
                    "0x4f99e2: el rumbo {} de los rayos de la {:#x} no cabe en el perfil PC53", actor.angles.z, skill));
            point.x = static_cast<float>(static_cast<double>(point.x) - *sine);
            point.y = static_cast<float>(*cosine + static_cast<double>(point.y));
            if (context.joints.empty())
                return CutAt(0x4f99fe, "falta el pool de joints de los 0x10f/2", skill, actor, slot);
            const Kernel::JointRequest request{.type = BoltJoint,
                                               .firstPosition = point,
                                               .secondPosition = point,
                                               .angles = actor.angles,
                                               .subtype = BoltSubtype,
                                               .ownerContext = &actor,
                                               .scale = BoltWidth,
                                               .key = -1,
                                               .skill = 0,
                                               .skillSerial = 0,
                                               .target = -1};
            if (auto created =
                    Joints::CreateFaunaJoint(context.joints, request, context.random, Kernel::JointOwnerKind::Actor);
                !created.HasValue())
                return Core::Unexpected(std::format("0x4f99fe: {}", created.Error()));
        }
    }
    // 0x4f9a0d..0x4f9a80: hasta 8, 0x4ec110 con el hueso y (0, 0, 0) a o+0x194; después, o+0x194 más el seno del
    // rumbo (+0x24 · π/180) · 300 y o+0x198 menos el coseno · 300, cada uno en la x87 y guardado en float. FSIN y FCOS
    // quedan retenidos y el FMUL por 300 redondea una sola vez: el perfil PC53 (0x4f9a61..0x4f9a7a).
    if (step <= LastBoltStep)
    {
        const glm::vec3 point = BonePoint(actor, bone, glm::vec3(0.0f), context.world);
        const double angle = static_cast<double>(actor.angles.z) * Degree;
        const auto sine = Math::Detail::SineProductPC53(angle, HandReach);
        const auto cosine = Math::Detail::CosineProductPC53(angle, HandReach);
        if (!sine || !cosine)
            return Core::Unexpected(std::format(
                "0x4f9a63: el rumbo {} de la mano de la {:#x} no cabe en el perfil PC53", actor.angles.z, skill));
        actor.skillHandPoint = point;
        actor.skillHandPoint.x = static_cast<float>(*sine + static_cast<double>(actor.skillHandPoint.x));
        actor.skillHandPoint.y = static_cast<float>(static_cast<double>(actor.skillHandPoint.y) - *cosine);
    }
    // 0x4f9a86..0x4f9ba2: de 6 a 12, 0x4ec110 con el hueso a un punto, corrido (+0x381 − 8) · 10 + 100 por el rumbo, y
    // dos 0x518cf0(0x812, punto, o+0x1c, (1; 1; 1), 1, o, −1, 0, 0, 0, 0); con el blanco de +0x3a4 vivo y +0x381 desde
    // 10, el blanco + 0x1a0 en '#' (0x23).
    constexpr std::uint8_t FirstSparkStep = 6;
    constexpr std::uint8_t LastSparkStep = 12;
    if (step >= FirstSparkStep && step <= LastSparkStep)
    {
        // El largo ((+0x381 − 8) · 10 + 100) y el rumbo quedan en la x87; FSIN y FCOS retenidos por el largo, un solo
        // redondeo (PC53), y la suma guardada en float.
        constexpr double SparkStepLength = 10.0, SparkBase = 100.0; // 0x6b87a8 y 0x6b86b4
        constexpr std::int32_t SparkSubtype = 1;
        constexpr std::int16_t SparkEmitter = 0x812;
        glm::vec3 point = BonePoint(actor, bone, glm::vec3(0.0f), context.world);
        const double length = static_cast<double>(static_cast<int>(step) - 8) * SparkStepLength + SparkBase;
        const double angle = static_cast<double>(actor.angles.z) * Degree;
        const auto sine = Math::Detail::SineDoubleProductPC53(angle, length);
        const auto cosine = Math::Detail::CosineDoubleProductPC53(angle, length);
        if (!sine || !cosine)
            return Core::Unexpected(std::format("0x4f9b2c: el rumbo {} de las chispas de la {:#x} no cabe en el "
                                                "perfil PC53",
                                                actor.angles.z, skill));
        point.x = static_cast<float>(*sine + static_cast<double>(point.x));
        point.y = static_cast<float>(static_cast<double>(point.y) - *cosine);
        for (const std::uint32_t address : {0x4f9b49u, 0x4f9b69u})
        {
            if (auto made = CreateHighEffect({.type = SparkEmitter,
                                              .position = point,
                                              .angles = actor.angles,
                                              .light = glm::vec3(1.0f),
                                              .subtype = SparkSubtype,
                                              .owner = &actor},
                                             context, address);
                !made.HasValue())
                return made;
        }
        const auto actors = context.world.Actors();
        if (step >= 10 && actor.attackTarget != 0xffff && actor.attackTarget < actors.size() &&
            actors[actor.attackTarget].alive)
            actors[actor.attackTarget].byte1a0 = 0x23;
    }
    // 0x4f9ba9..0x4f9bc1: desde 12, +0x381 en el byte de [0x6c88ac].
    if (step >= LastSparkStep)
        actor.byte381 = static_cast<std::uint8_t>(context.world.Runtime().skillThreshold);
    return {};
}

// 0x4fa895..0x4fa993: la 0x2c. Pasado el frame 5, +0x381 en 15; la posición con dos rand() % 30 − 15 (x e y) y z + 20,
// y cuatro veces un rand() % 60 + 150 en la x de los ángulos (la z, la de o), 0x530860(0x617, punto, punto, ángulos, 0,
// sin dueño, 10,0f, −1, 0, 0, −1) y 0x53fe70(0x5d9, punto, ángulos, o+0xf4, 2, 1,5f, 0). Cada suma, en la x87 y
// guardada en float.
Core::Expected<void, std::string> StepTheSkill2c(Kernel::ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                 const Actors::CharacterStepContext& context)
{
    if (FrameOf(actor) > FifthFrame)
        actor.byte381 = AttackDone;
    constexpr int Spread = 30;
    constexpr double Center = 15.0; // 0x6b87bc
    constexpr double Lift = 20.0;   // 0x6b86d0
    glm::vec3 point = actor.position;
    const int x = context.random.Next() % Spread;
    point.x = static_cast<float>((static_cast<double>(x) - Center) + static_cast<double>(point.x));
    const int y = context.random.Next() % Spread;
    point.y = static_cast<float>((static_cast<double>(y) - Center) + static_cast<double>(point.y));
    point.z = static_cast<float>(static_cast<double>(point.z) + Lift);
    constexpr int Bolts = 4;
    constexpr int PitchSpan = 60;
    constexpr int PitchBase = 150;
    constexpr std::int32_t BoltJoint = 0x617;
    constexpr float BoltWidth = 10.0f;
    constexpr std::int32_t FlameSubtype = 2;
    constexpr float FlameScale = 1.5f;
    for (int bolt = 0; bolt < Bolts; ++bolt)
    {
        glm::vec3 angles{static_cast<float>((context.random.Next() % PitchSpan) + PitchBase), 0.0f, actor.angles.z};
        if (context.joints.empty())
            return CutAt(0x4fa964, "falta el pool de joints de los 0x617/0", skill, actor, slot);
        const Kernel::JointRequest request{.type = BoltJoint,
                                           .firstPosition = point,
                                           .secondPosition = point,
                                           .angles = angles,
                                           .subtype = 0,
                                           .ownerContext = nullptr,
                                           .scale = BoltWidth,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto created = Joints::CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
            return Core::Unexpected(std::format("0x4fa964: {}", created.Error()));
        if (context.particles == nullptr)
            return CutAt(0x4fa980, "falta el pool de partículas de las 0x5d9/2", skill, actor, slot);
        const auto flame =
            context.particles->CreateChecked(Particles::FireParticle, point, angles, actor.light, FlameSubtype,
                                             FlameScale, context.worldClock, context.random);
        if (!flame.HasValue())
        {
            return Core::Unexpected(std::format("0x4fa980: la 0x5d9/2 del lugar {} falla con error {}", slot,
                                                static_cast<unsigned>(flame.Error())));
        }
    }
    return {};
}

// 0x4f9bce..0x4f9e60: la 0x2f.
Core::Expected<void, std::string> StepTheSkill2f(Kernel::ActorState& actor, std::uint8_t skill, std::size_t,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr std::uint8_t SoundStep = 10;
    constexpr std::uint8_t HandStep = 4;
    constexpr std::uint8_t SparkStep = 8;
    constexpr Audio::SoundId SwingSound{0x7d};
    const std::uint8_t step = actor.byte381;
    if (step == SoundStep)
    {
        // 0x4f9bf3: 0x41ed10(0x7d, 0, 0).
        RequestTheSound(context, SwingSound);
    }
    else if (step == HandStep)
    {
        // 0x4f9c0e..0x4f9c8d: 0x4ec110 con el hueso de la mano y 0x518cf0(0x813, punto, o+0x1c, (1; 1; 0,5), el modelo
        // de la mano (MOVSX), o, −1, 0, 0, 0, 0).
        constexpr std::int16_t BoltEmitter = 0x813;
        const Kernel::ActorItemPart& hand = actor.hands[HandOf(actor)];
        const glm::vec3 point = BonePoint(actor, hand.bone, glm::vec3(0.0f), context.world);
        return CreateHighEffect({.type = BoltEmitter,
                                 .position = point,
                                 .angles = actor.angles,
                                 .light = {1.0f, 1.0f, 0.5f},
                                 .subtype = std::bit_cast<std::int16_t>(hand.model),
                                 .owner = &actor},
                                context, 0x4f9c8d);
    }
    else if (step == SparkStep)
    {
        // 0x4f9ca2..0x4f9d3c: la posición 50 por delante (0x6b86b0) y 110 más arriba (0x6ba184), y dos
        // 0x518cf0(0x812, punto, o+0x1c, (1; 1; 1), 0, o, −1, 0, 0, 0, 0). FSIN y FCOS retenidos por 50 (PC53).
        constexpr float Ahead = 50.0f;
        constexpr double Lift = 110.0;
        constexpr std::int16_t SparkEmitter = 0x812;
        const double angle = static_cast<double>(actor.angles.z) * Degree;
        const auto sine = Math::Detail::SineProductPC53(angle, Ahead);
        const auto cosine = Math::Detail::CosineProductPC53(angle, Ahead);
        if (!sine || !cosine)
            return Core::Unexpected(std::format(
                "0x4f9cb6: el rumbo {} de las chispas de la {:#x} no cabe en el perfil PC53", actor.angles.z, skill));
        glm::vec3 point = actor.position;
        point.x = static_cast<float>(*sine + static_cast<double>(point.x));
        point.y = static_cast<float>(static_cast<double>(point.y) - *cosine);
        point.z = static_cast<float>(static_cast<double>(point.z) + Lift);
        for (const std::uint32_t address : {0x4f9d1cu, 0x4f9d3cu})
        {
            if (auto made = CreateHighEffect({.type = SparkEmitter,
                                              .position = point,
                                              .angles = actor.angles,
                                              .light = glm::vec3(1.0f),
                                              .owner = &actor},
                                             context, address);
                !made.HasValue())
                return made;
        }
    }
    // 0x4f9d44..0x4f9e52: en 13 y 14, tres veces la posición de o 145 por delante por el rumbo (+0x24 · π/180; FSIN y
    // FCOS retenidos y el FMUL por 145 redondea una sola vez: el perfil PC53) y 110 más arriba (0x6ba184), tres
    // rand() % 60 − 30 sumados con FILD + FADD (salvo en 11, 0x4f9dab, que no llega) y 0x518cf0(0x10f, punto, o+0x1c,
    // (0,3; 0,3; 0,3) de 0x4f9d78, el modelo de la mano (MOVSX), o, −1, 0, 0, 0, 0) (0x4f9e49, lote switch de la CL11).
    // Con la tabla llena, el lazo sigue con sus rand().
    constexpr std::uint8_t FirstRockStep = 13;
    constexpr std::uint8_t LastRockStep = 14;
    if (step < FirstRockStep || step > LastRockStep)
        return {};
    constexpr int Rocks = 3;
    constexpr float RockReach = 145.0f; // 0x6ba180
    constexpr double RockRise = 110.0;  // 0x6ba184
    constexpr int Scatter = 60;
    constexpr int ScatterCentre = 30;
    constexpr std::int16_t RockEffect = 0x10f;
    constexpr glm::vec3 RockLight{0.3f, 0.3f, 0.3f}; // 0x3e99999a
    const std::int32_t subtype = std::bit_cast<std::int16_t>(actor.hands[HandOf(actor)].model);
    for (int rock = 0; rock < Rocks; ++rock)
    {
        const double angle = static_cast<double>(actor.angles.z) * Degree;
        const auto sine = Math::Detail::SineProductPC53(angle, RockReach);
        const auto cosine = Math::Detail::CosineProductPC53(angle, RockReach);
        if (!sine || !cosine)
            return Core::Unexpected(std::format(
                "0x4f9d8f: el rumbo {} de la piedra de la {:#x} no cabe en el perfil PC53", actor.angles.z, skill));
        glm::vec3 point = actor.position;
        point.x = static_cast<float>(*sine + static_cast<double>(point.x));
        point.y = static_cast<float>(static_cast<double>(point.y) - *cosine);
        point.z = static_cast<float>(static_cast<double>(point.z) + RockRise);
        for (int axis = 0; axis < 3; ++axis)
        {
            const int offset = context.random.Next() % Scatter - ScatterCentre;
            point[axis] = static_cast<float>(static_cast<double>(offset) + static_cast<double>(point[axis]));
        }
        const Kernel::ModelEffectArguments arguments{.type = RockEffect,
                                                     .position = point,
                                                     .angles = actor.angles,
                                                     .light = RockLight,
                                                     .subtype = subtype,
                                                     .owner = &actor};
        if (auto made = Combat::CreateBlastEffect(arguments, &actor, context); !made.HasValue())
            return Core::Unexpected(made.Error());
    }
    return {};
}

// 0x4f9e61..0x4f9ef2: la 0x34. El 0x20a de la 0x22 a la 0x97, desde el frame 5, vuelve al 5 (+0x114) con la key
// anterior en 4 (+0x118, que MuRet guarda truncada); en 3, el efecto 0x621/0 y el sonido 0x81; [0x6c88ac] en 5, que
// adelanta la habilidad (0x4fdbda) a cuando +0x381 llega a 5.
Core::Expected<void, std::string> StepTheSkill34(Kernel::ActorState& actor, std::uint8_t, std::size_t,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t HeldKey = 4;
    const std::uint8_t action = ActionOf(actor);
    if (IsPlayer(actor) && action >= Tables::FirstBusyAction && action <= Tables::LastBusyAction &&
        FrameOf(actor) >= FifthFrame)
    {
        actor.animation.previousKey = HeldKey;
        actor.animation.frame = FifthFrame;
    }
    // 0x4f9ea1..0x4f9ed8: en 3, 0x518cf0(0x621, o+0x10, o+0x1c, o+0xf4, 0, o, −1, 0, 0, 0, 0) y 0x41ed10(0x81, o, 0).
    constexpr std::uint8_t GlowStep = 3;
    if (actor.byte381 == GlowStep)
    {
        static_cast<void>(context.world.Map().effects.CreatePiercingShot({.type = Effects::PiercingShotGlowEffect,
                                                                          .position = actor.position,
                                                                          .angles = actor.angles,
                                                                          .light = actor.light,
                                                                          .owner = &actor}));
        if (context.soundAt)
            context.soundAt(Audio::SoundId{0x81}, actor.position);
        else
            RequestTheSound(context, Audio::SoundId{0x81});
    }
    // 0x4f9ee2.
    context.world.Runtime().skillThreshold = ShortThreshold;
    return {};
}

// 0x4f9ef3..0x4fa490: la 0x37, solo el 0x20a en la 0x3d.
Core::Expected<void, std::string> StepTheSkill37(Kernel::ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                 const Actors::CharacterStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    constexpr std::uint8_t ThrowAction = 0x3d;
    if (!IsPlayer(actor) || ActionOf(actor) != ThrowAction)
    {
        // 0x4fa47e.
        runtime.skillThreshold = FullThreshold;
        return {};
    }
    // 0x4f9f0c..0x4f9f3e: en 1 y 2, 0x518cf0(0x621, o+0x10, o+0x1c, o+0xf4, 1, o, −1, 0, 0, 0, 0).
    constexpr std::uint8_t LastGlowStep = 2;
    if (actor.byte381 >= 1 && actor.byte381 <= LastGlowStep)
    {
        constexpr std::int32_t HeldGlowSubtype = 1;
        if (auto made = CreateHighEffect({.type = Effects::PiercingShotGlowEffect,
                                          .position = actor.position,
                                          .angles = actor.angles,
                                          .light = actor.light,
                                          .subtype = HeldGlowSubtype,
                                          .owner = &actor},
                                         context, 0x4f9f3e);
            !made.HasValue())
            return made;
    }
    // 0x4f9f46..0x4f9f57: antes del frame 3, o con NaN, sale.
    if (!(FrameOf(actor) >= ThirdFrame))
    {
        runtime.skillThreshold = FullThreshold;
        return {};
    }
    // 0x4f9f5d..0x4f9f6f: o+0x8a = 0x558eb0(c, [0x6c8e30]).
    const auto key = Hero::ChosenKeyOf(context.world, slot, context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    actor.word8a = static_cast<std::int16_t>(key.Value());
    // 0x4f9f7a: 0x41ed10(0x72, 0, 0).
    constexpr Audio::SoundId ThrowSound = Sound::KnightSkill3Sound;
    RequestTheSound(context, ThrowSound);
    // 0x4f9f87: 0x59fb80(0x37, 0, 0, 0), el lugar de la skill en el registro propio.
    constexpr std::uint8_t ThrowSkill = 0x37;
    const std::int32_t index = OwnSkillIndex(context, ThrowSkill);
    // 0x4f9fae: 0x518cf0(0x5be, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a, lugar, 0, 0, 0) (Scenes/SkillBurstEffects).
    // Detrás (0x4f9fb3..0x4fa47e): el registro propio abierto y cerrado en línea para fijar la velocidad de la acción
    // 0x3d de Player.bmd (+0x3d4 de las acciones de +0x30 del modelo 0x20a) en +0x4c · 0,004f + 0,54f (0x6b89e8,
    // 0x6ba17c), y +0x381 en 15.
    if (auto made = CreateHighEffect({.type = Effects::SweepEmitterEffect,
                                      .position = actor.position,
                                      .angles = actor.angles,
                                      .light = actor.light,
                                      .subtype = 0,
                                      .owner = &actor,
                                      .key = static_cast<std::uint16_t>(actor.word8a),
                                      .byte89 = static_cast<std::uint8_t>(index)},
                                     context, 0x4f9fae);
        !made.HasValue())
        return Core::Unexpected(std::format("{} (la habilidad {:#x} del lugar {})", made.Error(), skill, slot));
    float product = 0.0f;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        product = static_cast<float>(static_cast<double>(context.world.OwnCharacter().attackSpeed) *
                                     static_cast<double>(0.004f));
    }
    context.world.ActionSpeeds().Set(ThrowAction,
                                     static_cast<float>(static_cast<double>(product) + static_cast<double>(0.54f)));
    actor.byte381 = AttackDone;
    runtime.skillThreshold = FullThreshold;
    return {};
}

// 0x4fa4a8..0x4fa5d3: la 0x38, solo el 0x20a en la 0x8d.
Core::Expected<void, std::string> StepTheSkill38(Kernel::ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr std::uint8_t SpinAction = 0x8d;
    if (IsPlayer(actor) && ActionOf(actor) == SpinAction)
    {
        // 0x4fa4c1..0x4fa5ba: cinco 0x518cf0(0xce, o+0x10, ángulos, o+0xf4, 2, o, −1, 0, 0, 0, 0) con los de o y la z
        // 40 menos y después 20 más cada vez (fld, fsub o fadd y fstp sobre el local), cuyas altas cuentan en el
        // registro propio; después 0x41ed10(0x72, 0, 0) y +0x381 en 15.
        constexpr int Waves = 5;
        constexpr double FirstTurn = 40.0; // 0x6b8bf4
        constexpr double NextTurn = 20.0;  // 0x6b86d0
        constexpr std::int32_t WaveSubtype = 2;
        constexpr Audio::SoundId SpinSound = Sound::KnightSkill3Sound;
        constexpr std::array<std::uint32_t, Waves> Calls{0x4fa4fa, 0x4fa523, 0x4fa54f, 0x4fa578, 0x4fa5a4};
        glm::vec3 angles = actor.angles;
        angles.z = static_cast<float>(static_cast<double>(angles.z) - FirstTurn);
        for (int wave = 0; wave < Waves; ++wave)
        {
            if (wave > 0)
                angles.z = static_cast<float>(static_cast<double>(angles.z) + NextTurn);
            const auto created = Effects::CreateWizardEffect(
                context, Effects::WizardArguments(Effects::MagicEffect, actor.position, angles, actor.light,
                                                  WaveSubtype, &actor));
            if (!created.HasValue())
            {
                return Core::Unexpected(std::format("{:#x}: el 0xce/2 de la habilidad {:#x} del lugar {}: {}",
                                                    Calls[static_cast<std::size_t>(wave)], skill, slot,
                                                    created.Error()));
            }
        }
        RequestTheSound(context, SpinSound);
        actor.byte381 = AttackDone;
    }
    // 0x4fa5c3.
    context.world.Runtime().skillThreshold = FullThreshold;
    return {};
}

Core::Expected<void, std::string> CreateSkill39Bolts(Kernel::ActorState& actor,
                                                     const Actors::CharacterStepContext& context,
                                                     std::initializer_list<std::int32_t> keys, std::uint32_t address)
{
    constexpr std::int32_t BoltJoint = 0x60f, BoltSubtype = 0x17;
    constexpr float Width = 40.0f;
    for (const std::int32_t key : keys)
    {
        const Kernel::JointRequest request{
            BoltJoint, actor.position, actor.position, actor.angles, BoltSubtype, nullptr, Width, key, 0, 0, -1};
        const auto created = Joints::CreateFaunaJoint(context.joints, request, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, created.Error()));
    }
    return {};
}

// 0x4fa994..0x4faaca: la 0x39, de cualquier tipo.
Core::Expected<void, std::string> StepTheSkill39(Kernel::ActorState& actor, const Actors::CharacterStepContext& context)
{
    constexpr Audio::SoundId BoltSound{0x270};
    const float frame = FrameOf(actor);
    if (frame > FifthFrame)
    {
        // 0x4fa9ab..0x4faa29: tres 0x530860(0x60f, o+0x10, o+0x10, o+0x1c, 0x17, sin dueño, 40,0f, la clave 0, 1 y 4,
        // 0, 0, −1), +0x381 en 15 y 0x41ed10(0x270, 0, 0).
        if (auto created = CreateSkill39Bolts(actor, context, {0, 1, 4}, 0x4fa9ca); !created.HasValue())
            return created;
        actor.byte381 = AttackDone;
        RequestTheSound(context, BoltSound);
    }
    if (frame > BoltOpening && frame < BoltClosing)
    {
        // 0x4faa62..0x4faab0: dos 0x530860(0x60f, …, 0x17, …, la clave 2 y 3, …) y 0x41ed10(0x270, 0, 0).
        if (auto created = CreateSkill39Bolts(actor, context, {2, 3}, 0x4faa81); !created.HasValue())
            return created;
        RequestTheSound(context, BoltSound);
    }
    // 0x4faaba.
    context.world.Runtime().skillThreshold = FullThreshold;
    return {};
}

// Los casos sin efectos: +0x381 en 15 cuando el que pega llega a su acción y su frame; la 0x41, en 10 si no.
void StepTheQuietSkill(Kernel::ActorState& actor, std::uint8_t skill)
{
    const std::uint8_t action = ActionOf(actor);
    const float frame = FrameOf(actor);
    const bool player = IsPlayer(actor);
    const auto isOneOf = [action](std::initializer_list<std::uint8_t> actions)
    { return std::ranges::find(actions, action) != actions.end(); };
    bool done = false;
    switch (skill)
    {
    case 0x28:
        // 0x4fa5d4..0x4fa616: desde el frame 14, el 0x20a en la 0x45.
        done = frame >= FourteenthFrame && player && action == 0x45;
        break;
    case 0x30:
        // 0x4fa617..0x4fa64f: con +0x381 en 10 o más, el 0x20a en la 0x3f.
        done = actor.byte381 > 9 && player && action == 0x3f;
        break;
    case 0x31:
        // 0x4fa845..0x4fa87e: desde el frame 5, el 0x20a en la 0x40 o la 0x41.
        done = frame >= FifthFrame && player && isOneOf({0x40, 0x41});
        break;
    case 0x35:
    {
        // 0x4fa650..0x4fa6c4: desde el frame 5, el 0x20a de la 0x22 a la 0x97 o un tipo de 0x152 a 0x1c9 (MOVSX, JL y
        // JGE) en la 3 o la 4.
        const auto type = std::bit_cast<std::int16_t>(actor.type);
        const bool striking = player && action >= 0x22 && action <= 0x97;
        const bool monster = type >= 0x152 && type < 0x1ca && action >= 3 && action <= 4;
        done = frame >= FifthFrame && (striking || monster);
        break;
    }
    case 0x3c:
    case 0x3d:
    case 0x4a:
        // 0x4fa6c5..0x4fa707, 0x4fa708..0x4fa75e y 0x4faacb..0x4fab19: desde el frame 3, el 0x20a en la 0x4c, la 0x4e
        // o la 0x58.
        done = frame >= ThirdFrame && player && isOneOf({0x4c, 0x4e, 0x58});
        break;
    case 0x3e:
        // 0x4fa7b5..0x4fa7f7: desde el frame 5, el 0x20a en la 0x53.
        done = frame >= FifthFrame && player && action == 0x53;
        break;
    case 0x3f:
        // 0x4fa7f8..0x4fa844: con +0x381 en 6 o más, el 0x20a en la 0x4d, la 0x4f o la 0x5a.
        done = actor.byte381 > 5 && player && isOneOf({0x4d, 0x4f, 0x5a});
        break;
    case 0x41:
        // 0x4fa75f..0x4fa7b4: desde el frame 5,5, el 0x20a en la 0x94, la 0x51 o la 0x5b; si no, +0x381 en 10
        // (0x4fa7a9), que vuelve a contar.
        if (!(frame >= FifthAndHalfFrame && player && isOneOf({0x94, 0x51, 0x5b})))
        {
            constexpr std::uint8_t Recount = 10;
            actor.byte381 = Recount;
            return;
        }
        done = true;
        break;
    case 0x44:
    case 0x46:
    case 0x47:
        // 0x4fb0dc y 0x4fab4a: siempre.
        done = true;
        break;
    case 0x48:
        // 0x4fa87f..0x4fa894: desde el frame 3,5, de cualquier tipo.
        done = frame >= ThirdAndHalfFrame;
        break;
    case 0x49:
        // 0x4fab1a..0x4fab49: desde el frame 3, el 0x20a en la 0x86.
        done = frame >= ThirdFrame && player && action == 0x86;
        break;
    default:
        // 0x43 y 0x45: 0x4fb0e6, nada.
        break;
    }
    if (done)
        actor.byte381 = AttackDone;
}

// El caso corrió: 0x4f97b0 sale, o el corte con lo que hizo.
Core::Expected<bool, std::string> Handled(Core::Expected<void, std::string> result)
{
    if (!result.HasValue())
        return Core::Unexpected(result.Error());
    return true;
}
} // namespace

Core::Expected<bool, std::string> StepTheFrameSkill(Kernel::ActorState& actor, std::uint8_t skill, std::size_t slot,
                                                    const Actors::CharacterStepContext& context)
{
    // 0x4f988d..0x4f9899: la habilidad − 0x28 hasta 0x22 por la tabla de 0x4fb10c; los que van a 0x4fab5d, sin caso.
    switch (skill)
    {
    case 0x2b:
        return Handled(StepTheSkill2b(actor, skill, slot, context));
    case 0x2c:
        return Handled(StepTheSkill2c(actor, skill, slot, context));
    case 0x2f:
        return Handled(StepTheSkill2f(actor, skill, slot, context));
    case 0x34:
        return Handled(StepTheSkill34(actor, skill, slot, context));
    case 0x37:
        return Handled(StepTheSkill37(actor, skill, slot, context));
    case 0x38:
        return Handled(StepTheSkill38(actor, skill, slot, context));
    case 0x39:
        return Handled(StepTheSkill39(actor, context));
    case 0x28:
    case 0x30:
    case 0x31:
    case 0x35:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x41:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4a:
        StepTheQuietSkill(actor, skill);
        return true;
    default:
        return false;
    }
}
} // namespace Client::Skills
