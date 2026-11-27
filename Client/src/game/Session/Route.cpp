#include "Session/Route.h"

#include "Scenes/ActionSend.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ChatReceive.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"

#include <cstdint>

namespace Client::Session::Detail
{
Scenes::ChatReceiveContext RoutedPart<const Scenes::ChatReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                             App::ServerSession& session)
{
    return ChatContextOf(world, session);
}

Scenes::ActorCombatContext RoutedPart<const Scenes::ActorCombatContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                             App::ServerSession&)
{
    return Scenes::CombatContextOf(world);
}

Scenes::ActionSender RoutedPart<const Scenes::ActionSender&>::Of(const Scenes::WorldEntryResources&,
                                                                 App::ServerSession& session)
{
    return [&session](std::uint8_t direction, std::uint8_t action) { session.SendAction(direction, action); };
}
} // namespace Client::Session::Detail
