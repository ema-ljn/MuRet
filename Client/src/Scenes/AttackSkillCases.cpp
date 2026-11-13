#include "Scenes/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorFlags.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

// Los casos de habilidad de 0x4fd5f0 (0x4fdeeb, 0x504372 y 0x5042ce), cotejados con el PE c32bbd5f: 06, "El paso del
// ataque".
namespace Client::Scenes
{
namespace
{
using Render::Models::CrtRandom;
using Render::Models::LogicalAccess;

// 0x517630 y los accesos en línea a +0x38e: una entrada de un byte, un rand() al cerrarla en 0.
constexpr std::uint16_t SkillCloseRandoms = 1;
constexpr std::int16_t NoTarget = -1;
constexpr std::size_t PoolSlots = 400;
// +0x78 & 0x4000: los casos 0x1b, 0x1c y 0x10 con blanco no hacen nada (0x50451c, 0x5046ca, 0x504836).
constexpr std::uint32_t QuietFlag = 0x4000;
// El joint 0x10f, el de los calcos que se van con 0x534f90 (0x4fe3ce, 0x504876).
constexpr std::int32_t FlameJoint = 0x10f;

// 0x517630 (thiscall sobre 0x57630c8): abre +0x38e, lo lee y lo cierra.
std::uint8_t ReadTheSkill(ActorState& actor, CrtRandom& random)
{
    const LogicalAccess access(actor.byte38eAccesses, random, SkillCloseRandoms);
    return actor.byte38e;
}

// 0x59fb80(skill): abre el registro propio ([0x7681eb4], 0x40ad90), busca `skill` entre los 64 bytes de +0x6c de
// [0x7681eb0] y lo cierra (0x40b4d0: 3.848 rand() si queda en 0). Devuelve su lugar, o 0. La misma que la de la flecha
// en AttackStep.cpp.
std::int32_t OwnSkillIndex(const CharacterStepContext& context, std::uint8_t skill)
{
    constexpr std::size_t OwnSkills = 64;
    const LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                            Render::Models::OwnRecordCloseRandoms);
    for (std::size_t index = 0; index < OwnSkills; ++index)
    {
        if (context.world.OwnCharacter().SkillAt(index) == skill)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}

// 0x41ed10(sonido, …): la CL10 decide cómo suena (WorldRuntime::lastSoundId en el juego).
void RequestTheSound(const CharacterStepContext& context, std::int32_t sound)
{
    if (context.soundRequest)
        context.soundRequest(sound);
}

// Lo que crea un caso y MuRet no tiene: un efecto con modelo (0x518cf0), un joint (0x530860) o una partícula (0x53fe70)
// de la CL11, con lo de antes y sus rand().
Core::Unexpected<std::string> CutAt(std::uint32_t address, std::string_view what, std::uint8_t skill, std::size_t slot)
{
    return Core::Unexpected(
        std::format("{:#x}: {} de la habilidad {:#x} del lugar {}, sin portar (CL11)", address, what, skill, slot));
}

// Un tipo de la mano como WORD con signo (MOVSX).
std::int32_t HandOf(const ActorState& actor, std::size_t hand)
{
    return std::bit_cast<std::int16_t>(actor.hands[hand].model);
}

// 0x4fdf2b..0x4fdf57 (y 0x4fdfb3, 0x4fdfe4, 0x4fec30): o+0x8c = el arma de la derecha (+0x2b8, MOVSX) − 0x212 y o+0x8e
// = su nivel (+0x2ba), que leen por su dueño los efectos 0xf4 y 0xfa (Scenes/GameDrawTail.cpp).
void KeepTheWeapon(ActorState& actor)
{
    constexpr std::int32_t FirstWeaponModel = 0x212;
    actor.word8c = static_cast<std::int16_t>(HandOf(actor, 0) - FirstWeaponModel);
    actor.byte8e = actor.hands[0].level;
}

// El blanco de +0x3a4 (MOVSX) de los casos 0x44, 0x46 y 0x47: el lugar del pool sin mirar el rango (0x4fe1ad y sus
// copias). Fuera del pool, MuRet corta: el original leería otra memoria.
Core::Expected<ActorState*, std::string> TargetOf(ActorState& actor, std::size_t slot,
                                                  const CharacterStepContext& context, std::uint32_t address)
{
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    if (target == NoTarget)
        return nullptr;
    auto actors = context.world.Actors();
    if (target < 0 || static_cast<std::size_t>(target) >= actors.size())
    {
        return Core::Unexpected(
            std::format("{:#x}: el blanco {} de +0x3a4 del lugar {} está fuera del pool", address, target, slot));
    }
    return &actors[static_cast<std::size_t>(target)];
}

// 0x4fdf2b..0x4fdfae, la 0x2a: el arma, 0x59fb80(0x2a) y 0x518cf0(0xfa, o+0x10, o+0x1c, o+0xf4, 0, o, o+0x8a, lugar, 0,
// 0, 0), el terremoto con el estado 0 (que su dibujo hace con el arma de o, 0x52d43a); después el sonido 0x95. El
// prefijo de 0x518cf0 deja +0x8a y +0x89 antes del caso (0x51a66d), que no los lee: MuRet los escribe después del alta.
// Con la tabla llena, nada (y el sonido igual).
Core::Expected<void, std::string> ShakeTheGround(ActorState& actor, const CharacterStepContext& context)
{
    constexpr std::uint8_t QuakeSkill = 0x2a;
    constexpr std::int32_t QuakeSound = 0x95;
    KeepTheWeapon(actor);
    const std::int32_t index = OwnSkillIndex(context, QuakeSkill);
    World::ModelEffects& effects = context.world.Map().effects;
    const auto created = effects.CreateEarthquake(
        actor.position, actor.angles, actor.light, 0,
        World::ModelEffectOwner{.type = &actor.type, .effect = nullptr, .angles = &actor.angles},
        context.world.Runtime().ownRecordDepth, context.world.OwnCharacter().bytef04, context.random);
    if (created.has_value())
    {
        World::ModelEffect& quake = effects.Effects()[*created];
        quake.disturbanceState = actor.word8a;
        quake.byte89 = static_cast<std::uint8_t>(index & 0xff);
        quake.byte1bd = 0;
        quake.ownerContext = &actor;
        quake.itemType = &actor.word8c;
        quake.itemLevel = &actor.byte8e;
    }
    RequestTheSound(context, QuakeSound);
    return {};
}

// 0x4fe190..0x4fe24e (la 0x44, el subtipo 0) y 0x4fe442..0x4fe500 (la 0x47, el 1): con un blanco que no es o, el joint
// 0x632 del blanco hacia sí mismo (z + 0x6ba198) y el sonido 0x267.
Core::Expected<void, std::string> BindTheTarget(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context, std::uint32_t read,
                                                std::uint32_t joint)
{
    const auto target = TargetOf(actor, slot, context, read);
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    if (target.Value() == nullptr || target.Value() == &actor)
        return {};
    return CutAt(joint, "el joint 0x632 sobre el blanco (0x530860)", skill, slot);
}

// 0x4fe383..0x4fe43d, la 0x46: con blanco, 0x534f90(0x10f, blanco, 4) y (…, 9), el calco 0x61e/6 sobre él (0x518cf0)
// y el sonido 0x269.
Core::Expected<void, std::string> FreeTheTarget(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context)
{
    const auto target = TargetOf(actor, slot, context, 0x4fe399);
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    if (target.Value() == nullptr)
        return {};
    if (context.joints.empty())
        return Core::Unexpected(std::format("0x4fe3ce: faltan los joints del juego (0x534f90) del lugar {}", slot));
    KillJointsOf(context.joints, FlameJoint, target.Value(), 4);
    KillJointsOf(context.joints, FlameJoint, target.Value(), 9);
    return CutAt(0x4fe424, "el calco 0x61e/6 sobre el blanco (0x518cf0)", skill, slot);
}

// 0x4fe986..0x4feab6 (la 0x40) y 0x502993..0x502aee (la 0x4b): con otra cosa que 0xa21 en la derecha, el 0x117/0 en su
// hueso (+0x2bd, 0x4ec110); con otra cosa que 0xa19 o 0xe12..0x1011 en la izquierda, el 0x117/1 en el suyo (+0x2dd).
// Después, la 0x40 suena (0x1cf) y la 0x4b crea el 0x138 (0x502ae6).
Core::Expected<void, std::string> SwingTheHands(const ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                const CharacterStepContext& context, std::uint32_t right,
                                                std::uint32_t left)
{
    constexpr std::int32_t PlainRight = 0xa21;
    constexpr std::int32_t PlainLeft = 0xa19;
    constexpr std::int32_t FirstShield = 0xe12;
    constexpr std::int32_t PastShields = 0x1012;
    if (HandOf(actor, 0) != PlainRight)
        return CutAt(right, "el efecto 0x117/0 en el hueso de la derecha (0x518cf0)", skill, slot);
    const std::int32_t held = HandOf(actor, 1);
    if (held != PlainLeft && !(held >= FirstShield && held < PastShields))
        return CutAt(left, "el efecto 0x117/1 en el hueso de la izquierda (0x518cf0)", skill, slot);
    constexpr std::uint8_t RaiseSkill = 0x40;
    if (skill != RaiseSkill)
        return CutAt(0x502ae6, "el efecto 0x138 sobre o (0x518cf0)", skill, slot);
    constexpr std::int32_t RaiseSound = 0x1cf;
    RequestTheSound(context, RaiseSound);
    return {};
}

// 0x5023e4..0x502433 (y 0x5024d9..0x502528): la distancia en celdas de o a otro, con dx guardado en float, dy en la
// pila (53 bits) por su copia en float, la raíz (0x6a1920) sobre 100 (0x6b86b4) y el cociente guardado en float.
float CellsBetween(const ActorState& from, const ActorState& to)
{
    constexpr double CellSize = 100.0;
    const auto dx = static_cast<float>(static_cast<double>(from.position.x) - static_cast<double>(to.position.x));
    const double dy = static_cast<double>(from.position.y) - static_cast<double>(to.position.y);
    const auto storedDy = static_cast<float>(dy);
    const double squared = dy * static_cast<double>(storedDy) + static_cast<double>(dx) * static_cast<double>(dx);
    return static_cast<float>(std::sqrt(squared) / CellSize);
}

// 0x502352..0x502949, la 0x4c: el sonido 0x27d y el lazo de los 400 lugares hasta juntar diez blancos (0x502569): un
// monstruo vivo (+0x88 en 2) que no está en la acción 6, o un jugador vivo (+0x88 en 1) fuera de la 0xc3 que 0x5588d0
// deja atacar; de cada uno, la distancia y el alcance de la 0x4c sin dueño (0x5526f0(0x4c, 0), con los rand() de su
// entrada), y entra si el alcance no es menor (FCOMP con C0). Después rand() % 360, que no se usa antes del corte. Con
// +0x434 en −1 o desde 400, nada más; si no, la primera de dos vueltas: el punto de 0x4f5b10(o, 0, −140, 130), sin
// rand(), el ángulo con rand() % 360, 0 y rand() % 360, y el joint 0x14f hacia el blanco de +0x434. La lista de los
// diez la leen los joints de después (0x5026fa).
Core::Expected<void, std::string> StrikeAround(const ActorState& actor, std::size_t slot, std::uint8_t skill,
                                               const CharacterStepContext& context)
{
    constexpr std::int32_t AroundSound = 0x27d;
    constexpr std::size_t MostTargets = 10;
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t MonsterObject = 2;
    constexpr std::uint8_t MonsterFallAction = 6;
    constexpr std::uint8_t PlayerFallAction = 0xc3;
    RequestTheSound(context, AroundSound);
    const HeroSkillContext ranges{.world = context.world, .random = context.random, .skills = context.skills};
    std::size_t gathered = 0;
    for (const ActorState& other : context.world.Actors().first<PoolSlots>())
    {
        bool candidate = other.alive && other.byte88 == MonsterObject && other.animation.action != MonsterFallAction;
        if (!candidate && other.alive && other.byte88 == PlayerObject && other.animation.action != PlayerFallAction)
        {
            const auto attackable = IsAttackableActor(context.world, other, context.controlDown);
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            candidate = attackable.Value();
        }
        if (candidate)
        {
            const float cells = CellsBetween(actor, other);
            const auto range = SkillRangeOf(skill, ranges);
            if (!range.HasValue())
                return Core::Unexpected(std::format("0x50243d: {} (la 0x4c del lugar {})", range.Error(), slot));
            if (range.Value() >= cells)
                ++gathered;
        }
        if (gathered >= MostTargets)
            break;
    }
    static_cast<void>(context.random.Next());
    const std::int32_t target = actor.skillTarget4c;
    if (target == NoTarget || target >= static_cast<std::int32_t>(PoolSlots))
        return {};
    if (target < 0)
    {
        return Core::Unexpected(
            std::format("0x5025b8: el blanco {} de +0x434 del lugar {} está fuera del pool", target, slot));
    }
    static_cast<void>(context.random.Next());
    static_cast<void>(context.random.Next());
    return CutAt(0x5026a4, "el primero de los cuatro joints 0x14f hacia el blanco de +0x434 (0x530860)", skill, slot);
}
} // namespace

Core::Expected<void, std::string> StepTheHeadSkill(ActorState& actor, std::size_t slot, std::uint8_t skill,
                                                   const CharacterStepContext& context)
{
    switch (skill)
    {
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
        // 0x4fdef2: 0x518cf0(0x61e, o+0x10, o+0x1c, o+0xf4, 3, o, −1, 0, 0, 0, 0).
        return CutAt(0x4fdf1e, "el calco 0x61e/3 sobre o (0x518cf0)", skill, slot);
    case 0x2a:
        return ShakeTheGround(actor, context);
    case 0x37:
    case 0x38:
        // 0x4fdfb3 y 0x4fdfe4: el arma, y nada más.
        KeepTheWeapon(actor);
        return {};
    case 0x30:
        // 0x4fe015: treinta y seis vueltas; la primera, 0x530860(0x613, …, 2, o, 60, …) sin rand() antes.
        return CutAt(0x4fe0e4, "el primero de los 36 joints 0x613/2 (0x530860)", skill, slot);
    case 0x43:
        return CutAt(0x4fe172, "el joint 0x632/7 sobre o (0x530860)", skill, slot);
    case 0x44:
        return BindTheTarget(actor, slot, skill, context, 0x4fe1a6, 0x4fe235);
    case 0x45:
        return CutAt(0x4fe322, "el primero de los 36 joints 0x613/0x15 (0x530860)", skill, slot);
    case 0x46:
        return FreeTheTarget(actor, slot, skill, context);
    case 0x47:
        return BindTheTarget(actor, slot, skill, context, 0x4fe458, 0x4fe4e7);
    case 0x48:
        // 0x4fe53b: +0x78 de o en 0, antes de los seis joints 0x10f.
        actor.flags78 = 0;
        return CutAt(0x4fe590, "el primero de los seis joints 0x10f (el /5, 0x530860)", skill, slot);
    case 0x35:
        // 0x4fe795: los puntos con 0x6a1a90 y 0x6a19e0 (sin rand()) y cuatro joints 0x619.
        return CutAt(0x4fe856, "el primero de los cuatro joints 0x619/0xa (0x530860)", skill, slot);
    case 0x40:
        return SwingTheHands(actor, slot, skill, context, 0x4fea10, 0x4fea9b);
    case 0x3f:
        return CutAt(0x4feae8, "el efecto 0xcb/2 sobre o (0x518cf0)", skill, slot);
    case 0x41:
        // 0x4feb3b: el punto de 0x4f5b10(o, 0, −90, −50), más alto en las acciones 0x51 y 0x5b, sin rand().
        return CutAt(0x4febbb, "el efecto 0x62c (0x518cf0)", skill, slot);
    case 0x31:
        return CutAt(0x4fec15, "el efecto 0x6ac sobre o (0x518cf0)", skill, slot);
    case 0x29:
        // 0x4fec30: el arma y 0x59fb80(0x29) antes del 0xf3.
        KeepTheWeapon(actor);
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x4fec9a, "el efecto 0xf3 sobre o (0x518cf0)", skill, slot);
    case 0x0a:
        // 0x4fecb5: 0x59fb80(0xa) antes del 0xcb; lo de Kalima de después (0x4fed2d) no se alcanza.
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x4fecf3, "el efecto 0xcb sobre o (0x518cf0)", skill, slot);
    case 0x28:
        return CutAt(0x4ff4ef, "el efecto 0xcb/1 sobre o (0x518cf0)", skill, slot);
    case 0x26:
        // 0x4ff51c: el punto de la celda +0x39c/+0x39d con la altura de 0x5f93c0 (sin rand()).
        return CutAt(0x4ff5b2, "el primer efecto 0xc2/6 (0x518cf0)", skill, slot);
    case 0x27:
        // 0x4ffcbe: la celda con su altura y la primera de diez vueltas: rand() % 50 · 0 en la z, y el 0x108/1.
        static_cast<void>(context.random.Next());
        return CutAt(0x4ffdaf, "el primero de los diez efectos 0x108 (0x518cf0)", skill, slot);
    case 0x05:
        // 0x500496: la celda con su altura y 0x59fb80(5); el chorro 0x5de con o de dueño, cuyo paso con dueño
        // (0x52ae2a) no está portado.
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x500531, "el chorro 0x5de/0 con o de dueño (0x518cf0)", skill, slot);
    case 0x08:
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x500c49, "el efecto 0xcf sobre o (0x518cf0)", skill, slot);
    case 0x0c:
        // 0x500c64: el punto de 0x4f5b10(o, −20, −90, 100), sin rand(), y 0x59fb80(0xc).
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x500cb9, "el efecto 0x5e8 (0x518cf0)", skill, slot);
    case 0x0d:
        // 0x500cd4: la celda con su altura y 0x59fb80(0xd).
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x500d7b, "el primer efecto 0xf5 (0x518cf0)", skill, slot);
    case 0x0e:
        return CutAt(0x501494, "las piedras de 0x51fff0(o+0x10, 0)", skill, slot);
    case 0x09:
        // 0x501bab: la primera de cuatro vueltas, con 0x59fb80(9).
        static_cast<void>(OwnSkillIndex(context, skill));
        return CutAt(0x501c67, "el primero de los joints 0x613/0 (0x530860)", skill, slot);
    case 0x4c:
        return StrikeAround(actor, slot, skill, context);
    case 0x2c:
        return CutAt(0x502975, "el efecto 0x13b sobre o (0x518cf0)", skill, slot);
    case 0x4b:
        return SwingTheHands(actor, slot, skill, context, 0x502a23, 0x502ab4);
    default:
        // 0x502aff, como el 0.
        return {};
    }
}

Core::Expected<void, std::string> StepTheTargetSkill(ActorState& actor, ActorState& victim, std::size_t slot,
                                                     std::uint8_t skill, const CharacterStepContext& context)
{
    switch (skill)
    {
    case 0x18:
    {
        // 0x504379..0x5043aa: 0x53e2b0(c, o, 0, 0x59fb80(+0x38e), 1, 0), la tanda sin blanco; sigue en la 0x34
        // (0x5043b2).
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        if (auto fired = FireTheArrow(actor, nullptr, slot, index, 1, 0, context); !fired.HasValue())
            return fired;
    }
        [[fallthrough]];
    case 0x34:
    case 0x33:
    {
        // 0x5043b2..0x5043fb y 0x504403..0x50444b: la primera lectura es la skill de 0x53e2b0 y la segunda va a
        // 0x59fb80; 0x53e2b0(c, o, 0, lugar, 0, skill).
        const std::uint8_t shot = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        return FireTheArrow(actor, nullptr, slot, index, 0, shot, context);
    }
    case 0x2e:
    {
        // 0x504453..0x5044b4: lo mismo con el blanco, 0x53e2b0(c, o, blanco, lugar, 0, skill), y el sonido 0x26e.
        constexpr std::int32_t BlastSound = 0x26e;
        const std::uint8_t shot = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, ReadTheSkill(actor, context.random));
        if (auto fired = FireTheArrow(actor, &victim, slot, index, 0, shot, context); !fired.HasValue())
            return fired;
        RequestTheSound(context, BlastSound);
        return {};
    }
    case 0x1a:
        // 0x5044b9..0x5044f1: 0x518cf0(0x61e, blanco+0x10, blanco+0x1c, blanco+0xf4, 1, blanco, −1, 0, 0, 0, 0), el
        // calco que suelta los 619/1 hacia su dueño (GameCharacterEffects.h). Con la tabla llena, nada.
        static_cast<void>(
            context.world.Map().effects.CreatePetSummon(victim.position, victim.angles, victim.light, &victim));
        return {};
    case 0x1b:
        // 0x5044fe: con +0x38d y sin 0x4000 en o+0x78, el calco 0x61e/2 sobre el blanco.
        if (actor.skillSucceeded == 0 || (actor.flags78 & QuietFlag) != 0)
            return {};
        return CutAt(0x504561, "el calco 0x61e/2 sobre el blanco (0x518cf0)", skill, slot);
    case 0x1c:
        if ((actor.flags78 & QuietFlag) != 0)
            return {};
        return CutAt(0x50470f, "el calco 0x61e/3 sobre el blanco (0x518cf0)", skill, slot);
    case 0x07:
        return CutAt(0x504768, "el efecto 0xc1 sobre el blanco (0x518cf0)", skill, slot);
    case 0x10:
    {
        // 0x504808..0x5048e5: el 0x189 prende 0x100 en su +0x78 y sale; con 0x4000, nada; si no, 0x100 en +0x78 del
        // blanco, el sonido 0x84, 0x534f90(0x10f, blanco, 0) y cinco joints 0x10f/0.
        constexpr std::uint16_t SelfFlagType = 0x189;
        constexpr std::uint32_t ShieldFlag = 0x100;
        constexpr std::int32_t ShieldSound = 0x84;
        if (actor.type == SelfFlagType)
        {
            actor.flags78 |= ShieldFlag;
            return {};
        }
        if ((actor.flags78 & QuietFlag) != 0)
            return {};
        victim.flags78 |= ShieldFlag;
        RequestTheSound(context, ShieldSound);
        if (context.joints.empty())
            return Core::Unexpected(std::format("0x504876: faltan los joints del juego (0x534f90) del lugar {}", slot));
        KillJointsOf(context.joints, FlameJoint, &victim, 0);
        return CutAt(0x5048db, "el primero de los cinco joints 0x10f/0 sobre el blanco (0x530860)", skill, slot);
    }
    case 0x01:
    {
        // 0x5048ea: el 0x20a crea el 0xc3; los demás, diez partículas 0x5f2/1.
        constexpr std::uint16_t PlayerType = 0x20a;
        if (actor.type == PlayerType)
            return CutAt(0x504926, "el efecto 0xc3 sobre el blanco (0x518cf0)", skill, slot);
        return CutAt(0x50497e, "la primera de las diez partículas 0x5f2/1 (0x53fe70)", skill, slot);
    }
    case 0x02:
        return CutAt(0x5049ef, "el efecto 0xc2 sobre el blanco (0x518cf0)", skill, slot);
    case 0x2d:
        return CutAt(0x504a3a, "el primero de los tres efectos 0x139 (0x518cf0)", skill, slot);
    case 0x49:
        return CutAt(0x504b33, "el joint 0x61c/4 (0x530860)", skill, slot);
    case 0x4a:
        return CutAt(0x504b80, "el efecto 0x116/2 (0x518cf0)", skill, slot);
    case 0x04:
        return CutAt(0x504bce, "el efecto 0xc2/1 (0x518cf0)", skill, slot);
    case 0x0b:
    {
        // 0x504be9: el 0x164 crea dos 0xce más, el primero con 10 más en el rumbo.
        constexpr std::uint16_t TripleType = 0x164;
        if (actor.type == TripleType)
            return CutAt(0x504c38, "el primero de los tres efectos 0xce (0x518cf0)", skill, slot);
        return CutAt(0x504cc0, "el efecto 0xce (0x518cf0)", skill, slot);
    }
    case 0x3c:
        return CutAt(0x504d05, "el primero de los efectos 0x114/1 (0x518cf0)", skill, slot);
    case 0x3d:
        // 0x504d87: el punto del primer hueso de o (0x4ec110), sin rand().
        return CutAt(0x504e40, "el primero de los efectos 0x116 (0x518cf0)", skill, slot);
    case 0x11:
    {
        // 0x504f56..0x504fd0: los +0x376 de las tablas de 0x50561a y 0x505672 que van a 0x504fd7, y el 0x76, no
        // hacen nada; los demás (los jugadores, con 0xffff) crean el 0xd7 (el tipo 0x15e) o el 0x5ca.
        constexpr std::array<std::uint16_t, 31> QuietKinds{
            0x25, 0x2e, 0x3d, 0x42, 0x45, 0x46, 0x49, 0x4b, 0x4d, 0x57, 0x59, 0x5d, 0x5f, 0x63,  0x70, 0x74,
            0x76, 0x7a, 0x7c, 0x80, 0x82, 0x8d, 0x8f, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0x125, 0x12f};
        constexpr std::uint16_t RayType = 0x15e;
        if (std::ranges::find(QuietKinds, actor.word376) != QuietKinds.end())
            return {};
        if (actor.type == RayType)
            return CutAt(0x50501a, "el efecto 0xd7 (0x518cf0)", skill, slot);
        return CutAt(0x505053, "el efecto 0x5ca (0x518cf0)", skill, slot);
    }
    default:
        // 0x505069: el 3 y los demás.
        return {};
    }
}

Core::Expected<void, std::string> SparkTheTarget(const ActorState& actor, const ActorState& victim, std::size_t slot)
{
    // 0x52e740: la partícula 0x5c6/0 en el blanco con sus ángulos, y veinte veces dos rand() y la 0x5c5/0; la primera
    // ya es de la CL11, sin rand() antes.
    return Core::Unexpected(
        std::format("0x52e77b: la partícula 0x5c6/0 de 0x52e740 sobre el blanco (tipo {:#x}) con la "
                    "acción {:#x} del lugar {}, sin portar (CL11)",
                    victim.type, actor.animation.action, slot));
}
} // namespace Client::Scenes
