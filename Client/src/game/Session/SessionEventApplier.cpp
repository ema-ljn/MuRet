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
#include "Combat/ActorCombat.h"
#include "Player/LevelUpPointReply.h"
#include "Session/MapServerMoveApply.h"
#include "Chat/ChatReceive.h"
#include "Items/FloorItemPackets.h"
#include "Guild/GuildState.h"
#include "Messenger/GameFriends.h"
#include "Messenger/GameLetters.h"
#include "Items/InventoryMoves.h"
#include "Items/InventoryReply.h"
#include "Combat/MagicAttackCount.h"
#include "Actors/MovementReplies.h"
#include "Npc/NpcWindows.h"
#include "PersonalShop/PersonalShopLists.h"
#include "Pets/PetOrders.h"
#include "Pets/PetStats.h"
#include "Quests/QuestReplies.h"
#include "Session/ReturnToSelection.h"
#include "FrontEnd/SelectionState.h"
#include "Skills/SkillReplies.h"
#include "Social/SocialEvents.h"
#include "Actors/ActorsState.h"
#include "Actors/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitions.h"
#include "Messenger/LetterWindows.h"

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
                                       &m_resources.cloth,
                                       m_resources.models.SkeletonOf(Tables::PlayerSkeletonModelId));
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
    Messenger::FlushTheChatRoomOrders(m_resources.world.Runtime().oldWindows.chatRooms, &session);
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldEvent(const App::WorldEvent& event,
                                                                       App::ServerSession& session)
{
    if (const auto* room = std::get_if<App::ChatRoomPacket>(&event))
        return Messenger::ApplyRoomPacket(room->window, room->packet, ChatContextOf(m_resources, session));
    if (const auto* closed = std::get_if<App::ChatRoomClosed>(&event))
        return Messenger::ApplyChatRoomClosed(*closed, ChatContextOf(m_resources, session));
    if (std::holds_alternative<App::ChatRoomPingDue>(event))
    {
        Messenger::PingTheChatRooms(m_resources.world.Runtime().oldWindows);
        return {};
    }
    return ApplyWorldContent(event, session);
}

Core::Expected<void, std::string> SessionEventApplier::CopyToPacketRing(std::span<const std::byte> packet)
{
    Actors::ActorsState* const actors = ActorsOf(m_resources);
    if (actors == nullptr)
        return Core::Unexpected(std::string("0x4e2b10: no Actors state for the viewport's queue (0x77f27b4)"));
    Actors::StoreViewportPacket(actors->viewportPackets, packet);
    return {};
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
        Items::ClearForInventoryReply(m_resources);
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
        Guild::ResetTheGuildTable(m_resources.world.Runtime().guild);
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
        const auto applied = Items::ApplyInventory(*inventory, m_resources);
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
