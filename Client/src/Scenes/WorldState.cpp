#include "Scenes/WorldState.h"

#include "Data/Models/PlayerModels.h"

#include "Protocol/Character/CharacterList.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/HelperTable.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/FaunaState.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "World/Leaves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "World/Particles.h"

#include <cstddef>
#include <span>

namespace Client::Scenes
{
std::span<ActorState, ActorPoolSize> WorldState::Actors()
{
    return m_actors;
}

std::span<const ActorState, ActorPoolSize> WorldState::Actors() const
{
    return m_actors;
}

OwnCharacterRecord& WorldState::OwnCharacter()
{
    return m_own;
}

const OwnCharacterRecord& WorldState::OwnCharacter() const
{
    return m_own;
}

OwnInventory& WorldState::Inventory()
{
    return m_inventory;
}

const OwnInventory& WorldState::Inventory() const
{
    return m_inventory;
}

WorldRuntime& WorldState::Runtime()
{
    return m_runtime;
}

const WorldRuntime& WorldState::Runtime() const
{
    return m_runtime;
}

WorldMap& WorldState::Map()
{
    return m_map;
}

const WorldMap& WorldState::Map() const
{
    return m_map;
}

ItemSetState& WorldState::ItemSets()
{
    return m_itemSets;
}

const ItemSetState& WorldState::ItemSets() const
{
    return m_itemSets;
}

HelperTable& WorldState::Helpers()
{
    return m_helpers;
}

const HelperTable& WorldState::Helpers() const
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

DynamicWorldLight& WorldState::DynamicLighting()
{
    return m_dynamicLighting;
}

const DynamicWorldLight& WorldState::DynamicLighting() const
{
    return m_dynamicLighting;
}

PlayerActionSpeeds& WorldState::ActionSpeeds()
{
    return m_actionSpeeds;
}
const PlayerActionSpeeds& WorldState::ActionSpeeds() const
{
    return m_actionSpeeds;
}
FaunaState& WorldState::Fauna()
{
    return m_fauna;
}
const FaunaState& WorldState::Fauna() const
{
    return m_fauna;
}
FaunaModels& WorldState::FaunaResources()
{
    return m_faunaResources;
}
const FaunaModels& WorldState::FaunaResources() const
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
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        return;
    }
    m_playerModel.scale = actor.scale;
    m_playerModel.origin = actor.position;
}

FaunaDrawCarry& WorldState::PoseCarry()
{
    return m_poseCarry;
}
const FaunaDrawCarry& WorldState::PoseCarry() const
{
    return m_poseCarry;
}
GameDrawTailState& WorldState::DrawTail()
{
    return *m_drawTail;
}
const GameDrawTailState& WorldState::DrawTail() const
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
void WorldState::PrepareSelectedActor(const Protocol::Character::CharacterEntry& chosen,
                                      Render::Models::CrtRandom& random)
{
    {
        const Render::Models::LogicalAccess access(m_runtime.ownRecordDepth, random,
                                                   Render::Models::OwnRecordCloseRandoms);
        m_own.level = chosen.level;
        m_own.characterClass = Protocol::Character::CharacterClassOf(chosen.classByte);
        m_own.skin = 0;
        // 0x634433..0x63444e: un strcpy desde +0x1d9. Copia hasta el primer cero, incluido, y lo que
        // sigue en el registro queda como estaba.
        std::size_t length = 0;
        while (length < chosen.name.size() && chosen.name[length] != std::byte{0})
        {
            m_own.name[length] = chosen.name[length];
            ++length;
        }
        m_own.name[length] = std::byte{0};
    }
    m_runtime.loadingOnceFlag = 0;
}
} // namespace Client::Scenes
