#pragma once

#include "Session/IWorldEntryPublisher.h"
#include "Session/IWorldTransitionPublisher.h"

namespace Client::App
{
class ServerSession;
}

namespace Client::CastleSiege
{
struct CastleReceiveContext;
} // namespace Client::CastleSiege

namespace Client::Chat
{
struct ChatReceiveContext;
} // namespace Client::Chat

namespace Client::Events
{
struct EventPacketContext;
} // namespace Client::Events

namespace Client::Guild
{
struct GuildReceiveContext;
} // namespace Client::Guild

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

// What today's appliers take from the bag and from the session that applies the packet. SessionEventApplier and the
// routed rows share them until AR4 gives each module its own ports.
namespace Client::Session
{
// The bag with the session's GameServer: 0x42a760's B9 02 on loading map 30; the C1 03 31 of 0x419900 (closing the
// quests) and of 0x657a90 (closing an event NPC), with the 3D of the trade (0x657fe1) and the 3F 07 of another's shop
// (0x5eb17f) that those closings send; and the NPC windows' packets.
[[nodiscard]] Scenes::WorldEntryResources WithTheSession(const Scenes::WorldEntryResources& resources,
                                                         App::ServerSession& session);

// 0x6d5edc is the slot chosen from the list. The guild's window that the 01 of the Guild Master opens (0x4e345c) sends
// through the session's GameServer.
[[nodiscard]] Chat::ChatReceiveContext ChatContextOf(const Scenes::WorldEntryResources& resources,
                                                     App::ServerSession& session);

// What the guild's packets send goes through the session's GameServer, also the C1 18 of the war's gesture (0x576a60).
[[nodiscard]] Guild::GuildReceiveContext GuildContextOf(const Scenes::WorldEntryResources& resources,
                                                        App::ServerSession& session);

// What the castle's packets send goes through the session's GameServer.
[[nodiscard]] CastleSiege::CastleReceiveContext CastleContextOf(const Scenes::WorldEntryResources& resources,
                                                                App::ServerSession& session);

// GetTickCount is the session's clock in ms (Kernel::TickCountMs), 0 without a clock. What the events send goes through
// the session's GameServer.
[[nodiscard]] Events::EventPacketContext EventContextOf(const Scenes::WorldEntryResources& resources,
                                                        App::ServerSession& session,
                                                        const Items::InventoryServices& inventory);

// The 0x3d of a reply, and what a map change asks for, go to the session that applies it.
class SessionPublisher final : public Scenes::IWorldEntryPublisher, public Scenes::IWorldTransitionPublisher
{
public:
    explicit SessionPublisher(App::ServerSession& session);

    void PublishWorldEntry() override;
    void ConfirmTeleport() override;
    void RequestCrywolfState() override;

private:
    App::ServerSession& m_session;
};
} // namespace Client::Session
