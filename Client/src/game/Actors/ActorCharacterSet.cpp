#include "Actors/ActorCharacterSet.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Actors/ActorEquipment.h"
#include "Kernel/ActorState.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/HelperTable.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Actors
{
namespace
{
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::uint16_t HeightOffsetPet = 0x1c15;

std::uint8_t ByteOf(Kernel::Equipment equipment, std::size_t index)
{
    return std::to_integer<std::uint8_t>(equipment[index]);
}

std::uint8_t BitOf(std::uint8_t value, int bit)
{
    return static_cast<std::uint8_t>((value >> bit) & 1);
}

struct PetAndHelper
{
    std::uint16_t pet = Kernel::NoItemModel;
    std::uint16_t helper = Kernel::NoItemModel;
};

// 0x513b3c..0x513bd6: the pet of b[4] & 3 and its helper.
PetAndHelper BasePet(Kernel::Equipment equipment)
{
    constexpr std::uint16_t Guardian = 0x1c12;
    constexpr std::uint16_t Imp = 0x1c13;
    constexpr std::uint16_t Unilia = 0x1c14;
    constexpr std::uint16_t UniliaHelper = 0xc6;
    constexpr std::uint16_t DinorantHelper = 0x110;
    switch (ByteOf(equipment, 4) & 3)
    {
    case 0:
        return {Guardian, Guardian};
    case 1:
        return {Imp, Kernel::NoItemModel}; // 0x513bc0..0x513bc6 has no case 1.
    case 2:
        return {Unilia, UniliaHelper};
    default:
        return (ByteOf(equipment, 9) & 1) != 0 ? PetAndHelper{HeightOffsetPet, DinorantHelper} : PetAndHelper{};
    }
}

// Where the pets' helpers go: with `own` as nullptr, to the pool with 0x443fa0 and the character's slot; otherwise,
// to that own helper with 0x443d70 (the object of 0x5139f0, 0x513b7b..0x513ce5).
struct PetTarget
{
    Kernel::WorldState& world;
    Kernel::ActorState& actor;
    std::size_t slot = 0;
    Scenes::HelperState* own = nullptr;
};

Core::Expected<void, std::string> CreatePet(const PetTarget& target, PetAndHelper pet, Crt::CrtRandom& random)
{
    Kernel::ActorState& actor = target.actor;
    actor.pet.model = pet.pet;
    actor.previousHad1c15 = pet.pet == HeightOffsetPet;
    if (pet.pet == Kernel::NoItemModel)
    {
        actor.pet.flags1b = 0;
        actor.pet.flags1c = 0;
    }
    if (pet.helper == Kernel::NoItemModel)
    {
        return {};
    }
    if (target.own != nullptr)
    {
        auto built = Scenes::ConstructHelper(*target.own, pet.helper, actor.position, actor, Kernel::ActorPoolSize,
                                             target.world.Runtime().map, &random);
        return built.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(built.Error());
    }
    auto created = Scenes::CreateHelper(target.world, pet.helper, actor.position, target.slot, &random);
    return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
}

// 0x513a12..0x513b27: the hands and the wings.
Core::Expected<void, std::string> PutHandsAndWings(Kernel::ActorState& actor,
                                                   const Kernel::CharacterAppearance& appearance,
                                                   const Scenes::WorldEntryResources& resources)
{
    actor.hands[0].model = appearance.rightHand ? appearance.rightHand->model : Kernel::NoItemModel;
    actor.hands[1].model = appearance.leftHand ? appearance.leftHand->model : Kernel::NoItemModel;
    // 0x513aab..0x513ab4: the raven of class 4 with 0xa05 and 0x420cf0.
    if (actor.hands[1].model == Raven)
    {
        if (auto raven = Scenes::ReplaceTheRaven(actor, resources); !raven.HasValue())
            return raven;
    }
    actor.wings.model = appearance.wings.value_or(Kernel::NoItemModel);
    if (!appearance.wings)
    {
        actor.wings.flags1b = 0;
        actor.wings.flags1c = 0;
    }
    return {};
}

// 0x513cf3..0x513d27: +0x360 with bit 1 of b[11]. Between 1 and 3 (the Blood Castle event weapon) it stays.
void PutTheEventWeapon(Kernel::ActorState& actor, Kernel::Equipment equipment)
{
    constexpr std::uint8_t LastEventWeapon = 3;
    constexpr std::uint8_t Byte11Weapon = 4;
    const bool kept = actor.byte360 != 0 && actor.byte360 <= LastEventWeapon;
    if (kept)
        return;
    actor.byte360 = (ByteOf(equipment, 11) & 2) != 0 ? Byte11Weapon : std::uint8_t{0};
}

// 0x513d84..0x51409b: with +0x3e3 at 0, the clothes and the bits of the parts and of the hands.
void PutTheBody(Kernel::ActorState& actor, const Kernel::CharacterAppearance& appearance, Kernel::Equipment equipment)
{
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const Kernel::WornItem& worn = appearance.bodyParts[part];
        Kernel::ActorItemPart& body = actor.body[part];
        body.model = worn.model;
        body.level = worn.level;
        body.flags1b = static_cast<std::uint8_t>(worn.excellent);
        body.flags1c = static_cast<std::uint8_t>(worn.ancient);
    }
    const std::uint8_t excellent = ByteOf(equipment, 9);
    const std::uint8_t ancient = ByteOf(equipment, 10);
    actor.hands[0].flags1b = BitOf(excellent, 2);
    actor.hands[1].flags1b = BitOf(excellent, 1);
    actor.hands[0].flags1c = BitOf(ancient, 2);
    actor.hands[1].flags1c = BitOf(ancient, 1);
    actor.byte358 = BitOf(ancient, 0);
}

Core::Expected<void, std::string> CreatePets(const PetTarget& target, Kernel::Equipment equipment,
                                             Crt::CrtRandom& random)
{
    if (auto base = CreatePet(target, BasePet(equipment), random); !base.HasValue())
    {
        return base;
    }
    const std::uint8_t flags = ByteOf(equipment, 11);
    constexpr std::uint16_t DarkHorse = 0x1c16;
    if ((flags & 1) != 0)
    {
        if (auto horse = CreatePet(target, {DarkHorse, Scenes::DarkHorseModel}, random); !horse.HasValue())
        {
            return horse;
        }
    }
    constexpr std::uint16_t Fenrir = 0x1c37;
    if ((flags & 4) != 0)
    {
        auto& part = target.actor.pet;
        part.flags1b = static_cast<std::uint8_t>(ByteOf(equipment, 15) & 3);
        constexpr std::uint16_t RedFenrir = 0x14a;
        constexpr std::uint16_t BlueFenrir = 0x14c;
        constexpr std::uint16_t OtherFenrir = 0x14b;
        const auto helper = part.flags1b == 1 ? RedFenrir : part.flags1b == 2 ? BlueFenrir : OtherFenrir;
        return CreatePet(target, {Fenrir, helper}, random);
    }
    return {};
}

// 0x5139f0 from 0x513a12, on the character of `target`.
Core::Expected<void, std::string> ApplyTheSet(const PetTarget& target, Kernel::Equipment equipment,
                                              const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::ActorState& actor = target.actor;
    // 0x513a12: only players.
    if (actor.type != Tables::PlayerSkeletonModel)
        return {};
    const Kernel::CharacterAppearance appearance = Kernel::AppearanceOf(actor.characterClass, actor.skin, equipment);
    if (auto carried = PutHandsAndWings(actor, appearance, resources); !carried.HasValue())
        return carried;
    // 0x513b27..0x513b39: with the fourth argument at 0, 0x443d40; with a helper, its live byte at 0.
    if (target.own != nullptr)
        target.own->alive = false;
    else
        Scenes::TurnOffHelpersOf(world.Helpers(), target.slot);
    if (auto pets = CreatePets(target, equipment, resources.cloth.random); !pets.HasValue())
        return pets;
    // 0x513cee.
    Kernel::ReleaseTheLayer(actor);
    PutTheEventWeapon(actor, equipment);
    // 0x513d27..0x513d77: L = b[5] << 16 | b[6] << 8 | b[7]; the hands with bits 0..2 and 3..5, and the wings and
    // the pet with the level at 0.
    constexpr int LeftLevelShift = 3;
    const std::uint32_t levels = (static_cast<std::uint32_t>(ByteOf(equipment, 5)) << 16) |
                                 (static_cast<std::uint32_t>(ByteOf(equipment, 6)) << 8) | ByteOf(equipment, 7);
    actor.hands[0].level = Kernel::ItemLevelOf(static_cast<std::uint8_t>(levels & 7));
    actor.hands[1].level = Kernel::ItemLevelOf(static_cast<std::uint8_t>((levels >> LeftLevelShift) & 7));
    actor.wings.level = 0;
    actor.pet.level = 0;
    // 0x513d7e.
    if (actor.byte3e3 != 0)
        return {};
    PutTheBody(actor, appearance, equipment);
    // 0x5140a1 and 0x5140a7.
    ArmForTheEventMaps(actor, world.Runtime().map);
    ApplyHeadAndScale(actor);
    return {};
}
} // namespace

Core::Expected<void, std::string> CreateCharacterSetPets(Kernel::WorldState& world, std::size_t slot,
                                                         Kernel::Equipment equipment, Crt::CrtRandom& random)
{
    return CreatePets({.world = world, .actor = world.Actors()[slot], .slot = slot}, equipment, random);
}

Core::Expected<void, std::string> ApplyCharacterSet(std::size_t index, Kernel::Equipment equipment,
                                                    const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    return ApplyTheSet({.world = world, .actor = world.Actors()[index], .slot = index}, equipment, resources);
}

Core::Expected<void, std::string> ApplyCharacterSetTo(Kernel::ActorState& actor, Scenes::HelperState& helper,
                                                      Kernel::Equipment equipment,
                                                      const Scenes::WorldEntryResources& resources)
{
    return ApplyTheSet({.world = resources.world, .actor = actor, .slot = Kernel::ActorPoolSize, .own = &helper},
                       equipment, resources);
}
} // namespace Client::Actors
