#include "Session/SessionHandlers.h"

#include "Core/Expected.h"
#include "Protocol/Movement/World.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"
#include "Session/WorldTransitions.h"

#include <string>

namespace Client::Session
{
Core::Expected<void, std::string> ReceiveTeleport(const Protocol::Movement::Teleport& teleport,
                                                  App::ServerSession& session,
                                                  const Scenes::WorldEntryResources& resources)
{
    SessionPublisher publisher(session);
    return Scenes::ApplyTeleport(teleport, session.PlayerIndex(), WithTheSession(resources, session), publisher);
}

Core::Expected<void, std::string> ReceiveRegen(const Protocol::Movement::Regen& regen, App::ServerSession& session,
                                               const Scenes::WorldEntryResources& resources)
{
    return Scenes::ApplyRegen(regen, session.PlayerIndex(), WithTheSession(resources, session));
}
} // namespace Client::Session
