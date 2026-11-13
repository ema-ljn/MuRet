#include "Scenes/SkillReplies.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Skill.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorEffectFlags.h"
#include "Scenes/ActorFlags.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"
#include "UI/GameUiState.h"
#include "World/ModelEffects.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x5110d0 sin nadie con la clave.
constexpr std::size_t NoActor = 400;
// +0x38e y +0x42e: entradas de un byte de la tabla de protección, un rand() al cerrarlas en 0 (0x40b3f0 y en línea).
constexpr std::uint16_t ActorByteCloseRandoms = 1;
// Las monturas de +0x318 (el modelo del ítem): Uniria, Dinorant, el Dark Horse y Fenrir.
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;
constexpr std::uint16_t DarkHorseModel = 0x1c16;
constexpr std::uint16_t FenrirModel = 0x1c37;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t SummonerLikeClass = 4; // el Dark Lord (+0x1d4 & 7 en 4)
// 0x4cd3a5: el tipo que no suena con la skill 3.
constexpr std::uint16_t SilentSkill3Type = 0x1a5;
// 0x4cd369: con +0x376 en 0x4d no suena el 0x6f.
constexpr std::uint16_t SilentWord376 = 0x4d;
// 0x53d8d0 y 0x53d930: el destello del que se va y del que llega.
constexpr std::int16_t SparkEffect = 0x5c6;

using Protocol::Skill::ActorEffect;
using Protocol::Skill::DurationMagic;
using Protocol::Skill::MagicAttack;
using Protocol::Skill::MagicCancel;
using Protocol::Skill::SkillList;
using Protocol::Skill::SkillTarget;
using Protocol::Skill::SummonLife;

bool Riding(const ActorState& actor, std::uint16_t mount)
{
    return actor.pet.model == mount && actor.safeZone == 0;
}

bool RidingAPegasus(const ActorState& actor)
{
    return actor.pet.model >= UniriaModel && actor.pet.model <= DarkHorseModel && actor.safeZone == 0;
}

bool IsPlayer(const ActorState& actor)
{
    return actor.type == Data::Models::PlayerSkeletonModel;
}

// 0x4ea030 con el modelo del tipo del personaje (+2): sin él en MuRet, corta.
Core::Expected<void, std::string> Act(ActorState& actor, std::uint8_t action, const WorldEntryResources& resources)
{
    const Render::Models::Skeleton* skeleton = resources.models.SkeletonOf(actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: MuRet no tiene el modelo {:#x} del personaje", actor.type));
    Render::Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

// 0x4f4a10, la acción de ataque (Scenes::SetActorAttack, CL9 hito 3).
Core::Expected<void, std::string> AttackAction(ActorState& actor, const WorldEntryResources& resources)
{
    return SetActorAttack(actor, CombatContextOf(resources));
}

// 0x4cd68e y 0x4cdd2e: 0x4f4a10, el frame en 0 y +0x381 en 1 (0x4cde62).
Core::Expected<void, std::string> AttackAndSettle(ActorState& actor, const WorldEntryResources& resources)
{
    if (auto attacked = AttackAction(actor, resources); !attacked.HasValue())
        return attacked;
    actor.animation.frame = 0.0f;
    actor.byte381 = 1;
    return {};
}

// 0x41ed10(id, …): MuRet no tiene audio (CL10).
void Sound(const WorldEntryResources& resources, std::int32_t id)
{
    resources.world.Runtime().lastSoundId = id;
}

// 0x4f5290: en un jugador, las velocidades (0x4f3f80) y la acción de golpe por la montura, la clase o un rand(); en
// otro, 3 y 4 alternadas de a tres con +0x38f, que sube.
Core::Expected<void, std::string> HitAction(ActorState& actor, const WorldEntryResources& resources)
{
    if (!IsPlayer(actor))
    {
        const std::uint8_t action = actor.attackCount % 3 == 0 ? std::uint8_t{3} : std::uint8_t{4};
        if (auto acted = Act(actor, action, resources); !acted.HasValue())
            return acted;
        ++actor.attackCount;
        return {};
    }
    RefreshCombatActionSpeeds(resources);
    if (Riding(actor, UniriaModel) || Riding(actor, DinorantModel))
        return Act(actor, 0x97, resources);
    if (Riding(actor, FenrirModel))
        return Act(actor, 0x5d, resources);
    if ((actor.characterClass & 7u) == ElfClass)
        return Act(actor, 0x92, resources);
    const int bit = resources.cloth.random.Next() & 1;
    return Act(actor, static_cast<std::uint8_t>(0x8e + bit), resources);
}

// Las acciones de 0x4cc110 y 0x4cc1d0, por las manos: la izquierda (+0x2d8) con un bastón, o la derecha (+0x2b8) con
// otro arma, y el elfo con alas (+0x2f8) con la segunda de cada par.
struct HandActions
{
    std::uint8_t left = 0;
    std::uint8_t leftWinged = 0;
    std::uint8_t right = 0;
    std::uint8_t rightWinged = 0;
};
constexpr HandActions StaffHands{0x2e, 0x30, 0x2f, 0x31};  // 0x4cc110
constexpr HandActions RepeatHands{0x7e, 0x80, 0x7f, 0x81}; // 0x4cc1d0

Core::Expected<void, std::string> ActByTheHands(ActorState& actor, const HandActions& actions,
                                                const WorldEntryResources& resources)
{
    // Los CMP de WORD con signo: el 0xffff sin ítem es −1.
    const auto left = static_cast<std::int16_t>(actor.hands[1].model);
    const auto right = static_cast<std::int16_t>(actor.hands[0].model);
    const bool winged = (actor.characterClass & 7u) == ElfClass && actor.wings.model != NoItemModel;
    if ((left >= 0xa12 && left < 0xa19) || left == 0xa23 || left == 0xa26 || left == 0xa27)
        return Act(actor, winged ? actions.leftWinged : actions.left, resources);
    if ((right >= 0xa1a && right < 0xa21) || right == 0xa22 || (right >= 0xa24 && right < 0xc12))
        return Act(actor, winged ? actions.rightWinged : actions.right, resources);
    return {};
}

// 0x4e9c50(con, en): la acción de `on` por las manos de `by`: las dos 0x63, la derecha 0x64, la izquierda 0x65 y
// ninguna 0x62.
Core::Expected<void, std::string> ActByBothHands(const ActorState& by, ActorState& on,
                                                 const WorldEntryResources& resources)
{
    const bool right = by.hands[0].model != NoItemModel;
    const bool left = by.hands[1].model != NoItemModel;
    if (right && left)
        return Act(on, 0x63, resources);
    if (right)
        return Act(on, 0x64, resources);
    if (left)
        return Act(on, 0x65, resources);
    return Act(on, 0x62, resources);
}

// 0x53d8d0 y 0x53d930: el que se va (alpha a 0, +0x80 en 1) y el que llega (frame 5, +0x80 en 3, alpha a 1), con las
// velocidades, la acción 0x93, el destello 0x5c6 y el sonido 0x76.
Core::Expected<void, std::string> Vanish(ActorState& actor, bool arriving, const WorldEntryResources& resources)
{
    RefreshCombatActionSpeeds(resources);
    if (auto acted = Act(actor, 0x93, resources); !acted.HasValue())
        return acted;
    if (arriving)
    {
        actor.animation.frame = 5.0f;
        actor.byte80 = 3;
        actor.alphaTarget = 1.0f;
    }
    else
    {
        actor.alphaTarget = 0.0f;
        actor.byte80 = 1;
    }
    static_cast<void>(
        resources.world.Map().effects.Create(SparkEffect, actor.position, actor.angles, actor.light, 0.0f));
    Sound(resources, 0x76);
    return {};
}

// 0x4f2250 sobre `actor`, con el sonido de Fenrir que pida.
Core::Expected<void, std::string> Stop(ActorState& actor, const WorldEntryResources& resources)
{
    const auto context = TransitionStopContext(resources);
    if (!context.HasValue())
        return Core::Unexpected(context.Error());
    const auto stopped = SetActorStop(actor, context.Value());
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    if (stopped.Value().sound)
        resources.world.Runtime().lastSoundId = stopped.Value().sound;
    return {};
}

// La acción por la montura de las ramas 0x43 a 0x48 de 0x4cc530: Dark Horse, Uniria, Dinorant y Fenrir, fuera de la
// zona segura; si no, `otherwise`.
std::uint8_t MountedAction(const ActorState& actor, std::uint8_t otherwise)
{
    if (Riding(actor, DarkHorseModel))
        return 0x52;
    if (Riding(actor, UniriaModel))
        return 0x40;
    if (Riding(actor, DinorantModel))
        return 0x41;
    if (Riding(actor, FenrirModel))
        return 0x5d;
    return otherwise;
}

// La de las ramas 0x4b y 0x40 (0x4cd81a y 0x4cd841): el Dark Horse, Fenrir o 0x8e.
std::uint8_t ChargeAction(const ActorState& actor)
{
    if (Riding(actor, DarkHorseModel))
        return 0x52;
    if (Riding(actor, FenrirModel))
        return 0x5d;
    return 0x8e;
}

// La de las ramas 0x3c, 0x3d (0x4cd455) y 0x4a (0x4cd7b7): con un pegaso (en la 0x4a, solo el Dark Horse) 0x4e, con
// Fenrir 0x58 y si no 0x4c.
std::uint8_t ShoutAction(const ActorState& actor, bool onlyTheDarkHorse)
{
    const bool pegasus = onlyTheDarkHorse ? Riding(actor, DarkHorseModel) : RidingAPegasus(actor);
    if (pegasus)
        return 0x4e;
    if (Riding(actor, FenrirModel))
        return 0x58;
    return 0x4c;
}

// Un 0x4cc530 en curso: la fuente, el blanco y si la fuente es el héroe (0x743a170).
struct Cast
{
    const MagicAttack& attack;
    ActorState& source;
    ActorState& target;
    bool sourceIsHero;
    const WorldEntryResources& resources;
};

// 0x4cde62: +0x381 en 1 y vuelve.
Core::Expected<void, std::string> Settle(const Cast& cast)
{
    cast.source.byte381 = 1;
    return {};
}

// 0x4cd381, la rama 0x10: fuera del héroe, 0x4f5290 y el frame en 0.
Core::Expected<void, std::string> HitUnlessHero(const Cast& cast)
{
    if (!cast.sourceIsHero)
    {
        if (auto hit = HitAction(cast.source, cast.resources); !hit.HasValue())
            return hit;
        cast.source.animation.frame = 0.0f;
    }
    return Settle(cast);
}

// 0x4cd3e0, las ramas 1, 2, 4, 7, 0xb y 0x11: fuera del héroe, 0x4f5290 en un jugador y 0x4f4a10 en otro.
Core::Expected<void, std::string> StrikeUnlessHero(const Cast& cast)
{
    if (cast.sourceIsHero)
        return Settle(cast);
    if (!IsPlayer(cast.source))
        return AttackAndSettle(cast.source, cast.resources);
    if (auto hit = HitAction(cast.source, cast.resources); !hit.HasValue())
        return hit;
    cast.source.animation.frame = 0.0f;
    return Settle(cast);
}

// Una acción, +0x381 en 1 y un sonido, y vuelve (0x4cd4df, 0x4cd879 y sus parecidas).
Core::Expected<void, std::string> ActAndSound(const Cast& cast, std::uint8_t action, std::int32_t sound)
{
    if (auto acted = Act(cast.source, action, cast.resources); !acted.HasValue())
        return acted;
    cast.source.byte381 = 1;
    Sound(cast.resources, sound);
    return {};
}

// Una acción y 0x4cde62.
Core::Expected<void, std::string> ActAndSettle(const Cast& cast, std::uint8_t action)
{
    if (auto acted = Act(cast.source, action, cast.resources); !acted.HasValue())
        return acted;
    return Settle(cast);
}

Core::Expected<void, std::string> CastTheSkill(const Cast& cast)
{
    ActorState& source = cast.source;
    ActorState& target = cast.target;
    const WorldEntryResources& resources = cast.resources;
    WorldState& world = resources.world;
    const std::uint8_t skill = cast.attack.skill;
    const bool succeeded = source.skillSucceeded != 0;
    switch (skill)
    {
    // 0x4cd316, 0x4cd34b y 0x4cd35b.
    case 0xc9:
        source.flags78 |= 0x100;
        source.byte381 = 1;
        return AttackAction(source, resources);
    case 0xca:
        source.byte381 = 1;
        source.flags78 |= 0x8;
        return AttackAction(source, resources);
    case 0xc8:
        return AttackAction(source, resources);
    // 0x4cd369: suena y sigue como la 0x10.
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
        if (source.word376 != SilentWord376)
            Sound(resources, 0x6f);
        return HitUnlessHero(cast);
    case 0x10:
        return HitUnlessHero(cast);
    // 0x4cd3a5: suena, para al blanco si está en una de cuatro acciones y sigue como la 1.
    case 0x3:
    {
        if (source.type != SilentSkill3Type)
            Sound(resources, 0x50);
        const std::uint8_t action = target.animation.action;
        if (action == 0xcb || action == 0xcc || action == 0xc5 || action == 0xc7)
        {
            if (auto stopped = Stop(target, resources); !stopped.HasValue())
                return stopped;
        }
        return StrikeUnlessHero(cast);
    }
    case 0x1:
    case 0x2:
    case 0x4:
    case 0x7:
    case 0xb:
    case 0x11:
        return StrikeUnlessHero(cast);
    // 0x4cd410: el blanco se va y la fuente llega. Vuelve por 0x4cde69 (0x4cd442), sin pasar por el +0x381 de
    // 0x4cde62.
    case 0xf:
    {
        if (auto gone = Vanish(target, false, resources); !gone.HasValue())
            return gone;
        if (auto came = Vanish(source, true, resources); !came.HasValue())
            return came;
        if (cast.sourceIsHero)
            world.Runtime().movement.gateRequestBlocked = 0;
        Sound(resources, 0x83);
        return {};
    }
    case 0x6:
        return Vanish(source, false, resources);
    case 0x3c:
        return ActAndSound(cast, ShoutAction(source, false), 0x70);
    case 0x3d:
        return ActAndSound(cast, ShoutAction(source, false), 0x1c9);
    case 0x4a:
        return ActAndSound(cast, ShoutAction(source, true), 0x1c9);
    // 0x4cd4df..0x4cd59f: la acción es la skill + 0x25.
    case 0x13:
        return ActAndSound(cast, 0x38, 0x70);
    case 0x14:
        return ActAndSound(cast, 0x39, 0x71);
    case 0x15:
        return ActAndSound(cast, 0x3a, 0x72);
    case 0x16:
        return ActAndSound(cast, 0x3b, 0x73);
    case 0x17:
    {
        const std::uint8_t action = source.attackCount % 2 == 0 ? std::uint8_t{0x3c} : std::uint8_t{0x29};
        if (auto acted = Act(source, action, resources); !acted.HasValue())
            return acted;
        ++source.attackCount;
        source.byte381 = 1;
        Sound(resources, 0x73);
        return {};
    }
    case 0x38:
        return ActAndSound(cast, 0x8d, 0x73);
    case 0x39:
        return ActAndSound(cast, 0x84, 0x71);
    case 0x2c:
        return ActAndSound(cast, 0x85, 0x71);
    case 0x2f:
        return ActAndSettle(cast, source.pet.model == FenrirModel ? std::uint8_t{0x5f} : std::uint8_t{0x42});
    case 0x2b:
    {
        if (auto acted = Act(source, 0x43, resources); !acted.HasValue())
            return acted;
        if (!cast.sourceIsHero && IsPlayer(source))
            source.animation.frame = 0.0f;
        return Settle(cast);
    }
    case 0x18:
    case 0x34:
    case 0x36:
    {
        if (auto acted = ActByTheHands(source, StaffHands, resources); !acted.HasValue())
            return acted;
        return Settle(cast);
    }
    // 0x4cd617: con la skill salida, apaga el 0x628/0 del blanco y crea otro (CL11).
    case 0x37:
        if (succeeded)
        {
            KillModelEffectsOf(world.Map().effects, 0x628, &target, 0);
            return Core::Unexpected(std::string("0x4cd664: el efecto 0x628 de 0x518cf0 es de la CL11"));
        }
        if (IsPlayer(source))
            return {};
        return AttackAndSettle(source, resources);
    // 0x4cd6a5: con la skill salida, apaga el 0xc1/1 del blanco y crea el 0xc1/1 y el 0xc1/2 (CL11).
    case 0x33:
    {
        if (auto acted = ActByTheHands(source, StaffHands, resources); !acted.HasValue())
            return acted;
        if (!succeeded)
            return Settle(cast);
        KillModelEffectsOf(world.Map().effects, 0xc1, &target, 1);
        return Core::Unexpected(std::string("0x4cd6ff: el efecto 0xc1 de 0x518cf0 es de la CL11"));
    }
    case 0x2d:
        if (!cast.sourceIsHero && IsPlayer(source))
        {
            if (auto hit = HitAction(source, resources); !hit.HasValue())
                return hit;
            source.animation.frame = 0.0f;
        }
        source.animation.frame = 0.0f;
        return Settle(cast);
    case 0x49:
    {
        if (auto acted = Act(source, 0x86, resources); !acted.HasValue())
            return acted;
        source.animation.frame = 0.0f;
        return Settle(cast);
    }
    case 0x2e:
    {
        if (auto acted = ActByTheHands(source, RepeatHands, resources); !acted.HasValue())
            return acted;
        return Settle(cast);
    }
    case 0x4b:
        target.flags78 |= 0x200;
        return ActAndSettle(cast, ChargeAction(source));
    case 0x43:
        return ActAndSettle(cast, MountedAction(source, 0x3f));
    case 0x44:
    {
        if (auto acted = Act(source, MountedAction(source, 0x87), resources); !acted.HasValue())
            return acted;
        if (succeeded)
            RemoveActorFlag(0x1000, target, world);
        return Settle(cast);
    }
    case 0x46:
    {
        if (auto acted = Act(source, MountedAction(source, 0x3f), resources); !acted.HasValue())
            return acted;
        if (succeeded && (target.flags78 & 0x4000u) != 0x4000u && &source != &target)
            target.flags78 |= 0x4000;
        return Settle(cast);
    }
    case 0x47:
    {
        if (auto acted = Act(source, MountedAction(source, 0x87), resources); !acted.HasValue())
            return acted;
        if (succeeded)
            RemoveActorFlag(0x4000, target, world);
        return Settle(cast);
    }
    // 0x4cdad7: con la skill salida, 0x2000 en la fuente y su efecto 0x138 (CL11).
    case 0x45:
    {
        if (auto acted = Act(source, MountedAction(source, 0x3f), resources); !acted.HasValue())
            return acted;
        if (!succeeded)
            return Settle(cast);
        source.flags78 |= 0x2000;
        return Core::Unexpected(std::string("0x4cdb72: el efecto 0x138 de 0x518cf0 es de la CL11"));
    }
    case 0x48:
        if (succeeded)
        {
            for (const std::uint32_t mask : {0x4u, 0x8u, 0x10u, 0x100u, 0x800u, 0x2000u})
                RemoveActorFlag(mask, target, world);
        }
        return ActAndSettle(cast, MountedAction(source, 0x3f));
    // 0x4cdc37: sin 0x400 en el blanco, apaga su 0x5ac/2 y su joint 0x619/9 y crea otro 0x5ac/2 (CL11).
    case 0x35:
        if ((target.flags78 & 0x400u) != 0x400u)
        {
            KillModelEffectsOf(world.Map().effects, 0x5ac, &target, 2);
            KillJointsOf(world.DrawTail().joints, 0x619, &target, 9);
            return Core::Unexpected(std::string("0x4cdc88: el efecto 0x5ac/2 de 0x518cf0 es de la CL11"));
        }
        target.flags78 |= 0x400;
        return HitUnlessHero(cast);
    case 0x40:
        RefreshCombatActionSpeeds(resources);
        target.flags78 |= 0x800;
        return ActAndSettle(cast, ChargeAction(source));
    case 0x3f:
    {
        std::uint8_t action = 0x4d;
        if (RidingAPegasus(source))
            action = 0x4f;
        else if (Riding(source, FenrirModel))
            action = 0x5a;
        if (auto acted = Act(source, action, resources); !acted.HasValue())
            return acted;
        if (!IsPlayer(source))
            return AttackAndSettle(source, resources);
        return Settle(cast);
    }
    // 0x4cdd45: sin 0x10 en el blanco, apaga su 0x5ac/1 y crea otro (CL11).
    case 0x30:
        if ((target.flags78 & 0x10u) != 0x10u)
        {
            KillModelEffectsOf(world.Map().effects, 0x5ac, &target, 1);
            return Core::Unexpected(std::string("0x4cdd84: el efecto 0x5ac/1 de 0x518cf0 es de la CL11"));
        }
        target.flags78 |= 0x10;
        return ActAndSettle(cast, 0x3f);
    // 0x4cdda4: en los mapas 8 y 10 (Atlans y el cielo), 0x41.
    case 0x31:
    {
        const std::int32_t map = world.Runtime().map;
        return ActAndSettle(cast, map == 8 || map == 10 ? std::uint8_t{0x41} : std::uint8_t{0x40});
    }
    // 0x4cddd3: el efecto 0x112 en la fuente (CL11) y el sonido 0x94.
    case 0x3b:
        return Core::Unexpected(std::string("0x4cddfa: el efecto 0x112 de 0x518cf0 es de la CL11"));
    case 0x3a:
    {
        if (auto acted = Act(source, 0x44, resources); !acted.HasValue())
            return acted;
        Sound(resources, 0x92);
        source.byte1bc = 0;
        return {};
    }
    // 0x4cde2f: en el héroe, apaga el derecho (0x4cde43..0x4cde58): el soltado (0x7d36840), el clic (0x7d36880), el
    // abajo (0x7d3685c) y los frames de la carga (0x7d368a4), que así no se suelta otra vez.
    case 0x28:
    {
        if (auto acted = Act(source, 0x45, resources); !acted.HasValue())
            return acted;
        if (cast.sourceIsHero)
        {
            UI::GameUiState& ui = world.Runtime().ui;
            ui.rightReleased = false;
            ui.rightClick = false;
            ui.rightDown = false;
            ui.rightHoldFrames = 0;
        }
        return Settle(cast);
    }
    case 0x32:
        return Settle(cast);
    default:
        return {};
    }
}

// 0x4ce000: la acción de otro jugador por la skill con duración (la tabla de 0x4ced44).
Core::Expected<void, std::string> DurationAction(ActorState& actor, std::uint8_t skill,
                                                 const WorldEntryResources& resources)
{
    switch (skill)
    {
    // 0x4cea88 y 0x4ceaad.
    case 0x18:
    case 0x34:
    case 0x36:
        return AttackAction(actor, resources);
    case 0xc:
        return Act(actor, 0x94, resources);
    case 0xa:
        return Act(actor, 0x96, resources);
    case 0xe:
        return Act(actor, 0x95, resources);
    case 0x41:
        return Act(actor, actor.pet.model == FenrirModel ? std::uint8_t{0x5b} : std::uint8_t{0x94}, resources);
    case 0x3e:
    {
        if (auto acted = Act(actor, 0x53, resources); !acted.HasValue())
            return acted;
        Sound(resources, 0x1ce);
        return {};
    }
    case 0x2a:
        return Act(actor, 0x3e, resources);
    case 0x2f:
        return Act(actor, actor.pet.model == FenrirModel ? std::uint8_t{0x5f} : std::uint8_t{0x42}, resources);
    case 0x29:
    case 0x37:
        return Act(actor, 0x3d, resources);
    case 0x38:
        return Act(actor, 0x8d, resources);
    case 0x2b:
        return Act(actor, 0x43, resources);
    case 0x43:
    case 0x46:
        return Act(actor, 0x3f, resources);
    case 0x4c:
        return ActByBothHands(actor, actor, resources);
    default:
        return HitAction(actor, resources);
    }
}
} // namespace

Core::Expected<void, std::string> ApplyMagicAttack(const MagicAttack& attack, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    const std::size_t sourceIndex = ActorWithKey(world, attack.source);
    const std::size_t targetIndex = ActorWithKey(world, attack.target);
    if (targetIndex == NoActor)
        return {};
    runtime.skillSource = static_cast<std::int32_t>(sourceIndex);
    const auto actors = world.Actors();
    ActorState& source = actors[sourceIndex];
    ActorState& target = actors[targetIndex];
    const bool sourceIsHero = sourceIndex == runtime.hero;
    // 0x4ccea1: la 0x3b va derecho a la tabla.
    if (attack.skill != 0x3b)
    {
        if (!sourceIsHero && attack.skill != 0x6 && attack.skill != 0xf && target.seen)
            source.angles.z = static_cast<float>(Render::Models::CreateAngle(source.position.x, source.position.y,
                                                                             target.position.x, target.position.y));
        source.attackTarget = static_cast<std::uint16_t>(targetIndex);
        if (attack.skill == 0x4c)
            source.skillTarget4c = static_cast<std::int32_t>(targetIndex);
        source.skillSucceeded = attack.succeeded ? std::uint8_t{1} : std::uint8_t{0};
        // 0x4ccf07..0x4cd2ef: la skill en +0x38e, abierta y cerrada en línea.
        const Render::Models::LogicalAccess access(source.byte38eAccesses, resources.cloth.random,
                                                   ActorByteCloseRandoms);
        source.byte38e = attack.skill;
    }
    // 0x4cce50: sin la fuente, el original sigue por la rama de la skill sobre el lugar 400, con lo que haya ahí (el
    // tipo del modelo de 0x4ea030, la montura, las manos). MuRet deja lo de arriba en el 400 y descarta la rama
    // (07, Q-C1237).
    if (sourceIndex == NoActor)
    {
        resources.log.Debug("se descarta la rama de la skill {:#x} del 19: la fuente {:#x} no está", attack.skill,
                            attack.source);
        return {};
    }
    return CastTheSkill(Cast{attack, source, target, sourceIsHero, resources});
}

Core::Expected<void, std::string> ApplyDurationMagic(const DurationMagic& magic, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const std::size_t index = ActorWithKey(world, magic.key);
    ActorState& actor = world.Actors()[index];
    {
        const Render::Models::LogicalAccess access(actor.byte38eAccesses, resources.cloth.random,
                                                   ActorByteCloseRandoms);
        actor.byte38e = magic.skill;
    }
    // 0x4cea3e..0x4cea4d: FILD, por 1/255 (0x6b88f8) y por 360 (0x6b869c), exacto en double (Q-C1201).
    actor.angles.z = static_cast<float>(static_cast<double>(magic.direction) * static_cast<double>(1.0f / 255.0f) *
                                        static_cast<double>(360.0f));
    // 0x4ce76c: sin el personaje, el 400. Su acción sale del tipo que haya ahí (0x4cea38); las dos ramas ponen el frame
    // en 0, porque el 400 nunca es el héroe. MuRet descarta la acción y deja el frame (07, Q-C1204).
    if (index == NoActor)
    {
        resources.log.Debug("se descarta la acción de la skill {:#x} del 1E: la clave {:#x} no está", magic.skill,
                            magic.key);
        actor.animation.frame = 0.0f;
    }
    // 0x4cece8: otro que un jugador, 0x4f4a10 y el frame en 0, también el héroe.
    else if (!IsPlayer(actor))
    {
        if (auto attacked = AttackAction(actor, resources); !attacked.HasValue())
            return attacked;
        actor.animation.frame = 0.0f;
    }
    else if (index != world.Runtime().hero)
    {
        if (auto acted = DurationAction(actor, magic.skill, resources); !acted.HasValue())
            return acted;
        actor.animation.frame = 0.0f;
    }
    // 0x4cecfe..: la celda, +0x381, +0x3a4 y +0x380.
    actor.skillCellY = magic.y;
    actor.skillCellX = magic.x;
    actor.byte381 = 1;
    actor.attackTarget = 0xffff;
    actor.byte380 = 0;
    return {};
}

void ApplyMagicCancel(const MagicCancel& cancel, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& actor = world.Actors()[ActorWithKey(world, cancel.key)];
    std::uint32_t mask = 0;
    switch (cancel.skill)
    {
    case 0x1:
    case 0x26:
        mask = 0x1;
        break;
    case 0x7:
    case 0x27:
        mask = 0x2;
        break;
    case 0x1c:
        mask = 0x4;
        break;
    case 0x1b:
        mask = 0x8;
        break;
    case 0x30:
        mask = 0x10;
        break;
    case 0x33:
        mask = 0x20;
        break;
    case 0x37:
        mask = 0x40;
        break;
    case 0x10:
        mask = 0x100;
        break;
    case 0x4b:
        mask = 0x200;
        break;
    case 0x35:
        mask = 0x400;
        break;
    case 0x40:
        mask = 0x800;
        break;
    case 0x43:
        mask = 0x1000;
        break;
    case 0x45:
        mask = 0x2000;
        break;
    case 0x46:
        mask = 0x4000;
        break;
    // 0x4cbfc8 y 0x4cbfdd: 0x439d40 antes.
    case 0xc9:
        SettleMonsterFlags(actor);
        mask = 0x8;
        break;
    case 0xca:
        SettleMonsterFlags(actor);
        mask = 0x100;
        break;
    default:
        return;
    }
    RemoveActorFlag(mask, actor, world);
}

Core::Expected<void, std::string> ApplyActorState(const Protocol::Skill::ActorState& state,
                                                  const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& actor = world.Actors()[ActorWithKey(world, state.key)];
    const bool lit = (actor.flags78 & state.mask) == state.mask;
    if (state.type == 1)
    {
        // 0x4cbe47: sin todas, 0x4e6380(máscara, personaje) (Scenes::ApplyEffectFlags, CL9 hito 1).
        if (!lit)
        {
            WorldRuntime& runtime = world.Runtime();
            return ApplyEffectFlags(actor, state.mask, &actor == &world.Actors()[runtime.hero], runtime.lastSoundId);
        }
        // 0x4cbe52..0x4cbe72: el NEG/SBB deja +0x7c entero en 0x200000 si lo tenía y en 0 si no (Q-C1202).
        constexpr std::uint32_t Sitting = 0x200000;
        actor.flags7c = (actor.flags7c & Sitting) == Sitting ? Sitting : 0u;
        return {};
    }
    if (lit)
        RemoveActorFlag(state.mask, actor, world);
    return {};
}

Core::Expected<void, std::string> ApplySkillTarget(const SkillTarget& target, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const std::size_t sourceIndex = ActorWithKey(world, target.source);
    const std::size_t targetIndex = ActorWithKey(world, target.target);
    if (targetIndex == NoActor)
        return {};
    world.Runtime().skillSource = static_cast<std::int32_t>(sourceIndex);
    ActorState& source = world.Actors()[sourceIndex];
    {
        const Render::Models::LogicalAccess access(source.byte42eAccesses, resources.cloth.random,
                                                   ActorByteCloseRandoms);
        source.byte42e = target.skill;
    }
    source.attackTarget = static_cast<std::uint16_t>(targetIndex);
    // 0x4cc2a9: SAR 0xf del blanco entero; con el bit, 0x5110d0 no lo encontró y ya volvió.
    source.skillSucceeded = (target.target >> 15) != 0 ? std::uint8_t{1} : std::uint8_t{0};
    // 0x4cc503..0x4cc51c: 0x4f4a10, el frame en 0 y +0x381 en 1.
    if (target.skill < 1 || target.skill > 0xd)
        return {};
    // 0x4cc2b0: sin la fuente, el 400, cuyo 0x4f4a10 sale del tipo que haya ahí. MuRet lo descarta y deja el frame y
    // +0x381 (07, Q-C1237).
    if (sourceIndex == NoActor)
    {
        resources.log.Debug("se descarta la acción de la skill {:#x} del 69: la fuente {:#x} no está", target.skill,
                            target.source);
        source.animation.frame = 0.0f;
        source.byte381 = 1;
        return {};
    }
    return AttackAndSettle(source, resources);
}

Core::Expected<void, std::string> ApplyActorEffect(const ActorEffect& effect, const WorldEntryResources& resources)
{
    const std::size_t index = ActorWithKey(resources.world, effect.key);
    if (index >= NoActor)
        return {};
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t LevelUpKind = 0x10;
    if (resources.world.Actors()[index].byte88 != PlayerObject || effect.kind != LevelUpKind)
        return {};
    return Core::Unexpected(std::string("0x4dbfc5: los 15 joints 0x60f/2 de 0x530860 son de la CL11"));
}

Core::Expected<void, std::string> ApplySkillList(const SkillList& list, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    OwnCharacterRecord& own = world.OwnCharacter();
    Render::Models::CrtRandom& random = resources.cloth.random;
    {
        const Render::Models::LogicalAccess access(runtime.ownRecordDepth, random,
                                                   Render::Models::OwnRecordCloseRandoms);
        if (list.count == Protocol::Skill::SkillListRemoveOne)
            own.SetSkillAt(list.slot, 0);
        else if (list.count == Protocol::Skill::SkillListAddOne)
            own.SetSkillAt(list.slot, list.skill);
        // 0x4c52a6..0x4c52c2: el mismo lugar `cantidad` veces.
        else if (list.list == Protocol::Skill::SkillListClearSlot)
        {
            if (list.count > 0)
                own.SetSkillAt(list.slot, 0);
        }
        else
        {
            // 0x4c535a..0x4c53b7: con la lista en 0, los 256 en 0; después las entradas y las cuatro del Dark Lord.
            if (list.list == 0)
            {
                own.words6c.fill(0);
                own.skillsAc.fill(0);
            }
            for (const Protocol::Skill::SkillListEntry& entry : list.entries)
                own.SetSkillAt(entry.slot, entry.skill);
            if ((world.Actors()[runtime.hero].characterClass & 7u) == SummonerLikeClass)
            {
                for (std::uint8_t order = 0; order < 4; ++order)
                    own.SetSkillAt(0x78u + order, static_cast<std::uint8_t>(0x78 + order));
            }
        }
    }
    // 0x4c53f6..0x4c5595: otra vez abierto, las cuentas y +0x425 del héroe.
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    own.byte6a = 0;
    own.byte6b = 0;
    constexpr std::size_t CountedSlots = 0x40;
    for (std::size_t slot = 0; slot < CountedSlots; ++slot)
    {
        const std::uint8_t skill = OwnSkillByte(own, slot);
        if (skill == 0)
            continue;
        ++own.byte6a;
        if (resources.skills == nullptr)
            return Core::Unexpected(std::string("0x4c54ed: el F3 11 lee Skill.bmd y MuRet no lo cargó"));
        const auto record = resources.skills->Record(skill);
        if (!record)
            return Core::Unexpected(std::format("0x4c54ed: Skill.bmd no tiene la skill {}", skill));
        std::uint8_t hidden = 0;
        {
            // 0x4c549a..0x4c54fa: abre la entrada y la cierra con 0x4e7c40.
            const Render::Models::LogicalAccess entry(runtime.movement.skillDepths[skill], random,
                                                      static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
            constexpr std::size_t HiddenOffset = 0x31;
            hidden = std::to_integer<std::uint8_t>((*record)[HiddenOffset]);
        }
        if (hidden == 1)
            ++own.byte6b;
    }
    ActorState& hero = world.Actors()[runtime.hero];
    if (hero.skillIndex >= own.byte6a)
        hero.skillIndex = 0;
    if (own.byte6a == 1)
        hero.skillIndex = 0;
    if (OwnSkillByte(own, hero.skillIndex) == 0)
        hero.skillIndex = 0;
    // 0x4c5557..0x4c5588: saltea las 0x43 a 0x48, hasta 256 veces, con el byte que envuelve.
    constexpr int MaximumSkips = 0x100;
    for (int skipped = 0; skipped < MaximumSkips; ++skipped)
    {
        const std::uint8_t skill = OwnSkillByte(own, hero.skillIndex);
        if (skill < 0x43 || skill > 0x48)
            break;
        hero.skillIndex = static_cast<std::uint8_t>(hero.skillIndex + 1);
    }
    return {};
}

void ApplySummonLife(const SummonLife& summon, const WorldEntryResources& resources)
{
    resources.world.Runtime().hud.summonLife = summon.life;
}
} // namespace Client::Scenes
