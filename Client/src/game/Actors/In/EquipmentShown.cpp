#include "Actors/In/EquipmentShown.h"

#include "Kernel/ActorSearch.h"
#include "Actors/ActorEquipment.h"
#include "Actors/ViewportHooks.h"
#include "Core/Expected.h"
#include "Kernel/ActorState.h"
#include "Kernel/CharacterAppearance.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Protocol/Actors/Viewport.h"
#include "Assets/ModelIds.h"
#include "Scenes/OwnItemPlacement.h"
#include "Tables/PlayerModels.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Actors::In
{
namespace
{
std::uint8_t ByteOf(std::span<const std::byte> bytes, std::size_t index)
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

// 0x513340 with the whole nibble: from 8 to 15 it leaves through the JA with 0 (0x51334e).
std::uint8_t ShownLevelOf(std::uint8_t bits)
{
    constexpr std::uint8_t LastLevelBits = 7;
    return bits > LastLevelBits ? std::uint8_t{0} : Kernel::ItemLevelOf(bits);
}

std::uint16_t ModelOf(std::uint16_t type)
{
    return static_cast<std::uint16_t>(type + Tables::FirstItemModel);
}

struct ShownItem
{
    std::uint16_t type = 0;
    std::uint8_t level = 0;
    std::uint8_t excellent = 0;
    std::uint8_t ancient = 0;
};

// 0x4c83ec..0x4c8486: the hands. An empty right hand does not touch the level; an empty left one releases the published
// hero's raven ([0x743a170], 0x4c8438).
Core::Expected<void, std::string> ShowInTheHand(Kernel::ActorState& actor, std::size_t hand, const ShownItem& item,
                                                const IViewportHooks& hooks)
{
    Kernel::ActorItemPart& part = actor.hands[hand];
    if (item.type == Scenes::NoInventoryItem)
    {
        part.model = Kernel::NoItemModel;
        part.flags1b = 0;
        if (hand == 1)
            hooks.ReleaseThePublishedHerosRaven();
        return {};
    }
    part.model = ModelOf(item.type);
    part.level = item.level;
    part.flags1b = item.excellent;
    if (hand == 1)
        return hooks.RecreateTheRaven(actor);
    return {};
}

// 0x4c848b..0x4c8667: the five of the body; an empty one takes the class's part with the three bytes at 0.
void ShowOnTheBody(Kernel::ActorState& actor, std::size_t index, const ShownItem& item)
{
    Kernel::ActorItemPart& part = actor.body[index];
    if (item.type == Scenes::NoInventoryItem)
    {
        part.model = Kernel::ClassPartModel(static_cast<Kernel::BodyPart>(index), actor.characterClass);
        part.level = 0;
        part.flags1b = 0;
        part.flags1c = 0;
        return;
    }
    part.model = ModelOf(item.type);
    part.level = item.level;
    part.flags1b = item.excellent;
    part.flags1c = item.ancient;
}

// 0x4c8711..0x4c87d4: the pet and its helper (the table at 0x4c882c on the type − 0x1a00).
Core::Expected<void, std::string> ShowThePet(Kernel::WorldState& world, std::size_t index, const ShownItem& item,
                                             const IViewportHooks& hooks)
{
    Kernel::ActorState& actor = world.Actors()[index];
    constexpr std::uint16_t Dinorant = 0x1c15;
    constexpr std::uint16_t GuardianAngelHelper = 0x1c12;
    constexpr std::uint16_t UniriaHelper = 0xc6;
    constexpr std::uint16_t DinorantHelper = 0x110;
    if (item.type == Scenes::NoInventoryItem)
    {
        actor.pet.model = Kernel::NoItemModel;
        actor.previousHad1c15 = false;
        hooks.TurnOffTheHelpersOf(index);
        return {};
    }
    actor.pet.model = ModelOf(item.type);
    actor.previousHad1c15 = actor.pet.model == Dinorant;
    actor.pet.level = 0;
    constexpr std::uint16_t FirstPet = 0x1a00;
    std::uint16_t helper = 0;
    switch (item.type - FirstPet)
    {
    case 0:
        helper = GuardianAngelHelper;
        break;
    case 2:
        helper = UniriaHelper;
        break;
    case 3:
        helper = DinorantHelper;
        break;
    case 4:
        helper = Assets::DarkHorseModel;
        break;
    case 0x25:
    {
        // 0x4c879a..0x4c87ca: Fenrir, with the excellent bits (& 0x3f) at +0x31b.
        constexpr std::uint16_t RedFenrir = 0x14a;
        constexpr std::uint16_t BlueFenrir = 0x14c;
        constexpr std::uint16_t OtherFenrir = 0x14b;
        actor.pet.flags1b = item.excellent;
        helper = item.excellent == 1 ? RedFenrir : item.excellent == 2 ? BlueFenrir : OtherFenrir;
        break;
    }
    default:
        return {};
    }
    return hooks.CreateTheHelper(helper, actor.position, index);
}
} // namespace

Core::Expected<void, std::string> ApplyEquipmentShown(const Protocol::Actors::EquipmentShown& shown,
                                                      Kernel::WorldState& world, const IViewportHooks& hooks)
{
    const std::size_t index = Kernel::ActorWithKey(world, shown.key);
    Kernel::ActorState& actor = world.Actors()[index];
    const std::span<const std::byte> bytes = shown.item;
    // 0x4c83b4..0x4c83dc: pkt[6] (byte 1), pkt[8] (byte 3) and pkt[9] (byte 4).
    constexpr std::uint8_t LevelMask = 0xf;
    constexpr int SlotShift = 4;
    constexpr std::uint8_t ExcellentMask = 0x3f;
    const std::uint8_t placeAndLevel = ByteOf(bytes, 1);
    const ShownItem item{.type = Scenes::InventoryItemType(shown.item),
                         .level = ShownLevelOf(placeAndLevel & LevelMask),
                         .excellent = static_cast<std::uint8_t>(ByteOf(bytes, 3) & ExcellentMask),
                         .ancient = ByteOf(bytes, 4)};
    const auto place = static_cast<std::uint8_t>(placeAndLevel >> SlotShift);
    constexpr std::uint8_t FirstBodyPlace = 2;
    constexpr std::uint8_t LastBodyPlace = 6;
    constexpr std::uint8_t WingsPlace = 7;
    constexpr std::uint8_t PetPlace = 8;
    if (place < FirstBodyPlace)
    {
        if (auto shownInHand = ShowInTheHand(actor, place, item, hooks); !shownInHand.HasValue())
            return shownInHand;
    }
    else if (place <= LastBodyPlace)
    {
        ShowOnTheBody(actor, place - FirstBodyPlace, item);
    }
    else if (place == WingsPlace)
    {
        // 0x4c86e4..0x4c870c.
        if (item.type == Scenes::NoInventoryItem)
        {
            actor.wings.model = Kernel::NoItemModel;
        }
        else
        {
            actor.wings.level = 0;
            actor.wings.model = ModelOf(item.type);
        }
    }
    else if (place == PetPlace)
    {
        if (auto pet = ShowThePet(world, index, item, hooks); !pet.HasValue())
            return pet;
    }
    // 0x4c87d7..0x4c87de.
    ArmForTheEventMaps(actor, world.Runtime().map);
    ApplyHeadAndScale(actor);
    return {};
}
} // namespace Client::Actors::In
