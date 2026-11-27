#include "Session/SessionContexts.h"

#include "Core/PixelSize.h"
#include "Models/Cloth.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ChatReceive.h"
#include "Session/ServerSession.h"
#include "Session/WorldEntry.h"

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
