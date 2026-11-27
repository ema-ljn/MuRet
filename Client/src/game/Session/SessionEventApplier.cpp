#include "Session/SessionEventApplier.h"

#include "Session/CharacterEvent.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"
#include "Models/IModelLibrary.h"
#include "Actors/ActionReply.h"
#include "Scenes/ActorCombat.h"
#include "Player/LevelUpPointReply.h"
#include "Session/MapServerMoveApply.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/FloorItemPackets.h"
#include "Scenes/GuildState.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameLetters.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/InventoryReply.h"
#include "Scenes/MagicAttackCount.h"
#include "Actors/MovementReplies.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/PetOrders.h"
#include "Scenes/PetStats.h"
#include "Scenes/QuestReplies.h"
#include "Session/ReturnToSelection.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SkillReplies.h"
#include "Scenes/SocialEvents.h"
#include "Actors/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitions.h"
#include "UI/LetterWindows.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace Client::Scenes
{
namespace
{
using Session::ChatContextOf;
using Session::SessionPublisher;
using Session::WithTheSession;
} // namespace

SessionEventApplier::SessionEventApplier(const WorldEntryResources& resources) : m_resources(resources) {}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_resources.selection.Apply(std::span(&event, 1), m_resources.camera, m_resources.cameraInput,
                                       &m_resources.cloth, m_resources.models.SkeletonOf(Tables::PlayerSkeletonModel));
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent& event,
                                                                  App::ServerSession& session)
{
    auto applied = ApplyWorldEvent(event, session);
    AfterWorldReception(session);
    return applied;
}

const WorldEntryResources* SessionEventApplier::RoutedWorld() const
{
    return &m_resources;
}

void SessionEventApplier::AfterWorldReception(App::ServerSession& session)
{
    FlushTheChatRoomOrders(m_resources.world.Runtime().oldWindows.chatRooms, &session);
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldEvent(const App::WorldEvent& event,
                                                                       App::ServerSession& session)
{
    if (const auto* room = std::get_if<App::ChatRoomPacket>(&event))
        return ApplyRoomPacket(room->window, room->packet, ChatContextOf(m_resources, session));
    if (const auto* closed = std::get_if<App::ChatRoomClosed>(&event))
        return ApplyChatRoomClosed(*closed, ChatContextOf(m_resources, session));
    if (std::holds_alternative<App::ChatRoomPingDue>(event))
    {
        UI::PingTheChatRooms(m_resources.world.Runtime().oldWindows);
        return {};
    }
    return ApplyWorldContent(event, session);
}

void SessionEventApplier::CopyToPacketRing(std::span<const std::byte> packet)
{
    Actors::StoreViewportPacket(m_resources.world.Runtime().viewportPackets, packet);
}

void SessionEventApplier::ClearLogoutRequest()
{
    m_resources.world.Runtime().ui.logoutRequested = false;
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldContent(const App::WorldEvent& event,
                                                                         App::ServerSession& session)
{
    if (std::holds_alternative<App::ClearInventoryReceived>(event))
    {
        ClearForInventoryReply(m_resources);
        return {};
    }
    if (std::holds_alternative<App::ReturnToSelectionReceived>(event))
    {
        return PrepareReturnToSelection(WithTheSession(m_resources, session));
    }
    if (std::holds_alternative<App::SelectionWorldReset>(event))
        return CompleteReturnToSelection(WithTheSession(m_resources, session));
    if (std::holds_alternative<App::ServerListLogoutCleanup>(event))
    {
        return PrepareReturnToServerList(m_resources);
    }
    if (std::holds_alternative<App::ReturnToServerListReceived>(event))
    {
        return ApplyReturnToServerList(m_resources);
    }
    if (std::holds_alternative<App::MapServerLeft>(event))
    {
        const Protocol::FrontEnd::CharacterEntry* chosen = session.ChosenCharacter();
        return ApplyMapServerLeave(m_resources,
                                   chosen != nullptr ? std::optional<std::uint8_t>(chosen->slot) : std::nullopt);
    }
    if (std::holds_alternative<App::GuildTableReset>(event))
    {
        ResetTheGuildTable(m_resources.world.Runtime().guild);
        return {};
    }
    if (std::holds_alternative<App::MapServerAuthenticationPrepared>(event))
    {
        PrepareMapServerAuthentication(m_resources);
        return {};
    }
    if (std::holds_alternative<App::MapServerJoined>(event))
    {
        return ApplyMapServerJoin(WithTheSession(m_resources, session));
    }
    if (const auto* inventory = std::get_if<Protocol::Items::InventoryList>(&event))
    {
        const auto applied = ApplyInventory(*inventory, m_resources);
        if (!applied.HasValue())
        {
            return Core::Unexpected(applied.Error());
        }
        return {};
    }
    const auto& reply = std::get<Protocol::FrontEnd::CharacterSelectResult>(event);
    SessionPublisher publisher(session);
    const auto entered = ApplyWorldEntry(reply, session.PlayerIndex(), WithTheSession(m_resources, session), publisher);
    if (!entered.HasValue())
    {
        return Core::Unexpected(entered.Error());
    }
    // ApplyWorldEntry despachó reproducción y detenciones en orden; el vector queda para diagnóstico.
    return {};
}
} // namespace Client::Scenes
