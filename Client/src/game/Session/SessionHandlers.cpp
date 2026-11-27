#include "Session/SessionHandlers.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Movement/World.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Social/Trade.h"
#include "Scenes/EventPackets.h"
#include "Scenes/FloorItemPackets.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/SocialEvents.h"
#include "Kernel/WorldState.h"
#include "Session/Route.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEntry.h"
#include "Session/WorldTransitions.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Session
{
namespace
{
template <void (*Release)(Kernel::WorldState&)> void Released(App::ServerSession& session)
{
    const Scenes::WorldEntryResources* const world = session.RoutedWorld();
    if (world == nullptr)
        return;
    Release(world->world);
    session.FinishRoutedReception({});
}
} // namespace

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

void PickUpWhenShort::Unreadable(App::ServerSession& session, const Network::Packet& packet,
                                 Network::ProtocolError error, std::string_view what)
{
    if (error == Network::ProtocolError::TooShort)
    {
        if (const auto zen = Protocol::Items::ReadShortZenPickUp(packet); zen.HasValue())
        {
            session.Log().Debug("el 22 con el zen llegó corto: se aplica el zen, sin el sonido (07 Q-C1240)");
            Deliver<&Scenes::ApplyShortZenPickUp>(session, zen.Value());
            return;
        }
    }
    session.IgnoreUnreadable(what);
    if (error == Network::ProtocolError::TooShort)
        Released<&Scenes::ApplyShortPickUpResult>(session);
}

void DropWhenShort::Unreadable(App::ServerSession& session, const Network::Packet& packet, Network::ProtocolError error,
                               std::string_view what)
{
    session.IgnoreUnreadable(what);
    if (error != Network::ProtocolError::TooShort)
        return;
    if (const auto unread = Protocol::Items::ReadShortDropResult(packet); unread.HasValue())
        Deliver<&Scenes::ApplyShortDropResult>(session, unread.Value());
}

void StatFruitWhenShort::Unreadable(App::ServerSession& session, const Network::Packet&, Network::ProtocolError error,
                                    std::string_view what)
{
    session.IgnoreUnreadable(what);
    if (error == Network::ProtocolError::TooShort)
        Released<&Scenes::ApplyShortStatFruit>(session);
}

Core::Expected<void, std::string> ReceiveShopItemList(const Protocol::PersonalShop::ShopItemList& list,
                                                      App::ServerSession& session,
                                                      const Scenes::WorldEntryResources& resources)
{
    return Scenes::ApplyShopItemList(list, WithTheSession(resources, session), [&session](Protocol::Outgoing outgoing)
                                     { session.SendGamePacket(std::move(outgoing)); });
}
} // namespace Client::Session
