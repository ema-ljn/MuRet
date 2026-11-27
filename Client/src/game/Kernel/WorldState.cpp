#include "Kernel/WorldState.h"

#include "Kernel/GameContext.h"

#include "Tables/PlayerModels.h"

#include "Crt/CrtRandom.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Terrain/DynamicWorldLight.h"
#include "Scenes/HelperTable.h"
#include "Player/ItemSetState.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Scenes/FaunaState.h"
#include "Assets/FaunaModels.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "World/Leaves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Kernel
{
Kernel::GameContext& WorldState::Context()
{
    return m_context;
}

const Kernel::GameContext& WorldState::Context() const
{
    return m_context;
}

std::span<ActorState, ActorPoolSize> WorldState::Actors()
{
    return m_context.Actors();
}

std::span<const ActorState, ActorPoolSize> WorldState::Actors() const
{
    return m_context.Actors();
}

ActorState& WorldState::Hero()
{
    return m_context.Hero();
}

const ActorState& WorldState::Hero() const
{
    return m_context.Hero();
}

std::span<Actors::Drawing::CharacterCloth, ActorPoolSize> WorldState::ActorCloths()
{
    return *m_actorCloths;
}

std::span<const Actors::Drawing::CharacterCloth, ActorPoolSize> WorldState::ActorCloths() const
{
    return *m_actorCloths;
}

OwnCharacterRecord& WorldState::OwnCharacter()
{
    return m_context.OwnCharacter();
}

const OwnCharacterRecord& WorldState::OwnCharacter() const
{
    return m_context.OwnCharacter();
}

OwnInventory& WorldState::Inventory()
{
    return m_context.Inventory();
}

const OwnInventory& WorldState::Inventory() const
{
    return m_context.Inventory();
}

WorldRuntime& WorldState::Runtime()
{
    return m_context.Runtime();
}

const WorldRuntime& WorldState::Runtime() const
{
    return m_context.Runtime();
}

WorldMap& WorldState::Map()
{
    return m_context.Map();
}

const WorldMap& WorldState::Map() const
{
    return m_context.Map();
}

Player::ItemSetState& WorldState::ItemSets()
{
    return m_itemSets;
}

const Player::ItemSetState& WorldState::ItemSets() const
{
    return m_itemSets;
}

Scenes::HelperTable& WorldState::Helpers()
{
    return m_helpers;
}

const Scenes::HelperTable& WorldState::Helpers() const
{
    return m_helpers;
}

World::Particles& WorldState::Particles()
{
    return *m_particles;
}

const World::Particles& WorldState::Particles() const
{
    return *m_particles;
}

Terrain::DynamicWorldLight& WorldState::DynamicLighting()
{
    return m_dynamicLighting;
}

const Terrain::DynamicWorldLight& WorldState::DynamicLighting() const
{
    return m_dynamicLighting;
}

Actors::PlayerActionSpeeds& WorldState::ActionSpeeds()
{
    return m_actionSpeeds;
}
const Actors::PlayerActionSpeeds& WorldState::ActionSpeeds() const
{
    return m_actionSpeeds;
}
Scenes::FaunaState& WorldState::Fauna()
{
    return m_fauna;
}
const Scenes::FaunaState& WorldState::Fauna() const
{
    return m_fauna;
}
Assets::FaunaModels& WorldState::FaunaResources()
{
    return m_faunaResources;
}
const Assets::FaunaModels& WorldState::FaunaResources() const
{
    return m_faunaResources;
}
SharedModelPlacement& WorldState::PlayerModel()
{
    return m_playerModel;
}

const SharedModelPlacement& WorldState::PlayerModel() const
{
    return m_playerModel;
}

void WorldState::PlacePlayerModel(const ActorState& actor)
{
    PlacePlayerModel(actor, actor.position);
}

void WorldState::PlacePlayerModel(const ActorState& actor, const glm::vec3& origin)
{
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        return;
    }
    m_playerModel.scale = actor.scale;
    m_playerModel.origin = origin;
    m_playerModel.action = actor.animation.action;
}

void WorldState::PlaceShadowModel(const ActorState& actor)
{
    SharedModelPlacement placement = ModelPlacement(Tables::PlayerShadowModel);
    placement.scale = actor.scale;
    placement.origin = actor.position;
    PlaceModel(Tables::PlayerShadowModel, placement);
}

void WorldState::PlaceModel(const ActorState& actor)
{
    PlaceModel(actor.type,
               SharedModelPlacement{.scale = actor.scale, .origin = actor.position, .action = actor.animation.action});
}

void WorldState::PlaceModel(std::uint16_t type, const SharedModelPlacement& placement)
{
    if (type == Tables::PlayerSkeletonModel)
    {
        m_playerModel = placement;
        return;
    }
    m_modelPlacements[type] = placement;
}

SharedModelPlacement WorldState::ModelPlacement(std::uint16_t type) const
{
    if (type == Tables::PlayerSkeletonModel)
        return m_playerModel;
    const auto found = m_modelPlacements.find(type);
    return found != m_modelPlacements.end() ? found->second : SharedModelPlacement{};
}

Scenes::FaunaDrawCarry& WorldState::PoseCarry()
{
    return m_poseCarry;
}
const Scenes::FaunaDrawCarry& WorldState::PoseCarry() const
{
    return m_poseCarry;
}
Scenes::GameDrawTailState& WorldState::DrawTail()
{
    return *m_drawTail;
}
const Scenes::GameDrawTailState& WorldState::DrawTail() const
{
    return *m_drawTail;
}
World::Leaves& WorldState::Leaves()
{
    return *m_leaves;
}
const World::Leaves& WorldState::Leaves() const
{
    return *m_leaves;
}

// El original copia de chars[elegido] lo que 0x4c20a0 le puso con la lista (0x4c21dd..0x4c220c): la
// clase de 0x517270, la piel 0 (0x4c21d9; la de crear es la de la vista previa, que nadie escribe),
// el nivel de [0xc] y los diez bytes del nombre con el cero de +0x1e3. MuRet los toma de la entrada.
void WorldState::PrepareSelectedActor(const Protocol::FrontEnd::CharacterEntry& chosen, Crt::CrtRandom& random)
{
    WorldRuntime& runtime = m_context.Runtime();
    {
        OwnCharacterRecord& own = m_context.OwnCharacter();
        const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        own.level = chosen.level;
        own.characterClass = Protocol::FrontEnd::CharacterClassOf(chosen.classByte);
        own.skin = 0;
        // 0x634433..0x63444e: un strcpy desde +0x1d9. Copia hasta el primer cero, incluido, y lo que
        // sigue en el registro queda como estaba.
        std::size_t length = 0;
        while (length < chosen.name.size() && chosen.name[length] != std::byte{0})
        {
            own.name[length] = chosen.name[length];
            ++length;
        }
        own.name[length] = std::byte{0};
    }
    runtime.loadingOnceFlag = 0;
}
} // namespace Client::Kernel
