#include "Scenes/Skill55cbc0.h"

#include "Core/Expected.h"
#include "Protocol/Skill.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkillSupport.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill4f5290.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"

#include <bit>
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
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0,01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr std::uint8_t ThrustSkill = 0x49;
constexpr std::uint8_t ThrustAction = 0x86;

// 0x55fa54 por la skill − 1 (0x49 entradas) a 0x55fa44: 0, el 19 y 0x4f5290; 1, el 1E; 2, el 19 de la 0x49.
enum class ReachedCase : std::uint8_t
{
    Strike,
    Duration,
    Thrust,
    Nothing,
};

ReachedCase CaseOf(std::uint8_t skill)
{
    switch (skill)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
    case 0xb:
    case 0x11:
    case 0x2d:
        return ReachedCase::Strike;
    case 0xd:
        return ReachedCase::Duration;
    case ThrustSkill:
        return ReachedCase::Thrust;
    default:
        return ReachedCase::Nothing;
    }
}

std::optional<std::uint8_t> LowByteOf(double value)
{
    const auto truncated = World::OriginalFtolLow(value);
    if (!truncated)
        return std::nullopt;
    return static_cast<std::uint8_t>(*truncated & 0xff);
}

// 0x55d4a9..0x55d4e4 (y 0x55e150.., 0x55f409..): la X (+0x41c) y la Y (+0x420) del héroe publicado, cada una abierta y
// cerrada, y el D3 de girar con su ángulo.
Core::Expected<void, std::string> TurnThePublishedHero(const HeroSkillContext& context, std::string_view where)
{
    ActorState& published = context.world.Actors()[context.world.Runtime().hero];
    const std::uint8_t x = ReadTheSkillCell(published.cellXAccesses, published.cellX, context.random);
    const std::uint8_t y = ReadTheSkillCell(published.cellYAccesses, published.cellY, context.random);
    return SendTheTurn(x, y, published.angles.z, context, where);
}

// 0x55cd47..0x55d499 y 0x55d9da..0x55e133: sin "webzen" y con el freno (que con la 0x28 no frena), el C1 06 19.
Core::Expected<void, std::string> StrikeTheTarget(std::uint8_t skill, const ActorState& target,
                                                  const HeroSkillContext& context, std::string_view where)
{
    if (HasTheSilentName(context.world))
        return {};
    const auto passes = PassesTheSkillBrake(skill, context, where);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (passes.Value())
        SendTheSkillPacket(context, Protocol::Skill::WriteMagicAttack(skill, target.key));
    return {};
}
} // namespace

Core::Expected<void, std::string> UseTheSkillOnItsTarget(ActorState& user, ActorState& turned,
                                                         const HeroSkillContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const WeaponSkillRequest& request = runtime.movement.weaponSkill;
    // 0x55cbe4..0x55cc9a: la skill del lugar, con el registro propio abierto y cerrado.
    std::uint8_t skill = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        skill = OwnSkillByte(world.OwnCharacter(), request.slot);
    }
    if (skill == 0x2b || skill == 0x2f)
        return {};
    if (skill == ThrustSkill)
    {
        // 0x55ccb1..0x55cccc: el CMP de WORD con signo.
        const auto right = static_cast<std::int16_t>(world.Actors()[runtime.hero].hands[0].model);
        if (right < 0xc12 || right >= 0xe12)
            return {};
    }
    // 0x55ccd2..0x55cd26: el blanco sin mirar el −1. Con −1 el original lee la posición de actor[−1], antes del pool
    // (comportamiento indefinido); MuRet no lee fuera del pool y corta (07, Q-C1239). Los que llaman no llegan con él:
    // HeroAttack sale antes con −1 o menos y la rama del Wizard (0x58eecc) pide un elegido atacable.
    if (request.target < 0 || static_cast<std::size_t>(request.target) >= world.Actors().size())
        return Core::Unexpected(
            std::format("0x55ccd2: el blanco {} de la skill del arma está fuera del pool", request.target));
    ActorState& target = world.Actors()[static_cast<std::size_t>(request.target)];
    user.attackPosition = target.position;
    turned.angles.z = static_cast<float>(
        Render::Models::CreateAngle(turned.position.x, turned.position.y, target.position.x, target.position.y));
    const ActorCombatContext combat = CombatContextOf(context);
    switch (CaseOf(skill))
    {
    case ReachedCase::Strike:
    {
        if (auto struck = StrikeTheTarget(skill, target, context, "0x55cdc8"); !struck.HasValue())
            return struck;
        if (auto hit = SetTheHitAction(user, combat); !hit.HasValue())
            return hit;
        return TurnThePublishedHero(context, "0x55d76a");
    }
    case ReachedCase::Thrust:
    {
        if (auto struck = StrikeTheTarget(skill, target, context, "0x55da5b"); !struck.HasValue())
            return struck;
        if (auto acted = SetTheActorAction(turned, ThrustAction, combat); !acted.HasValue())
            return acted;
        RefreshCombatActionSpeeds(world, context.random);
        return TurnThePublishedHero(context, "0x55e40e");
    }
    case ReachedCase::Duration:
    {
        // 0x55e67e..0x55e699: la clave por 0x558eb0(p1, blanco), sin rand(). Con el blanco en −1 el original deja
        // 0xffff, pero ese −1 ya cortó en 0x55ccd2 (Q-C1239): acá el blanco siempre está en el pool.
        const auto chosen = ChosenKeyOf(world, SlotOf(world, user), request.target, context.controlDown);
        if (!chosen.HasValue())
            return Core::Unexpected(chosen.Error());
        const auto key = static_cast<std::uint16_t>(chosen.Value());
        if (!HasTheSilentName(world))
        {
            runtime.movement.lastShotSkill = skill;
            // 0x55e881..0x55ea8e: la celda de +0x3a8/+0x3ac de p1 y el rumbo de p2, cada uno el byte bajo del ftol.
            const auto x = LowByteOf(static_cast<double>(user.attackPosition.x) * static_cast<double>(CellReciprocal));
            const auto y = LowByteOf(static_cast<double>(user.attackPosition.y) * static_cast<double>(CellReciprocal));
            const auto direction =
                LowByteOf(static_cast<double>(turned.angles.z) * static_cast<double>(DirectionScale));
            if (!x || !y || !direction)
                return Core::Unexpected(std::string("0x55e890: la celda o el rumbo del 1E fuera del perfil ftol"));
            SendTheSkillPacket(context, Protocol::Skill::WriteDurationMagic(
                                            {.skill = skill, .x = *x, .y = *y, .direction = *direction, .key = key}));
        }
        if (auto hit = SetTheHitAction(user, combat); !hit.HasValue())
            return hit;
        return TurnThePublishedHero(context, "0x55f7dd");
    }
    case ReachedCase::Nothing:
        break;
    }
    return {};
}
} // namespace Client::Scenes
