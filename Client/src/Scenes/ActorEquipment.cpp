#include "Scenes/ActorEquipment.h"

#include "Core/Expected.h"

#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
using Render::Models::LogicalAccess;
using Render::Models::OwnRecordCloseRandoms;
constexpr std::uint16_t ModelBase = 0x212;

void ApplyPart(ActorEquipmentPart& part, const World::ItemState& item, std::uint16_t emptyModel, bool body)
{
    part.model = item.id == 0xffff ? emptyModel : static_cast<std::uint16_t>(item.id + ModelBase);
    part.level = static_cast<std::uint8_t>((item.scalar1Low >> 3) & 15u);
    part.flags1b = item.flags1b;
    if (body)
        part.flags1c = item.flags1c;
}
GameLogic::WornItem Worn(const ActorEquipmentPart& part)
{
    return {.model = part.model, .level = part.level, .excellent = part.flags1b != 0, .ancient = part.flags1c != 0};
}
} // namespace

Core::Expected<void, std::string> ApplyActorEquipment(ActorState& actor, WorldState& world,
                                                      Render::Models::ClothWorld& cloth)
{
    const Render::Models::LogicalAccess record(world.Runtime().ownRecordDepth, cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
    const auto& source = world.OwnCharacter().equipment;
    ApplyPart(actor.hands[0], source[0], 0xffff, false);
    ApplyPart(actor.hands[1], source[1], 0xffff, false);
    ApplyPart(actor.wings, source[7], 0xffff, false);
    ApplyPart(actor.pet, source[8], 0xffff, false);
    actor.appearance.rightHand = actor.hands[0].model == 0xffff ? std::nullopt : std::optional{Worn(actor.hands[0])};
    actor.appearance.leftHand = actor.hands[1].model == 0xffff ? std::nullopt : std::optional{Worn(actor.hands[1])};
    actor.appearance.wings = actor.wings.model == 0xffff ? std::nullopt : std::optional{actor.wings.model};
    actor.appearance.pet = actor.pet.model == 0xffff ? std::nullopt : std::optional{actor.pet.model};
    actor.previousHad1c15 = actor.pet.model == 0x1c15;
    if (!actor.hideBody)
    {
        for (std::size_t i = 0; i < actor.body.size(); ++i)
        {
            const auto part = static_cast<GameLogic::BodyPart>(i);
            ApplyPart(actor.body[i], source[i + 2], GameLogic::ClassPartModel(part, actor.appearance.characterClass),
                      true);
            actor.appearance.bodyParts[i] = Worn(actor.body[i]);
        }
    }
    GameLogic::RefreshAppearanceHeadAndScale(actor.appearance);
    // Los helpers previos550d10 aún no están ligados; la cola propia transforma el estado vivo.
    // No se descartan sorteos ni se reconstruye su representación protegida.
    return UpdateOwnDerivedStatsTail(world.OwnCharacter().derived, cloth.random);
}
} // namespace Client::Scenes
