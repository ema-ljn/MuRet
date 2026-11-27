#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Events/Events.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"

#include <string>
#include <string_view>

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

// A 22, 23 or 2C shorter than what the client reads (the GameServer 0.97, 07 Q-C1240): the original reads what was
// left in its receive buffer, which MuRet does not make up, but some writes happen on all its paths. The row warns
// that it cannot read it, as for any other, and then makes those writes.

// 0x4d0cc0 leaves the pick-up request (0x6c82e8) at −1 (0x4d10b9) on all its paths. The C3 08 22 FE of the GameServer
// 0.97 (the zen on buying, selling or repairing) brings the zen, though not the seven bytes the sound reads from
// pkt[4]: it is applied without a warning.
struct PickUpWhenShort
{
    static void Unreadable(App::ServerSession& session, const Network::Packet& packet, Network::ProtocolError error,
                           std::string_view what);
};

// 0x4d10d0 leaves the dropped place at −1 (0x4d13d1 and 0x4d13e5) on all its paths.
struct DropWhenShort
{
    static void Unreadable(App::ServerSession& session, const Network::Packet& packet, Network::ProtocolError error,
                           std::string_view what);
};

// 0x4de500 leaves the use's wait (0x5767954) at 0 on all its paths (0x4de7e4 and the other five exits, 0x4deb0e).
struct StatFruitWhenShort
{
    static void Unreadable(App::ServerSession& session, const Network::Packet& packet, Network::ProtocolError error,
                           std::string_view what);
};

// `Apply` with the bag bound to the session's senders (WithTheSession): the NPC windows and the trade's answer.
template <typename Received,
          Core::Expected<void, std::string> (*Apply)(const Received&, const Scenes::WorldEntryResources&)>
[[nodiscard]] Core::Expected<void, std::string> BoundToTheSession(const Received& received, App::ServerSession& session,
                                                                  const Scenes::WorldEntryResources& resources)
{
    return Apply(received, WithTheSession(resources, session));
}
} // namespace Client::Session
