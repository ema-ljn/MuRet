#pragma once

#include "Core/Expected.h"
#include "Protocol/Events/Events.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"

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

namespace Client::Protocol::PersonalShop
{
struct ShopItemList;
}

namespace Client::Protocol::Social
{
struct TradeRequest;
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

// 0x4d52bc..0x4d5553: with a personal shop open, the C1 04 37 00 that Scenes::ApplyTradeRequest returns goes out in
// clear, with the XOR32.
[[nodiscard]] Core::Expected<void, std::string> ReceiveTradeRequest(const Protocol::Social::TradeRequest& request,
                                                                    App::ServerSession& session,
                                                                    const Scenes::WorldEntryResources& resources);

[[nodiscard]] Core::Expected<void, std::string> ReceiveShopItemList(const Protocol::PersonalShop::ShopItemList& list,
                                                                    App::ServerSession& session,
                                                                    const Scenes::WorldEntryResources& resources);

// What the original does outside its tables is discarded with a warning, and the session goes on (Q-C174, Q-C778).
[[nodiscard]] Core::Expected<void, std::string> ReceiveEventPacket(const Protocol::Events::EventPacket& packet,
                                                                   App::ServerSession& session,
                                                                   const Scenes::WorldEntryResources& resources);

// `Apply` with the bag bound to the session's senders (WithTheSession): the NPC windows and the trade's answer.
template <typename Received,
          Core::Expected<void, std::string> (*Apply)(const Received&, const Scenes::WorldEntryResources&)>
[[nodiscard]] Core::Expected<void, std::string> BoundToTheSession(const Received& received, App::ServerSession& session,
                                                                  const Scenes::WorldEntryResources& resources)
{
    return Apply(received, WithTheSession(resources, session));
}
} // namespace Client::Session
