#include "Scenes/SelectionActor.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/Cloth.h"
#include "Actors/ActorActions.h"
#include "Actors/ActorCharacterSet.h"
#include "Actors/ActorConstruction.h"
#include "Kernel/ActorState.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneId.h"
#include "Kernel/WorldState.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"

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

void PutAppearance(Kernel::ActorState& actor, const Kernel::CharacterAppearance& appearance)
{
    actor.characterClass = appearance.characterClass;
    actor.skin = appearance.skin;
    actor.scale = appearance.scale;
    actor.head = appearance.head.value_or(Kernel::NoItemModel);
    // 0x512a30 escribe el modelo, el nivel y los bytes +3 y +4: el hueso y la animación de lo que cuelga
    // quedan como estaban.
    const auto put = [](Kernel::ActorItemPart& part, const Kernel::WornItem& item)
    {
        part.model = item.model;
        part.level = item.level;
        part.flags1b = static_cast<std::uint8_t>(item.excellent);
        part.flags1c = static_cast<std::uint8_t>(item.ancient);
    };
    for (std::size_t index = 0; index < actor.body.size(); ++index)
    {
        put(actor.body[index], appearance.bodyParts[index]);
    }
    if (appearance.rightHand)
    {
        put(actor.hands[0], *appearance.rightHand);
    }
    else
    {
        actor.hands[0].model = Kernel::NoItemModel;
    }
    if (appearance.leftHand)
    {
        put(actor.hands[1], *appearance.leftHand);
    }
    else
    {
        actor.hands[1].model = Kernel::NoItemModel;
    }
    actor.wings.model = appearance.wings.value_or(Kernel::NoItemModel);
    actor.pet.model = appearance.pet.value_or(Kernel::NoItemModel);
    actor.previousHad1c15 = actor.pet.model == HeightOffsetPet;
}

Core::Expected<void, std::string> EquipSelectionActor(Kernel::WorldState& world, Kernel::ActorState& actor,
                                                      const Protocol::FrontEnd::CharacterEntry& entry,
                                                      Models::ClothWorld& cloth,
                                                      const Actors::ActorConstructionContext& construction)
{
    const auto appearance = Kernel::AppearanceOf(actor.characterClass, 0, entry.equipment);
    actor.hands[0].model = appearance.rightHand ? appearance.rightHand->model : Kernel::NoItemModel;
    actor.hands[1].model = appearance.leftHand ? appearance.leftHand->model : Kernel::NoItemModel;
    if (actor.hands[1].model == Raven && (construction.map < 18 || construction.map > 23))
    {
        constexpr float CellCentre = 50.0f;
        const double height =
            Actors::HeightSkippedIn(construction.scene)
                ? 0.0
                : Terrain::TerrainHeightAt(*construction.terrain.heights, *construction.terrain.attributes, CellCentre,
                                           CellCentre, construction.fixedHeight);
        actor.darkSpirit = CreateDarkSpirit(
            actor.cellX, actor.cellY, height, cloth.random,
            {.cellX = actor.cellXAccesses, .cellY = actor.cellYAccesses, .counter = cloth.openCounterAccesses});
    }
    actor.wings.model = appearance.wings.value_or(Kernel::NoItemModel);
    if (!appearance.wings)
    {
        actor.wings.flags1b = 0;
        actor.wings.flags1c = 0;
    }
    Kernel::TurnOffHelpersOf(world.Helpers(), entry.slot);
    if (auto pets =
            Actors::CreateCharacterSetPets(world, entry.slot, entry.equipment, TableHelpers(world, &cloth.random));
        !pets.HasValue())
    {
        return pets;
    }
    PutAppearance(actor, appearance);
    actor.byte358 = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(entry.equipment[10]) & 1);
    return {};
}
} // namespace

Core::Expected<void, std::string> ConstructSelectionActor(Kernel::WorldState& world,
                                                          const Protocol::FrontEnd::CharacterEntry& entry,
                                                          const Actors::ActorConstructionContext& construction,
                                                          Models::ClothWorld& cloth, const Models::Skeleton& skeleton)
{
    if (entry.slot >= Kernel::SelectionActorCount)
    {
        return Core::Unexpected(std::string("slot de selección fuera del pool"));
    }
    Kernel::ActorState& actor = world.Actors()[entry.slot];
    const CharacterSlot& place = CharacterSlots[entry.slot];
    const auto built =
        Actors::ConstructActor(actor, Tables::PlayerSkeletonModel, {.angle = place.angle}, construction, cloth);
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
    const auto characterClass = Protocol::FrontEnd::CharacterClassOf(entry.classByte);
    PutAppearance(actor, Kernel::ClassAppearance(characterClass, 0));
    // 0x51736f, antes de 0x5139f0: las partes de la clase base, aun para una segunda clase.
    constexpr std::uint8_t ClassMask = 7;
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        actor.body[part].model = Kernel::ClassPartModel(static_cast<Kernel::BodyPart>(part),
                                                        static_cast<std::uint8_t>(characterClass & ClassMask));
    }
    actor.head = Kernel::HeadUnder(actor.body[0].model, characterClass).value_or(Kernel::NoItemModel);
    auto stopped = Actors::SetActorStop(actor, {.runtime = world.Runtime(),
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
