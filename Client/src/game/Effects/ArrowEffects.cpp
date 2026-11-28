#include "Effects/ArrowEffects.h"
#include "Math/X87.h"
#include "Terrain/TerrainAttributeQuery.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/ModelEffectArguments.h"
#include "Math/Angles.h"
#include "Math/FloatConstants.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRotation.h"
#include "Actors/ActorCreation.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Joints/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointFamilies.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Actors/SkillHits.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Effects/GameModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleBurst.h"
#include "Particles/Particles.h"
#include "Particles/SmokeParticleKernels.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "WorldObjects/WorldObjectStepServices.h"
#include "Terrain/WorldTerrain.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// 0x53e2b0, 0x53d9a0, los casos de flecha de 0x518cf0 y de 0x5208a0, 0x520170, 0x517850 y 0x51fe10, cotejados con
// el PE c32bbd5f (06, "La flecha"). CL9 hito 4: 0x53e741, 0x53e2e0, los casos 0x13a y 0x105 de 0x518cf0, 0x51c601,
// 0x52413a, 0x5267dc y 0x5201ed.
namespace Client::Effects
{
namespace
{
using Crt::CrtRandom;
using Models::LogicalAccess;

// Los tipos de flecha (las tablas de 0x53e240 y 0x53e270/0x53e28c).
constexpr std::int16_t BowArrow = 0xdb;
constexpr std::int16_t CrossbowArrow = 0xdc;
constexpr std::int16_t SerpentArrow = 0xdd;
constexpr std::int16_t LightArrow = 0xde;
constexpr std::int16_t ElvenArrow = 0xdf;
constexpr std::int16_t ArquebusArrow = 0xe0;
constexpr std::int16_t NatureArrow = 0xe1;
constexpr std::int16_t AquagoldArrow = 0xe2;
constexpr std::int16_t BluewingArrow = 0xe3;
constexpr std::int16_t TourArrow = 0xed;
constexpr std::int16_t SaintArrow = 0xf7;
constexpr std::int16_t CelestialArrow = 0xf8;
constexpr std::int16_t ViperArrow = 0xf9;
constexpr std::int16_t ArchangelArrow = 0x10e;
constexpr std::int16_t ReignArrow = 0x111;
constexpr std::int16_t TwentiethArrow = 0x11c;
constexpr std::int16_t NoArrow = 0;

// 0x53df39..0x53df4e: el modelo de +0xb6c (o +0x2b8) menos 0xa1a, del 0 al 0xb, por la tabla de 0x53e240.
constexpr std::int32_t FirstRightModel = 0xa1a;
constexpr std::array<std::int16_t, 12> RightArrows{CrossbowArrow, CrossbowArrow, ArquebusArrow,  LightArrow,
                                                   SerpentArrow,  BluewingArrow, AquagoldArrow,  NoArrow,
                                                   SaintArrow,    NoArrow,       ArchangelArrow, ReignArrow};
// 0x53e0ec..0x53e106: el de +0xbb4 (o +0x2d8) menos 0xa12, del 0 al 0x15, por los bytes de 0x53e28c.
constexpr std::int32_t FirstLeftModel = 0xa12;
constexpr std::array<std::int16_t, 22> LeftArrows{
    BowArrow, BowArrow,       ElvenArrow, BowArrow, BowArrow,       BowArrow,  NatureArrow, NoArrow,
    NoArrow,  NoArrow,        NoArrow,    NoArrow,  NoArrow,        NoArrow,   NoArrow,     NoArrow,
    NoArrow,  CelestialArrow, NoArrow,    NoArrow,  TwentiethArrow, ViperArrow};
// 0x53de6b..0x53de8a: el tipo del registro propio más 0x212, salvo el −1 de la mano vacía.
constexpr std::int32_t ItemModelBase = 0x212;
constexpr std::int32_t EmptyHand = -1;

constexpr std::int16_t ToInt16(std::uint16_t value)
{
    return std::bit_cast<std::int16_t>(value);
}

using Math::D;
using Math::F32;

// fld del corrimiento girado y fadd de la posición: cada eje se guarda de su suma.
void AddRotated(glm::vec3& position, const glm::vec3& rotated)
{
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(rotated[axis]) + D(position[axis]));
}

// 0x51a1b6 (y 0x51a4b7 en el 0xe2): abre el registro propio (0x4e79a0, no gasta), copia su +0xf04 a +0x8c (MOVZX) y lo
// cierra (0x40f370: 3.848 rand() si la cuenta queda en 0).
void CopyTheOwnCount(Effects::ModelEffect& effect, Kernel::WorldState& world, CrtRandom& random)
{
    const LogicalAccess own(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    effect.word8c = world.OwnCharacter().bytef04;
}

// 0x53e365..0x53e440 (y 0x53e4dd..0x53e611, 0x53e75b..0x53e92c): el registro propio abierto en línea, +0xf04 uno más
// (la cuenta de la tanda, que las flechas copian a +0x8c) y cerrado (0x40b4d0: 3.848 rand() si queda en 0).
void CountTheVolley(Kernel::WorldState& world, CrtRandom& random)
{
    const LogicalAccess own(world.Runtime().ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    ++world.OwnCharacter().bytef04;
}

// 0x518cf0(0x105, la posición de la flecha, sus ángulos, su luz, subtipo, la flecha, −1, 0, 0, 0, 0): el /0 es la
// estela de la skill 0x34 (0x51c590..0x51c5a9) y el /3, el pulso de la 0xf8/1 en su vida 13 (0x523f72). El caso 0x105
// (0x519fc9..0x51a101): vida 100, +0x64 en 0, +0x68 en 1 y +0xe4 en 0; el /0, la escala 12 (0x519ff3) y el /3, la
// escala 10 y la luz (0,9; 0,4; 0,6) (0x51a01b), los dos con la partida en la posición del dueño (0x51a040); después,
// cuatro joints 610/0..3 con la clave 30 y el subtipo del alta de noveno argumento, el sonido 7a sin posición y la
// escala en 1. Cada alta busca el primer hueco, aun con saturación parcial; sin rand().
Core::Expected<void, std::string> CreateTheArrowTrail(Effects::ModelEffect& arrow, Kernel::WorldState& world,
                                                      CrtRandom& random, std::int32_t subtype)
{
    constexpr std::int16_t TrailType = 0x105;
    constexpr std::int32_t TrailLife = 100, PulseSubtype = 3;
    constexpr float TrailScale = 12.0f, PulseScale = 10.0f; // 0x41400000, 0x41200000
    const Kernel::ModelEffectArguments arguments{.type = TrailType,
                                                 .position = arrow.transform.position,
                                                 .angles = arrow.transform.angles,
                                                 .light = arrow.light,
                                                 .subtype = subtype,
                                                 .owner = &arrow,
                                                 .key = 0xffff,
                                                 .byte89 = 0,
                                                 .byte88 = 0,
                                                 .byte1bd = 0,
                                                 .scale = 0.0f};
    Effects::ModelEffects& effects = world.Map().effects;
    const auto slot = effects.Begin(arguments);
    if (!slot.has_value())
        return {};
    Effects::ModelEffect& trail = effects.Effects()[*slot];
    trail.life = TrailLife;
    trail.objectState.blendMesh = 0;
    trail.objectState.light = 1.0f;
    trail.turnDirection = 0.0f;
    if (subtype == PulseSubtype)
    {
        trail.transform.scale = PulseScale;
        trail.light = {std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu),
                       std::bit_cast<float>(0x3f19999au)};
    }
    else
    {
        trail.transform.scale = TrailScale;
    }
    trail.start = arrow.transform.position;
    trail.owner.effect = &arrow;
    constexpr std::int32_t TrailJoint = 0x610, JointLife = 30, NoTarget = -1;
    for (std::int32_t joint = 0; joint < 4; ++joint)
    {
        const auto made = Joints::CreateFaunaJoint(world.DrawTail().joints,
                                                   {TrailJoint, trail.transform.position, trail.transform.position,
                                                    trail.transform.angles, joint, &trail, trail.transform.scale,
                                                    JointLife, subtype, 0, NoTarget},
                                                   random, Kernel::JointOwnerKind::ModelEffect);
        if (!made.HasValue())
            return Core::Unexpected("0x51a08a/aa/cd/ed: " + made.Error());
    }
    constexpr Audio::SoundId TrailSound = Sound::AquaFlashSound;
    world.Runtime().RequestSound(TrailSound);
    trail.transform.scale = 1.0f; // 51a101, después de los cuatro intentos, incluso con la tabla llena.
    return {};
}

// 0x51c601..0x51c6d1: la flecha 0xdb con el subtipo 3 o 4 (la del asedio): vida 40, escala 1,5, +0xe4 en (rand() % 100
// + 50) · 0,0666…f (0x6ba1e0), la partida en la posición del héroe ([0x743a170]) y, con el 3, la y de la velocidad en
// −(rand() % 30 + 50) y la x de los ángulos en −(rand() % 20 + 45); con el 4, la velocidad en −(rand() % 15 + 55) y la
// x en −10. Cada fild más la constante y el fchs en la x87, guardados en float.
void BeginTheSiegeArrow(Effects::ModelEffect& effect, Kernel::WorldState& world, CrtRandom& random)
{
    constexpr std::int32_t SiegeArrowLife = 40;
    constexpr float SiegeArrowScale = 1.5f; // 0x3fc00000
    constexpr int TurnSpan = 100, TurnBase = 50;
    constexpr double TurnStep = static_cast<double>(std::bit_cast<float>(0x3d888889u));
    effect.life = SiegeArrowLife;
    effect.transform.scale = SiegeArrowScale;
    effect.turnDirection = F32(static_cast<double>((random.Next() % TurnSpan) + TurnBase) * TurnStep);
    effect.start = world.Hero().position;
    if (effect.subtype == 3)
    {
        constexpr int SpeedSpan = 30, PitchSpan = 20;
        constexpr double SpeedBase = 50.0; // 0x6b86b0
        constexpr double PitchBase = 45.0; // 0x6b8848
        effect.velocity.y = F32(-(static_cast<double>(random.Next() % SpeedSpan) + SpeedBase));
        effect.transform.angles.x = F32(-(static_cast<double>(random.Next() % PitchSpan) + PitchBase));
        return;
    }
    constexpr int SpeedSpan = 15;
    constexpr double SpeedBase = 55.0; // 0x6b8990
    effect.velocity.y = F32(-(static_cast<double>(random.Next() % SpeedSpan) + SpeedBase));
    effect.transform.angles.x = -10.0f;
}

// 0x51c43b..0x51c722: el alta común de las flechas 0xdb..0xe1, 0xe3, 0xf9 y 0x11c.
Core::Expected<void, std::string> BeginTheArrow(Effects::ModelEffect& effect, Kernel::WorldState& world,
                                                CrtRandom& random)
{
    const std::int16_t type = effect.type;
    // 0x51c43b..0x51c477: +0x64 (el blendMesh) en 1 con el 0xdb, en −2 con el 0xe1 y en 0 (ESI) con el 0xdd, el 0xde,
    // el 0xdf y el 0xe3; los demás, el −1 del prefijo.
    if (type == BowArrow)
        effect.objectState.blendMesh = 1;
    else if (type == NatureArrow)
        effect.objectState.blendMesh = -2;
    else if (type == SerpentArrow || type == LightArrow || type == ElvenArrow || type == BluewingArrow)
        effect.objectState.blendMesh = 0;
    // 0x51c47a..0x51c4e3: +0xd8 en 1, vida 30 y (−10; −60; 135) girado por los ángulos y sumado a la posición.
    constexpr std::int32_t ArrowLife = 30;
    constexpr glm::vec3 Muzzle{-10.0f, -60.0f, 135.0f};
    effect.speed = 1.0f;
    effect.life = ArrowLife;
    AddRotated(effect.transform.position,
               Math::OriginalVectorRotate(Muzzle, Math::OriginalAngleMatrix(effect.transform.angles)));
    // 0x51c4e6..0x51c587: la escala y la y de la velocidad.
    if (type == TourArrow)
    {
        // 0x51c507..0x51c536: el disparo de Crywolf gira después de aplicar la boca del arma.
        effect.transform.angles.x = F32(D(effect.transform.angles.x) - 90.0);
        effect.transform.scale = 1.0f;
        effect.velocity.y = -42.0f;
        effect.velocity.z = 4.0f;
    }
    else if (type == BluewingArrow)
    {
        effect.transform.scale = std::bit_cast<float>(0x3fe66666u);
        effect.velocity.y = -50.0f;
        effect.turnDirection = -10.0f;
    }
    else if (type == ViperArrow)
    {
        effect.transform.scale = 1.0f;
        effect.velocity.y = -70.0f;
        // 0x51c542..0x51c56c: 0x530860(0x610, posición, posición, ángulos, 0xe, flecha, 50, 40, 0, 0, −1).
        constexpr std::int32_t ViperTrailSubtype = 0xe, ViperTrailKey = 0x28;
        constexpr float ViperTrailWidth = 50.0f; // 0x42480000
        const auto made = Joints::CreateFaunaJoint(world.DrawTail().joints,
                                                   {0x610, effect.transform.position, effect.transform.position,
                                                    effect.transform.angles, ViperTrailSubtype, &effect,
                                                    ViperTrailWidth, ViperTrailKey, 0, 0, -1},
                                                   random, Kernel::JointOwnerKind::ModelEffect);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x51c56c: {}", made.Error()));
    }
    else
    {
        effect.transform.scale = std::bit_cast<float>(0x3f4ccccdu);
        effect.velocity.y = -70.0f;
    }
    // 0x51c58a..0x51c5ba: con el subtipo 2 (la skill 0x34), la estela 0x105 con la flecha de dueño y, después, +0x100
    // en 0 y +0x88 en 1 (el choque de 0x5201ed), se haya creado o no.
    constexpr std::int32_t PiercingSubtype = 2;
    if (effect.subtype == PiercingSubtype)
    {
        if (auto trail = CreateTheArrowTrail(effect, world, random, 0); !trail.HasValue())
            return trail;
        effect.dword100 = 0;
        effect.state = 1;
    }
    if (type == NatureArrow && effect.subtype == 1)
    {
        const auto made = Joints::CreateFaunaJoint(world.DrawTail().joints,
                                                   {0x610, effect.transform.position, effect.transform.position,
                                                    effect.transform.angles, 13, &effect, 20, 40, 0, 0, -1},
                                                   random, Kernel::JointOwnerKind::ModelEffect);
        if (!made.HasValue())
            return Core::Unexpected(made.Error());
    }
    if (type == BowArrow && (effect.subtype == 3 || effect.subtype == 4))
    {
        BeginTheSiegeArrow(effect, world, random);
        CopyTheOwnCount(effect, world, random);
        return {};
    }
    if (type == TwentiethArrow)
    {
        // 0x51c6dd..0x51c705: +0x64 en 1 y la y de los ángulos en rand() % 360, antes del joint.
        constexpr int FullTurn = Math::FullTurn;
        effect.objectState.blendMesh = 1;
        effect.transform.angles.y = static_cast<float>(random.Next() % FullTurn);
        // 0x51c6ef..0x51c713: 0x530860(0x610, posición, posición, ángulos, 0xc, flecha, 40, 80, 0, 0, −1).
        constexpr std::int32_t TwentiethTrailSubtype = 0xc, TwentiethTrailKey = 0x50;
        constexpr float TwentiethTrailWidth = 40.0f; // 0x42200000
        const auto made = Joints::CreateFaunaJoint(world.DrawTail().joints,
                                                   {0x610, effect.transform.position, effect.transform.position,
                                                    effect.transform.angles, TwentiethTrailSubtype, &effect,
                                                    TwentiethTrailWidth, TwentiethTrailKey, 0, 0, -1},
                                                   random, Kernel::JointOwnerKind::ModelEffect);
        if (!made.HasValue())
            return Core::Unexpected(std::format("0x51c713: {}", made.Error()));
    }
    CopyTheOwnCount(effect, world, random);
    return {};
}

// 0x51a41e..0x51a4ed: el alta de la flecha 0xe2.
void BeginTheAquagoldArrow(Effects::ModelEffect& effect, Kernel::WorldState& world, CrtRandom& random)
{
    constexpr std::int32_t FirstLife = 30;
    constexpr std::int32_t AquagoldLife = 40;
    constexpr glm::vec3 Muzzle{-10.0f, -60.0f, 135.0f};
    effect.speed = 1.0f;
    effect.life = FirstLife;
    AddRotated(effect.transform.position,
               Math::OriginalVectorRotate(Muzzle, Math::OriginalAngleMatrix(effect.transform.angles)));
    effect.objectState.blendMesh = 0;
    effect.transform.scale = 1.0f;
    effect.velocity.y = -30.0f;
    effect.turnDirection = -10.0f;
    effect.life = AquagoldLife;
    // 0x51a4a2..0x51a4b0: con el subtipo 2, +0x100 en 0 y +0x88 en 1.
    if (effect.subtype == 2)
    {
        effect.dword100 = 0;
        effect.state = 1;
    }
    CopyTheOwnCount(effect, world, random);
}

// 51a2bd..51a417: el punto de partida usa los ángulos originales; después escribe los del caller, no los del efecto.
Core::Expected<void, std::string> BeginCelestialArrow(Effects::ModelEffect& effect, Kernel::WorldState& world,
                                                      CrtRandom& random, glm::vec3* callerAngles)
{
    constexpr glm::vec3 Muzzle{-10.0f, -100.0f, 15.0f};
    effect.velocity = {0.0f, -60.0f, 0.0f};
    effect.start = effect.transform.position;
    AddRotated(effect.start, Math::OriginalVectorRotate(Muzzle, Math::OriginalAngleMatrix(effect.transform.angles)));
    glm::vec3 angles{0.0f, 0.0f, effect.transform.angles.z};
    if (callerAngles != nullptr)
        *callerAngles = angles;
    // 0x51a37a..0x51a419: con el /1, tres joints 0x60f/0x19 (0x51a3b7), y si no cuatro 0x60f/0xb (0x51a405), desde la
    // partida, con los ángulos del caller, la flecha de dueño, escala 50, clave −1 y el noveno argumento en 1 sólo en
    // el segundo; después 0x51a1b0. Los dos joints van en WanderingJoints.cpp (lote joints de la CL11).
    constexpr std::int32_t ArrowJoint = 0x60f, NoKey = -1, NoTarget = -1;
    constexpr float ArrowJointScale = 50.0f; // 0x42480000
    const bool celestial = effect.subtype == 1;
    const std::int32_t jointSubtype = celestial ? 0x19 : 0xb;
    const int joints = celestial ? 3 : 4;
    for (int joint = 0; joint < joints; ++joint)
    {
        const auto made = Joints::CreateFaunaJoint(world.DrawTail().joints,
                                                   {ArrowJoint, effect.start, effect.start, angles, jointSubtype,
                                                    &effect, ArrowJointScale, NoKey, joint == 1 ? 1 : 0, 0, NoTarget},
                                                   random, Kernel::JointOwnerKind::ModelEffect);
        if (!made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", celestial ? 0x51a3b7u : 0x51a405u, made.Error()));
    }
    CopyTheOwnCount(effect, world, random);
    return {};
}

// 51a10f/51a1e6/51a2bd/51be81: prefijos y colas que el allocator de joints lleno también alcanza.
Core::Expected<void, std::string> BeginAdvancedArrow(Effects::ModelEffect& effect, Kernel::WorldState& world,
                                                     CrtRandom& random, glm::vec3* callerAngles)
{
    if (effect.type == ReignArrow && effect.subtype != 0 && effect.subtype != 2)
        return {};
    constexpr std::int32_t ArrowLife = 30;
    constexpr float MuzzleLift = 130.0f;
    effect.life = ArrowLife;
    effect.objectState.blendMesh = -2;
    effect.transform.scale = 1.0f;
    effect.transform.position.z = F32(D(effect.transform.position.z) + D(MuzzleLift));
    if (effect.type == CelestialArrow)
        return BeginCelestialArrow(effect, world, random, callerAngles);
    if (effect.type == ReignArrow)
    {
        effect.turnDirection = -10.0f;
        if (effect.subtype == 2)
        {
            if (auto trail = CreateTheArrowTrail(effect, world, random, 0); !trail.HasValue())
                return trail;
            effect.dword100 = 0;
            effect.state = 1;
        }
    }
    effect.velocity = {0.0f, -70.0f, 0.0f};
    effect.point40 = effect.transform.position;
    // 0x51a173, 0x51a24a y 0x51bf35: el 0x618/5 desde la posición, con los ángulos del efecto, el efecto de dueño y
    // ancho 100; sigue su +0x40 (BeamJoints.cpp). Sin rand().
    {
        const auto address = effect.type == SaintArrow ? 0x51a24au : effect.type == ReignArrow ? 0x51bf35u : 0x51a173u;
        constexpr std::int32_t ArrowBeamSubtype = 5;
        constexpr float ArrowBeamWidth = 100.0f; // 0x42c80000
        const Kernel::JointRequest request{.type = Joints::JointFamilies::BeamJoint,
                                           .firstPosition = effect.transform.position,
                                           .secondPosition = effect.transform.position,
                                           .angles = effect.transform.angles,
                                           .subtype = ArrowBeamSubtype,
                                           .ownerContext = &effect,
                                           .scale = ArrowBeamWidth,
                                           .key = -1,
                                           .skill = 0,
                                           .skillSerial = 0,
                                           .target = -1};
        if (auto made =
                Joints::CreateFaunaJoint(world.DrawTail().joints, request, random, Kernel::JointOwnerKind::ModelEffect);
            !made.HasValue())
            return Core::Unexpected(std::format("{:#x}: {}", address, made.Error()));
    }
    if (effect.type != ReignArrow && effect.subtype != 0)
    {
        if (auto trail = CreateTheArrowTrail(effect, world, random, 0); !trail.HasValue())
            return trail;
        effect.dword100 = 0;
        effect.state = 1;
    }
    CopyTheOwnCount(effect, world, random);
    return {};
}

// 0x518cf0(tipo, o+0x10, o+0x1c, o+0xf4, subtipo, o, o+0x8a, lugar, bandera, 0, 0), como lo llama 0x53d9a0.
Core::Expected<void, std::string> CreateTheArrow(std::int16_t type, Kernel::ActorState& owner, std::int32_t subtype,
                                                 std::int32_t skillIndex, std::uint8_t flag,
                                                 const Actors::CharacterStepContext& context)
{
    const Kernel::ModelEffectArguments arguments{.type = type,
                                                 .position = owner.position,
                                                 .angles = owner.angles,
                                                 .light = owner.light,
                                                 .subtype = subtype,
                                                 .owner = &owner,
                                                 .key = static_cast<std::uint16_t>(owner.word8a),
                                                 .byte89 = static_cast<std::uint8_t>(skillIndex & 0xff),
                                                 .byte88 = flag,
                                                 .byte1bd = 0,
                                                 .scale = 0.0f};
    return CreateTheArrowEffect(arguments, context.world, context.random, &owner.angles);
}

// 0x53deff..0x53df2a: 0x518cf0(0x13a, o+0x10, o+0x1c, o+0xf4, 0, el blanco, o+0x8a, lugar, bandera, 0, 0), el tiro de
// la skill 0x2e. El caso 0x13a (0x51df6a..0x51e01a): +0xd8 en 1, vida 20, escala 1,8f, +0x64 en −2, la y de la
// velocidad en −30, la luz en (0,3f; 0,8f; 1), (−10; −80; 200) girado por los ángulos y sumado a la posición y la x de
// los ángulos en −30; después, el joint 0x632/4 (0x51e01a) y la cuenta de la tanda. Con la tabla llena, nada y sin
// rand().
Core::Expected<void, std::string> CreateTheBlast(Kernel::ActorState& owner, Kernel::ActorState* target,
                                                 std::int32_t skillIndex, std::uint8_t flag,
                                                 const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t BlastType = 0x13a;
    constexpr std::int32_t BlastLife = 20;
    constexpr glm::vec3 Muzzle{-10.0f, -80.0f, 200.0f};
    const Kernel::ModelEffectArguments arguments{.type = BlastType,
                                                 .position = owner.position,
                                                 .angles = owner.angles,
                                                 .light = owner.light,
                                                 .subtype = 0,
                                                 .owner = target,
                                                 .key = static_cast<std::uint16_t>(owner.word8a),
                                                 .byte89 = static_cast<std::uint8_t>(skillIndex & 0xff),
                                                 .byte88 = flag,
                                                 .byte1bd = 0,
                                                 .scale = 0.0f};
    Effects::ModelEffects& effects = context.world.Map().effects;
    const auto slot = effects.Begin(arguments);
    if (!slot.has_value())
        return {};
    Effects::ModelEffect& blast = effects.Effects()[*slot];
    blast.speed = 1.0f;
    blast.life = BlastLife;
    blast.transform.scale = std::bit_cast<float>(0x3fe66666u);
    blast.objectState.blendMesh = Models::EveryMeshBlends;
    blast.velocity.y = -30.0f;
    blast.light = {std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3f4ccccdu), 1.0f};
    AddRotated(blast.transform.position,
               Math::OriginalVectorRotate(Muzzle, Math::OriginalAngleMatrix(blast.transform.angles)));
    blast.transform.angles.x = -30.0f;
    // 0x51e01a: 0x530860(0x632, o+0x10, o+0x10, o+0x1c, 4, el efecto, 40, 0x32, 0, 0, −1), el rayo que sigue al 0x13a
    // (JointFamilies::StormJoint, alta 0x534977, sin rand()); con los joints llenos, nada.
    constexpr std::int32_t BlastJointSubtype = 4, BlastJointKey = 0x32, NoTarget = -1;
    constexpr float BlastJointWidth = 40.0f; // 0x42200000
    const Kernel::JointRequest request{.type = Joints::JointFamilies::StormJoint,
                                       .firstPosition = blast.transform.position,
                                       .secondPosition = blast.transform.position,
                                       .angles = blast.transform.angles,
                                       .subtype = BlastJointSubtype,
                                       .ownerContext = &blast,
                                       .scale = BlastJointWidth,
                                       .key = BlastJointKey,
                                       .skill = 0,
                                       .skillSerial = 0,
                                       .target = NoTarget};
    if (auto made = Joints::CreateFaunaJoint(context.world.DrawTail().joints, request, context.random,
                                             Kernel::JointOwnerKind::ModelEffect);
        !made.HasValue())
        return Core::Unexpected(std::format("0x51e01a: {}", made.Error()));
    // 0x51e01f..0x51e04d: la cuenta de la tanda en +0x8c.
    CopyTheOwnCount(blast, context.world, context.random);
    return {};
}

// 0x53e642..0x53e73a: quince puntos del hueso de la mano, un joint par y una chispa por iteración.
Core::Expected<void, std::string> CreateTheReignHandShine(Kernel::ActorState& actor,
                                                          const Actors::CharacterStepContext& context)
{
    constexpr int Samples = 15;
    constexpr int FullTurn = Math::FullTurn;
    constexpr std::int32_t ShineJoint = 0x617;
    constexpr std::int32_t ShineSubtype = 3;
    constexpr glm::vec3 HandOffset{0.0f, 10.0f, -130.0f};
    const std::size_t handBone = actor.hands[0].bone;
    const Math::BoneTransform matrix =
        handBone < actor.bodyPose.size() ? actor.bodyPose[handBone] : Math::BoneTransform(0.0f);
    const Kernel::SharedModelPlacement placement = context.world.ModelPlacement(actor.type);
    glm::vec3 point =
        Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), HandOffset, placement.scale, placement.origin);
    glm::vec3 angles{};
    for (int sample = 0; sample < Samples; ++sample)
    {
        angles.x = static_cast<float>(context.random.Next() % FullTurn);
        angles.y = 0.0f;
        angles.z = 0.0f;
        if ((context.random.Next() & 1) == 0)
        {
            const Kernel::JointRequest request{ShineJoint, point, point, angles, ShineSubtype, nullptr, 10.0f,
                                               -1,         0,     0,     -1};
            const auto joint = Joints::CreateFaunaJoint(context.joints, request, context.random);
            if (!joint.HasValue())
                return Core::Unexpected(std::format("0x53e6a8: {}", joint.Error()));
        }
        if (context.particles == nullptr)
            return Core::Unexpected(std::string("0x53e73a: falta el pool de partículas del brillo de la mano"));
        const auto particle = context.particles->CreateChecked(
            Particles::FairySparkParticle, point, angles, glm::vec3(1.0f), 0, 1.0f, context.worldClock, context.random);
        if (!particle.HasValue())
            return Core::Unexpected(std::format("0x53e73a: la partícula 0x5c5/0 falla con error {}",
                                                static_cast<unsigned>(particle.Error())));
    }
    return {};
}

// 0x53d9a0(c, o, blanco, lugar, bandera, skill).
Core::Expected<void, std::string> ShootTheArrows(Kernel::ActorState& actor, Kernel::ActorState* target,
                                                 std::int32_t skillIndex, std::uint8_t flag, std::uint8_t skill,
                                                 const Actors::CharacterStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.hero >= context.world.Actors().size())
        return Core::Unexpected(std::string("0x53d9cf: el héroe publicado ([0x743a170]) no está en el pool"));
    // 0x53d9ac..0x53d9cf: las de +0x2b8 y +0x2d8 (MOVSX); con el héroe ([0x743a170]), 0x53d9d5..0x53de8a: las del
    // registro propio (+0xb6c y +0xbb4, MOVSX) más 0x212, con el registro abierto y cerrado en línea.
    std::int32_t right = ToInt16(actor.hands[0].model);
    std::int32_t left = ToInt16(actor.hands[1].model);
    if (&actor == &context.world.Hero())
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        const auto& equipment = context.world.OwnCharacter().equipment;
        right = std::bit_cast<std::int16_t>(equipment[0].id);
        left = std::bit_cast<std::int16_t>(equipment[1].id);
        if (right != EmptyHand)
            right += ItemModelBase;
        if (left != EmptyHand)
            left += ItemModelBase;
    }
    // 0x53de8d..0x53def9: el subtipo de las flechas y los sonidos de las skills (0x41ed10(sonido, o, 0), CL10).
    constexpr std::uint8_t PiercingSkill = 0x34;
    constexpr std::uint8_t ArrowRainSkill = 0x33;
    constexpr Audio::SoundId PiercingSound{0x55};
    constexpr Audio::SoundId ArrowRainSound{0x82};
    std::int32_t subtype = 0;
    if (skill == PiercingSkill)
    {
        subtype = 2;
        if (context.soundAt)
            context.soundAt(PiercingSound, actor.position);
        else if (context.soundRequest)
            context.soundRequest(PiercingSound);
    }
    runtime.lastShotSkill = skill;
    if (skill == ArrowRainSkill)
    {
        if (context.soundAt)
            context.soundAt(ArrowRainSound, actor.position);
        else if (context.soundRequest)
            context.soundRequest(ArrowRainSound);
    }
    constexpr std::uint8_t MarkedFlag = 2;
    constexpr std::int32_t MarkedSubtype = 0x63;
    if (flag == MarkedFlag)
        subtype = MarkedSubtype;
    // 0x53def9..0x53df38: con la skill 0x2e, solo el efecto 0x13a (con el blanco de dueño y el lugar) y sale.
    constexpr std::uint8_t BlastSkill = 0x2e;
    if (skill == BlastSkill)
        return CreateTheBlast(actor, target, skillIndex, flag, context);
    // 0x53df39..0x53e0e6: la de la derecha; 0x53e0ec..0x53e235: la de la izquierda, que sale al crearla.
    const auto arrowOf = [](const auto& table, std::int32_t model, std::int32_t first)
    {
        const auto index = static_cast<std::uint32_t>(model - first);
        return index < table.size() ? table[index] : NoArrow;
    };
    for (const std::int16_t type :
         {arrowOf(RightArrows, right, FirstRightModel), arrowOf(LeftArrows, left, FirstLeftModel)})
    {
        if (type == NoArrow)
            continue;
        if (auto created = CreateTheArrow(type, actor, subtype, skillIndex, flag, context); !created.HasValue())
            return created;
    }
    return {};
}

// 0x517850(efecto, radio, _, clase): el primero de los 400 vivos, que no sea de la clase (+0x88; 0 no excluye), ni el
// dueño (+0x108), que se vea (+0x16c), que no sea el héroe salvo con la clase 2 y que no se esté muriendo (+0x389), a
// `radius` o menos en el plano (la raíz en la x87 con PC53, guardada en float; NaN choca).
bool HitsSomeone(const Effects::ModelEffect& effect, Kernel::WorldState& world, const Kernel::ActorState& hero,
                 float radius, std::uint8_t kind)
{
    constexpr std::uint8_t HeroKind = 2;
    for (const Kernel::ActorState& actor : world.Actors().first(Actors::CreatedActorSearch))
    {
        if (!actor.alive)
            continue;
        if (kind != 0 && kind == actor.byte88)
            continue;
        if (effect.ownerContext == &actor)
            continue;
        const double dx = D(effect.transform.position.x) - D(actor.position.x);
        const double dy = D(effect.transform.position.y) - D(actor.position.y);
        const float distance = F32(std::sqrt((dx * dx) + (dy * dy)));
        if (!actor.seen)
            continue;
        if (&actor == &hero && kind != HeroKind)
            continue;
        if (actor.dying != 0)
            continue;
        if (!(distance > radius))
            return true;
    }
    return false;
}

// 0x51fe10(punto, 1): veinte 0x53fe70(0x5c5, punto con la z 80 más, (rand() % 60 + 150; 0; rand() % 30), (1; 1; 1),
// 2, 1,0f, 0) y la 0x5ed/0 con la luz en 0,7f.
Core::Expected<void, std::string> BurstAt(const glm::vec3& position, const ArrowStepContext& context)
{
    constexpr Audio::SoundId BurstSound = Sound::ExplosionSound;
    if (context.particles == nullptr)
    {
        static_cast<void>(context.random.Next());
        static_cast<void>(context.random.Next());
        return Core::Unexpected(std::string("0x51fe9e: falta el pool de partículas de 0x51fe10"));
    }
    const auto created =
        Particles::CreateParticleBurst(*context.particles, position, context.random, true, context.worldClock);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x51fe9e: el estallido de 0x51fe10 falla con error {}",
                                            static_cast<unsigned>(created.Error())));
    if (created.Value())
        context.world.Runtime().RequestSound(BurstSound);
    return {};
}

// 0x5201ed..0x5205ad: el choque de la flecha con +0x88 (la bandera 1 de 0x53e2e0 o el 1 de la skill 0x34) y sin el
// subtipo 0x63.
// 0x52055f y0x52083f: el dueño durable es la flecha física, no el personaje que la disparó.
Core::Expected<void, std::string> CreatePiercingImpactJoint(Effects::ModelEffect& effect,
                                                            const ArrowStepContext& context, std::string_view address)
{
    constexpr std::int32_t FlareJoint = 0x60f, ImpactSubtype = 6;
    constexpr float Width = 30.0f;
    const Kernel::JointRequest request{FlareJoint,
                                       effect.transform.position,
                                       effect.transform.position,
                                       effect.transform.angles,
                                       ImpactSubtype,
                                       &effect,
                                       Width,
                                       -1,
                                       0,
                                       0,
                                       -1};
    const auto created = Joints::CreateFaunaJoint(context.world.DrawTail().joints, request, context.random,
                                                  Kernel::JointOwnerKind::ModelEffect);
    if (!created.HasValue())
        return Core::Unexpected(std::format("{}: {}", address, created.Error()));
    return {};
}

Core::Expected<void, std::string> HitWithTheMarkedArrow(Effects::ModelEffect& effect, const ArrowStepContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    // 0x5201ed..0x5201fe: rand() & 1 (con signo, sobre un rand() no negativo); con 1, nada.
    if ((context.random.Next() & 1) != 0)
        return {};
    // 0x520204..0x520211: solo con el héroe ([0x743a170]) de dueño (+0x108).
    const Kernel::ActorState& hero = world.Hero();
    if (effect.ownerContext != static_cast<const void*>(&hero))
        return {};
    // 0x520217..0x520415: la skill del lugar +0x89, con el registro propio abierto y cerrado en línea.
    std::uint8_t skill = 0;
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        skill = Kernel::OwnSkillByte(world.OwnCharacter(), effect.byte89);
    }
    constexpr std::uint8_t ArrowRainSkill = 0x33;
    constexpr std::uint8_t PiercingSkill = 0x34;
    constexpr std::int32_t PiercingSubtype = 2;
    if (skill == PiercingSkill)
    {
        // 0x520422..0x520456: la 0xe2 a más de 500 (0x6b8a98) del héroe en el plano no choca (la raíz queda en la x87
        // con PC53; NaN sigue).
        constexpr double FarAway = 500.0;
        if (effect.type == AquagoldArrow)
        {
            const double dx = D(effect.transform.position.x) - D(hero.position.x);
            const double dy = D(effect.transform.position.y) - D(hero.position.y);
            if (std::sqrt((dy * dy) + (dx * dx)) > FarAway)
                return {};
        }
        // 0x52045c..0x520473: con +0x100 positivo, uno menos y nada.
        if (effect.dword100 > 0)
        {
            --effect.dword100;
            return {};
        }
    }
    // 0x520474..0x5204a1: 0x517a20(+0x89, la posición, 100, el byte +0x8c, +0x8a, 0); sin blancos, nada.
    constexpr float HitRadius = 100.0f; // 0x42c80000
    const Skills::SkillHitsRequest request{.skillIndex = effect.byte89,
                                           .point = effect.transform.position,
                                           .radius = HitRadius,
                                           .volley = static_cast<std::uint8_t>(effect.word8c & 0xff),
                                           .key = effect.disturbanceState,
                                           .extra = 0};
    const auto hit = Skills::HitTheTargetsAround(request, world, Terrain::TerrainOrEmpty(context.terrain),
                                                 context.random, context.send);
    if (!hit.HasValue())
        return Core::Unexpected(hit.Error());
    if (!hit.Value())
        return {};
    const bool bursts = effect.type == AquagoldArrow || effect.type == CelestialArrow;
    if (skill == ArrowRainSkill)
    {
        // 0x520583..0x5205ad.
        effect.alive = false;
        return bursts ? BurstAt(effect.transform.position, context) : Core::Expected<void, std::string>{};
    }
    if (skill == PiercingSkill)
    {
        // 0x520504..0x52056d: con el subtipo 2 sigue viva, +0x100 en 5 (la 0xf8 con más de 14 de vida y la 0xe2) o en
        // 2, y el joint 0x60f/6; si no, se apaga y 0x51fe10.
        if (effect.subtype == PiercingSubtype)
        {
            constexpr std::int32_t LongPause = 5, ShortPause = 2, CelestialLife = 14;
            effect.dword100 =
                (effect.type == CelestialArrow && effect.life > CelestialLife) || effect.type == AquagoldArrow
                    ? LongPause
                    : ShortPause;
            return CreatePiercingImpactJoint(effect, context, "0x52055f");
        }
        effect.alive = false;
        return BurstAt(effect.transform.position, context);
    }
    // 0x5204b5..0x5204fd: se apaga, el sonido rand() % 4 + 0x4b (0x41ed10(sonido, efecto, 0), CL10) y 0x51fe10 con la
    // 0xe2 y la 0xf8.
    constexpr std::int32_t FirstHitSound = 0x4b;
    effect.alive = false;
    const auto sound = static_cast<std::int32_t>(context.random.Next() & 3) + FirstHitSound;
    if (context.soundRequest)
        context.soundRequest(Audio::SoundId{sound});
    return bursts ? BurstAt(effect.transform.position, context) : Core::Expected<void, std::string>{};
}

// 0x520170(efecto): el choque de la flecha.
Core::Expected<void, std::string> HitWithTheArrow(Effects::ModelEffect& effect, const ArrowStepContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    // 0x520179..0x5201d2: con el asedio (0x4294b0), el bit 0x100 de la celda (0x5f82c0) la frena y le deja media vida.
    if (runtime.castleSiege.siegeActive)
    {
        const auto& attributes = Terrain::TerrainOrEmpty(context.terrain).attributes;
        if (!attributes)
            return Core::Unexpected(std::string("0x52018d: faltan los atributos para el choque de la flecha"));
        const auto query =
            Terrain::TerrainAttributeQueryAt(*attributes, effect.transform.position.x, effect.transform.position.y);
        if (!query.HasValue())
            return Core::Unexpected(std::string("0x52018d: dirección del atributo fuera de la tabla del mundo"));
        if ((query.Value() & 0x100) != 0)
        {
            effect.speed = 0;
            effect.velocity = glm::vec3(0);
            effect.life /= 2;
            return {};
        }
    }
    // 0x5201d3..0x5201e7: con +0x88 (la bandera de 0x53d9a0, o el 1 del subtipo 2) y sin el subtipo 0x63, rand() & 1,
    // el dueño héroe, la skill del registro propio y 0x517a20.
    constexpr std::int32_t MarkedSubtype = 0x63;
    if (effect.state != 0 && effect.subtype != MarkedSubtype)
        return HitWithTheMarkedArrow(effect, context);
    // 0x5205ae..0x5205c7: 0x517850(efecto, 100, +0x8a, 0).
    constexpr float HitRadius = 100.0f;
    if (runtime.hero >= context.world.Actors().size())
        return Core::Unexpected(std::string("0x517850: el héroe publicado ([0x743a170]) no está en el pool"));
    if (!HitsSomeone(effect, context.world, context.world.Hero(), HitRadius, 0))
        return {};
    // 0x5205cd..0x5207d1: el registro propio abierto y cerrado en línea para leer la skill del lugar +0x89.
    std::uint8_t skill = 0;
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        skill = Kernel::OwnSkillByte(context.world.OwnCharacter(), effect.byte89);
    }
    const bool bursts = effect.type == AquagoldArrow || effect.type == CelestialArrow;
    constexpr std::uint8_t ArrowRainSkill = 0x33;
    constexpr std::uint8_t PiercingSkill = 0x34;
    if (skill == ArrowRainSkill)
    {
        // 0x520866..0x520887.
        effect.alive = false;
        return bursts ? BurstAt(effect.transform.position, context) : Core::Expected<void, std::string>{};
    }
    if (skill == PiercingSkill)
    {
        // 0x52081b..0x52085c: con el subtipo 2, el joint 0x60f/6 y sigue viva.
        if (effect.subtype == 2)
        {
            return CreatePiercingImpactJoint(effect, context, "0x52083f");
        }
        effect.alive = false;
        return BurstAt(effect.transform.position, context);
    }
    // 0x5207e3..0x520811.
    effect.alive = false;
    if (bursts && effect.subtype != 1)
        return BurstAt(effect.transform.position, context);
    return {};
}

// ftol([0x5767b88]) · 0,1f, el giro de los sprites de la flecha; `negated`, con el NEG del entero.
Core::Expected<float, std::string> SpriteTurn(float worldClock, bool negated, std::string_view address)
{
    const auto whole = Math::OriginalFtolLow(D(worldClock));
    if (!whole.has_value())
        return Core::Unexpected(std::format("{}: el reloj del mundo no cabe en el ftol", address));
    const auto turned = negated ? std::bit_cast<std::int32_t>(0u - std::bit_cast<std::uint32_t>(*whole)) : *whole;
    constexpr double Tenth = Math::TenthAsDouble;
    return F32(static_cast<double>(turned) * Tenth);
}

// Los dos sprites que las flechas 0xe1, 0xe2 y 0xe3 sueltan en su lugar (0x5491e0(textura, posición, escala, luz,
// efecto, ±ftol(reloj) · 0,1f, 0)).
Core::Expected<void, std::string> TwoSprites(const Effects::ModelEffect& effect, std::uint16_t texture,
                                             const glm::vec3& light, float firstScale, float secondScale,
                                             const ArrowStepContext& context, std::string_view address)
{
    if (context.sprites == nullptr)
        return Core::Unexpected(
            std::format("{}: la flecha {:#x} pide la tabla de sprites (0x5491e0)", address, effect.type));
    const auto first = SpriteTurn(context.worldClock, false, address);
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    static_cast<void>(context.sprites->Create(texture, effect.transform.position, firstScale, light, first.Value(), 0));
    const auto second = SpriteTurn(context.worldClock, true, address);
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    static_cast<void>(
        context.sprites->Create(texture, effect.transform.position, secondScale, light, second.Value(), 0));
    return {};
}

// El punto de las chispas: tres rand() con su máscara y su centro (and con signo sobre un rand() no negativo), cada
// uno guardado en float y sumado a la posición en la x87.
glm::vec3 ScatteredPoint(const glm::vec3& position, CrtRandom& random, std::array<int, 3> masks,
                         std::array<int, 3> centres)
{
    glm::vec3 offset{};
    for (std::size_t axis = 0; axis < 3; ++axis)
        offset[static_cast<int>(axis)] = static_cast<float>((random.Next() & masks[axis]) - centres[axis]);
    glm::vec3 point{};
    for (int axis = 0; axis < 3; ++axis)
        point[axis] = F32(D(offset[axis]) + D(position[axis]));
    return point;
}

// 0x5f9780(x, y, (luz · 0,6f; luz · 0,8f; luz · 0,8f), 2, 0x7b4ccc4), la de las flechas 0xe1, 0xe2 y 0xe3.
Core::Expected<void, std::string> GreenLight(const Effects::ModelEffect& effect, float luminosity,
                                             const ArrowStepContext& context, std::string_view address)
{
    const double six = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    const double eight = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
    const glm::vec3 color{F32(D(luminosity) * six), F32(D(luminosity) * eight), F32(D(luminosity) * eight)};
    return WorldObjects::AddDynamicLight(context.lighting, effect.transform.position, color, 2, address);
}

// 0x52413a..0x5241f1: la flecha 0xdb con el subtipo 3 o 4. Con el 3, +0xe4 se suma a la x de los ángulos; la x queda en
// 50 como mucho (NaN queda). Si la celda (ftol(x · 0,01f), ftol(y · 0,01f), con el byte bajo de cada una, 0x5f82a0) no
// tiene el 0x08 en el byte bajo de su atributo (0x7d0d348), con el terreno (0x5f93c0) más alto que la z, la z queda en
// la altura + 10 (0x6b87a8) y la velocidad en 0. Después, 0x53fe70(0x5d9, posición, ángulos, `color`, 5, 1,0f, 0), sin
// el choque.
Core::Expected<void, std::string> StepTheSiegeArrow(Effects::ModelEffect& effect, const glm::vec3& color,
                                                    const ArrowStepContext& context)
{
    if (effect.subtype == 3)
        effect.transform.angles.x = F32(D(effect.turnDirection) + D(effect.transform.angles.x));
    constexpr float HighestPitch = 50.0f; // 0x6b86b0
    if (effect.transform.angles.x > HighestPitch)
        effect.transform.angles.x = HighestPitch;
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
    {
        return Core::Unexpected(std::string("0x524186: la flecha 0xdb del asedio lee el atributo de su celda "
                                            "(0x7d0d348) sin el mapa cargado"));
    }
    constexpr double CellsPerUnit = static_cast<double>(0.01f); // 0x6b86c0
    const auto x = Math::OriginalFtolLow(D(effect.transform.position.x) * CellsPerUnit);
    const auto y = Math::OriginalFtolLow(D(effect.transform.position.y) * CellsPerUnit);
    if (!x || !y)
        return Core::Unexpected(std::string("0x52416a: la posición de la flecha 0xdb por 0,01 no entra en el entero de "
                                            "0x69fd3c"));
    constexpr std::int32_t CellMask = 0xff;
    constexpr std::uint16_t NoGroundBit = 0x08;
    const std::size_t cell =
        Data::Terrain::CellIndex(static_cast<std::uint8_t>(*x & CellMask), static_cast<std::uint8_t>(*y & CellMask));
    if ((terrain.attributes->cells[cell] & NoGroundBit) == 0)
    {
        if (!context.terrainHeight)
            return Core::Unexpected(std::string("0x52419f: la flecha 0xdb del asedio pide la altura del terreno "
                                                "(0x5f93c0)"));
        constexpr double Lift = 10.0; // 0x6b87a8
        const double height = context.terrainHeight(effect.transform.position.x, effect.transform.position.y);
        if (height > D(effect.transform.position.z))
        {
            effect.transform.position.z = F32(height + Lift);
            effect.velocity = glm::vec3(0.0f);
        }
    }
    // 0x5241d2..0x5241e9.
    constexpr std::int32_t SiegeFireSubtype = 5;
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x5241e9: falta el pool de partículas del fuego 0x5d9 de la flecha"));
    const auto created =
        context.particles->CreateChecked(Particles::FireParticle, effect.transform.position, effect.transform.angles,
                                         color, SiegeFireSubtype, 1.0f, context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x5241e9: el fuego 0x5d9/5 de la flecha 0xdb del asedio falla con error "
                                            "{}, sin portar (CL11)",
                                            static_cast<unsigned>(created.Error())));
    return {};
}

// 0x52406c..0x52410b: la flecha 0xdb.
Core::Expected<void, std::string> StepBowArrow(Effects::ModelEffect& effect, float luminosity,
                                               const ArrowStepContext& context)
{
    const double eight = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
    const double half = 0.5;
    const double fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    const glm::vec3 color{F32(D(luminosity) * eight), F32(D(luminosity) * half), F32(D(luminosity) * fifth)};
    if (auto lit = WorldObjects::AddDynamicLight(context.lighting, effect.transform.position, color, 2, "0x5240a5");
        !lit.HasValue())
        return lit;
    // 0x5240aa..0x5240bc: los subtipos 3 y 4 (los del asedio) no chocan.
    if (effect.subtype == 3 || effect.subtype == 4)
        return StepTheSiegeArrow(effect, color, context);
    // 0x5240be..0x5240fa: con el subtipo 5, el fuego lleva (luz · 0,1f; luz · 0,6f; luz · 0,3f).
    glm::vec3 fire = color;
    constexpr std::int32_t BlueFireSubtype = 5;
    if (effect.subtype == BlueFireSubtype)
    {
        const double tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
        const double six = static_cast<double>(std::bit_cast<float>(0x3f19999au));
        const double three = static_cast<double>(std::bit_cast<float>(0x3e99999au));
        fire = {F32(D(luminosity) * tenth), F32(D(luminosity) * six), F32(D(luminosity) * three)};
    }
    // 0x5240fd y 0x524110..0x524127: 0x53fe70(0x5d9, posición, ángulos, el color, 0, 1,0f, 0).
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x524127: falta el pool de partículas del fuego 0x5d9 de la flecha"));
    const auto created =
        context.particles->CreateChecked(Particles::FireParticle, effect.transform.position, effect.transform.angles,
                                         fire, 0, 1.0f, context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x524127: el fuego 0x5d9/0 de la flecha falla con error {}",
                                            static_cast<unsigned>(created.Error())));
    return HitWithTheArrow(effect, context);
}

// 0x5267dc..0x526979: la flecha0xe1/1 de Crywolf yla fortaleza. Los dos sprites 0x5d5 con la luz (0,1f; 0,4f;
// 0,1f) y las escalas 0,3f y 0,7f, la luz dinámica (luz · 0,2f; luz · 0,8f; luz · 0,2f), la y de los ángulos 60 más
// (0x6b86cc) y, con más de 25 de vida y par, dos veces el punto ((& 31) − 16; (& 63) − 32; (& 31) − 16) y
// 0x53fe70(0x60f, punto, ángulos, (0,4f;1;0,2f),5,0,2f,0). Después, choque ycola comunes.
Core::Expected<void, std::string> StepTheFortressArrow(Effects::ModelEffect& effect, float luminosity,
                                                       const ArrowStepContext& context)
{
    constexpr std::uint16_t Glow = 0x5d5;
    const float tenth = std::bit_cast<float>(0x3dcccccdu);
    const glm::vec3 light{tenth, std::bit_cast<float>(0x3ecccccdu), tenth};
    if (auto drawn = TwoSprites(effect, Glow, light, std::bit_cast<float>(0x3e99999au),
                                std::bit_cast<float>(0x3f333333u), context, "0x526821");
        !drawn.HasValue())
        return drawn;
    const double fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)); // 0x6b8854
    const double eight = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu)); // 0x6b8a38
    const glm::vec3 color{F32(D(luminosity) * fifth), F32(D(luminosity) * eight), F32(D(luminosity) * fifth)};
    if (auto lit = WorldObjects::AddDynamicLight(context.lighting, effect.transform.position, color, 2, "0x52688a");
        !lit.HasValue())
        return lit;
    constexpr double Turn = 60.0; // 0x6b86cc
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + Turn);
    constexpr std::int32_t SparklingLife = 25;
    if (effect.life > SparklingLife && effect.life % 2 == 0)
    {
        constexpr int SparkCount = 2;
        for (int index = 0; index < SparkCount; ++index)
        {
            const auto point = ScatteredPoint(effect.transform.position, context.random, {31, 63, 31}, {16, 32, 16});
            if (context.particles == nullptr)
                return Core::Unexpected(std::string("0x526961: falta el pool60f/5 del misilE1/1"));
            Particles::PortableParticleTrigonometry trigonometry;
            const auto created =
                context.particles->CreateChecked(0x60f, point, effect.transform.angles, {.4f, 1, .2f}, 5, .2f,
                                                 std::cref(context.worldClock), context.random, 0, trigonometry);
            if (!created.HasValue())
                return Core::Unexpected(std::string("0x526961: falló el constructor60f/5"));
        }
    }
    return HitWithTheArrow(effect, context);
}

// 0x52697e..0x526aea: la flecha 0xe1 (salvo el subtipo 1, 0x5267dc).
Core::Expected<void, std::string> StepNatureArrow(Effects::ModelEffect& effect, float luminosity,
                                                  const ArrowStepContext& context)
{
    constexpr std::uint16_t Glow = 0x5d5;
    if (auto drawn = TwoSprites(effect, Glow, effect.light, 0.5f, 1.0f, context, "0x5269b1"); !drawn.HasValue())
        return drawn;
    // 0x5269ed..0x526a9a: cuatro veces el punto ((& 31) − 16; (& 63) − 32; (& 31) − 16) y 0x53fe70(0x608 + rand() % 3,
    // punto, ángulos, +0xf4, 0, 1,0f, 0).
    constexpr int Shards = 4;
    constexpr std::int32_t FirstShard = 0x608;
    for (int shard = 0; shard < Shards; ++shard)
    {
        const glm::vec3 point = ScatteredPoint(effect.transform.position, context.random, {31, 63, 31}, {16, 32, 16});
        const std::int32_t type = FirstShard + (context.random.Next() % 3);
        if (context.particles == nullptr)
            return Core::Unexpected(std::string("0x526a8b: falta el pool de partículas de la flecha 0xe1"));
        const auto created = context.particles->CreateChecked(type, point, effect.transform.angles, effect.light, 0,
                                                              1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x526a8b: la partícula {:#x}/0 de la flecha 0xe1 falla con error {} "
                                                "(CL11)",
                                                type, static_cast<unsigned>(created.Error())));
    }
    // 0x526aa0..0x526ad9: la y de los ángulos 30 más (0x6b8858) y la luz dinámica.
    constexpr double Turn = 30.0;
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + Turn);
    if (auto lit = GreenLight(effect, luminosity, context, "0x526ad9"); !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

// 0x526c61..0x526dc8: la flecha 0xe3.
Core::Expected<void, std::string> StepBluewingArrow(Effects::ModelEffect& effect, float luminosity,
                                                    const ArrowStepContext& context)
{
    constexpr std::uint16_t Glow = 0x5d5;
    if (auto drawn = TwoSprites(effect, Glow, effect.light, 0.5f, 1.0f, context, "0x526c94"); !drawn.HasValue())
        return drawn;
    // 0x526cd0..0x526d6e: cuatro veces el punto ((& 15) − 8, en los tres ejes) y 0x53fe70(0x607, punto, ángulos,
    // +0xf4, 1, 1,0f, 0).
    constexpr int Bubbles = 4;
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x526d5f: falta el pool de partículas de la flecha 0xe3"));
    for (int bubble = 0; bubble < Bubbles; ++bubble)
    {
        const glm::vec3 point = ScatteredPoint(effect.transform.position, context.random, {15, 15, 15}, {8, 8, 8});
        const auto created =
            context.particles->CreateChecked(Particles::DropParticle, point, effect.transform.angles, effect.light, 1,
                                             1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x526d5f: la 0x607/1 de la flecha 0xe3 falla con error {} (CL11)",
                                                static_cast<unsigned>(created.Error())));
    }
    // 0x526d74..0x526d88: 0x53fe70(0x5f2, posición, ángulos, +0xf4, 0, 1,0f, 0).
    const auto smoke =
        context.particles->CreateChecked(Particles::BreathParticle, effect.transform.position, effect.transform.angles,
                                         effect.light, 0, 1.0f, context.worldClock, context.random);
    if (!smoke.HasValue())
        return Core::Unexpected(std::format("0x526d88: la 0x5f2/0 de la flecha 0xe3 falla con error {}",
                                            static_cast<unsigned>(smoke.Error())));
    if (auto lit = GreenLight(effect, luminosity, context, "0x526dba"); !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

// 0x524237..0x52443f: la flecha 0xe2.
Core::Expected<void, std::string> StepAquagoldArrow(Effects::ModelEffect& effect, float luminosity,
                                                    const ArrowStepContext& context)
{
    constexpr std::uint16_t Glow = 0x5ac;
    const glm::vec3 light{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3ecccccdu)};
    if (auto drawn = TwoSprites(effect, Glow, light, 1.0f, 2.0f, context, "0x52427c"); !drawn.HasValue())
        return drawn;
    constexpr int Bubbles = 4;
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x52434e: falta el pool de partículas de la flecha 0xe2"));
    for (int bubble = 0; bubble < Bubbles; ++bubble)
    {
        const glm::vec3 point = ScatteredPoint(effect.transform.position, context.random, {15, 15, 15}, {8, 8, 8});
        const auto created =
            context.particles->CreateChecked(Particles::DropParticle, point, effect.transform.angles, effect.light, 1,
                                             1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x52434e: la 0x607/1 de la flecha 0xe2 falla con error {} (CL11)",
                                                static_cast<unsigned>(created.Error())));
    }
    // 0x52435d..0x5243bd: la caída. La z más +0xe4 y +0xe4 menos 1 (0x6b87d4), cada uno en la x87 y guardado en
    // float; con el terreno (0x5f93c0) por encima de la z (el FCOM sin C0 ni C3: ni menor, ni igual, ni NaN), la z al
    // terreno, +0xe4 por −0,4f (0x6ba18c) y la y de la velocidad (+0xd0) por 0,4f (0x6b87b4): rebota.
    if (!context.terrainHeight)
        return Core::Unexpected(
            std::string("0x524382: la caída de la flecha 0xe2 pide la altura del terreno (0x5f93c0)"));
    glm::vec3& position = effect.transform.position;
    position.z = F32(D(position.z) + D(effect.turnDirection));
    constexpr double Fall = 1.0; // 0x6b87d4
    effect.turnDirection = F32(D(effect.turnDirection) - Fall);
    const double ground = context.terrainHeight(position.x, position.y);
    if (ground > D(position.z))
    {
        constexpr double Bounce = static_cast<double>(std::bit_cast<float>(0xbecccccdu)); // 0x6ba18c
        constexpr double Brake = static_cast<double>(std::bit_cast<float>(0x3ecccccdu));  // 0x6b87b4
        position.z = F32(ground);
        effect.turnDirection = F32(D(effect.turnDirection) * Bounce);
        effect.velocity.y = F32(D(effect.velocity.y) * Brake);
    }
    // 0x5243bf..0x52440a: en la última vida, 0x51fe10(posición, 1) y, con el héroe ([0x743a170]) de dueño (+0x108) y
    // sin el subtipo 0x63, 0x517a20(+0x89, posición, 100, +0x8c, +0x8a, 0), del hito 4.
    if (effect.life == 1)
    {
        if (auto burst = BurstAt(position, context); !burst.HasValue())
            return burst;
        Kernel::WorldRuntime& runtime = context.world.Runtime();
        if (runtime.hero >= context.world.Actors().size())
            return Core::Unexpected(std::string("0x5243d3: el héroe publicado ([0x743a170]) no está en el pool"));
        constexpr std::int32_t MarkedSubtype = 0x63;
        if (effect.ownerContext == &context.world.Hero() && effect.subtype != MarkedSubtype)
        {
            const Skills::SkillHitsRequest hits{.skillIndex = effect.byte89,
                                                .point = position,
                                                .radius = 100.0f,
                                                .volley = static_cast<std::uint8_t>(effect.word8c),
                                                .key = static_cast<std::uint16_t>(effect.disturbanceState),
                                                .extra = 0};
            if (auto hit = Skills::HitTheTargetsAround(hits, context.world, Terrain::TerrainOrEmpty(context.terrain),
                                                       context.random, context.send);
                !hit.HasValue())
                return Core::Unexpected(hit.Error());
        }
    }
    // 0x52440d..0x52443f: la luz (luz · 0,6f; · 0,8f; · 0,8f) y el choque.
    if (auto lit = GreenLight(effect, luminosity, context, "0x52443a"); !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

// 0x526aef..0x526c51: la flecha 0x11c.
Core::Expected<void, std::string> StepTwentiethArrow(Effects::ModelEffect& effect, float luminosity,
                                                     const ArrowStepContext& context)
{
    constexpr double Turn = 60.0; // 0x6b86cc
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + Turn);
    if (effect.life % 2 == 0)
    {
        constexpr int SparkCount = 3;
        // 0x526b24..0x526bcb: tres veces el punto al azar y 0x53fe70(0x60f, punto, ángulos, (0,4f; 0,2f; 1), 5, 0,2f,
        // 0).
        if (context.particles == nullptr)
            return Core::Unexpected(std::string("0x526bc2: falta el pool de la 0x60f/5 de la flecha 0x11c"));
        constexpr glm::vec3 Violet{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e4ccccdu), 1.0f};
        constexpr std::int32_t ShineSubtype = 5;
        constexpr float ShineScale = std::bit_cast<float>(0x3e4ccccdu); // 0x526b77
        for (int index = 0; index < SparkCount; ++index)
        {
            const auto point = ScatteredPoint(effect.transform.position, context.random, {31, 63, 31}, {16, 32, 16});
            Particles::PortableParticleTrigonometry trigonometry;
            const auto created = context.particles->CreateChecked(
                Particles::OrbitParticle, point, effect.transform.angles, Violet, ShineSubtype, ShineScale,
                std::cref(context.worldClock), context.random, 0, trigonometry);
            if (!created.HasValue())
                return Core::Unexpected(std::format("0x526bc2: la 0x60f/5 de la flecha 0x11c falla con el error {}",
                                                    static_cast<unsigned>(created.Error())));
        }
    }
    effect.point40 = effect.transform.position;
    // 0x526c05: 0x518cf0(0x114, posición, ángulos, luz, 3, 0, 0, 0, 0, 0, 0), la onda 0x114/3 con la clave en 0 (su
    // escala, 0); sin rand().
    constexpr std::int32_t WaveSubtype = 3;
    static_cast<void>(context.world.Map().effects.CreateWaveEffect({.type = 0x114,
                                                                    .position = effect.transform.position,
                                                                    .angles = effect.transform.angles,
                                                                    .light = effect.light,
                                                                    .subtype = WaveSubtype,
                                                                    .key = 0}));
    constexpr glm::vec3 Tint{0.6f, 0.2f, 0.8f};
    glm::vec3 color{};
    for (int axis = 0; axis < 3; ++axis)
        color[axis] = F32(D(luminosity) * D(Tint[axis]));
    if (auto lit = WorldObjects::AddDynamicLight(context.lighting, effect.transform.position, color, 2, "0x526c44");
        !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

Core::Expected<void, std::string> ArrowLightAt(const glm::vec3& position, float luminosity, const glm::vec3& tint,
                                               const ArrowStepContext& context, std::string_view address)
{
    glm::vec3 color{};
    for (int axis = 0; axis < 3; ++axis)
        color[axis] = F32(D(luminosity) * D(tint[axis]));
    constexpr int Radius = 2;
    return WorldObjects::AddDynamicLight(context.lighting, position, color, Radius, address);
}

// 523f33..524067: la /1 hace Hit; las otras iluminan start y salen sin choque.
Core::Expected<void, std::string> StepCelestialArrow(Effects::ModelEffect& effect, float luminosity,
                                                     const ArrowStepContext& context)
{
    constexpr glm::vec3 Pink{0.9f, 0.4f, 0.6f}, Blue{0.2f, 0.4f, 1.0f};
    const bool single = effect.subtype == 1;
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + (single ? 60.0 : 30.0));
    if (single)
    {
        // 0x523f4b..0x523f72: en la vida 13, el pulso 0x105/3 con la flecha de dueño (sus joints y su sonido 0x7a).
        constexpr std::int32_t PulseLife = 13, PulseSubtype = 3;
        if (effect.life == PulseLife)
        {
            if (auto pulse = CreateTheArrowTrail(effect, context.world, context.random, PulseSubtype);
                !pulse.HasValue())
                return pulse;
        }
        if (auto hit = HitWithTheArrow(effect, context); !hit.HasValue())
            return hit;
        return ArrowLightAt(effect.transform.position, luminosity, Pink, context, "0x523fca");
    }
    if (auto lit = ArrowLightAt(effect.start, luminosity, Blue, context, "0x524015"); !lit.HasValue())
        return lit;
    if (effect.subtype == 0)
        return {};
    if (effect.life == 13)
        return CreateTheArrowTrail(effect, context.world, context.random, 0);
    if (effect.life == 30)
    {
        effect.dword100 = 0;
        effect.state = 1;
    }
    return {};
}

// 52593c..525a5f: el giro y +30, tres humos 0x5f2/13 en la posición de la flecha con sus ángulos, la luz 1 de
// 0x520921 y escala 1 (0x525972, 0x52598b, 0x5259a4; tres rand() cada uno) y el estallido terminal.
Core::Expected<void, std::string> StepReignArrow(Effects::ModelEffect& effect, float luminosity,
                                                 const ArrowStepContext& context)
{
    if (effect.subtype != 0 && effect.subtype != 2)
        return {};
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + 30.0);
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x525972: falta el pool de partículas de la flecha 0x111"));
    for (const std::string_view call : {"0x525972", "0x52598b", "0x5259a4"})
    {
        const auto created = context.particles->CreateChecked(
            Particles::BreathParticle, effect.transform.position, effect.transform.angles, glm::vec3(1.0f),
            Particles::EmberSmokeSubtype, 1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{}: el humo 0x5f2/13 de la flecha 0x111 falla con el error {}", call,
                                                static_cast<unsigned>(created.Error())));
    }
    if (effect.life == 1)
    {
        if (auto burst = BurstAt(effect.transform.position, context); !burst.HasValue())
            return burst;
    }
    effect.point40 = effect.transform.position;
    AddRotated(effect.point40,
               Math::OriginalVectorRotate(effect.velocity, Math::OriginalAngleMatrix(effect.transform.angles)));
    constexpr glm::vec3 Orange{1.0f, 0.4f, 0.2f};
    if (auto lit = ArrowLightAt(effect.transform.position, luminosity, Orange, context, "0x525a54"); !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

Core::Expected<void, std::string> StepAdvancedArrow(Effects::ModelEffect& effect, float luminosity,
                                                    const ArrowStepContext& context)
{
    if (effect.type == CelestialArrow)
        return StepCelestialArrow(effect, luminosity, context);
    if (effect.type == ReignArrow)
        return StepReignArrow(effect, luminosity, context);
    if (effect.type == ViperArrow)
    {
        effect.transform.angles.y = F32(D(effect.transform.angles.y) + 35.0);
        return HitWithTheArrow(effect, context);
    }
    const bool archangel = effect.type == ArchangelArrow;
    if (archangel)
    {
        constexpr double GlowStep = static_cast<double>(0.025f);
        effect.objectState.light = effect.life > 25 ? F32(static_cast<double>(30 - effect.life) * GlowStep) : 1.0f;
    }
    const double turn = archangel ? static_cast<double>(context.random.Next() % 60 + 30) : 30.0;
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + turn);
    effect.point40 = effect.transform.position;
    if (archangel)
        AddRotated(effect.point40,
                   Math::OriginalVectorRotate(effect.velocity, Math::OriginalAngleMatrix(effect.transform.angles)));
    constexpr glm::vec3 Orange{1.0f, 0.4f, 0.2f}, Blue{0.2f, 0.4f, 1.0f};
    if (auto lit = ArrowLightAt(effect.transform.position, luminosity, archangel ? Orange : Blue, context, "0x523ec4");
        !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

} // namespace

Core::Expected<void, std::string> ScatterCombatBurstAt(const glm::vec3& position,
                                                       const Actors::CharacterStepContext& context)
{
    constexpr int Groups = 8;
    constexpr int YawStep = 45;
    constexpr std::array Subtypes{Effects::RisingStoneChipSubtype, std::int32_t{0}};
    constexpr glm::vec3 Offset{0.0f, -220.0f, 0.0f};
    auto& effects = context.world.Map().effects;
    for (int group = 0; group < Groups; ++group)
    {
        const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(group * YawStep)};
        const glm::vec3 rotated = Math::OriginalVectorRotate(Offset, Math::OriginalAngleMatrix(angles));
        glm::vec3 point{};
        for (int axis = 0; axis < 3; ++axis)
            point[axis] = static_cast<float>(static_cast<double>(rotated[axis]) + static_cast<double>(position[axis]));
        if (auto burst = CreateCombatBurstAt(point, context); !burst.HasValue())
            return Core::Unexpected(burst.Error());
        // 0x520119 y 0x520152: cada selector precede a su intento, incluso con el pool lleno.
        for (const auto subtype : Subtypes)
        {
            const auto type = static_cast<std::int16_t>(Effects::FirstStoneChipEffect + (context.random.Next() & 1));
            static_cast<void>(effects.CreateStoneChip(type, point, angles, glm::vec3(1.0f), context.random, subtype));
        }
    }
    return {};
}

Core::Expected<void, std::string> CreateAquagoldArrow(Kernel::ActorState& owner, const glm::vec3& angles,
                                                      const Actors::CharacterStepContext& context)
{
    constexpr std::int16_t ArrowType = 0xe2;
    const Kernel::ModelEffectArguments arguments{.type = ArrowType,
                                                 .position = owner.position,
                                                 .angles = angles,
                                                 .light = owner.light,
                                                 .subtype = 0,
                                                 .owner = &owner,
                                                 .key = 0xffff,
                                                 .byte89 = 0,
                                                 .byte88 = 0,
                                                 .byte1bd = 0,
                                                 .scale = 0.0f};
    const auto slot = context.world.Map().effects.Begin(arguments);
    if (!slot.has_value())
        return {};
    Effects::ModelEffect& effect = context.world.Map().effects.Effects()[*slot];
    BeginTheAquagoldArrow(effect, context.world, context.random);
    return {};
}

Core::Expected<void, std::string> CreateCombatBurstAt(const glm::vec3& position,
                                                      const Actors::CharacterStepContext& context)
{
    if (context.particles == nullptr)
    {
        static_cast<void>(context.random.Next());
        static_cast<void>(context.random.Next());
        return Core::Unexpected(std::string("0x51fe9e: falta el pool de partículas de 0x51fe10"));
    }
    const auto created =
        Particles::CreateParticleBurst(*context.particles, position, context.random, true, context.worldClock);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x51fe9e: el estallido de 0x51fe10 falla con error {}",
                                            static_cast<unsigned>(created.Error())));
    if (!created.Value())
        return {};
    constexpr Audio::SoundId BurstSound = Sound::ExplosionSound;
    context.world.Runtime().RequestSound(BurstSound);
    if (context.soundRequest)
        context.soundRequest(BurstSound);
    return {};
}

bool IsArrowEffect(std::int16_t type)
{
    return (type >= BowArrow && type <= BluewingArrow) || (type >= SaintArrow && type <= ViperArrow) ||
           type == ArchangelArrow || type == ReignArrow || type == TwentiethArrow || type == TourArrow;
}

Core::Expected<void, std::string> CreateTheArrowEffect(const Kernel::ModelEffectArguments& arguments,
                                                       Kernel::WorldState& world, CrtRandom& random,
                                                       glm::vec3* callerAngles)
{
    if (!IsArrowEffect(arguments.type))
    {
        return Core::Unexpected(
            std::format("0x518cf0: el efecto {:#x} no es una flecha (su caso no está acá)", arguments.type));
    }
    Effects::ModelEffects& effects = world.Map().effects;
    const auto slot = effects.Begin(arguments);
    if (!slot.has_value())
        return {};
    Effects::ModelEffect& effect = effects.Effects()[*slot];
    switch (arguments.type)
    {
    case AquagoldArrow:
        BeginTheAquagoldArrow(effect, world, random);
        return {};
    case SaintArrow:
    case CelestialArrow:
    case ArchangelArrow:
    case ReignArrow:
        return BeginAdvancedArrow(effect, world, random, callerAngles);
    default:
        return BeginTheArrow(effect, world, random);
    }
}

Core::Expected<void, std::string> FireTheArrow(Kernel::ActorState& actor, Kernel::ActorState* target,
                                               std::size_t /*slot*/, std::int32_t skillIndex, std::uint8_t flag,
                                               std::uint8_t skill, const Actors::CharacterStepContext& context)
{
    // 0x53e2bb..0x53e2ce y 0x53e741..0x53e92c: las skills 0x34, 0x33 y 0x2e, un tiro y la cuenta de la tanda, sin el
    // brillo de la 0xa25.
    constexpr std::array<std::uint8_t, 3> SkillShots{0x34, 0x33, 0x2e};
    if (std::ranges::find(SkillShots, skill) != SkillShots.end())
    {
        if (auto shot = ShootTheArrows(actor, target, skillIndex, flag, skill, context); !shot.HasValue())
            return shot;
        CountTheVolley(context.world, context.random);
        return {};
    }
    // 0x53e2d4..0x53e611: con la bandera en 1 (la skill 0x18 de 0x502b12), los tiros con +0x24 de o corrido (cada
    // corrimiento, fld/fadd o fsub/fstp) y la cuenta de la tanda.
    if (flag == 1)
    {
        float& yaw = actor.angles.z;
        const auto add = [&yaw](double by) { yaw = F32(D(yaw) + by); };
        const auto subtract = [&yaw](double by) { yaw = F32(D(yaw) - by); };
        const auto shoot = [&]() { return ShootTheArrows(actor, target, skillIndex, flag, skill, context); };
        constexpr double Five = 5.0, Ten = 10.0, Fifteen = 15.0, Twenty = 20.0, Thirty = 30.0;
        constexpr std::uint16_t ArchangelCrossbow = 0xa24;
        constexpr std::uint16_t ReignCrossbow = 0xa25;
        const std::uint16_t right = actor.hands[0].model;
        Core::Expected<void, std::string> shot;
        if (right == ArchangelCrossbow || right == ReignCrossbow)
        {
            // 0x53e445..0x53e4da: cuatro, en a + 5 (0x6b87f8), + 10 (0x6b87a8), − 20 (0x6b86d0) y − 10, y + 30
            // (0x6b8858): +0x24 queda en a + 15 (Q-C1218).
            add(Five);
            if (shot = shoot(); !shot.HasValue())
                return shot;
            add(Ten);
            if (shot = shoot(); !shot.HasValue())
                return shot;
            subtract(Twenty);
            if (shot = shoot(); !shot.HasValue())
                return shot;
            subtract(Ten);
            if (shot = shoot(); !shot.HasValue())
                return shot;
            add(Thirty);
        }
        else
        {
            // 0x53e300..0x53e362: tres, en a, + 15 (0x6b87bc) y − 30 (0x6b8858), y + 15: vuelve a a.
            if (shot = shoot(); !shot.HasValue())
                return shot;
            add(Fifteen);
            if (shot = shoot(); !shot.HasValue())
                return shot;
            subtract(Thirty);
            if (shot = shoot(); !shot.HasValue())
                return shot;
            add(Fifteen);
        }
        CountTheVolley(context.world, context.random);
    }
    else
    {
        // 0x53e616..0x53e62d.
        if (auto shot = ShootTheArrows(actor, target, skillIndex, flag, skill, context); !shot.HasValue())
            return shot;
    }
    // 0x53e630..0x53e73a: con 0xa25 en +0x2b8, el punto del hueso de +0x2bd (0x4ec110) y quince giros rand() % 360,
    // con un rand() par el joint 0x617/3 y siempre la 0x5c5/0.
    constexpr std::uint16_t ReignCrossbow = 0xa25;
    if (actor.hands[0].model == ReignCrossbow)
        return CreateTheReignHandShine(actor, context);
    return {};
}

Core::Expected<void, std::string> StepArrow(Effects::ModelEffect& effect, const Models::Skeleton* skeleton,
                                            const ArrowStepContext& context)
{
    if (!effect.alive)
        return {};
    if (!IsArrowEffect(effect.type))
        return Core::Unexpected(std::format("0x5208a0: el efecto {:#x} no es una flecha", effect.type));
    // 0x5208c5: el primer rand() de todo efecto vivo, la luz local.
    const float luminosity = Effects::StepLuminosity(context.random.Next(), effect.life);
    Core::Expected<void, std::string> stepped;
    switch (effect.type)
    {
    case TourArrow:
        // 0x524210 → 0x52c4e9: luz roja y cola común; no pasa por el choque de las flechas.
        effect.light = {1.0f, 0.0f, 0.0f};
        stepped =
            WorldObjects::AddDynamicLight(context.lighting, effect.transform.position, effect.light, 2, "0x52c4f1");
        break;
    case BowArrow:
        stepped = StepBowArrow(effect, luminosity, context);
        break;
    case CrossbowArrow:
    case SerpentArrow:
    case LightArrow:
    case ElvenArrow:
    case ArquebusArrow:
        // 0x5267c7 → 0x526dc2: solo el choque.
        stepped = HitWithTheArrow(effect, context);
        break;
    case NatureArrow:
        stepped = effect.subtype == 1 ? StepTheFortressArrow(effect, luminosity, context)
                                      : StepNatureArrow(effect, luminosity, context);
        break;
    case BluewingArrow:
        stepped = StepBluewingArrow(effect, luminosity, context);
        break;
    case AquagoldArrow:
        stepped = StepAquagoldArrow(effect, luminosity, context);
        break;
    case TwentiethArrow:
        stepped = StepTwentiethArrow(effect, luminosity, context);
        break;
    case SaintArrow:
    case CelestialArrow:
    case ViperArrow:
    case ArchangelArrow:
    case ReignArrow:
        stepped = StepAdvancedArrow(effect, luminosity, context);
        break;
    default:
        // 0x523ed1 (0xf7), 0x523f33 (0xf8), 0x5241f6 (0xf9), 0x523de1 (0x10e) y 0x52593c (0x111): sus altas cortan.
        return Core::Unexpected(std::format("0x5208a0: el paso de la flecha {:#x}, sin portar (CL11: su alta corta en "
                                            "el joint)",
                                            effect.type));
    }
    if (!stepped.HasValue())
        return stepped;
    // 0x52cb98: la cola no mira el byte vivo; la flecha que el choque apagó también se anima, se mueve y pierde una
    // vida.
    const bool alive = effect.alive;
    effect.alive = true;
    const glm::vec3 position = effect.transform.position;
    Effects::StepModelEffect(effect, skeleton);
    if (effect.type == CelestialArrow)
    {
        // 0x52ccbe..0x52cd3f: la 0xf8 no se mueve; la velocidad girada por sus ángulos corre la partida (+0x17c).
        effect.transform.position = position;
        effect.previousPosition = position;
        AddRotated(effect.start,
                   Math::OriginalVectorRotate(effect.velocity, Math::OriginalAngleMatrix(effect.transform.angles)));
    }
    if (!alive)
        effect.alive = false;
    return {};
}

Core::Expected<void, std::string> HitTheArrow(Effects::ModelEffect& effect, const ArrowStepContext& context)
{
    return HitWithTheArrow(effect, context);
}
} // namespace Client::Effects
