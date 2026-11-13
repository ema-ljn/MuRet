#include "Scenes/Skill4e9c50.h"

#include "Core/Expected.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/Skill4f5290.h"

#include <string>

namespace Client::Scenes
{
Core::Expected<void, std::string> ActByTheHands(const ActorState& by, ActorState& on, const ActorCombatContext& context)
{
    const bool right = by.hands[0].model != NoItemModel;
    const bool left = by.hands[1].model != NoItemModel;
    if (right && left)
        return SetTheActorAction(on, 0x63, context);
    if (right)
        return SetTheActorAction(on, 0x64, context);
    if (left)
        return SetTheActorAction(on, 0x65, context);
    return SetTheActorAction(on, 0x62, context);
}
} // namespace Client::Scenes
