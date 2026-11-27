#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::App
{
class ServerSession;
}

namespace Client::Protocol::Movement
{
struct Regen;
struct Teleport;
} // namespace Client::Protocol::Movement

namespace Client::Scenes
{
struct WorldEntryResources;
}

// The routed rows whose applier needs what only the session lends (Session/Route.h): until AR4 gives the modules their
// ports, they bind it here.
namespace Client::Session
{
[[nodiscard]] Core::Expected<void, std::string> ReceiveTeleport(const Protocol::Movement::Teleport& teleport,
                                                                App::ServerSession& session,
                                                                const Scenes::WorldEntryResources& resources);

[[nodiscard]] Core::Expected<void, std::string> ReceiveRegen(const Protocol::Movement::Regen& regen,
                                                             App::ServerSession& session,
                                                             const Scenes::WorldEntryResources& resources);
} // namespace Client::Session
