#include "Scenes/ArrowEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/OriginalFtol.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// 0x53e2b0, 0x53d9a0, los casos de flecha de 0x518cf0 y de 0x5208a0, 0x520170, 0x517850 y 0x51fe10, cotejados con
// el PE c32bbd5f (06, "La flecha").
namespace Client::Scenes
{
namespace
{
using Render::Models::CrtRandom;
using Render::Models::LogicalAccess;

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

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

// fld del corrimiento girado y fadd de la posición: cada eje se guarda de su suma.
void AddRotated(glm::vec3& position, const glm::vec3& rotated)
{
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(rotated[axis]) + D(position[axis]));
}

// 0x51a1b6 (y 0x51a4b7 en el 0xe2): abre el registro propio (0x4e79a0, no gasta), copia su +0xf04 a +0x8c (MOVZX) y lo
// cierra (0x40f370: 3.848 rand() si la cuenta queda en 0).
void CopyTheOwnCount(World::ModelEffect& effect, WorldState& world, CrtRandom& random)
{
    const LogicalAccess own(world.Runtime().ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    effect.word8c = world.OwnCharacter().bytef04;
}

// 0x51c43b..0x51c722: el alta común de las flechas 0xdb..0xe1, 0xe3, 0xf9 y 0x11c.
Core::Expected<void, std::string> BeginTheArrow(World::ModelEffect& effect, WorldState& world, CrtRandom& random)
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
    AddRotated(effect.transform.position, Render::Models::OriginalVectorRotate(
                                              Muzzle, Render::Models::OriginalAngleMatrix(effect.transform.angles)));
    // 0x51c4e6..0x51c587: la escala y la y de la velocidad.
    if (type == BluewingArrow)
    {
        effect.transform.scale = std::bit_cast<float>(0x3fe66666u);
        effect.velocity.y = -50.0f;
        effect.turnDirection = -10.0f;
    }
    else if (type == ViperArrow)
    {
        effect.transform.scale = 1.0f;
        effect.velocity.y = -70.0f;
        return Core::Unexpected(std::string("0x51c56c: el joint 0x610/0xe de la flecha 0xf9 (0x530860), sin portar "
                                            "(CL11)"));
    }
    else
    {
        effect.transform.scale = std::bit_cast<float>(0x3f4ccccdu);
        effect.velocity.y = -70.0f;
    }
    // 0x51c58a: con el subtipo 2 (la skill 0x34), el efecto 0x105 con la flecha de dueño, +0x100 en 0 y +0x88 en 1.
    constexpr std::int32_t PiercingSubtype = 2;
    if (effect.subtype == PiercingSubtype)
    {
        return Core::Unexpected(std::format(
            "0x51c5a9: el efecto 0x105 de la flecha {:#x} con el subtipo 2 (la skill 0x34), del hito 4", type));
    }
    if (type == NatureArrow && effect.subtype == 1)
        return Core::Unexpected(std::string("0x51c713: el joint 0x610/0xd de la flecha 0xe1/1 (0x530860), sin portar "
                                            "(CL11)"));
    if (type == BowArrow && (effect.subtype == 3 || effect.subtype == 4))
    {
        return Core::Unexpected(std::format(
            "0x51c601: la flecha 0xdb con el subtipo {} (vida 40, escala 1,5 y rand()), del hito 4", effect.subtype));
    }
    if (type == TwentiethArrow)
    {
        // 0x51c6dd..0x51c705: +0x64 en 1 y la y de los ángulos en rand() % 360, antes del joint.
        constexpr int FullTurn = 360;
        effect.objectState.blendMesh = 1;
        effect.transform.angles.y = static_cast<float>(random.Next() % FullTurn);
        return Core::Unexpected(std::string("0x51c713: el joint 0x610/0xc de la flecha 0x11c (0x530860), sin portar "
                                            "(CL11)"));
    }
    CopyTheOwnCount(effect, world, random);
    return {};
}

// 0x51a41e..0x51a4ed: el alta de la flecha 0xe2.
void BeginTheAquagoldArrow(World::ModelEffect& effect, WorldState& world, CrtRandom& random)
{
    constexpr std::int32_t FirstLife = 30;
    constexpr std::int32_t AquagoldLife = 40;
    constexpr glm::vec3 Muzzle{-10.0f, -60.0f, 135.0f};
    effect.speed = 1.0f;
    effect.life = FirstLife;
    AddRotated(effect.transform.position, Render::Models::OriginalVectorRotate(
                                              Muzzle, Render::Models::OriginalAngleMatrix(effect.transform.angles)));
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

// 0x518cf0(tipo, o+0x10, o+0x1c, o+0xf4, subtipo, o, o+0x8a, lugar, bandera, 0, 0), como lo llama 0x53d9a0, y el caso
// del tipo. Lleno, nada y sin rand().
Core::Expected<void, std::string> CreateTheArrow(std::int16_t type, ActorState& owner, std::int32_t subtype,
                                                 std::int32_t skillIndex, std::uint8_t flag,
                                                 const CharacterStepContext& context)
{
    World::ModelEffects& effects = context.world.Map().effects;
    const World::ModelEffectArguments arguments{.type = type,
                                                .position = owner.position,
                                                .angles = owner.angles,
                                                .light = owner.light,
                                                .subtype = subtype,
                                                .owner = &owner,
                                                .key = owner.word8a,
                                                .byte89 = static_cast<std::uint8_t>(skillIndex & 0xff),
                                                .byte88 = flag,
                                                .byte1bd = 0,
                                                .scale = 0.0f};
    const auto slot = effects.Begin(arguments);
    if (!slot.has_value())
        return {};
    World::ModelEffect& effect = effects.Effects()[*slot];
    switch (type)
    {
    case AquagoldArrow:
        BeginTheAquagoldArrow(effect, context.world, context.random);
        return {};
    case SaintArrow:
        // 0x51a1e6..0x51a24a: vida 30, +0x64 en −2, escala 1 y la z 130 más antes del joint.
        return Core::Unexpected(std::string("0x51a24a: el joint 0x618/5 de la flecha 0xf7 (0x530860), sin portar "
                                            "(CL11)"));
    case CelestialArrow:
        return Core::Unexpected(std::string("0x51a405: los cuatro joints 0x60f/0xb de la flecha 0xf8 (0x530860), sin "
                                            "portar (CL11)"));
    case ArchangelArrow:
        return Core::Unexpected(std::string("0x51a173: el joint 0x618/5 de la flecha 0x10e (0x530860), sin portar "
                                            "(CL11)"));
    case ReignArrow:
        // 0x51be81..0x51be8b: con un subtipo que no es 0 ni 2, sale por 0x51db73 con solo el prefijo.
        if (subtype != 0 && subtype != 2)
            return {};
        return Core::Unexpected(std::string("0x51bf35: el joint 0x618/5 de la flecha 0x111 (0x530860), sin portar "
                                            "(CL11)"));
    default:
        return BeginTheArrow(effect, context.world, context.random);
    }
}

// 0x53d9a0(c, o, blanco, lugar, bandera, skill).
Core::Expected<void, std::string> ShootTheArrows(ActorState& actor, std::size_t slot, std::int32_t skillIndex,
                                                 std::uint8_t flag, std::uint8_t skill,
                                                 const CharacterStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x53d9ac..0x53d9cf: las de +0x2b8 y +0x2d8 (MOVSX); con el héroe ([0x743a170]), 0x53d9d5..0x53de8a: las del
    // registro propio (+0xb6c y +0xbb4, MOVSX) más 0x212, con el registro abierto y cerrado en línea.
    std::int32_t right = ToInt16(actor.hands[0].model);
    std::int32_t left = ToInt16(actor.hands[1].model);
    if (&actor == &context.world.Actors()[runtime.hero])
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
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
    constexpr std::int32_t PiercingSound = 0x55;
    constexpr std::int32_t ArrowRainSound = 0x82;
    std::int32_t subtype = 0;
    if (skill == PiercingSkill)
    {
        subtype = 2;
        if (context.soundRequest)
            context.soundRequest(PiercingSound);
    }
    runtime.movement.lastShotSkill = skill;
    if (skill == ArrowRainSkill && context.soundRequest)
        context.soundRequest(ArrowRainSound);
    constexpr std::uint8_t MarkedFlag = 2;
    constexpr std::int32_t MarkedSubtype = 0x63;
    if (flag == MarkedFlag)
        subtype = MarkedSubtype;
    // 0x53def9..0x53df2a: con la skill 0x2e, solo el efecto 0x13a (con el blanco y el lugar) y sale.
    constexpr std::uint8_t BlastSkill = 0x2e;
    if (skill == BlastSkill)
        return Core::Unexpected(
            std::format("0x53df2a: el efecto 0x13a de la skill 0x2e del lugar {}, del hito 4", slot));
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
bool HitsSomeone(const World::ModelEffect& effect, WorldState& world, float radius, std::uint8_t kind)
{
    constexpr std::uint8_t HeroKind = 2;
    const auto actors = world.Actors();
    const ActorState* hero = &actors[world.Runtime().hero];
    for (const ActorState& actor : actors)
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
        if (&actor == hero && kind != HeroKind)
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
    constexpr int Sparks = 20;
    constexpr int PitchSpan = 60, PitchBase = 150, YawSpan = 30;
    constexpr double Lift = 80.0; // 0x6b9ffc
    if (context.particles == nullptr)
        return Core::Unexpected(std::string("0x51fe9e: falta el pool de partículas de 0x51fe10"));
    const glm::vec3 point{position.x, position.y, F32(D(position.z) + Lift)};
    glm::vec3 angles{};
    for (int spark = 0; spark < Sparks; ++spark)
    {
        angles.x = static_cast<float>((context.random.Next() % PitchSpan) + PitchBase);
        angles.y = 0.0f;
        angles.z = static_cast<float>(context.random.Next() % YawSpan);
        const auto created = context.particles->CreateChecked(World::FairySparkParticle, point, angles, glm::vec3(1.0f),
                                                              2, 1.0f, context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x51fe9e: la chispa 0x5c5/2 de 0x51fe10 falla con error {}, sin "
                                                "portar (CL11)",
                                                static_cast<unsigned>(created.Error())));
    }
    const glm::vec3 dim(std::bit_cast<float>(0x3f333333u));
    const auto created = context.particles->CreateChecked(World::ExplosionParticle, point, angles, dim, 0, 1.0f,
                                                          context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(
            std::format("0x51fee0: la 0x5ed/0 de 0x51fe10 falla con error {}", static_cast<unsigned>(created.Error())));
    return {};
}

// 0x520170(efecto): el choque de la flecha.
Core::Expected<void, std::string> HitWithTheArrow(World::ModelEffect& effect, const ArrowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x520179..0x5201d2: con el asedio (0x4294b0), el bit 0x100 de la celda (0x5f82c0) la frena y le deja media vida.
    if (runtime.siegeActive)
    {
        return Core::Unexpected(std::format("0x52018d: la flecha {:#x} con el asedio prendido (el atributo de "
                                            "0x5f82c0), sin portar (CL9.5)",
                                            effect.type));
    }
    // 0x5201d3..0x5201e7: con +0x88 (la bandera de 0x53d9a0, o la 0xe2 del subtipo 2) y sin el subtipo 0x63, rand() &
    // 1, el dueño héroe, la skill del registro propio y 0x517a20.
    constexpr std::int32_t MarkedSubtype = 0x63;
    if (effect.state != 0 && effect.subtype != MarkedSubtype)
    {
        return Core::Unexpected(
            std::format("0x5201ed: la flecha {:#x} con +0x88 en {} (0x517a20), del hito 4", effect.type, effect.state));
    }
    // 0x5205ae..0x5205c7: 0x517850(efecto, 100, +0x8a, 0).
    constexpr float HitRadius = 100.0f;
    if (!HitsSomeone(effect, context.world, HitRadius, 0))
        return {};
    // 0x5205cd..0x5207d1: el registro propio abierto y cerrado en línea para leer la skill del lugar +0x89.
    std::uint8_t skill = 0;
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
        skill = OwnSkillByte(context.world.OwnCharacter(), effect.byte89);
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
            return Core::Unexpected(std::string("0x52083f: el joint 0x60f/6 de la flecha de la skill 0x34 "
                                                "(0x530860), sin portar (CL11)"));
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
    const auto whole = World::OriginalFtolLow(D(worldClock));
    if (!whole.has_value())
        return Core::Unexpected(std::format("{}: el reloj del mundo no cabe en el ftol", address));
    const auto turned = negated ? std::bit_cast<std::int32_t>(0u - std::bit_cast<std::uint32_t>(*whole)) : *whole;
    constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    return F32(static_cast<double>(turned) * Tenth);
}

// Los dos sprites que las flechas 0xe1, 0xe2 y 0xe3 sueltan en su lugar (0x5491e0(textura, posición, escala, luz,
// efecto, ±ftol(reloj) · 0,1f, 0)).
Core::Expected<void, std::string> TwoSprites(const World::ModelEffect& effect, std::uint16_t texture,
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

// 0x5f9780(x, y, (luz · 0,6f; luz · 0,8f; luz · 0,8f), 2, 0x7b4ccc4), la de las flechas 0xe1 y 0xe3.
Core::Expected<void, std::string> GreenLight(const World::ModelEffect& effect, float luminosity,
                                             const ArrowStepContext& context, std::string_view address)
{
    const double six = static_cast<double>(std::bit_cast<float>(0x3f19999au));
    const double eight = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
    const glm::vec3 color{F32(D(luminosity) * six), F32(D(luminosity) * eight), F32(D(luminosity) * eight)};
    return World::AddDynamicLight(context.lighting, effect.transform.position, color, 2, address);
}

// 0x52406c..0x52410b: la flecha 0xdb.
Core::Expected<void, std::string> StepBowArrow(World::ModelEffect& effect, float luminosity,
                                               const ArrowStepContext& context)
{
    const double eight = static_cast<double>(std::bit_cast<float>(0x3f4ccccdu));
    const double half = 0.5;
    const double fifth = static_cast<double>(std::bit_cast<float>(0x3e4ccccdu));
    const glm::vec3 color{F32(D(luminosity) * eight), F32(D(luminosity) * half), F32(D(luminosity) * fifth)};
    if (auto lit = World::AddDynamicLight(context.lighting, effect.transform.position, color, 2, "0x5240a5");
        !lit.HasValue())
        return lit;
    // 0x5240aa..0x5240bc: los subtipos 3 y 4 son de las skills.
    if (effect.subtype == 3 || effect.subtype == 4)
    {
        return Core::Unexpected(
            std::format("0x5240b0: el paso de la flecha 0xdb con el subtipo {}, del hito 4", effect.subtype));
    }
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
        context.particles->CreateChecked(World::FireParticle, effect.transform.position, effect.transform.angles, fire,
                                         0, 1.0f, context.worldClock, context.random);
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x524127: el fuego 0x5d9/0 de la flecha falla con error {}",
                                            static_cast<unsigned>(created.Error())));
    return HitWithTheArrow(effect, context);
}

// 0x52697e..0x526aea: la flecha 0xe1 (salvo el subtipo 1, 0x5267dc).
Core::Expected<void, std::string> StepNatureArrow(World::ModelEffect& effect, float luminosity,
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
Core::Expected<void, std::string> StepBluewingArrow(World::ModelEffect& effect, float luminosity,
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
            context.particles->CreateChecked(World::DropParticle, point, effect.transform.angles, effect.light, 1, 1.0f,
                                             context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x526d5f: la 0x607/1 de la flecha 0xe3 falla con error {} (CL11)",
                                                static_cast<unsigned>(created.Error())));
    }
    // 0x526d74..0x526d88: 0x53fe70(0x5f2, posición, ángulos, +0xf4, 0, 1,0f, 0).
    const auto smoke =
        context.particles->CreateChecked(World::BreathParticle, effect.transform.position, effect.transform.angles,
                                         effect.light, 0, 1.0f, context.worldClock, context.random);
    if (!smoke.HasValue())
        return Core::Unexpected(std::format("0x526d88: la 0x5f2/0 de la flecha 0xe3 falla con error {}",
                                            static_cast<unsigned>(smoke.Error())));
    if (auto lit = GreenLight(effect, luminosity, context, "0x526dba"); !lit.HasValue())
        return lit;
    return HitWithTheArrow(effect, context);
}

// 0x524237..0x524356: la flecha 0xe2, hasta el final de sus chispas.
Core::Expected<void, std::string> StepAquagoldArrow(World::ModelEffect& effect, const ArrowStepContext& context)
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
            context.particles->CreateChecked(World::DropParticle, point, effect.transform.angles, effect.light, 1, 1.0f,
                                             context.worldClock, context.random);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x52434e: la 0x607/1 de la flecha 0xe2 falla con error {} (CL11)",
                                                static_cast<unsigned>(created.Error())));
    }
    // 0x52435d..0x524448: la caída sobre el terreno (0x5f93c0), 0x51fe10 y 0x517a20 en la última vida y la luz.
    return Core::Unexpected(std::string("0x52435d: la caída de la flecha 0xe2 sobre el terreno, sin portar"));
}

// 0x526aef..0x526c51: la flecha 0x11c (su alta corta antes, en 0x51c713).
Core::Expected<void, std::string> StepTwentiethArrow(World::ModelEffect& effect, const ArrowStepContext& context)
{
    constexpr double Turn = 60.0; // 0x6b86cc
    effect.transform.angles.y = F32(D(effect.transform.angles.y) + Turn);
    if (effect.life % 2 == 0)
    {
        static_cast<void>(ScatteredPoint(effect.transform.position, context.random, {31, 63, 31}, {16, 32, 16}));
        return Core::Unexpected(std::string("0x526bc2: la 0x60f/5 de la flecha 0x11c (0x53fe70), sin portar (CL11)"));
    }
    return Core::Unexpected(std::string("0x526c05: el efecto 0x114 de la flecha 0x11c (0x518cf0), sin portar"));
}
} // namespace

bool IsArrowEffect(std::int16_t type)
{
    return (type >= BowArrow && type <= BluewingArrow) || (type >= SaintArrow && type <= ViperArrow) ||
           type == ArchangelArrow || type == ReignArrow || type == TwentiethArrow;
}

Core::Expected<void, std::string> FireTheArrow(ActorState& actor, std::size_t slot, std::int32_t skillIndex,
                                               std::uint8_t flag, std::uint8_t skill,
                                               const CharacterStepContext& context)
{
    // 0x53e2bb..0x53e2ce: las skills 0x34, 0x33 y 0x2e van a 0x53e741.
    constexpr std::array<std::uint8_t, 3> SkillShots{0x34, 0x33, 0x2e};
    if (std::ranges::find(SkillShots, skill) != SkillShots.end())
    {
        return Core::Unexpected(std::format("0x53e2be: el tiro de la skill {:#x} del lugar {} (0x53e741, con +0xf08 "
                                            "del registro propio), del hito 4",
                                            skill, slot));
    }
    // 0x53e2d4..0x53e2da: con la bandera en 1 (la skill 0x18 de 0x502b12), las flechas de +0x2b8 0xa24 y 0xa25 o tres
    // con +0x24 corrido (0x53e2e0..0x53e611).
    if (flag == 1)
    {
        return Core::Unexpected(
            std::format("0x53e2da: el tiro de la skill 0x18 sin blanco del lugar {}, del hito 4", slot));
    }
    // 0x53e616..0x53e62d.
    if (auto shot = ShootTheArrows(actor, slot, skillIndex, flag, skill, context); !shot.HasValue())
        return shot;
    // 0x53e630..0x53e73a: con 0xa25 en +0x2b8, el punto del hueso de +0x2bd (0x4ec110) y quince giros rand() % 360,
    // con un rand() par el joint 0x617/3 y siempre la 0x5c5/0.
    constexpr std::uint16_t ReignCrossbow = 0xa25;
    if (actor.hands[0].model == ReignCrossbow)
    {
        return Core::Unexpected(std::format("0x53e642: el brillo de la 0xa25 del lugar {} (el hueso de la mano con "
                                            "0x4ec110, quince 0x617/3 y 0x5c5/0), sin portar",
                                            slot));
    }
    return {};
}

Core::Expected<void, std::string> StepArrow(World::ModelEffect& effect, const Render::Models::Skeleton* skeleton,
                                            const ArrowStepContext& context)
{
    if (!effect.alive)
        return {};
    if (!IsArrowEffect(effect.type))
        return Core::Unexpected(std::format("0x5208a0: el efecto {:#x} no es una flecha", effect.type));
    // 0x5208c5: el primer rand() de todo efecto vivo, la luz local.
    const float luminosity = World::StepLuminosity(context.random.Next(), effect.life);
    Core::Expected<void, std::string> stepped;
    switch (effect.type)
    {
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
        if (effect.subtype == 1)
            return Core::Unexpected(std::string("0x5267dc: el paso de la flecha 0xe1/1, del hito 4"));
        stepped = StepNatureArrow(effect, luminosity, context);
        break;
    case BluewingArrow:
        stepped = StepBluewingArrow(effect, luminosity, context);
        break;
    case AquagoldArrow:
        stepped = StepAquagoldArrow(effect, context);
        break;
    case TwentiethArrow:
        stepped = StepTwentiethArrow(effect, context);
        break;
    default:
        // 0x523ed1 (0xf7), 0x523f33 (0xf8), 0x5241f6 (0xf9), 0x523de1 (0x10e) y 0x52593c (0x111): sus altas cortan.
        return Core::Unexpected(std::format("0x5208a0: el paso de la flecha {:#x}, sin portar (su alta corta en el "
                                            "joint)",
                                            effect.type));
    }
    if (!stepped.HasValue())
        return stepped;
    // 0x52cb98: la cola no mira el byte vivo; la flecha que el choque apagó también se anima, se mueve y pierde una
    // vida.
    const bool alive = effect.alive;
    effect.alive = true;
    World::StepModelEffect(effect, skeleton);
    if (!alive)
        effect.alive = false;
    return {};
}
} // namespace Client::Scenes
