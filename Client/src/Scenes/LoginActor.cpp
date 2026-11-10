#include "Scenes/LoginActor.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Animation.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneId.h"
#include "World/TerrainHeight.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
void EquipTheLoginActor(ActorState& actor)
{
    const GameLogic::CharacterAppearance appearance = LoginCharacter();
    actor.key = static_cast<std::uint16_t>(LoginActorSlot);
    actor.light = glm::vec3(LoginCharacterLight);
    actor.characterClass = appearance.characterClass;
    actor.skin = appearance.skin;
    actor.flags78 = 0;
    actor.flags7c = 0;
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        actor.body[part].model = appearance.bodyParts[part].model;
        actor.body[part].level = appearance.bodyParts[part].level;
    }
    actor.hands[0].model = appearance.rightHand->model;
    actor.hands[0].level = appearance.rightHand->level;
    actor.hands[1].model = appearance.leftHand->model;
    actor.wings.model = *appearance.wings;
    actor.pet.model = *appearance.pet;
    actor.head = GameLogic::HeadUnder(actor.body[0].model, actor.characterClass).value_or(NoItemModel);
    actor.scale = GameLogic::ClassScale(actor.characterClass, actor.skin).value_or(actor.scale);
}
} // namespace

void PrepareLoginPresentationActor(ActorState& actor, const Render::Models::Skeleton& skeleton)
{
    EquipTheLoginActor(actor);
    actor.alive = true;
    actor.seen = true;
    actor.type = Data::Models::PlayerSkeletonModel;
    actor.safeZone = 0;
    actor.alphaTarget = 1.0f;
    actor.alpha = 1.0f;
    actor.animation = {};
    actor.darkSpirit.reset();
    const auto transform = LoginCharacterTransform();
    actor.position = transform.position;
    actor.angles = transform.angles;
    actor.scale = transform.scale;
    Render::Models::SetAction(actor.animation, skeleton, LoginCharacterStandingAction);
}

Core::Expected<void, std::string> ConstructLoginActor(ActorState& actor, const ActorConstructionContext& construction,
                                                      WorldRuntime& runtime, Render::Models::ClothWorld& cloth,
                                                      const Render::Models::Skeleton& skeleton)
{
    const auto built = ConstructActor(actor, Data::Models::PlayerSkeletonModel, {}, construction, cloth);
    if (!built.HasValue())
    {
        return built;
    }
    EquipTheLoginActor(actor);
    const auto stopped = SetActorStop(
        actor, {.runtime = runtime, .scene = SceneId::Login, .playerSkeleton = skeleton, .random = cloth.random});
    if (!stopped.HasValue())
    {
        return Core::Unexpected(stopped.Error());
    }
    const auto transform = LoginCharacterTransform();
    actor.position = transform.position;
    actor.angles = transform.angles;
    actor.scale = transform.scale;
    // 0x420d60, después del caballo: la celda lógica sigue en (0, 0), aunque el dueño ya está situado.
    constexpr float CellCentre = 50.0f;
    const double ground = World::TerrainHeightAt(*construction.terrain.heights, *construction.terrain.attributes,
                                                 CellCentre, CellCentre, construction.fixedHeight);
    actor.darkSpirit = CreateDarkSpirit(
        actor.cellX, actor.cellY, ground, cloth.random,
        {.cellX = actor.cellXAccesses, .cellY = actor.cellYAccesses, .counter = cloth.openCounterAccesses});
    return {};
}
} // namespace Client::Scenes
