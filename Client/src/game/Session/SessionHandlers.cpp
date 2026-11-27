#include "Session/SessionHandlers.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Movement/World.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"
#include "Scenes/EventPackets.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/SocialEvents.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"
#include "Session/WorldTransitions.h"

#include <optional>
#include <string>
#include <utility>

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

Core::Expected<void, std::string> ReceiveTradeRequest(const Protocol::Social::TradeRequest& request,
                                                      App::ServerSession& session,
                                                      const Scenes::WorldEntryResources& resources)
{
    auto reply = Scenes::ApplyTradeRequest(request, resources);
    if (!reply.HasValue())
        return Core::Unexpected(std::move(reply).Error());
    if (std::optional<Protocol::Outgoing>& answer = reply.Value())
        session.SendGamePacket(std::move(*answer));
    return {};
}

Core::Expected<void, std::string> ReceiveEventPacket(const Protocol::Events::EventPacket& packet,
                                                     App::ServerSession& session,
                                                     const Scenes::WorldEntryResources& resources)
{
    const Scenes::InventoryServices inventory = Scenes::InventoryServicesOf(resources);
    auto applied = Scenes::ApplyEventPacket(packet, EventContextOf(resources, session, inventory));
    if (!applied.HasValue())
        return Core::Unexpected(std::move(applied).Error());
    if (const std::optional<std::string>& discarded = applied.Value().discarded)
        resources.log.Warning("se descarta el resto del paquete de evento: {}", *discarded);
    return {};
}

Core::Expected<void, std::string> ReceiveShopItemList(const Protocol::PersonalShop::ShopItemList& list,
                                                      App::ServerSession& session,
                                                      const Scenes::WorldEntryResources& resources)
{
    return Scenes::ApplyShopItemList(list, WithTheSession(resources, session), [&session](Protocol::Outgoing outgoing)
                                     { session.SendGamePacket(std::move(outgoing)); });
}
} // namespace Client::Session
