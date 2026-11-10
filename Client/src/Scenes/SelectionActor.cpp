#include "Scenes/SelectionActor.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldState.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::uint16_t HeightOffsetPet = 0x1c15;
constexpr float CharacterOwnLight = 0.3f;

void PutAppearance(ActorState& actor, const GameLogic::CharacterAppearance& appearance)
{
    actor.characterClass = appearance.characterClass;
    actor.skin = appearance.skin;
    actor.scale = appearance.scale;
    actor.head = appearance.head.value_or(NoItemModel);
    const auto part = [](const GameLogic::WornItem& item)
    {
        return ActorItemPart{.model = item.model,
                             .level = item.level,
                             .flags1b = static_cast<std::uint8_t>(item.excellent),
                             .flags1c = static_cast<std::uint8_t>(item.ancient)};
    };
    for (std::size_t index = 0; index < actor.body.size(); ++index)
    {
        actor.body[index] = part(appearance.bodyParts[index]);
    }
    if (appearance.rightHand)
    {
        actor.hands[0] = part(*appearance.rightHand);
    }
    else
    {
        actor.hands[0].model = NoItemModel;
    }
    if (appearance.leftHand)
    {
        actor.hands[1] = part(*appearance.leftHand);
    }
    else
    {
        actor.hands[1].model = NoItemModel;
    }
    actor.wings.model = appearance.wings.value_or(NoItemModel);
    actor.pet.model = appearance.pet.value_or(NoItemModel);
    actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
}

struct PetAndHelper
{
    std::uint16_t pet = NoItemModel;
    std::uint16_t helper = NoItemModel;
};

PetAndHelper BasePet(const Protocol::Character::CharacterEntry& entry)
{
    constexpr std::uint16_t Guardian = 0x1c12;
    constexpr std::uint16_t Imp = 0x1c13;
    constexpr std::uint16_t Unilia = 0x1c14;
    constexpr std::uint16_t UniliaHelper = 0xc6;
    constexpr std::uint16_t DinorantHelper = 0x110;
    const auto value = std::to_integer<std::uint8_t>(entry.equipment[4]) & 3;
    switch (value)
    {
    case 0:
        return {Guardian, Guardian};
    case 1:
        return {Imp, NoItemModel}; // 0x513bc0..0x513bc6 no tiene caso 1.
    case 2:
        return {Unilia, UniliaHelper};
    default:
        return (std::to_integer<std::uint8_t>(entry.equipment[9]) & 1) != 0
                   ? PetAndHelper{HeightOffsetPet, DinorantHelper}
                   : PetAndHelper{};
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

Core::Expected<void, std::string> CreatePets(WorldState& world, std::size_t slot,
                                             const Protocol::Character::CharacterEntry& entry,
                                             Render::Models::CrtRandom& random)
{
    if (auto base = CreatePet(world, slot, BasePet(entry), random); !base.HasValue())
    {
        return base;
    }
    const auto flags = std::to_integer<std::uint8_t>(entry.equipment[11]);
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
        part.flags1b = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(entry.equipment[15]) & 3);
        constexpr std::uint16_t RedFenrir = 0x14a;
        constexpr std::uint16_t BlueFenrir = 0x14c;
        constexpr std::uint16_t OtherFenrir = 0x14b;
        const auto helper = part.flags1b == 1 ? RedFenrir : part.flags1b == 2 ? BlueFenrir : OtherFenrir;
        return CreatePet(world, slot, {Fenrir, helper}, random);
    }
    return {};
}

Core::Expected<void, std::string> EquipSelectionActor(WorldState& world, ActorState& actor,
                                                      const Protocol::Character::CharacterEntry& entry,
                                                      Render::Models::ClothWorld& cloth,
                                                      const ActorConstructionContext& construction)
{
    const auto appearance = GameLogic::AppearanceOf(actor.characterClass, 0, entry.equipment);
    actor.hands[0].model = appearance.rightHand ? appearance.rightHand->model : NoItemModel;
    actor.hands[1].model = appearance.leftHand ? appearance.leftHand->model : NoItemModel;
    if (actor.hands[1].model == Raven && (construction.map < 18 || construction.map > 23))
    {
        constexpr float CellCentre = 50.0f;
        const double height =
            HeightSkippedIn(construction.scene)
                ? 0.0
                : World::TerrainHeightAt(*construction.terrain.heights, *construction.terrain.attributes, CellCentre,
                                         CellCentre, construction.fixedHeight);
        actor.darkSpirit = CreateDarkSpirit(
            actor.cellX, actor.cellY, height, cloth.random,
            {.cellX = actor.cellXAccesses, .cellY = actor.cellYAccesses, .counter = cloth.openCounterAccesses});
    }
    actor.wings.model = appearance.wings.value_or(NoItemModel);
    if (!appearance.wings)
    {
        actor.wings.flags1b = 0;
        actor.wings.flags1c = 0;
    }
    TurnOffHelpersOf(world.Helpers(), entry.slot);
    if (auto pets = CreatePets(world, entry.slot, entry, cloth.random); !pets.HasValue())
    {
        return pets;
    }
    PutAppearance(actor, appearance);
    actor.byte358 = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(entry.equipment[10]) & 1);
    return {};
}
} // namespace

Core::Expected<void, std::string> ConstructSelectionActor(WorldState& world,
                                                          const Protocol::Character::CharacterEntry& entry,
                                                          const ActorConstructionContext& construction,
                                                          Render::Models::ClothWorld& cloth,
                                                          const Render::Models::Skeleton& skeleton)
{
    if (entry.slot >= SelectionActorCount)
    {
        return Core::Unexpected(std::string("slot de selección fuera del pool"));
    }
    ActorState& actor = world.Actors()[entry.slot];
    const CharacterSlot& place = CharacterSlots[entry.slot];
    const auto built =
        ConstructActor(actor, Data::Models::PlayerSkeletonModel, {.angle = place.angle}, construction, cloth);
    if (!built.HasValue())
    {
        return built;
    }
    actor.key = entry.slot;
    actor.position.x = place.position.x;
    actor.position.y = place.position.y;
    actor.light = glm::vec3(CharacterOwnLight);
    actor.flags78 = 0;
    actor.flags7c = 0;
    const auto characterClass = Protocol::Character::CharacterClassOf(entry.classByte);
    PutAppearance(actor, GameLogic::ClassAppearance(characterClass, 0));
    // 0x51736f, antes de 0x5139f0: las partes de la clase base, aun para una segunda clase.
    constexpr std::uint8_t ClassMask = 7;
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        actor.body[part].model = GameLogic::ClassPartModel(static_cast<GameLogic::BodyPart>(part),
                                                           static_cast<std::uint8_t>(characterClass & ClassMask));
    }
    actor.head = GameLogic::HeadUnder(actor.body[0].model, characterClass).value_or(NoItemModel);
    auto stopped = SetActorStop(actor, {.runtime = world.Runtime(),
                                        .scene = SceneId::CharacterSelect,
                                        .playerSkeleton = skeleton,
                                        .random = cloth.random});
    if (!stopped.HasValue())
    {
        return Core::Unexpected(stopped.Error());
    }
    actor.controlCode = entry.flags;
    actor.level = entry.level;
    actor.name = entry.name;
    actor.byte1e3 = 0;
    if (entry.withoutEquipment)
    {
        return {};
    }
    const auto carried = EquipSelectionActor(world, actor, entry, cloth, construction);
    if (!carried.HasValue())
    {
        return carried;
    }
    actor.guildStatus = entry.guildStatus;
    return {};
}
} // namespace Client::Scenes
