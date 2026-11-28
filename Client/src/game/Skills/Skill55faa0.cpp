#include "Skills/Skill55faa0.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Protocol/Skills/Skill.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"
#include "Movement/GameMovementState.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkillSupport.h"
#include "Skills/HeroSkills.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill4f5290.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Skills
{
namespace
{
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;

bool InHalfOpenRange(std::int32_t value, std::int32_t first, std::int32_t end)
{
    return value >= first && value < end;
}

// 0x4f4915..0x4f4938: a bow in the left hand (+0x2d8, the signed WORD CMP): 0xa12 to 0xa18, 0xa23, 0xa26 or 0xa27.
bool BowInTheLeft(const Kernel::ActorState& actor)
{
    const auto left = static_cast<std::int16_t>(actor.hands[1].model);
    return InHalfOpenRange(left, 0xa12, 0xa19) || left == 0xa23 || left == 0xa26 || left == 0xa27;
}

// 0x4f493a..0x4f4967: a crossbow in the right hand (+0x2b8): 0xa1a to 0xa20, 0xa22 or 0xa24 to 0xc11.
bool CrossbowInTheRight(const Kernel::ActorState& actor)
{
    const auto right = static_cast<std::int16_t>(actor.hands[0].model);
    return InHalfOpenRange(right, 0xa1a, 0xa21) || right == 0xa22 || InHalfOpenRange(right, 0xa24, 0xc12);
}

// 0x4f48e0(character): on a player, the speeds (0x4f3f80) and the shooting action by hands. Riding Uniria or Dinorant
// (+0x318) outside the safe zone (+0x3e2), 0x82 with the bow and 0x83 with the crossbow; otherwise, with the bow 0x7e
// without wings (+0x2f8 at −1) and 0x80 with them, and with the crossbow 0x7f and 0x81. Without a ranged weapon, none.
// With or without an action, of any type, +0x38f goes up.
Core::Expected<void, std::string> SetTheShotAction(Kernel::ActorState& actor, const Combat::ActorCombatContext& context)
{
    if (actor.type == Tables::PlayerSkeletonModel)
    {
        Combat::RefreshCombatActionSpeeds(context.world, context.random);
        const bool riding = (actor.pet.model == UniriaModel || actor.pet.model == DinorantModel) && actor.safeZone == 0;
        const bool winged = actor.wings.model != Kernel::NoItemModel;
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

// The inline C1 06 19 (0x56018f.., 0x5608fd.. and 0x56109a..): without "webzen" in the published hero and with the
// brake (which with 0x28 does not brake), with the target's key.
Core::Expected<void, std::string> StrikeTheTarget(std::uint8_t skill, const Kernel::ActorState& target,
                                                  const HeroSkillContext& context, std::string_view where)
{
    if (HasTheSilentName(context.world))
        return {};
    const auto passes = PassesTheSkillBrake(skill, context, where);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (passes.Value())
        SendTheSkillPacket(context, Protocol::Skills::WriteMagicAttack(skill, target.key));
    return {};
}

// 0x561c50 (CheckTheArrows) with the context's services, or without them.
Core::Expected<bool, std::string> HasArrows(const HeroSkillContext& context)
{
    static const Items::InventoryServices NoServices{};
    return Items::CheckTheArrows(
        Items::InventoryMoveContext{.world = context.world,
                                    .random = context.random,
                                    .services = context.inventory != nullptr ? *context.inventory : NoServices});
}
} // namespace

Core::Expected<void, std::string> UseTheElfSkillOnItsTarget(Kernel::ActorState& user, Kernel::ActorState& turned,
                                                            const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Movement::WeaponSkillRequest request = runtime.movement.weaponSkill;
    // 0x55fac2..0x55ffcf: the turning D3, before anything, with the published hero's current angle.
    {
        Kernel::ActorState& published = world.Hero();
        const std::uint8_t x = ReadTheSkillCell(published.cellXAccesses, published.cellX, context.random);
        const std::uint8_t y = ReadTheSkillCell(published.cellYAccesses, published.cellY, context.random);
        if (auto sent = SendTheTurn(x, y, published.angles.z, context, "0x55fd72"); !sent.HasValue())
            return sent;
    }
    // 0x55ffd6..0x560080: the slot's skill, with the own record opened and closed.
    std::uint8_t skill = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        skill = Kernel::OwnSkillByte(world.OwnCharacter(), request.slot);
    }
    // 0x560080..0x5600dc: the target without checking the −1, which MuRet does not read outside the pool.
    if (request.target < 0 || static_cast<std::size_t>(request.target) >= world.Actors().size())
        return Core::Unexpected(
            std::format("0x560080: el blanco {} de la skill del elfo está fuera del pool", request.target));
    const Kernel::ActorState& target = world.Actors()[static_cast<std::size_t>(request.target)];
    user.attackPosition = target.position;
    turned.angles.z = static_cast<float>(
        Math::CreateAngle(turned.position.x, turned.position.y, target.position.x, target.position.y));
    const Combat::ActorCombatContext combat = CombatContextOf(context);
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
        return Combat::SetActorAttack(user, combat);
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
} // namespace Client::Skills
