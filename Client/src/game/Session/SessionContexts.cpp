#include "Session/SessionContexts.h"

#include "Kernel/FaithfulPacer.h"
#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/Outgoing.h"
#include "CastleSiege/CastleSiegeReceive.h"
#include "Chat/ChatReceive.h"
#include "Events/EventPackets.h"
#include "Guild/GuildReceive.h"
#include "Session/ServerSession.h"
#include "Session/WorldEntry.h"
#include "Tables/PlayerModels.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

namespace Client::Session
{
namespace
{
// Without a window to measure, the original's 640x480.
constexpr Core::PixelSize ScreenWithoutAWindow{640, 480};
} // namespace

Scenes::WorldEntryResources WithTheSession(const Scenes::WorldEntryResources& resources, App::ServerSession& session)
{
    Scenes::WorldEntryResources bound = resources;
    bound.requestCastleSiegeState = [&session] { session.RequestCastleSiegeState(); };
    bound.sendNpcWindowPacket = [&session](Protocol::Outgoing packet)
    { return session.SendNpcWindowPacket(std::move(packet)); };
    bound.sendWindowClose = [&session] { session.SendWindowClose(); };
    bound.sendGamePacket = [&session](Protocol::Outgoing outgoing) { session.SendGamePacket(std::move(outgoing)); };
    return bound;
}

Scenes::ChatReceiveContext ChatContextOf(const Scenes::WorldEntryResources& resources, App::ServerSession& session)
{
    const Protocol::FrontEnd::CharacterEntry* chosen = session.ChosenCharacter();
    return Scenes::ChatReceiveContext{
        .world = resources.world,
        .random = resources.cloth.random,
        .measure = resources.textMeasure,
        .screen = resources.screen ? resources.screen() : ScreenWithoutAWindow,
        .scene = resources.scene ? resources.scene() : std::nullopt,
        .texts = resources.texts,
        .chosenSlot = chosen != nullptr ? std::optional<std::uint8_t>(chosen->slot) : std::nullopt,
        .guildSend = [&session](Protocol::Outgoing packet) { session.SendGuildPacket(std::move(packet)); },
        .requestServerList = [&session] { session.ResendServerListRequest(); },
        .rooms = &session,
        .worldEntry = &resources};
}

Scenes::GuildReceiveContext GuildContextOf(const Scenes::WorldEntryResources& resources, App::ServerSession& session)
{
    const std::uint32_t now =
        resources.clock != nullptr
            ? static_cast<std::uint32_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(resources.clock->Now()).count())
            : 0U;
    return Scenes::GuildReceiveContext{.world = resources.world,
                                       .random = resources.cloth.random,
                                       .measure = resources.textMeasure,
                                       .screen = resources.screen ? resources.screen() : ScreenWithoutAWindow,
                                       .texts = resources.texts,
                                       .playerKey = session.PlayerIndex(),
                                       .now = now,
                                       .send = [&session](Protocol::Outgoing packet)
                                       { session.SendGuildPacket(std::move(packet)); },
                                       .models = &resources.models,
                                       .worldEntry = &resources};
}

Scenes::CastleReceiveContext CastleContextOf(const Scenes::WorldEntryResources& resources, App::ServerSession& session)
{
    return Scenes::CastleReceiveContext{
        .world = resources.world,
        .random = resources.cloth.random,
        .log = resources.log,
        .measure = resources.textMeasure,
        .screen = resources.screen ? resources.screen() : ScreenWithoutAWindow,
        .texts = resources.texts,
        .send = [&session](Protocol::Outgoing packet) { session.SendCastlePacket(std::move(packet)); },
        .dataRoot = resources.dataRoot,
        .tick = resources.clock != nullptr ? App::TickCountMs(resources.clock->Now()) : 0U,
        .entryResources = &resources,
        .terrain = Scenes::WritableTerrainOf(resources),
        .faunaModels = Scenes::FaunaModelsOf(resources)};
}

Scenes::EventPacketContext EventContextOf(const Scenes::WorldEntryResources& resources, App::ServerSession& session,
                                          const Scenes::InventoryServices& inventory)
{
    return Scenes::EventPacketContext{
        .world = resources.world,
        .random = resources.cloth.random,
        .texts = resources.texts,
        .items = resources.items,
        .measure = resources.textMeasure,
        .screen = resources.screen ? resources.screen() : ScreenWithoutAWindow,
        .tickCount = resources.clock != nullptr ? App::TickCountMs(resources.clock->Now()) : 0U,
        .playerSkeleton = resources.models.SkeletonOf(Tables::PlayerSkeletonModelId),
        .models = &resources.models,
        .sendWindowClose = [&session] { session.SendWindowClose(); },
        // SendChat sends any world packet.
        .send = [&session](Protocol::Outgoing outgoing) { session.SendChat(std::move(outgoing)); },
        .dataRoot = resources.dataRoot,
        .sendNpcWindow = [&session](Protocol::Outgoing outgoing)
        { return session.SendNpcWindowPacket(std::move(outgoing)); },
        .inventory = &inventory,
        .terrain = Scenes::WritableTerrainOf(resources)};
}

SessionPublisher::SessionPublisher(App::ServerSession& session) : m_session(session) {}

void SessionPublisher::PublishWorldEntry()
{
    m_session.PublishWorldEntry();
}

void SessionPublisher::ConfirmTeleport()
{
    m_session.ConfirmTeleport();
}

void SessionPublisher::RequestCrywolfState()
{
    m_session.RequestCrywolfState();
}
} // namespace Client::Session
