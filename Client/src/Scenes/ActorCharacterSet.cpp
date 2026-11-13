#include "Scenes/ActorCharacterSet.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/HelperTable.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::uint16_t HeightOffsetPet = 0x1c15;

std::uint8_t ByteOf(GameLogic::Equipment equipment, std::size_t index)
{
    return std::to_integer<std::uint8_t>(equipment[index]);
}

std::uint8_t BitOf(std::uint8_t value, int bit)
{
    return static_cast<std::uint8_t>((value >> bit) & 1);
}

struct PetAndHelper
{
    std::uint16_t pet = NoItemModel;
    std::uint16_t helper = NoItemModel;
};

// 0x513b3c..0x513bd6: la mascota de b[4] & 3 y su ayudante.
PetAndHelper BasePet(GameLogic::Equipment equipment)
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
        return {Imp, NoItemModel}; // 0x513bc0..0x513bc6 no tiene caso 1.
    case 2:
        return {Unilia, UniliaHelper};
    default:
        return (ByteOf(equipment, 9) & 1) != 0 ? PetAndHelper{HeightOffsetPet, DinorantHelper} : PetAndHelper{};
    }
}

Core::Expected<void, std::string> CreatePet(WorldState& world, std::size_t slot, PetAndHelper pet,
                                            Render::Models::CrtRandom& random)
{
    ActorState& actor = world.Actors()[slot];
    actor.pet.model = pet.pet;
    actor.previousHad1c15 = pet.pet == HeightOffsetPet;
    if (pet.pet == NoItemModel)
    {
        actor.pet.flags1b = 0;
        actor.pet.flags1c = 0;
    }
    if (pet.helper == NoItemModel)
    {
        return {};
    }
    auto created = CreateHelper(world, pet.helper, actor.position, slot, &random);
    return created.HasValue() ? Core::Expected<void, std::string>{} : Core::Unexpected(created.Error());
}

// 0x513a12..0x513b27: las manos y las alas.
Core::Expected<void, std::string> PutHandsAndWings(ActorState& actor, const GameLogic::CharacterAppearance& appearance,
                                                   const WorldEntryResources& resources)
{
    actor.hands[0].model = appearance.rightHand ? appearance.rightHand->model : NoItemModel;
    actor.hands[1].model = appearance.leftHand ? appearance.leftHand->model : NoItemModel;
    // 0x513aab..0x513ab4: el cuervo de la clase 4 con 0xa05 y 0x420cf0.
    if (actor.hands[1].model == Raven)
    {
        if (auto raven = ReplaceTheRaven(actor, resources); !raven.HasValue())
            return raven;
    }
    actor.wings.model = appearance.wings.value_or(NoItemModel);
    if (!appearance.wings)
    {
        actor.wings.flags1b = 0;
        actor.wings.flags1c = 0;
    }
    return {};
}

// 0x513cf3..0x513d27: +0x360 con el bit 1 de b[11]. Entre 1 y 3 (el arma del evento de Blood Castle) queda.
void PutTheEventWeapon(ActorState& actor, GameLogic::Equipment equipment)
{
    constexpr std::uint8_t LastEventWeapon = 3;
    constexpr std::uint8_t Byte11Weapon = 4;
    const bool kept = actor.byte360 != 0 && actor.byte360 <= LastEventWeapon;
    if (kept)
        return;
    actor.byte360 = (ByteOf(equipment, 11) & 2) != 0 ? Byte11Weapon : 0;
}

// 0x513d84..0x51409b: con +0x3e3 en 0, la ropa y los bits de las partes y de las manos.
void PutTheBody(ActorState& actor, const GameLogic::CharacterAppearance& appearance, GameLogic::Equipment equipment)
{
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const GameLogic::WornItem& worn = appearance.bodyParts[part];
        ActorItemPart& body = actor.body[part];
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
} // namespace

Core::Expected<void, std::string> CreateCharacterSetPets(WorldState& world, std::size_t slot,
                                                         GameLogic::Equipment equipment,
                                                         Render::Models::CrtRandom& random)
{
    if (auto base = CreatePet(world, slot, BasePet(equipment), random); !base.HasValue())
    {
        return base;
    }
    const std::uint8_t flags = ByteOf(equipment, 11);
    constexpr std::uint16_t DarkHorse = 0x1c16;
    if ((flags & 1) != 0)
    {
        if (auto horse = CreatePet(world, slot, {DarkHorse, DarkHorseModel}, random); !horse.HasValue())
        {
            return horse;
        }
    }
    constexpr std::uint16_t Fenrir = 0x1c37;
    if ((flags & 4) != 0)
    {
        auto& part = world.Actors()[slot].pet;
        part.flags1b = static_cast<std::uint8_t>(ByteOf(equipment, 15) & 3);
        constexpr std::uint16_t RedFenrir = 0x14a;
        constexpr std::uint16_t BlueFenrir = 0x14c;
        constexpr std::uint16_t OtherFenrir = 0x14b;
        const auto helper = part.flags1b == 1 ? RedFenrir : part.flags1b == 2 ? BlueFenrir : OtherFenrir;
        return CreatePet(world, slot, {Fenrir, helper}, random);
    }
    return {};
}

Core::Expected<void, std::string> ApplyCharacterSet(std::size_t index, GameLogic::Equipment equipment,
                                                    const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& actor = world.Actors()[index];
    // 0x513a12: solo los jugadores.
    if (actor.type != Data::Models::PlayerSkeletonModel)
        return {};
    const GameLogic::CharacterAppearance appearance =
        GameLogic::AppearanceOf(actor.characterClass, actor.skin, equipment);
    if (auto carried = PutHandsAndWings(actor, appearance, resources); !carried.HasValue())
        return carried;
    // 0x513b2e: con el cuarto argumento en 0, 0x443d40.
    TurnOffHelpersOf(world.Helpers(), index);
    if (auto pets = CreateCharacterSetPets(world, index, equipment, resources.cloth.random); !pets.HasValue())
        return pets;
    // 0x513cee.
    ReleaseTheLayer(actor);
    PutTheEventWeapon(actor, equipment);
    // 0x513d27..0x513d77: L = b[5] << 16 | b[6] << 8 | b[7]; las manos con los bits 0..2 y 3..5, y las alas y la
    // mascota con el nivel en 0.
    constexpr int LeftLevelShift = 3;
    const std::uint32_t levels = (static_cast<std::uint32_t>(ByteOf(equipment, 5)) << 16) |
                                 (static_cast<std::uint32_t>(ByteOf(equipment, 6)) << 8) | ByteOf(equipment, 7);
    actor.hands[0].level = GameLogic::ItemLevelOf(static_cast<std::uint8_t>(levels & 7));
    actor.hands[1].level = GameLogic::ItemLevelOf(static_cast<std::uint8_t>((levels >> LeftLevelShift) & 7));
    actor.wings.level = 0;
    actor.pet.level = 0;
    // 0x513d7e.
    if (actor.byte3e3 != 0)
        return {};
    PutTheBody(actor, appearance, equipment);
    // 0x5140a1 y 0x5140a7.
    ArmForTheEventMaps(actor, world.Runtime().map);
    ApplyHeadAndScale(actor);
    return {};
}
} // namespace Client::Scenes
