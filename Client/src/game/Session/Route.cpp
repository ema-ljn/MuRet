#include "Session/Route.h"

#include "Protocol/Outgoing.h"
#include "Actors/ActionSend.h"
#include "Combat/ActorCombat.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Chat/ChatReceive.h"
#include "Guild/GuildReceive.h"
#include "Social/SocialEvents.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"

#include <cstdint>
#include <utility>

namespace Client::Session::Detail
{
Chat::ChatReceiveContext RoutedPart<const Chat::ChatReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                         App::ServerSession& session)
{
    return ChatContextOf(world, session);
}

Guild::GuildReceiveContext RoutedPart<const Guild::GuildReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                             App::ServerSession& session)
{
    return GuildContextOf(world, session);
}

CastleSiege::CastleReceiveContext
RoutedPart<const CastleSiege::CastleReceiveContext&>::Of(const Scenes::WorldEntryResources& world,
                                                         App::ServerSession& session)
{
    return CastleContextOf(world, session);
}

Combat::ActorCombatContext RoutedPart<const Combat::ActorCombatContext&>::Of(const Scenes::WorldEntryResources& world,
                                                                             App::ServerSession&)
{
    return Combat::CombatContextOf(world);
}

Actors::ActionSender RoutedPart<const Actors::ActionSender&>::Of(const Scenes::WorldEntryResources&,
                                                                 App::ServerSession& session)
{
    return [&session](std::uint8_t direction, std::uint8_t action) { session.SendAction(direction, action); };
}

Social::GamePacketSender RoutedPart<const Social::GamePacketSender&>::Of(const Scenes::WorldEntryResources&,
                                                                         App::ServerSession& session)
{
    return [&session](Protocol::Outgoing packet) { session.SendGamePacket(std::move(packet)); };
}
} // namespace Client::Session::Detail
