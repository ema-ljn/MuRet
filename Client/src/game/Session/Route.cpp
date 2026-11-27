#include "Session/Route.h"

#include "Protocol/Outgoing.h"
#include "Actors/ActionSend.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GuildReceive.h"
#include "Scenes/SocialEvents.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"

#include <cstdint>
#include <utility>

namespace Client::Session::Detail
{
Scenes::ChatReceiveContext RoutedPart<const Scenes::ChatReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                             App::ServerSession& session)
{
    return ChatContextOf(world, session);
}

Scenes::GuildReceiveContext RoutedPart<const Scenes::GuildReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                               App::ServerSession& session)
{
    return GuildContextOf(world, session);
}

Scenes::CastleReceiveContext
RoutedPart<const Scenes::CastleReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                    App::ServerSession& session)
{
    return CastleContextOf(world, session);
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

Scenes::GamePacketSender RoutedPart<const Scenes::GamePacketSender&>::Of(const Scenes::WorldEntryResources&,
                                                                         App::ServerSession& session)
{
    return [&session](Protocol::Outgoing packet) { session.SendGamePacket(std::move(packet)); };
}
} // namespace Client::Session::Detail
