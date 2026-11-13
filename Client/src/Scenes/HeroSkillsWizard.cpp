#include "Scenes/HeroSkillsWizard.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "GameLogic/PathFinder.h"
#include "Protocol/Skill.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkillSupport.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill4e9920.h"
#include "Scenes/Skill4e9c50.h"
#include "Scenes/Skill4f5290.h"
#include "Scenes/Skill551f40.h"
#include "Scenes/Skill55cbc0.h"
#include "Scenes/Skill57cf90.h"
#include "Scenes/Skill57ce80.h"
#include "Scenes/SkillDelays.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/OriginalFtol.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t WizardClass = 0;
constexpr std::uint8_t GladiatorClass = 3;
constexpr std::uint8_t ChargedSkill = 0x28;
constexpr std::uint8_t ReleasedChargedSkill = 0x3a;
constexpr std::uint8_t PlayerObject = 1;
constexpr std::uint32_t FrozenHeroFlag = 0x1000;
constexpr std::int32_t CastleSiegeMap = 30;
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0,01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr float CellSize = 100.0f;                                                // [0x6b86b4]
constexpr float HalfCell = 50.0f;                                                 // [0x6b86b0]
constexpr float WalkReach = std::bit_cast<float>(std::uint32_t{0x3f99999a});      // [0x6b8bb0], 1,2
constexpr float VisibleAlpha = std::bit_cast<float>(std::uint32_t{0x3f333333});   // [0x6b8a5c], 0,7
constexpr std::uint32_t TeleportWaitMs = 3000;                                    // 0x593146
constexpr std::int32_t MaximumTries = 10;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool IsPlayer(const ActorState& actor)
{
    return actor.type == Data::Models::PlayerSkeletonModel;
}

// 0x5808da..0x5808e9: fuera de 0x43 a 0x48, nada; las demás, de todas las clases, son la cola compartida.
Core::Expected<void, std::string> ToTheSharedTail(std::uint8_t skill)
{
    if (!InRange(skill, 0x43, 0x48))
        return {};
    return Core::Unexpected(std::format("0x5808ef: la skill {:#x} (0x43 a 0x48) es de la fase 2c", skill));
}

Core::Expected<void, std::string> Act(ActorState& actor, std::uint8_t action, const HeroSkillContext& context)
{
    return SetTheActorAction(actor, action, CombatContextOf(context));
}

std::optional<std::uint8_t> LowByteOf(double value)
{
    const auto truncated = World::OriginalFtolLow(value);
    if (!truncated)
        return std::nullopt;
    return static_cast<std::uint8_t>(*truncated & 0xff);
}

// El rumbo del 1E: ftol(+0x24 · 0,7111) (0x58c610 y los demás), el byte bajo.
Core::Expected<std::uint8_t, std::string> DirectionOf(const ActorState& hero, std::string_view where)
{
    const auto direction = LowByteOf(static_cast<double>(hero.angles.z) * static_cast<double>(DirectionScale));
    if (!direction)
        return Core::Unexpected(std::format("{}: el rumbo del 1E fuera del perfil ftol", where));
    return *direction;
}

struct SkillCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// La celda lógica del héroe para el 1E y el camino: la Y (+0x420) antes que la X (+0x41c), 4 + 4 rand().
SkillCell ReadTheHeroCell(ActorState& hero, const HeroSkillContext& context)
{
    const std::uint8_t y = ReadTheSkillCell(hero.cellYAccesses, hero.cellY, context.random);
    const std::uint8_t x = ReadTheSkillCell(hero.cellXAccesses, hero.cellX, context.random);
    return {x, y};
}

// La celda de +0x3a8/+0x3ac (ftol · 0,01, la X primero), sin rand(): la de la 5, la 0x26 y la 0x27.
Core::Expected<SkillCell, std::string> AttackCellOf(const ActorState& hero, std::string_view where)
{
    const auto x = LowByteOf(static_cast<double>(hero.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto y = LowByteOf(static_cast<double>(hero.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!x || !y)
        return Core::Unexpected(std::format("{}: la celda del blanco del 1E fuera del perfil ftol", where));
    return SkillCell{*x, *y};
}

// 0x58ecaa..0x58ecf9 (y 0x58b0cd.., 0x58dcec..): la distancia al centro de la celda del blanco (0x77e1ba8, 0x77e1bac)
// contra el alcance · 100, en la x87. Menor, igual o unordered (C0 o C3) está en alcance.
bool InReach(const ActorState& hero, const GameMovementState& state)
{
    const auto targetX = std::bit_cast<std::int32_t>(state.targetX);
    const auto targetY = std::bit_cast<std::int32_t>(state.targetY);
    const double dx = static_cast<double>(hero.position.x) -
                      (static_cast<double>(targetX) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double dy = static_cast<double>(hero.position.y) -
                      (static_cast<double>(targetY) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit = static_cast<double>(state.selectedSkillRange) * static_cast<double>(CellSize);
    return !(distance > limit);
}

// 0x4eac00(X, Y, x, y, +0x3e8, distancia) desde la celda del héroe: la Y y la X abiertas y cerradas antes (8 rand()).
// Con camino, +0x378 en 1; `withType` pone también +0x379 en 5 (el 0x55cbc0 de la llegada, 0x5650c0). Sin camino, nada.
Core::Expected<void, std::string> WalkToward(ActorState& hero, std::int32_t goalX, std::int32_t goalY, float distance,
                                             bool withType, const HeroSkillContext& context, std::string_view where)
{
    const SkillCell start = ReadTheHeroCell(hero, context);
    World::WorldTerrain& terrain = context.world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::format("{}: falta TerrainWall para el camino de la skill", where));
    constexpr std::int32_t LastCell = 0xff;
    if (!InRange(goalX, 0, LastCell) || !InRange(goalY, 0, LastCell))
        return Core::Unexpected(std::format("{}: la celda del blanco ({}, {}) fuera de la grilla, que PathFinding2 "
                                            "toma como índice lineal",
                                            where, goalX, goalY));
    const auto path = GameLogic::PathFinder(terrain.attributes->cells)
                          .Find({start.x, start.y},
                                {static_cast<std::uint8_t>(goalX), static_cast<std::uint8_t>(goalY)}, false, distance);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    if (withType)
        hero.movementType = 5;
    return {};
}

// 0x4f5370: 0x4ea030 con 0x93 en un jugador y 5 en otro.
Core::Expected<void, std::string> ActTheTeleport(ActorState& actor, const HeroSkillContext& context)
{
    return Act(actor, IsPlayer(actor) ? std::uint8_t{0x93} : std::uint8_t{5}, context);
}

// La acción con la que el Teleport y el Teleport Ally no corren (0x593071..0x5930b8, 0x59267b..0x592799): el jugador
// en 0x93 o el otro en 5, +0x80 en 1 o 2, o el alpha (+0x174) debajo de 0,7 o unordered.
bool CannotTeleport(const ActorState& actor)
{
    const std::uint8_t action = actor.animation.action;
    if (IsPlayer(actor) ? action == 0x93 : action == 5)
        return true;
    if (actor.byte80 == 1 || actor.byte80 == 2)
        return true;
    return !(actor.alpha >= VisibleAlpha);
}

class WizardBranch
{
public:
    WizardBranch(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
        : m_hero(hero), m_skill(skill), m_context(context), m_world(context.world), m_runtime(context.world.Runtime()),
          m_state(context.world.Runtime().movement)
    {
    }

    Core::Expected<void, std::string> Use();

private:
    Core::Expected<bool, std::string> PassesTheHead();
    Core::Expected<bool, std::string> Attackable() const
    {
        return IsAttackable(m_world, m_context.controlDown);
    }
    Core::Expected<ActorState*, std::string> ChosenActor(std::string_view where) const;
    Core::Expected<void, std::string> SendTheMagicAttack(std::uint16_t key, std::string_view brake);
    Core::Expected<void, std::string> ReleaseTheCharge();
    Core::Expected<void, std::string> ShootTheCharge();
    Core::Expected<void, std::string> RaiseTheSoulBarrier();
    Core::Expected<void, std::string> CastAround(std::uint8_t action, std::string_view where);
    Core::Expected<void, std::string> CastTheSkill4c();
    Core::Expected<void, std::string> UseByReach();
    Core::Expected<void, std::string> CastAtTheCell(std::string_view where);
    Core::Expected<void, std::string> CastTheMagicKey();
    Core::Expected<void, std::string> CastTheAquaBeam();
    Core::Expected<void, std::string> CastTheFlame();
    Core::Expected<void, std::string> CastAtTheWeaponTarget();
    Core::Expected<void, std::string> UseTheWizardOnly();
    Core::Expected<void, std::string> Teleport();
    Core::Expected<void, std::string> TeleportTheAlly();

    ActorState& m_hero;
    std::uint8_t m_skill;
    const HeroSkillContext& m_context;
    WorldState& m_world;
    WorldRuntime& m_runtime;
    GameMovementState& m_state;
    std::uint8_t m_class = 0;
    bool m_aimed = false;
};

Core::Expected<ActorState*, std::string> WizardBranch::ChosenActor(std::string_view where) const
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= m_world.Actors().size())
        return Core::Unexpected(std::format("{}: el elegido {} está fuera del pool", where, chosen));
    return &m_world.Actors()[static_cast<std::size_t>(chosen)];
}

// El C1 06 19 en línea (0x58a1e5.., 0x58a954.., 0x58b285.., 0x58ba70..): sin "webzen" en el héroe publicado y con el
// freno (que con la 0x28 no frena y anota igual).
Core::Expected<void, std::string> WizardBranch::SendTheMagicAttack(std::uint16_t key, std::string_view brake)
{
    if (HasTheSilentName(m_world))
        return {};
    const auto passes = PassesTheSkillBrake(m_skill, m_context, brake);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (passes.Value())
        SendTheSkillPacket(m_context, Protocol::Skill::WriteMagicAttack(m_skill, key));
    return {};
}

// 0x5897b7..0x58a117: true si sigue a 0x57ce80, con el registro propio ya cerrado.
Core::Expected<bool, std::string> WizardBranch::PassesTheHead()
{
    const Render::Models::LogicalAccess own(m_runtime.ownRecordDepth, m_context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    // 0x58982d..0x58987d: la 0x3a y la 0x28 cuestan lo de la 0x28, y la 0x28 sin stamina.
    const bool charged = m_skill == ReleasedChargedSkill || m_skill == ChargedSkill;
    const auto cost = SkillCostOf(charged ? ChargedSkill : m_skill, m_context, charged ? "0x589866" : "0x58984c");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    const auto mana = static_cast<std::int32_t>(cost.Value().mana);
    const auto stamina = m_skill == ChargedSkill ? 0 : static_cast<std::int32_t>(cost.Value().stamina);
    const auto energy = SkillEnergyRequirement(
        m_skill, SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
    if (!energy.HasValue())
        return Core::Unexpected(energy.Error());
    const OwnCharacterRecord& record = m_world.OwnCharacter();
    // 0x58988a..0x5898a5: JLE con signo contra +0x1e + +0x30.
    if (energy.Value() > static_cast<std::int32_t>(record.word30) + static_cast<std::int32_t>(record.energy))
        return false;
    // 0x58990f..0x58991a.
    if (mana > static_cast<std::int32_t>(record.mana))
    {
        if (auto potion = UseAManaPotion(m_context, "0x589936"); !potion.HasValue())
            return Core::Unexpected(potion.Error());
        return false;
    }
    // 0x58a036..0x58a06f.
    if (stamina > static_cast<std::int32_t>(record.bp))
    {
        if (charged)
        {
            UI::GameUiState& ui = m_runtime.ui;
            ui.rightReleased = false;
            ui.rightClick = false;
            ui.rightDown = false;
            ui.rightHoldFrames = 0;
        }
        return false;
    }
    // 0x58a071..0x58a08a: la espera de la skill del lugar del héroe publicado, con el registro abierto.
    const auto ready =
        TryStartSkillDelay(m_world.Actors()[m_runtime.hero].skillIndex,
                           SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
    if (!ready.HasValue())
        return Core::Unexpected(ready.Error());
    return ready.Value();
}

Core::Expected<void, std::string> WizardBranch::Use()
{
    m_class = static_cast<std::uint8_t>(m_hero.characterClass & ClassMask);
    // 0x5897a7..0x5897b1.
    if (m_class != WizardClass && m_class != GladiatorClass)
        return ToTheSharedTail(m_skill);
    const auto head = PassesTheHead();
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    if (!head.Value())
        return {};
    // 0x58a117..0x58a120: el resultado queda en [ebp−0x3d].
    const auto aimed = AimTheHeroSkill(m_hero, m_context);
    if (!aimed.HasValue())
        return Core::Unexpected(aimed.Error());
    m_aimed = aimed.Value();
    // 0x58a126..0x58a13a: la skill − 0xa por las tablas 0x599e54 y 0x599e34.
    switch (m_skill)
    {
    case 0xa:
        return CastAround(0x96, "0x58c1b6");
    case 0xe:
        return CastAround(0x95, "0x58cf37");
    case 0x10:
        return RaiseTheSoulBarrier();
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x2f:
        return {};
    case ChargedSkill:
        return ShootTheCharge();
    case ReleasedChargedSkill:
        return ReleaseTheCharge();
    case 0x4c:
        return CastTheSkill4c();
    default:
        return UseByReach();
    }
}

// 0x58a141..0x58a899: la 0x3a soltada. Sin un jugador, nada; si no, el 19 con la clave del jugador (0x57678bc), las
// velocidades (0x4f3f80), la acción 0x44 y +0x378 en 0.
Core::Expected<void, std::string> WizardBranch::ReleaseTheCharge()
{
    if (!IsPlayer(m_hero))
        return {};
    if (auto sent = SendTheMagicAttack(m_context.playerKey, "0x58a1be"); !sent.HasValue())
        return sent;
    RefreshCombatActionSpeeds(m_world, m_context.random);
    if (auto acted = Act(m_hero, 0x44, m_context); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}

// 0x58a89a..0x58b025: la 0x28 sin cargar (el ataque automático o el clic izquierdo trabado). La clave del elegido por
// 0x558eb0, o la del jugador; el freno no frena; la acción 0x45.
Core::Expected<void, std::string> WizardBranch::ShootTheCharge()
{
    if (!IsPlayer(m_hero))
        return {};
    const auto chosenKey =
        ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_runtime.selectedTargets[2], m_context.controlDown);
    if (!chosenKey.HasValue())
        return Core::Unexpected(chosenKey.Error());
    const std::uint16_t key =
        chosenKey.Value() == -1 ? m_context.playerKey : static_cast<std::uint16_t>(chosenKey.Value());
    if (auto sent = SendTheMagicAttack(key, "0x58a938"); !sent.HasValue())
        return sent;
    RefreshCombatActionSpeeds(m_world, m_context.random);
    if (auto acted = Act(m_hero, 0x45, m_context); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}

// 0x58b026..0x58c15b: Soul Barrier. Sin elegido, el 19 con la clave del jugador; con un elegido que no es jugador,
// 0x6c8e38 en −1 y nada; con uno de la party (0x59e680), +0x3a4, 0x7758580..88 y, fuera de alcance, el camino (+0x379
// en 5) y nada más; dentro, el 19 con la clave del blanco. Después del 19 (o de no mandarlo), 0x4f5290 y sigue como las
// demás (0x58c15e).
Core::Expected<void, std::string> WizardBranch::RaiseTheSoulBarrier()
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen == -1)
    {
        if (auto sent = SendTheMagicAttack(m_context.playerKey, "0x58ba54"); !sent.HasValue())
            return sent;
    }
    else
    {
        const auto target = ChosenActor("0x58b034");
        if (!target.HasValue())
            return Core::Unexpected(target.Error());
        if (target.Value()->byte88 != PlayerObject)
        {
            m_runtime.selectedTargets[4] = -1;
            return {};
        }
        // 0x59e680: el nombre del elegido entre los de la party (0x77f3cd0, de a 0x24); sin party, 0.
        if (m_runtime.hud.partyCount <= 0)
            return {};
        return Core::Unexpected(std::format("0x58b06e: 0x59e680 compara el nombre del elegido con los {} de la party "
                                            "(0x77f3cd0), que son del hito 7",
                                            m_runtime.hud.partyCount));
        // Lo que sigue con el elegido en la party (0x58b07e..0x58b9e0), para cuando el hito 7 dé los nombres:
        // +0x3a4 = elegido; 0x7758580..88 = {1, +0x425 del publicado, elegido}; fuera de alcance, WalkToward(...,
        // range, con +0x379 en 5) y nada; dentro, SendTheMagicAttack(clave de 0x7758588, "0x58b269") y 0x58c152.
    }
    // 0x58c152..0x58c15b.
    if (auto hit = SetTheHitAction(m_hero, CombatContextOf(m_context)); !hit.HasValue())
        return hit;
    return UseByReach();
}

// 0x58c1b6..0x58cf36 (Hellfire, 0xa) y 0x58cf37..0x58dcc3 (Inferno, 0xe): solo un jugador. Sin "webzen", 0x5767948 y
// el 1E con la celda del héroe (la Y y la X), el rumbo y la clave 0xffff; después, las velocidades, `action` y +0x378
// en 0.
Core::Expected<void, std::string> WizardBranch::CastAround(std::uint8_t action, std::string_view where)
{
    if (!IsPlayer(m_hero))
        return {};
    if (!HasTheSilentName(m_world))
    {
        m_state.lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = DirectionOf(m_hero, where);
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(m_context, Protocol::Skill::WriteDurationMagic({.skill = m_skill,
                                                                           .x = cell.x,
                                                                           .y = cell.y,
                                                                           .direction = direction.Value(),
                                                                           .key = Protocol::Skill::NoSkillTargetKey}));
    }
    RefreshCombatActionSpeeds(m_world, m_context.random);
    if (auto acted = Act(m_hero, action, m_context); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}

// 0x58dcc4..0x58eca3: la 0x4c. La celda de +0x3a8 (ftol · 0,01) es la meta del camino; en alcance y con un elegido
// atacable, la posición relativa (0x4e9920), el ángulo, la espera (0x552990), la clave (0x558eb0), +0x434 y el 1E con
// la clave mágica; después, +0x378 en 0 y, en un jugador, 0x4e9c50. Si no, sin elegido 0x6c8e38 en −1; con él, el
// camino hasta el alcance · 1,2 (sin +0x379).
Core::Expected<void, std::string> WizardBranch::CastTheSkill4c()
{
    const auto aimX =
        World::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto aimY =
        World::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!aimX || !aimY)
        return Core::Unexpected(std::string("0x58dcd3: la celda del blanco de la 0x4c fuera del perfil ftol"));
    bool casts = InReach(m_hero, m_state);
    if (casts)
    {
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        casts = attackable.Value();
    }
    if (!casts)
    {
        // 0x58ebaa..0x58eca3.
        if (m_runtime.selectedTargets[2] == -1)
        {
            m_runtime.selectedTargets[4] = -1;
            return {};
        }
        // 0x58ec44..0x58ec5a: FMUL y FSTP a un float de la pila.
        const auto reach =
            static_cast<float>(static_cast<double>(m_state.selectedSkillRange) * static_cast<double>(WalkReach));
        return WalkToward(m_hero, *aimX, *aimY, reach, false, m_context, "0x58ec65");
    }
    // 0x58dd50..0x58dd70: los ftol de +0x3ac y +0x3a8 sin escalar.
    const auto wholeY = World::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.y));
    const auto wholeX = World::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.x));
    if (!wholeX || !wholeY)
        return Core::Unexpected(std::string("0x58dd56: la posición del blanco de la 0x4c fuera del perfil ftol"));
    const auto relative = RelativeTargetCell(m_hero.position.x, m_hero.position.y, *wholeX, *wholeY);
    if (!relative)
        return Core::Unexpected(std::string("0x58dd70: un ftol de la celda relativa de la 0x4c fuera de su perfil"));
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen == -1)
    {
        m_hero.skillTarget4c = -1;
        return {};
    }
    m_hero.angles.z = static_cast<float>(Render::Models::CreateAngle(m_hero.position.x, m_hero.position.y,
                                                                     m_hero.attackPosition.x, m_hero.attackPosition.y));
    if (auto delayed = StartSkillDelay(
            m_world.Actors()[m_runtime.hero].skillIndex,
            SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
        !delayed.HasValue())
        return delayed;
    const auto key = ChosenKeyOf(m_world, SlotOf(m_world, m_hero), chosen, m_context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    m_hero.skillTarget4c = chosen;
    if (!HasTheSilentName(m_world))
    {
        m_state.lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = DirectionOf(m_hero, "0x58e232");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        // 0x58e6d9..0x58e6fe: 0x57cf90 en línea, con +0x1bd del héroe.
        const std::uint8_t magicKey = NextMagicKey(m_state, m_hero.magicKey);
        SendTheSkillPacket(m_context,
                           Protocol::Skill::WriteDurationMagic({.skill = m_skill,
                                                                .x = cell.x,
                                                                .y = cell.y,
                                                                .direction = direction.Value(),
                                                                .back = *relative,
                                                                .key = static_cast<std::uint16_t>(key.Value()),
                                                                .magicKey = magicKey}));
    }
    // 0x58eb62..0x58eb7a.
    m_hero.movement = 0;
    if (!IsPlayer(m_hero))
        return {};
    return SetTheActorAction(m_hero, ActionByTheHands(m_hero), CombatContextOf(m_context));
}

// 0x58c15e..0x58ef17: las demás por el alcance.
Core::Expected<void, std::string> WizardBranch::UseByReach()
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen != -1)
    {
        // 0x58c16e..0x58c1b1.
        m_state.weaponSkill.active = 1;
        m_state.weaponSkill.slot = m_world.Actors()[m_runtime.hero].skillIndex;
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        m_state.weaponSkill.target = attackable.Value() ? chosen : -1;
    }
    if (!InReach(m_hero, m_state))
    {
        // 0x58ecff..0x58edff: con un elegido atacable, el camino hasta el alcance (+0x379 en 5).
        if (chosen != -1)
        {
            const auto attackable = Attackable();
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            if (attackable.Value())
            {
                if (auto walked = WalkToward(m_hero, std::bit_cast<std::int32_t>(m_state.targetX),
                                             std::bit_cast<std::int32_t>(m_state.targetY), m_state.selectedSkillRange,
                                             true, m_context, "0x58edc6");
                    !walked.HasValue())
                    return walked;
            }
        }
        if (!InRange(m_skill, 0x43, 0x47) && m_skill != 0x4c)
            return {};
    }
    // 0x58ee05..0x58eeb2: la línea de vista desde la celda del héroe (la Y y la X).
    const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
    const World::WorldTerrain& terrain = m_world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver el blanco de la skill"));
    const auto seen =
        World::CanSeeTheCell(terrain.attributes->cells, cell.x, cell.y, std::bit_cast<std::int32_t>(m_state.targetX),
                             std::bit_cast<std::int32_t>(m_state.targetY));
    if (!seen)
        return Core::Unexpected(std::string("0x55886d: la línea hasta el blanco de la skill sale de TerrainWall"));
    if (!*seen)
        return UseTheWizardOnly();
    // 0x58eeb8..0x58eecc.
    if (m_runtime.selectedTargets[2] != -1)
    {
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        if (attackable.Value())
        {
            if (auto reached = UseTheSkillOnItsTarget(m_hero, m_hero, m_context); !reached.HasValue())
                return reached;
        }
    }
    if (!m_aimed)
        return UseTheWizardOnly();
    // 0x58eedf..0x58ef17: el ángulo hacia +0x3a8 y la skill − 5 por las tablas 0x599eac y 0x599e98.
    m_hero.angles.z = static_cast<float>(Render::Models::CreateAngle(m_hero.position.x, m_hero.position.y,
                                                                     m_hero.attackPosition.x, m_hero.attackPosition.y));
    switch (m_skill)
    {
    case 5:
        return CastTheFlame();
    case 8:
    case 9:
        return CastTheMagicKey();
    case 0xc:
        return CastTheAquaBeam();
    case 0x26:
    case 0x27:
        return CastAtTheWeaponTarget();
    default:
        return UseTheWizardOnly();
    }
}

// 0x58ef1e..0x58fcec: la 8 y la 9, de cualquier tipo. Sin "webzen", 0x5767948 y el 1E con la celda del héroe, el
// rumbo, la clave 0xffff y la clave mágica; después, 0x4f5290.
Core::Expected<void, std::string> WizardBranch::CastTheMagicKey()
{
    if (!HasTheSilentName(m_world))
    {
        m_state.lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = DirectionOf(m_hero, "0x58f371");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        // 0x58f822..0x58f849: 0x57cf90 en línea, con +0x1bd del héroe.
        const std::uint8_t magicKey = NextMagicKey(m_state, m_hero.magicKey);
        SendTheSkillPacket(m_context, Protocol::Skill::WriteDurationMagic({.skill = m_skill,
                                                                           .x = cell.x,
                                                                           .y = cell.y,
                                                                           .direction = direction.Value(),
                                                                           .key = Protocol::Skill::NoSkillTargetKey,
                                                                           .magicKey = magicKey}));
    }
    return SetTheHitAction(m_hero, CombatContextOf(m_context));
}

// 0x58fced..0x590b17: Aqua Beam (0xc), solo un jugador: como Hellfire, con la acción 0x94 y 0x77f3ba4 en 0. El +0x3e2
// que lee con Fenrir (0x590ae2) no lo usa.
Core::Expected<void, std::string> WizardBranch::CastTheAquaBeam()
{
    if (!IsPlayer(m_hero))
        return {};
    if (auto cast = CastAround(0x94, "0x59018f"); !cast.HasValue())
        return cast;
    m_state.dword77f3ba4 = 0;
    return {};
}

// 0x590b18..0x591858: Flame (5), de cualquier tipo. El 1E con la celda de +0x3a8 y la clave 0xffff; después, 0x4f5290.
Core::Expected<void, std::string> WizardBranch::CastTheFlame()
{
    if (!HasTheSilentName(m_world))
    {
        m_state.lastShotSkill = m_skill;
        const auto cell = AttackCellOf(m_hero, "0x590d01");
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        const auto direction = DirectionOf(m_hero, "0x590ef9");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(m_context, Protocol::Skill::WriteDurationMagic({.skill = m_skill,
                                                                           .x = cell.Value().x,
                                                                           .y = cell.Value().y,
                                                                           .direction = direction.Value(),
                                                                           .key = Protocol::Skill::NoSkillTargetKey}));
    }
    return SetTheHitAction(m_hero, CombatContextOf(m_context));
}

// 0x591859..0x5925ad: la 0x26 y la 0x27, solo un jugador. La clave del blanco de la skill del arma (0x7758588) por
// 0x558eb0, o 0xffff sin él; el 1E con la celda de +0x3a8; después, 0x4f5290 y +0x378 en 0.
Core::Expected<void, std::string> WizardBranch::CastAtTheWeaponTarget()
{
    if (!IsPlayer(m_hero))
        return {};
    std::uint16_t key = Protocol::Skill::NoSkillTargetKey;
    if (m_state.weaponSkill.target != -1)
    {
        const auto chosenKey =
            ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_state.weaponSkill.target, m_context.controlDown);
        if (!chosenKey.HasValue())
            return Core::Unexpected(chosenKey.Error());
        key = static_cast<std::uint16_t>(chosenKey.Value());
    }
    if (!HasTheSilentName(m_world))
    {
        m_state.lastShotSkill = m_skill;
        const auto cell = AttackCellOf(m_hero, "0x591a6c");
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        const auto direction = DirectionOf(m_hero, "0x591c67");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(m_context, Protocol::Skill::WriteDurationMagic({.skill = m_skill,
                                                                           .x = cell.Value().x,
                                                                           .y = cell.Value().y,
                                                                           .direction = direction.Value(),
                                                                           .key = key}));
    }
    if (auto hit = SetTheHitAction(m_hero, CombatContextOf(m_context)); !hit.HasValue())
        return hit;
    m_hero.movement = 0;
    return {};
}

// 0x5925ae..0x5925d3: el Magic Gladiator o sin blanco, a 0x5808da; el Wizard, la 6 y la 0xf.
Core::Expected<void, std::string> WizardBranch::UseTheWizardOnly()
{
    if (m_class != WizardClass || !m_aimed)
        return ToTheSharedTail(m_skill);
    if (m_skill == 6)
        return Teleport();
    if (m_skill == 0xf)
        return TeleportTheAlly();
    return ToTheSharedTail(m_skill);
}

// 0x593051..0x593873: Teleport (6) a la celda del blanco (0x77e1ba8, 0x77e1bac).
Core::Expected<void, std::string> WizardBranch::Teleport()
{
    if (m_runtime.cursorItemCount > 0 || (m_hero.flags78 & FrozenHeroFlag) == FrozenHeroFlag || CannotTeleport(m_hero))
        return {};
    // 0x5930be..0x5930f9: el TerrainWall de 0x5f82a0 (los dos con & 0xff) sin 0x20 ni 0x40.
    const World::WorldTerrain& terrain = m_world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x5930cc: falta TerrainWall para el Teleport"));
    const std::size_t index = ((m_state.targetY & 0xffu) << 8) + (m_state.targetX & 0xffu);
    std::uint32_t wall = terrain.attributes->cells[index];
    if ((wall & 0x20u) == 0x20u)
        wall -= 0x20u;
    if ((wall & 0x40u) == 0x40u)
        wall -= 0x40u;
    if (wall != 0)
        return {};
    m_hero.angles.z = static_cast<float>(Render::Models::CreateAngle(m_hero.position.x, m_hero.position.y,
                                                                     m_hero.attackPosition.x, m_hero.attackPosition.y));
    // 0x59311d..0x59314b: el byte 0x576794c, 0x77f3bb0 y 3.000 ms (resta sin signo) desde el último 1C.
    if (static_cast<std::uint8_t>(m_state.gateRequestBlocked) != 0 || m_state.warpPending)
        return {};
    if (!m_context.tickCount)
        return Core::Unexpected(std::string("0x59313a: la espera del Teleport sin GetTickCount (0x6b8144)"));
    if (m_context.tickCount() - m_state.lastTeleportMs < TeleportWaitMs)
        return {};
    m_state.gateRequestBlocked = 1;
    SendTheSkillPacket(m_context,
                       Protocol::Skill::WriteTeleportMagic(static_cast<std::uint8_t>(m_state.targetX & 0xffu),
                                                           static_cast<std::uint8_t>(m_state.targetY & 0xffu)));
    // 0x59383f..0x593857: con 0x1000 en +0x78, 0x4e6910; 0x593061 ya salió con él (*estático*).
    return ActTheTeleport(m_hero, m_context);
}

// 0x5925d9..0x593050: Teleport Ally (0xf) del elegido a una celda libre de al lado del héroe.
Core::Expected<void, std::string> WizardBranch::TeleportTheAlly()
{
    if (m_runtime.selectedTargets[2] == -1)
        return {};
    const auto chosen = ChosenActor("0x59266d");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    ActorState& target = *chosen.Value();
    if (m_runtime.siegeActive)
    {
        // 0x5925f3..0x5926c8: el puesto del asedio (+0x360) del héroe publicado y el del héroe, en el mismo par.
        const std::uint8_t published = m_world.Actors()[m_runtime.hero].byte360;
        const std::uint8_t own = m_hero.byte360;
        const auto pairOf = [](std::uint8_t role) -> int
        {
            if (role == 9 || role == 6)
                return 1;
            if (role == 0xa || role == 7)
                return 2;
            if (role == 0xb || role == 8)
                return 3;
            if (role == 0xd || role == 0xc)
                return 4;
            return 0;
        };
        if (pairOf(published) != 0 && pairOf(published) != pairOf(own))
            return {};
    }
    else
    {
        // 0x5926ca..0x592766: el nombre del elegido entre los de la party (0x77f3cd0, strncmp); sin party, nada.
        if (m_runtime.hud.partyCount <= 0)
            return {};
        return Core::Unexpected(std::format("0x59272c: comparar el nombre del elegido con los {} de la party "
                                            "(0x77f3cd0) es del hito 7",
                                            m_runtime.hud.partyCount));
    }
    // 0x592660..0x592799.
    if (m_runtime.cursorItemCount > 0 || CannotTeleport(target))
        return {};
    const auto heroX =
        World::OriginalFtolLow(static_cast<double>(m_hero.position.x) * static_cast<double>(CellReciprocal));
    const auto heroY =
        World::OriginalFtolLow(static_cast<double>(m_hero.position.y) * static_cast<double>(CellReciprocal));
    if (!heroX || !heroY)
        return Core::Unexpected(std::string("0x5927b2: la celda del héroe fuera del perfil ftol"));
    const World::WorldTerrain& terrain = m_world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x59280f: falta TerrainWall para el Teleport Ally"));
    // 0x5927cb..0x5928f1: una de las ocho celdas de al lado (dos rand() % 3, sin el centro) sin TerrainWall (salvo
    // 0x20), o en el asedio dentro del castillo; con diez celdas tapadas, nada.
    std::int32_t tries = 0;
    std::int32_t cellX = 0;
    std::int32_t cellY = 0;
    for (;;)
    {
        const std::int32_t dx = m_context.random.Next() % 3;
        const std::int32_t dy = m_context.random.Next() % 3;
        if (dx == 1 && dy == 1)
        {
            if (tries > MaximumTries)
                return {};
            continue;
        }
        cellX = dx + *heroX - 1;
        cellY = dy + *heroY - 1;
        m_state.targetX = std::bit_cast<std::uint32_t>(cellX);
        m_state.targetY = std::bit_cast<std::uint32_t>(cellY);
        // 0x5f8290 sin máscara: y · 256 + x.
        const std::int64_t index = static_cast<std::int64_t>(cellY) * 256 + cellX;
        if (index < 0 || index >= static_cast<std::int64_t>(terrain.attributes->cells.size()))
            return Core::Unexpected(
                std::format("0x592819: la celda ({}, {}) del Teleport Ally sale de TerrainWall", cellX, cellY));
        std::uint32_t wall = terrain.attributes->cells[static_cast<std::size_t>(index)];
        if ((wall & 0x20u) == 0x20u)
            wall -= 0x20u;
        if (m_runtime.map == CastleSiegeMap)
        {
            // 0x59283b..0x5928de: la X (0x412810/0x40b800) y la Y (0x4e7e40) del héroe publicado.
            ActorState& published = m_world.Actors()[m_runtime.hero];
            const std::uint8_t x = ReadTheSkillCell(published.cellXAccesses, published.cellX, m_context.random);
            const std::uint8_t y = ReadTheSkillCell(published.cellYAccesses, published.cellY, m_context.random);
            if (InRange(x, 0x96, 0xc8) && InRange(y, 0xb4, 0xe6))
                break;
        }
        if (wall == 0)
            break;
        ++tries;
        if (tries > MaximumTries)
            return {};
    }
    // 0x5928f6..0x592914: el ángulo del elegido hacia su propio +0x3a8.
    target.angles.z = static_cast<float>(Render::Models::CreateAngle(target.position.x, target.position.y,
                                                                     target.attackPosition.x, target.attackPosition.y));
    // 0x592917..0x592941: con 0x576794c, lo baja y nada.
    if (static_cast<std::uint8_t>(m_state.gateRequestBlocked) != 0)
    {
        m_state.gateRequestBlocked = 0;
        return {};
    }
    m_state.gateRequestBlocked = 1;
    SendTheSkillPacket(m_context,
                       Protocol::Skill::WriteTargetTeleport(target.key, static_cast<std::uint8_t>(cellX & 0xff),
                                                            static_cast<std::uint8_t>(cellY & 0xff)));
    return ActTheTeleport(target, m_context);
}
} // namespace

Core::Expected<void, std::string> UseTheWizardSkill(ActorState& hero, std::uint8_t skill,
                                                    const HeroSkillContext& context)
{
    return WizardBranch(hero, skill, context).Use();
}
} // namespace Client::Scenes
