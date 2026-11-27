#include "Actors/ActorEquipment.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Kernel/CharacterClass.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Actors/FullSetScan.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// 0x512a30, checked against PE c32bbd5f (493 instructions, 1,733 bytes), with 0x4053f0 (44, 139),
// 0x4053a0 (77 bytes) and 0x512930 (56, 205): 06, "El equipo propio en el personaje".
namespace Client::Actors
{
namespace
{
// The slots of the own record it reads, 0x48 apart from the type at +0xb6c. It reads nothing from 9 to 11.
constexpr std::size_t RightHandSlot = 0;
constexpr std::size_t LeftHandSlot = 1;
constexpr std::size_t FirstBodySlot = 2;
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t PetSlot = 8;

// The level of a part: (B1 >> 3) & 0xf of the item (0x512b4c..0x512b52).
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// With these, 0x512a30 does not call 0x4f2250 (0x512ba1..0x512bcd): maps 18 to 23, actions 0xc5 to
// 0xcc and those from 0x22 to 0x97.
constexpr std::int32_t FirstNoStopMap = 18;
constexpr std::int32_t LastNoStopMap = 23;
constexpr std::uint8_t FirstNoStopAction = 0xc5;
constexpr std::uint8_t LastNoStopAction = 0xcc;
constexpr std::uint8_t FirstBusyAction = 0x22;
constexpr std::uint8_t LastBusyAction = 0x97;

// The pet 0x511290 looks at in the previous word318.
constexpr std::uint16_t HeightOffsetPet = 0x1c15;

// The weapons 0x4053f0 sets on maps 18 to 23 by +0x1d4 & 7 (0x40540c..0x405472), with their
// Item.bmd names: 0 16 (Sword of Destruction) in both hands of classes 1, 3 and 4, 4 19
// (Great Reign Crossbow) in the right hand of 2 and 5 5 (Legendary Staff) in the right hand of 0.
constexpr std::uint16_t EventSwordModel = Tables::ItemModel(0, 16);
constexpr std::uint16_t EventCrossbowModel = Tables::ItemModel(4, 19);
constexpr std::uint16_t EventStaffModel = Tables::ItemModel(5, 5);
static_assert(EventSwordModel == 0x222 && EventCrossbowModel == 0xa25 && EventStaffModel == 0xc17);

bool InNoStopMaps(std::int32_t map)
{
    return map >= FirstNoStopMap && map <= LastNoStopMap;
}

std::uint16_t ModelOf(const Kernel::ItemState& item, std::uint16_t empty)
{
    if (item.id == Kernel::NoItemModel)
    {
        return empty;
    }
    return static_cast<std::uint16_t>(item.id + Tables::FirstItemModel);
}

std::uint8_t LevelOf(const Kernel::ItemState& item)
{
    return static_cast<std::uint8_t>((item.scalar1Low >> LevelShift) & LevelMask);
}

// 0x512abf..0x512b9b: the hands with their +0x1b byte; the wings and the pet, without bytes.
void ProjectCarried(Kernel::ActorState& actor, const Kernel::OwnCharacterRecord& own)
{
    static_assert(LeftHandSlot == RightHandSlot + 1);
    for (std::size_t side = 0; side < actor.hands.size(); ++side)
    {
        const Kernel::ItemState& item = own.equipment[RightHandSlot + side];
        Kernel::ActorItemPart& hand = actor.hands[side];
        hand.model = ModelOf(item, Kernel::NoItemModel);
        hand.level = LevelOf(item);
        hand.flags1b = item.flags1b;
    }
    actor.wings.model = ModelOf(own.equipment[WingsSlot], Kernel::NoItemModel);
    actor.wings.level = LevelOf(own.equipment[WingsSlot]);
    actor.pet.model = ModelOf(own.equipment[PetSlot], Kernel::NoItemModel);
    actor.pet.level = LevelOf(own.equipment[PetSlot]);
    actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
}

// 0x512bb3..0x512bcd.
bool CallsStop(const Kernel::ActorState& actor, std::int32_t map)
{
    const std::uint8_t action = actor.animation.action;
    const bool skipped = action >= FirstNoStopAction && action <= LastNoStopAction;
    const bool busy = action >= FirstBusyAction && action <= LastBusyAction;
    return !InNoStopMaps(map) && !skipped && !busy;
}

// 0x512cc7..0x512ec9: the five parts; an empty slot leaves the class's one.
void ProjectBody(Kernel::ActorState& actor, const Kernel::OwnCharacterRecord& own)
{
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const Kernel::ItemState& item = own.equipment[FirstBodySlot + part];
        const auto ofClass = Kernel::ClassPartModel(static_cast<Kernel::BodyPart>(part), actor.characterClass);
        Kernel::ActorItemPart& worn = actor.body[part];
        worn.model = ModelOf(item, ofClass);
        worn.level = LevelOf(item);
        worn.flags1b = item.flags1b;
        worn.flags1c = item.flags1c;
    }
}

// 0x4053a0: the hands, the wings and the pet without an item and with the level at 0, and the hands' +0x1b byte
// at 0. Those of the wings and the pet stay.
void ClearCarried(Kernel::ActorState& actor)
{
    for (Kernel::ActorItemPart& hand : actor.hands)
    {
        hand.model = Kernel::NoItemModel;
        hand.level = 0;
        hand.flags1b = 0;
    }
    actor.wings.model = Kernel::NoItemModel;
    actor.wings.level = 0;
    actor.pet.model = Kernel::NoItemModel;
    actor.pet.level = 0;
    actor.previousHad1c15 = false;
}

// 0x512cc7..0x512ef5: the clothes and what follows.
Core::Expected<void, std::string> Dress(Kernel::ActorState& actor, const ActorEquipmentContext& context)
{
    ProjectBody(actor, context.own);
    ArmForTheEventMaps(actor, context.stop.runtime.map);
    ApplyHeadAndScale(actor);
    // 0x512eda..0x512eec: nobody reads what it returns. With another character, 0x550990 reads what the last call
    // left.
    if (&actor == &context.publishedHero)
    {
        context.fullSetScan =
            ScanFullSet(ScannedCharacterOf(actor), context.own, context.ownRecordAccesses, context.stop.random).globals;
    }
    return Player::RecalculateOwnStats(context.own, {.itemSetTables = context.itemSetTables,
                                                     .itemSets = context.itemSets,
                                                     .items = context.stop.items,
                                                     .publishedHero = context.publishedHero,
                                                     .ownRecordAccesses = context.ownRecordAccesses,
                                                     .random = context.stop.random,
                                                     .held = context.held,
                                                     .scan505820 = context.fullSetScan});
}
} // namespace

void ArmForTheEventMaps(Kernel::ActorState& actor, std::int32_t map)
{
    if (!InNoStopMaps(map))
    {
        return;
    }
    ClearCarried(actor);
    Kernel::ActorItemPart& right = actor.hands[0];
    switch (Kernel::BaseClassOf(actor.characterClass))
    {
    case Kernel::CharacterClass::DarkWizard:
        right.model = EventStaffModel;
        break;
    case Kernel::CharacterClass::Elf:
        right.model = EventCrossbowModel;
        break;
    case Kernel::CharacterClass::DarkKnight:
    case Kernel::CharacterClass::MagicGladiator:
    case Kernel::CharacterClass::DarkLord:
        right.model = EventSwordModel;
        actor.hands[1].model = EventSwordModel;
        break;
    default:
        break;
    }
}

void ApplyHeadAndScale(Kernel::ActorState& actor)
{
    if (actor.byte3e3 != 0)
    {
        return;
    }
    const auto helm = actor.body[static_cast<std::size_t>(Kernel::BodyPart::Helm)].model;
    actor.head = Kernel::HeadUnder(helm, actor.characterClass).value_or(Kernel::NoItemModel);
    if (const std::optional<float> scale = Kernel::ClassScale(actor.characterClass, actor.skin))
    {
        actor.scale = *scale;
    }
}

Core::Expected<ActorStopResult, std::string> ApplyActorEquipment(Kernel::ActorState& actor,
                                                                 const ActorEquipmentContext& context)
{
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        return ActorStopResult{};
    }
    // 0x512a48..0x512aba opens it; the close, in 0x512bea..0x512cc6 or inline in 0x512efa..0x5130cf,
    // is this access's, also when MuRet stops earlier.
    const Models::LogicalAccess ownAccess(context.ownRecordAccesses, context.stop.random,
                                          Models::OwnRecordCloseRandoms);
    ProjectCarried(actor, context.own);
    ActorStopResult stop;
    if (CallsStop(actor, context.stop.runtime.map))
    {
        auto stopped = SetActorStop(actor, context.stop);
        if (!stopped.HasValue())
        {
            return Core::Unexpected(stopped.Error());
        }
        stop = stopped.Value();
    }
    if (actor.byte3e3 != 0)
    {
        return stop;
    }
    const auto dressed = Dress(actor, context);
    if (!dressed.HasValue())
    {
        return Core::Unexpected(dressed.Error());
    }
    return stop;
}
} // namespace Client::Actors
