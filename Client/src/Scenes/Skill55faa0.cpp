#include "Scenes/Skill55faa0.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Skill.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkillSupport.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill4f5290.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

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
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t end)
{
    return value >= first && value < end;
}

// 0x4f4915..0x4f4938: un arco en la izquierda (+0x2d8, el CMP de WORD con signo): 0xa12 a 0xa18, 0xa23, 0xa26 o 0xa27.
bool BowInTheLeft(const ActorState& actor)
{
    const auto left = static_cast<std::int16_t>(actor.hands[1].model);
    return InRange(left, 0xa12, 0xa19) || left == 0xa23 || left == 0xa26 || left == 0xa27;
}

// 0x4f493a..0x4f4967: una ballesta en la derecha (+0x2b8): 0xa1a a 0xa20, 0xa22 o 0xa24 a 0xc11.
bool CrossbowInTheRight(const ActorState& actor)
{
    const auto right = static_cast<std::int16_t>(actor.hands[0].model);
    return InRange(right, 0xa1a, 0xa21) || right == 0xa22 || InRange(right, 0xa24, 0xc12);
}

// 0x4f48e0(personaje): en un jugador, las velocidades (0x4f3f80) y la acción de tirar por las manos. Montado en Uniria
// o Dinorant (+0x318) fuera de la zona segura (+0x3e2), 0x82 con el arco y 0x83 con la ballesta; si no, con el arco
// 0x7e sin alas (+0x2f8 en −1) y 0x80 con ellas, y con la ballesta 0x7f y 0x81. Sin arma de tiro, ninguna. Con o sin
// acción, de cualquier tipo, +0x38f sube.
Core::Expected<void, std::string> SetTheShotAction(ActorState& actor, const ActorCombatContext& context)
{
    if (actor.type == Data::Models::PlayerSkeletonModel)
    {
        RefreshCombatActionSpeeds(context.world, context.random);
        const bool riding = (actor.pet.model == UniriaModel || actor.pet.model == DinorantModel) && actor.safeZone == 0;
        const bool winged = actor.wings.model != NoItemModel;
        std::optional<std::uint8_t> action;
        if (BowInTheLeft(actor))
            action = riding ? std::uint8_t{0x82} : (winged ? std::uint8_t{0x80} : std::uint8_t{0x7e});
        else if (CrossbowInTheRight(actor))
            action = riding ? std::uint8_t{0x83} : (winged ? std::uint8_t{0x81} : std::uint8_t{0x7f});
        if (action)
        {
            if (auto acted = SetTheActorAction(actor, *action, context); !acted.HasValue())
                return acted;
        }
    }
    ++actor.attackCount;
    return {};
}

// El C1 06 19 en línea (0x56018f.., 0x5608fd.. y 0x56109a..): sin "webzen" en el héroe publicado y con el freno (que
// con la 0x28 no frena), con la clave del blanco.
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

// 0x561c50 (CheckTheArrows) con los servicios del contexto, o sin ellos.
Core::Expected<bool, std::string> HasArrows(const HeroSkillContext& context)
{
    static const InventoryServices NoServices{};
    return CheckTheArrows(
        InventoryMoveContext{.world = context.world,
                             .random = context.random,
                             .services = context.inventory != nullptr ? *context.inventory : NoServices});
}
} // namespace

Core::Expected<void, std::string> UseTheElfSkillOnItsTarget(ActorState& user, ActorState& turned,
                                                            const HeroSkillContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const WeaponSkillRequest request = runtime.movement.weaponSkill;
    // 0x55fac2..0x55ffcf: el D3 de girar, antes que todo, con el ángulo del héroe publicado de ahora.
    {
        ActorState& published = world.Actors()[runtime.hero];
        const std::uint8_t x = ReadTheSkillCell(published.cellXAccesses, published.cellX, context.random);
        const std::uint8_t y = ReadTheSkillCell(published.cellYAccesses, published.cellY, context.random);
        if (auto sent = SendTheTurn(x, y, published.angles.z, context, "0x55fd72"); !sent.HasValue())
            return sent;
    }
    // 0x55ffd6..0x560080: la skill del lugar, con el registro propio abierto y cerrado.
    std::uint8_t skill = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        skill = OwnSkillByte(world.OwnCharacter(), request.slot);
    }
    // 0x560080..0x5600dc: el blanco sin mirar el −1, que MuRet no lee fuera del pool.
    if (request.target < 0 || static_cast<std::size_t>(request.target) >= world.Actors().size())
        return Core::Unexpected(
            std::format("0x560080: el blanco {} de la skill del elfo está fuera del pool", request.target));
    const ActorState& target = world.Actors()[static_cast<std::size_t>(request.target)];
    user.attackPosition = target.position;
    turned.angles.z = static_cast<float>(
        Render::Models::CreateAngle(turned.position.x, turned.position.y, target.position.x, target.position.y));
    const ActorCombatContext combat = CombatContextOf(context);
    switch (skill)
    {
    case 0x1a:
    case 0x1b:
    case 0x1c:
    {
        // 0x5600fa..0x56085a.
        if (auto struck = StrikeTheTarget(skill, target, context, "0x560170"); !struck.HasValue())
            return struck;
        return SetTheHitAction(user, combat);
    }
    case 0x33:
    {
        // 0x56085b..0x560ff6.
        const auto arrows = HasArrows(context);
        if (!arrows.HasValue())
            return Core::Unexpected(arrows.Error());
        if (!arrows.Value())
            return {};
        if (auto struck = StrikeTheTarget(skill, target, context, "0x5608de"); !struck.HasValue())
            return struck;
        return SetActorAttack(user, combat);
    }
    case 0x2e:
    {
        // 0x560ff7..0x5617f5.
        const auto arrows = HasArrows(context);
        if (!arrows.HasValue())
            return Core::Unexpected(arrows.Error());
        if (!arrows.Value())
            return {};
        if (auto struck = StrikeTheTarget(skill, target, context, "0x56107b"); !struck.HasValue())
            return struck;
        return SetTheShotAction(user, combat);
    }
    default:
        return {};
    }
}
} // namespace Client::Scenes
