#include "Scenes/AttackStep.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ArrowEffects.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/MonsterAttackStep.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/IFishStepServices.h"
#include "World/ItemState.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

// 0x4fdb9a..0x50539e con 0x4f97b0 y 0x4f5b70, cotejados con el PE c32bbd5f: 06, "El paso del ataque".
namespace Client::Scenes
{
namespace
{
using Render::Models::CrtRandom;
using Render::Models::LogicalAccess;

constexpr std::uint16_t PlayerType = Data::Models::PlayerSkeletonModel;
// 0x517630 y los accesos en línea a +0x38e: una entrada de un byte, un rand() al cerrarla en 0.
constexpr std::uint16_t SkillCloseRandoms = 1;
// 0x4fdbf1: el valor al que vuelve +0x381 cuando corre la habilidad, y el 15 de 0x4f97b0 (0x4fb0df).
constexpr std::uint8_t AttackDone = 15;
constexpr std::int16_t NoTarget = -1;

// 0x4f9899: los casos del switch de 0x4f97b0 (habilidad − 0x28, la tabla de 0x4fb10c); los demás van a 0x4fab5d.
constexpr std::array<std::uint8_t, 24> FrameSkills{0x28, 0x2b, 0x2c, 0x2f, 0x30, 0x31, 0x34, 0x35,
                                                   0x37, 0x38, 0x39, 0x3c, 0x3d, 0x3e, 0x3f, 0x41,
                                                   0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a};
// 0x4fdeeb: los casos del switch de la cabeza (habilidad − 5, los índices de 0x505530); los demás van a 0x502aff.
constexpr std::array<std::uint8_t, 36> HeadSkills{
    0x05, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x0e, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x26, 0x27, 0x28, 0x29,
    0x2a, 0x2c, 0x30, 0x31, 0x35, 0x37, 0x38, 0x3f, 0x40, 0x41, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x4b, 0x4c};
// 0x504372: los casos del primer switch con blanco (habilidad − 1, los índices de 0x5055c8); el 3 y los demás van a
// 0x505069.
constexpr std::array<std::uint8_t, 19> TargetSkills{0x01, 0x02, 0x04, 0x07, 0x0b, 0x10, 0x11, 0x18, 0x1a, 0x1b,
                                                    0x1c, 0x2d, 0x2e, 0x33, 0x34, 0x3c, 0x3d, 0x49, 0x4a};
// 0x5051f1: los casos del segundo (habilidad − 0x10, los índices de 0x505730), que van a 0x50534f sin el sonido.
constexpr std::array<std::uint8_t, 24> QuietSkills{0x10, 0x1a, 0x1b, 0x1c, 0x1e, 0x1f, 0x20, 0x21,
                                                   0x22, 0x23, 0x24, 0x28, 0x2b, 0x2f, 0x30, 0x35,
                                                   0x3f, 0x40, 0x43, 0x44, 0x45, 0x46, 0x48, 0x4b};
// 0x502f00..0x502fb5 y 0x50380e..0x503898: las acciones del 0x20a que tiran (el arco y la ballesta a pie, con y sin
// alas, en Uniria o Dinorant y en Fenrir).
constexpr std::array<std::uint8_t, 8> RangedActions{0x2e, 0x2f, 0x30, 0x31, 0x36, 0x37, 0x5e, 0x61};
// 0x50378c..0x5037be y 0x50406f..0x504092: los tipos que tiran con 0x53e2b0 (el 0x180, solo sin blanco).
constexpr std::array<std::uint16_t, 3> MissileTypes{0x168, 0x175, 0x17a};
constexpr std::uint16_t MissileTypeWithoutTarget = 0x180;

template <typename T, std::size_t N> bool IsOneOf(const std::array<T, N>& values, T value)
{
    return std::ranges::find(values, value) != values.end();
}

// 0x517630 (thiscall sobre 0x57630c8) y sus copias en línea: abre +0x38e, lo lee y lo cierra.
std::uint8_t ReadTheSkill(ActorState& actor, CrtRandom& random)
{
    const LogicalAccess access(actor.byte38eAccesses, random, SkillCloseRandoms);
    return actor.byte38e;
}

// Un tipo de la mano como WORD con signo (MOVSX).
std::int32_t TypeOf(const World::ItemState& item)
{
    return std::bit_cast<std::int16_t>(item.id);
}

// 0x4f97b0(c, o). 0x4f9780(o) (0x4f97bd: el 0x20a con la 0x25 o la 0x26) no saca rand() y solo lo leen los casos.
Core::Expected<void, std::string> StepTheAttackFrames(ActorState& actor, std::size_t slot,
                                                      const CharacterStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x4f97cf..0x4f987a: +0x38e, abierto y cerrado en línea.
    const std::uint8_t skill = ReadTheSkill(actor, context.random);
    // 0x4f987d.
    runtime.skillThreshold = AttackDone;
    if (IsOneOf(FrameSkills, skill))
    {
        return Core::Unexpected(std::format("0x4f9899: la habilidad {:#x} de +0x38e en el lugar {} (0x4f97b0) es del "
                                            "hito 4",
                                            skill, slot));
    }
    // 0x4fab5d..0x4fab7f: pasado el frame 1 (0x6b87d4), el 0x20a con la 0x3e termina (FCOMP: NaN no pasa).
    constexpr float FirstFrame = 1.0f;
    constexpr float StrikeFrame = 5.0f;
    constexpr std::uint8_t ShortAction = 0x3e;
    const float frame = actor.animation.frame;
    const std::uint8_t action = actor.animation.action;
    if (frame >= FirstFrame && actor.type == PlayerType && action == ShortAction)
    {
        actor.byte381 = AttackDone;
        return {};
    }
    // 0x4fab85..0x4fabda: antes del frame 5 (0x6b87f8), o con NaN, sale. Sigue el 0x20a con una acción de 0x22 a
    // 0x97 o un tipo de 0x152 a 0x1c9 con la 3 o la 4.
    if (!(frame >= StrikeFrame))
        return {};
    const bool player = actor.type == PlayerType && action >= 0x22 && action <= 0x97;
    const bool monster = actor.type >= 0x152 && actor.type < 0x1ca && action >= 3 && action <= 4;
    if (!player && !monster)
        return {};
    // 0x4fabe0..0x4fb08b: el registro propio, abierto y cerrado en línea (los 3.848 rand() de 0x40b4d0 si queda en
    // 0), para leer la mano izquierda (+0xbb4, MOVSX, sin el ítem del cursor).
    std::int32_t left = 0;
    {
        const LogicalAccess own(runtime.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
        left = TypeOf(context.world.OwnCharacter().equipment[1]);
    }
    // 0x4fb08c..0x4fb0da: el arco 0x815 en la izquierda del registro propio, sea quien sea el que pega: veinte
    // 0x53fe70(0x5c6, o+0x10, o+0x1c, o+0xf4, 12, 2,0f, 0), con seis rand() cada una (ninguno con el pool lleno).
    constexpr std::int32_t SparkBow = 0x815;
    constexpr int BowSparks = 20;
    constexpr float BowSparkScale = 2.0f;
    if (actor.byte381 >= 1 && left == SparkBow && actor.type == PlayerType)
    {
        if (context.particles == nullptr)
            return Core::Unexpected(
                std::format("0x4fb0d1: falta el pool de partículas de las veinte 0x5c6/12 del lugar {}", slot));
        for (int spark = 0; spark < BowSparks; ++spark)
        {
            const auto created = context.particles->CreateChecked(World::SparkParticle, actor.position, actor.angles,
                                                                  actor.light, World::BowSparkSubtype, BowSparkScale,
                                                                  context.worldClock, context.random);
            if (!created.HasValue())
                return Core::Unexpected(std::format("0x4fb0d1: la 0x5c6/12 del lugar {} falla con error {}", slot,
                                                    static_cast<unsigned>(created.Error())));
        }
    }
    actor.byte381 = AttackDone;
    return {};
}

// 0x4f5b70: lo del ataque que corre mientras sube +0x381.
Core::Expected<void, std::string> StepTheAttackHelpers(ActorState& actor, std::size_t slot,
                                                       const CharacterStepContext& context)
{
    // 0x4f5b97: un rand() sin condición, que no lee nadie.
    static_cast<void>(context.random.Next());
    // Los ayudantes de los mapas, el switch por +0x376 y la cola (MonsterAttackStep.h).
    return StepTheAttackCases(actor, slot, context);
}

// 0x59fb80(skill): abre el registro propio ([0x7681eb4], 0x40ad90), busca `skill` entre los 64 bytes de +0x6c de
// [0x7681eb0] y lo cierra (0x40b4d0: 3.848 rand() si queda en 0). Devuelve su lugar, o 0 si no está. Los otros dos
// argumentos que le empuja el caller quedan en la pila para 0x53e2b0.
std::int32_t OwnSkillIndex(const CharacterStepContext& context, std::uint8_t skill)
{
    constexpr std::size_t OwnSkills = 64;
    WorldRuntime& runtime = context.world.Runtime();
    const LogicalAccess own(runtime.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
    for (std::size_t index = 0; index < OwnSkills; ++index)
    {
        if (context.world.OwnCharacter().SkillAt(index) == skill)
            return static_cast<std::int32_t>(index);
    }
    return 0;
}

// 0x502b12..0x5037d5: sin blanco (+0x3a4 en −1).
Core::Expected<void, std::string> StrikeNothing(ActorState& actor, std::size_t slot,
                                                const CharacterStepContext& context)
{
    // 0x502b12..0x502ef9: +0x38e; con 0x18, la bandera (el quinto argumento de 0x53e2b0) en 1.
    constexpr std::uint8_t FlaggedSkill = 0x18;
    const std::uint8_t flag = ReadTheSkill(actor, context.random) == FlaggedSkill ? 1 : 0;
    const bool shoots = actor.type == PlayerType ? IsOneOf(RangedActions, actor.animation.action) : actor.byte88 == 1;
    if (shoots)
    {
        // 0x502fbe..0x503760: dos lecturas más; la primera es la skill de 0x53e2b0 (0x5031e4) y la segunda va a
        // 0x59fb80 (0x50376a), que da el lugar. 0x50377d: 0x53e2b0(c, o, 0, lugar, bandera, skill).
        const std::uint8_t skill = ReadTheSkill(actor, context.random);
        const std::uint8_t second = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, second);
        if (auto fired = FireTheArrow(actor, slot, index, flag, skill, context); !fired.HasValue())
            return fired;
    }
    // 0x503785..0x5037d0: los tipos que tiran, 0x53e2b0(c, o, 0, 0, 0, 0).
    if (IsOneOf(MissileTypes, actor.type) || actor.type == MissileTypeWithoutTarget)
        return FireTheArrow(actor, slot, 0, 0, 0, context);
    return {};
}

// 0x505224..0x50534a: el sonido de pegar, por las manos del registro propio (sea quien sea el que pega) o el ítem del
// cursor en la suya.
void PlayTheStrikeSound(const CharacterStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x50522f y 0x50534a: 0x4e79a0 y 0x40f370, los 3.848 rand() si el cierre lo deja en 0.
    const LogicalAccess own(runtime.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
    const auto& equipment = context.world.OwnCharacter().equipment;
    const World::ItemState* right = &equipment[0];
    const World::ItemState* left = &equipment[1];
    // 0x505258..0x5052ab: [0x7879fbc] > 0 (JLE) y [0x78ab9b8] == 0x78a9bb0; el lugar [0x78a6758], 0 la derecha y 1
    // la izquierda.
    if (runtime.cursorItemCount > 0 && runtime.cursorGrid == CursorGrid::Inventory)
    {
        if (runtime.slot78a6758 == 0)
            right = &runtime.cursorItem;
        else if (runtime.slot78a6758 == 1)
            left = &runtime.cursorItem;
    }
    // 0x5052b5..0x505337: las dos de 0x800 a 0x9ff, 0x4b; si no, 0x46; más rand() & 3 (con signo, 0x80000003).
    const auto tiro = [](std::int32_t type) { return type >= 0x800 && type < 0xa00; };
    const std::int32_t base = tiro(TypeOf(*right)) && tiro(TypeOf(*left)) ? 0x4b : 0x46;
    const auto sound = static_cast<std::int32_t>(context.random.Next() & 3) + base;
    if (context.soundRequest)
        context.soundRequest(sound);
}

// 0x5040df..0x5041a4: diez veces, tres rand() & 63 (la x y la y del blanco − 32 y su z + 0x5a, cada suma del fild en la
// x87) y 0x53fe70(0x5e4, punto, o+0x1c, [EBP − 0x30], 0, 1,0f, 0). La luz del local la pisa la creación de la 5e4
// (0x542be9); con el pool lleno, la 5e4 no saca sus cuatro rand() y las tres del punto salen igual.
Core::Expected<void, std::string> BleedOnTheTarget(const ActorState& actor, const ActorState& victim,
                                                   std::int16_t target, const CharacterStepContext& context)
{
    constexpr int Drops = 10;
    constexpr int Spread = 63;
    constexpr int Center = 32;
    constexpr int Height = 0x5a;
    const auto add = [](int offset, float base)
    { return static_cast<float>(static_cast<double>(offset) + static_cast<double>(base)); };
    for (int drop = 0; drop < Drops; ++drop)
    {
        const int x = (context.random.Next() & Spread) - Center;
        const int y = (context.random.Next() & Spread) - Center;
        const int z = (context.random.Next() & Spread) + Height;
        const glm::vec3 point{add(x, victim.position.x), add(y, victim.position.y), add(z, victim.position.z)};
        if (context.particles == nullptr)
            return Core::Unexpected(
                std::format("0x50419c: falta el pool de partículas de la sangre del blanco {}", target));
        const auto created = context.particles->CreateChecked(
            World::BloodParticle, point, actor.angles, glm::vec3(1.0f), 0, 1.0f, context.worldClock, context.random, 0);
        if (!created.HasValue())
        {
            return Core::Unexpected(std::format("0x50419c: la sangre 0x5e4 del blanco {} falla con error {}", target,
                                                static_cast<unsigned>(created.Error())));
        }
    }
    return {};
}

// 0x5037dd..0x50534a: con blanco.
Core::Expected<void, std::string> StrikeTheTarget(ActorState& actor, std::size_t slot, std::int16_t target,
                                                  const CharacterStepContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    auto actors = context.world.Actors();
    // 0x5037e0..0x5037f2: el lugar del pool, sin mirar el rango.
    if (target < 0 || static_cast<std::size_t>(target) >= actors.size())
    {
        return Core::Unexpected(
            std::format("0x5037e0: el blanco {} de +0x3a4 del lugar {} está fuera del pool", target, slot));
    }
    const ActorState& victim = actors[static_cast<std::size_t>(target)];
    if (actor.type == PlayerType && IsOneOf(RangedActions, actor.animation.action))
    {
        // 0x5038a1..0x504043: dos lecturas de +0x38e; la primera es la skill de 0x53e2b0 (0x503ac7) y la segunda va a
        // 0x59fb80 (0x504048), que da el lugar. 0x504060: 0x53e2b0(c, o, el blanco, lugar, 0, skill), y sigue.
        const std::uint8_t skill = ReadTheSkill(actor, context.random);
        const std::uint8_t second = ReadTheSkill(actor, context.random);
        const std::int32_t index = OwnSkillIndex(context, second);
        if (auto fired = FireTheArrow(actor, slot, index, 0, skill, context); !fired.HasValue())
            return fired;
    }
    // 0x504068..0x5040a9: los tipos que tiran con blanco, 0x53e2b0(c, o, el blanco, 0, 0, 0).
    if (IsOneOf(MissileTypes, actor.type))
    {
        if (auto fired = FireTheArrow(actor, slot, 0, 0, 0, context); !fired.HasValue())
            return fired;
    }
    // 0x5040b9: el blanco con daño (+0x384, el del último 0x15).
    if (victim.damage != 0)
    {
        constexpr std::uint16_t NoSparksType = 0x159;
        constexpr std::uint16_t SmokeType = 0x18e;
        if (victim.type != NoSparksType)
        {
            if (auto bled = BleedOnTheTarget(actor, victim, target, context); !bled.HasValue())
                return bled;
        }
        if (victim.type == SmokeType)
        {
            // 0x5041bf..0x50426f: cinco rand() & 1; con 0, el punto del blanco con la z en (z + 50) + rand() % 30 y
            // 0x518cf0(0x10b, punto, o+0x1c, o+0xf4, 0, sin dueño, −1, 0, 0, 0, 0) (0x504267, la piedra; 0x51bafb).
            constexpr int Puffs = 5;
            constexpr int HeightSpan = 30;
            constexpr double Lift = 50.0; // 0x6b86b0
            for (int i = 0; i < Puffs; ++i)
            {
                if ((context.random.Next() & 1) != 0)
                    continue;
                const float lifted = static_cast<float>(static_cast<double>(victim.position.z) + Lift);
                const int rise = context.random.Next() % HeightSpan;
                const glm::vec3 point{victim.position.x, victim.position.y,
                                      static_cast<float>(static_cast<double>(rise) + static_cast<double>(lifted))};
                static_cast<void>(context.world.Map().effects.CreateDebris(World::DebrisEffect, point, actor.angles,
                                                                           actor.light, context.random));
            }
            // 0x50427d: 0x41ed10(0x88, 0, 0).
            constexpr std::int32_t SmokeSound = 0x88;
            if (context.soundRequest)
                context.soundRequest(SmokeSound);
        }
        // 0x50428c: 0x42ea00(blanco) solo actúa en el mapa 30 con los tipos 0x19b y 0x19c.
        constexpr std::int32_t CastleMap = 30;
        if (runtime.map == CastleMap && (victim.type == 0x19b || victim.type == 0x19c))
            return Core::Unexpected(
                std::format("0x50428c: 0x42ea00 con el blanco {:#x}, sin portar (hito 13)", victim.type));
    }
    // 0x50429f..0x5042ce: las acciones 0x38 a 0x3c, 0x52e740.
    if (actor.animation.action > 0x37 && actor.animation.action < 0x3d)
    {
        return Core::Unexpected(std::format("0x5042ce: 0x52e740 con la acción {:#x} del lugar {}, sin portar (hito 4)",
                                            actor.animation.action, slot));
    }
    // 0x5042d3..0x50431c: los ángulos con el rumbo de 0x4e93f0 hacia el blanco, que solo leen los casos.
    const std::uint8_t first = ReadTheSkill(actor, context.random);
    if (IsOneOf(TargetSkills, first))
    {
        return Core::Unexpected(std::format("0x504372: la habilidad {:#x} de +0x38e del lugar {} con blanco es del "
                                            "hito 4",
                                            first, slot));
    }
    // 0x505069..0x505107: el punto del blanco, la bandera de la 0x25 y la 0x26 y la luz, que leen los casos de
    // habilidad. Con +0x380 en 2 (el 16 del héroe, mató), 0x530860(0x618, blanco+0x10, blanco+0x10, o+0x1c, 0 y 1, o,
    // 20,0f, −1, 0, 0, −1) (0x505169, 0x5051a4), sin rand().
    constexpr std::uint8_t KillerOnRecord = 2;
    if (actor.byte380 == KillerOnRecord)
    {
        if (context.joints.empty())
        {
            return Core::Unexpected(
                std::format("0x505169: faltan los joints de los 0x618 del lugar {} hacia el blanco {}", slot, target));
        }
        constexpr float KillBeamWidth = 20.0f;
        glm::vec3 from = victim.position;
        glm::vec3 to = victim.position;
        glm::vec3 angles = actor.angles;
        for (const std::int32_t subtype : {0, 1})
        {
            const World::FishEffectRequest request{.type = KillBeamJoint,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = &actor,
                                                   .scale = KillBeamWidth,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
            if (auto created = CreateFaunaJoint(context.joints, request, context.random); !created.HasValue())
                return Core::Unexpected(std::format("{:#x}: {}", subtype == 0 ? 0x505169 : 0x5051a4, created.Error()));
        }
    }
    // 0x5051bb: los casos de QuietSkills no suenan; los demás, salvo +0x376 de 0x44 a 0x4b.
    const std::uint8_t second = ReadTheSkill(actor, context.random);
    if (IsOneOf(QuietSkills, second) || (actor.word376 >= 0x44 && actor.word376 <= 0x4b))
        return {};
    PlayTheStrikeSound(context);
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheAttack(ActorState& actor, std::size_t slot,
                                                const CharacterStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    // 0x4fdb9a..0x4fdbd4.
    if (actor.byte381 != 0)
    {
        if (auto frames = StepTheAttackFrames(actor, slot, context); !frames.HasValue())
            return frames;
        if (auto helpers = StepTheAttackHelpers(actor, slot, context); !helpers.HasValue())
            return helpers;
        ++actor.byte381;
    }
    // 0x4fdbda..0x4fdbeb: JL, con signo.
    if (static_cast<std::int32_t>(actor.byte381) < runtime.skillThreshold)
        return {};
    // 0x4fdbf1..0x4fdc10.
    actor.byte381 = 0;
    const auto key = ChosenKeyOf(context.world, slot, context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    actor.word8a = static_cast<std::int16_t>(key.Value());
    // 0x4fdc17..0x4fdeeb: +0x38e y el switch de la cabeza.
    const std::uint8_t skill = ReadTheSkill(actor, context.random);
    if (IsOneOf(HeadSkills, skill))
    {
        return Core::Unexpected(
            std::format("0x4fdeeb: la habilidad {:#x} de +0x38e del lugar {} es del hito 4", skill, slot));
    }
    // 0x502aff..0x502b0c: +0x3a4 (MOVSX) en −1.
    const auto target = std::bit_cast<std::int16_t>(actor.attackTarget);
    auto struck =
        target == NoTarget ? StrikeNothing(actor, slot, context) : StrikeTheTarget(actor, slot, target, context);
    if (!struck.HasValue())
        return struck;
    // 0x505356..0x505388: 0x40ae60, +0x38e en 0 y 0x40b0a0, una escritura por la tabla.
    {
        const LogicalAccess access(actor.byte38eAccesses, context.random, SkillCloseRandoms);
        actor.byte38e = 0;
    }
    // 0x50538b y 0x505397.
    actor.word382 = 0;
    actor.byte380 = 0;
    return {};
}
} // namespace Client::Scenes
