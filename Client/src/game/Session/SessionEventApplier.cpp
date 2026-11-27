#include "Session/SessionEventApplier.h"

#include "Session/CharacterEvent.h"
#include "App/FaithfulPacer.h"
#include "Session/ServerSession.h"
#include "Session/SessionContexts.h"
#include "Session/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Tables/PlayerModels.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Messenger/Friends.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Messenger/Letters.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party/Party.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Pets/PetInfo.h"
#include "Protocol/Pets/PetOrders.h"
#include "Protocol/Quests/Quest.h"
#include "Protocol/Skills/Skill.h"
#include "Protocol/Social/Trade.h"
#include "Protocol/Weather/Weather.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Scenes/ActionReply.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/CastleBattleReceive.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/LevelUpPointReply.h"
#include "Session/MapServerMoveApply.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/EventPackets.h"
#include "Scenes/FloorItemPackets.h"
#include "Scenes/GuildReceive.h"
#include "Scenes/GuildState.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameLetters.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/InventoryReply.h"
#include "Scenes/MagicAttackCount.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/PetOrders.h"
#include "Scenes/PetStats.h"
#include "Scenes/QuestReplies.h"
#include "Session/ReturnToSelection.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SkillReplies.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/ViewportPacketRing.h"
#include "Session/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Session/WorldTransitions.h"
#include "UI/LetterWindows.h"
#include "World/LeafWeather.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Client::Scenes
{
namespace
{
using Session::ChatContextOf;
using Session::SessionPublisher;
using Session::WithTheSession;

void ApplyWeather(const Protocol::Weather::Weather& received, World::LeafWeather& weather)
{
    // 0x4e36c4/0x4e36ed: sólo target; el default de 0x4e5b4a no escribe nada ni pide rand().
    constexpr std::uint8_t NibbleShift = 4;
    constexpr std::uint8_t IntensityMask = 0x0f;
    constexpr std::int32_t TargetPerIntensity = 6;
    const auto mode = received.code >> NibbleShift;
    if (mode == 0)
        weather.target = 0;
    else if (mode == 1)
        weather.target = static_cast<std::int32_t>(received.code & IntensityMask) * TargetPerIntensity;
}

// 04 § Las ventanas de los NPC (Scenes/NpcWindows.h).
template <typename Event> constexpr bool IsNpcWindowEvent(const Event&)
{
    namespace Npc = Protocol::Npc;
    return std::is_same_v<Event, Npc::TalkResult> || std::is_same_v<Event, Npc::ItemList> ||
           std::is_same_v<Event, Npc::BuyResult> || std::is_same_v<Event, Npc::SellResult> ||
           std::is_same_v<Event, Npc::RepairResult> || std::is_same_v<Event, Npc::WarehouseMoney> ||
           std::is_same_v<Event, Npc::NpcWindowClosed> || std::is_same_v<Event, Npc::WarehouseState> ||
           std::is_same_v<Event, Npc::ChaosMixResult>;
}

std::optional<Core::Expected<void, std::string>>
ApplyNpcWindowEvent(const App::WorldEvent& event, const WorldEntryResources& unbound, App::ServerSession& session)
{
    namespace Npc = Protocol::Npc;
    if (std::visit([](const auto& value) { return !IsNpcWindowEvent(value); }, event))
        return std::nullopt;
    const WorldEntryResources resources = WithTheSession(unbound, session);
    if (const auto* talk = std::get_if<Npc::TalkResult>(&event))
        return ApplyTalkResult(*talk, resources);
    if (const auto* list = std::get_if<Npc::ItemList>(&event))
        return ApplyItemList(*list, resources);
    if (const auto* bought = std::get_if<Npc::BuyResult>(&event))
        return ApplyBuyResult(*bought, resources);
    if (const auto* sold = std::get_if<Npc::SellResult>(&event))
        return ApplySellResult(*sold, resources);
    if (const auto* repaired = std::get_if<Npc::RepairResult>(&event))
        return ApplyRepairResult(*repaired, resources);
    if (const auto* money = std::get_if<Npc::WarehouseMoney>(&event))
        return ApplyWarehouseMoney(*money, resources);
    if (const auto* closed = std::get_if<Npc::NpcWindowClosed>(&event))
        return ApplyNpcWindowClosed(*closed, resources);
    if (const auto* state = std::get_if<Npc::WarehouseState>(&event))
        return ApplyWarehouseState(*state, resources);
    if (const auto* mixed = std::get_if<Npc::ChaosMixResult>(&event))
        return ApplyChaosMixResult(*mixed, resources);
    return std::nullopt;
}

// Lo que los paquetes del gremio leen del juego y de la sesión (CL9 hito 8): lo que mandan va por el GameServer de la
// sesión, también los C1 18 del gesto de la guerra (0x576a60, el emote del hito 1, con los modelos de la escena).
GuildReceiveContext GuildContextOf(const WorldEntryResources& resources, App::ServerSession& session)
{
    const std::uint32_t now =
        resources.clock != nullptr
            ? static_cast<std::uint32_t>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(resources.clock->Now()).count())
            : 0U;
    return GuildReceiveContext{.world = resources.world,
                               .random = resources.cloth.random,
                               .measure = resources.textMeasure,
                               .screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480},
                               .texts = resources.texts,
                               .playerKey = session.PlayerIndex(),
                               .now = now,
                               .send = [&session](Protocol::Outgoing packet)
                               { session.SendGuildPacket(std::move(packet)); },
                               .models = &resources.models,
                               .worldEntry = &resources};
}

// Lo que los paquetes del castillo leen del juego (CL9 hito 12); lo que mandan va por el GameServer de la sesión.
CastleReceiveContext CastleContextOf(const WorldEntryResources& resources, App::ServerSession& session)
{
    return CastleReceiveContext{.world = resources.world,
                                .random = resources.cloth.random,
                                .log = resources.log,
                                .measure = resources.textMeasure,
                                .screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480},
                                .texts = resources.texts,
                                .send = [&session](Protocol::Outgoing packet)
                                { session.SendCastlePacket(std::move(packet)); },
                                .dataRoot = resources.dataRoot,
                                .tick = resources.clock != nullptr ? App::TickCountMs(resources.clock->Now()) : 0u,
                                .entryResources = &resources};
}

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
    if (const auto* guild = std::get_if<Protocol::Guild::GuildPacket>(&event))
        return ApplyGuildPacket(*guild, GuildContextOf(m_resources, session));
    if (auto skill = ApplySkillEvent(event); skill)
        return std::move(*skill);
    if (const auto* list = std::get_if<Protocol::Messenger::FriendList>(&event))
        return ApplyFriendList(*list, ChatContextOf(m_resources, session));
    if (const auto* added = std::get_if<Protocol::Messenger::FriendAddResult>(&event))
        return ApplyFriendAddResult(*added, ChatContextOf(m_resources, session));
    if (const auto* request = std::get_if<Protocol::Messenger::FriendRequest>(&event))
        return ApplyFriendRequest(*request, ChatContextOf(m_resources, session));
    if (const auto* deleted = std::get_if<Protocol::Messenger::FriendDeleteResult>(&event))
        return ApplyFriendDeleteResult(*deleted, ChatContextOf(m_resources, session));
    if (const auto* state = std::get_if<Protocol::Messenger::FriendState>(&event))
        return ApplyFriendState(*state, ChatContextOf(m_resources, session));
    // CL9 hito 10.
    if (const auto* sent = std::get_if<Protocol::Messenger::LetterSendResult>(&event))
        return ApplyLetterSendResult(*sent, ChatContextOf(m_resources, session));
    if (const auto* row = std::get_if<Protocol::Messenger::LetterRow>(&event))
        return ApplyLetterRow(*row, ChatContextOf(m_resources, session));
    if (const auto* letter = std::get_if<Protocol::Messenger::LetterRead>(&event))
        return ApplyLetterRead(*letter, ChatContextOf(m_resources, session));
    if (const auto* deleted = std::get_if<Protocol::Messenger::LetterDeleteResult>(&event))
        return ApplyLetterDeleteResult(*deleted, ChatContextOf(m_resources, session));
    if (const auto* answer = std::get_if<Protocol::Messenger::RoomAnswer>(&event))
        return ApplyRoomAnswer(*answer, ChatContextOf(m_resources, session));
    if (const auto* invited = std::get_if<Protocol::Messenger::InviteResult>(&event))
        return ApplyInviteResult(*invited, ChatContextOf(m_resources, session));
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

std::optional<Core::Expected<void, std::string>> SessionEventApplier::ApplySocial(const App::WorldEvent& event,
                                                                                  App::ServerSession& session)
{
    WorldState& world = m_resources.world;
    if (const auto* request = std::get_if<Protocol::Social::TradeRequest>(&event))
    {
        auto reply = ApplyTradeRequest(*request, m_resources);
        if (!reply.HasValue())
            return Core::Expected<void, std::string>(Core::Unexpected(std::move(reply).Error()));
        // 0x4d52bc..0x4d5553: con una tienda personal abierta, el C1 04 37 00 con el XOR32, en claro.
        if (reply.Value())
            session.SendGamePacket(std::move(*reply.Value()));
        return Core::Expected<void, std::string>{};
    }
    if (std::holds_alternative<Protocol::Social::ClearTradeRequest>(event))
    {
        session.SendGamePacket(ApplyClearTradeRequest(m_resources.cloth.random));
        return Core::Expected<void, std::string>{};
    }
    if (const auto* response = std::get_if<Protocol::Social::TradeResponse>(&event))
        return ApplyTradeResponse(*response, WithTheSession(m_resources, session));
    if (const auto* removed = std::get_if<Protocol::Social::TradeItemRemoved>(&event))
        return ApplyTradeItemRemoved(*removed, m_resources);
    if (const auto* added = std::get_if<Protocol::Social::TradeItemAdded>(&event))
        return ApplyTradeItemAdded(*added, m_resources);
    if (const auto* money = std::get_if<Protocol::Social::TradeMoneyResult>(&event))
    {
        ApplyTradeMoneyResult(*money, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* other = std::get_if<Protocol::Social::TradeOtherMoney>(&event))
    {
        ApplyTradeOtherMoney(*other, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* ok = std::get_if<Protocol::Social::TradeOkState>(&event))
    {
        ApplyTradeOkState(*ok, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* result = std::get_if<Protocol::Social::TradeResult>(&event))
        return ApplyTradeResult(*result, m_resources);
    if (const auto* invite = std::get_if<Protocol::Party::PartyRequest>(&event))
    {
        ApplyPartyRequest(*invite, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* answer = std::get_if<Protocol::Party::PartyRequestResult>(&event))
        return ApplyPartyRequestResult(*answer, m_resources);
    if (const auto* list = std::get_if<Protocol::Party::PartyList>(&event))
    {
        ApplyPartyList(*list, world, m_resources.log);
        return Core::Expected<void, std::string>{};
    }
    if (std::holds_alternative<Protocol::Party::PartyLeft>(event))
        return ApplyPartyLeft(m_resources);
    if (const auto* life = std::get_if<Protocol::Party::PartyLife>(&event))
    {
        ApplyPartyLife(*life, world, m_resources.log);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* start = std::get_if<Protocol::Social::DuelStart>(&event))
        return ApplyDuelStart(*start, m_resources);
    if (const auto* end = std::get_if<Protocol::Social::DuelEnd>(&event))
        return ApplyDuelEnd(*end, m_resources);
    if (const auto* duel = std::get_if<Protocol::Social::DuelRequest>(&event))
    {
        ApplyDuelRequest(*duel, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* score = std::get_if<Protocol::Social::DuelScore>(&event))
    {
        ApplyDuelScore(*score, world);
        return Core::Expected<void, std::string>{};
    }
    // Las tiendas personales (CL9 hito 7, fase 3): los carteles y los avisos del 3F.
    if (const auto* titles = std::get_if<Protocol::PersonalShop::ShopTitles>(&event))
    {
        ApplyShopTitles(*titles, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* created = std::get_if<Protocol::PersonalShop::ShopCreated>(&event))
    {
        ApplyShopCreated(*created, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* closed = std::get_if<Protocol::PersonalShop::ShopClosed>(&event))
    {
        ApplyShopClosed(*closed, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* notice = std::get_if<Protocol::PersonalShop::ShopNotice>(&event))
        return ApplyShopNotice(*notice, m_resources);
    if (const auto* title = std::get_if<Protocol::PersonalShop::ShopTitle>(&event))
    {
        ApplyOtherShopTitle(*title, world);
        return Core::Expected<void, std::string>{};
    }
    // Las listas de ítems (fase 4): abrir y cerrar las tiendas manda por la sesión (el F3 10 y el 3F 07).
    const GamePacketSender send = [&session](Protocol::Outgoing outgoing)
    { session.SendGamePacket(std::move(outgoing)); };
    if (std::holds_alternative<Protocol::PersonalShop::ShopOwnerLeft>(event))
        return ApplyShopOwnerLeft(m_resources, send);
    if (const auto* price = std::get_if<Protocol::PersonalShop::PriceResult>(&event))
        return ApplyPriceResult(*price, m_resources, send);
    if (const auto* list = std::get_if<Protocol::PersonalShop::ShopItemList>(&event))
        return ApplyShopItemList(*list, WithTheSession(m_resources, session), send);
    if (const auto* purchase = std::get_if<Protocol::PersonalShop::ShopPurchase>(&event))
        return ApplyShopPurchase(*purchase, m_resources, send);
    if (const auto* refresh = std::get_if<Protocol::PersonalShop::ShopRefresh>(&event))
        return ApplyShopRefresh(*refresh, m_resources);
    return std::nullopt;
}

// CL9 hito 4: las skills que llegan (Scenes/SkillReplies.h), las órdenes del cuervo (Scenes/PetOrders.h) y el A9 de
// las mascotas (Scenes/PetStats.h). nullopt si `event` no es una.
std::optional<Core::Expected<void, std::string>> SessionEventApplier::ApplySkillEvent(const App::WorldEvent& event)
{
    if (const auto* attack = std::get_if<Protocol::Skills::MagicAttack>(&event))
        return ApplyMagicAttack(*attack, m_resources);
    if (const auto* magic = std::get_if<Protocol::Skills::DurationMagic>(&event))
        return ApplyDurationMagic(*magic, m_resources);
    if (const auto* cancel = std::get_if<Protocol::Skills::MagicCancel>(&event))
    {
        ApplyMagicCancel(*cancel, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* state = std::get_if<Protocol::Skills::ActorState>(&event))
        return ApplyActorState(*state, m_resources);
    if (const auto* effect = std::get_if<Protocol::Skills::ActorEffect>(&event))
        return ApplyActorEffect(*effect, m_resources);
    if (const auto* target = std::get_if<Protocol::Skills::SkillTarget>(&event))
        return ApplySkillTarget(*target, m_resources);
    if (const auto* list = std::get_if<Protocol::Skills::SkillList>(&event))
        return ApplySkillList(*list, m_resources);
    if (const auto* summon = std::get_if<Protocol::Skills::SummonLife>(&event))
    {
        ApplySummonLife(*summon, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* order = std::get_if<Protocol::Pets::PetOrder>(&event))
    {
        ApplyPetOrder(*order, m_resources.world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* attack = std::get_if<Protocol::Pets::PetAttack>(&event))
    {
        ApplyPetAttack(*attack, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* info = std::get_if<Protocol::Pets::PetInfoReply>(&event))
        return ApplyPetInfo(*info, m_resources);
    return std::nullopt;
}

void SessionEventApplier::CopyToPacketRing(std::span<const std::byte> packet)
{
    StoreViewportPacket(m_resources.world.Runtime().viewportPackets, packet);
}

void SessionEventApplier::ClearLogoutRequest()
{
    m_resources.world.Runtime().ui.logoutRequested = false;
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldContent(const App::WorldEvent& event,
                                                                         App::ServerSession& session)
{
    if (const auto* weather = std::get_if<Protocol::Weather::Weather>(&event))
    {
        ApplyWeather(*weather, m_resources.world.Runtime().leafWeather);
        return {};
    }
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
    if (const auto* castle = std::get_if<Protocol::CastleSiege::CastleSiegePacket>(&event))
        return ApplyCastleSiegePacket(*castle, CastleContextOf(m_resources, session));
    if (auto npc = ApplyNpcWindowEvent(event, m_resources, session))
        return *std::move(npc);
    if (const auto* battle = std::get_if<Protocol::CastleSiege::CastleBattlePacket>(&event))
        return ApplyCastleBattlePacket(*battle, CastleContextOf(m_resources, session));
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
    if (const auto* moved = std::get_if<Protocol::Items::ItemMoveResult>(&event))
        return ApplyItemMoveResult(*moved, m_resources);
    if (const auto* life = std::get_if<Protocol::Items::LifeRefill>(&event))
        return ApplyLifeRefill(*life, m_resources.world, m_resources.cloth.random);
    if (const auto* mana = std::get_if<Protocol::Items::ManaRefill>(&event))
        return ApplyManaRefill(*mana, m_resources.world, m_resources.cloth.random);
    if (const auto* deleted = std::get_if<Protocol::Items::ItemDelete>(&event))
        return ApplyItemDelete(*deleted, m_resources);
    if (const auto* durability = std::get_if<Protocol::Items::Durability>(&event))
        return ApplyDurability(*durability, m_resources);
    if (const auto* change = std::get_if<Protocol::Items::EquipmentChange>(&event))
        return ApplyEquipmentChange(*change, m_resources);
    if (const auto* put = std::get_if<Protocol::Items::ItemPut>(&event))
        return ApplyItemPut(*put, m_resources);
    if (auto social = ApplySocial(event, session))
        return *social;
    // Las misiones (CL9 hito 2).
    if (const auto* flag = std::get_if<Protocol::Quests::QuestFlag>(&event))
    {
        ApplyQuestFlag(*flag, m_resources.world);
        return {};
    }
    if (const auto* list = std::get_if<Protocol::Quests::QuestList>(&event))
    {
        ApplyQuestList(*list, m_resources);
        return {};
    }
    if (const auto* state = std::get_if<Protocol::Quests::QuestStateChange>(&event))
        return ApplyQuestState(*state, WithTheSession(m_resources, session));
    if (const auto* set = std::get_if<Protocol::Quests::SetQuestResult>(&event))
        return ApplySetQuestResult(*set, WithTheSession(m_resources, session));
    if (const auto* result = std::get_if<Protocol::Quests::QuestResult>(&event))
        return ApplyQuestResult(*result, m_resources);
    // CL9 hito 5.
    if (const auto* created = std::get_if<Protocol::Items::FloorItemsCreate>(&event))
        return ApplyFloorItemsCreate(*created, m_resources);
    if (const auto* deleted = std::get_if<Protocol::Items::FloorItemsDelete>(&event))
    {
        ApplyFloorItemsDelete(*deleted, m_resources.world);
        return {};
    }
    if (const auto* picked = std::get_if<Protocol::Items::PickUpResult>(&event))
        return ApplyPickUpResult(*picked, m_resources);
    if (const auto* dropped = std::get_if<Protocol::Items::DropResult>(&event))
        return ApplyDropResult(*dropped, m_resources);
    if (const auto* fruit = std::get_if<Protocol::Items::StatFruit>(&event))
        return ApplyStatFruit(*fruit, m_resources.world, m_resources.cloth.random, m_resources.texts);
    if (const auto* zen = std::get_if<Protocol::Items::ShortZenPickUp>(&event))
        return ApplyShortZenPickUp(*zen, m_resources);
    if (std::holds_alternative<Protocol::Items::ShortPickUpResult>(event))
    {
        ApplyShortPickUpResult(m_resources.world);
        return {};
    }
    if (const auto* dropped = std::get_if<Protocol::Items::ShortDropResult>(&event))
    {
        ApplyShortDropResult(*dropped, m_resources.world);
        return {};
    }
    if (std::holds_alternative<Protocol::Items::ShortStatFruit>(event))
    {
        ApplyShortStatFruit(m_resources.world);
        return {};
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
    if (const auto* packet = std::get_if<Protocol::Events::EventPacket>(&event))
    {
        // CL9 hito 11: GetTickCount es el reloj de la sesión en ms (App::TickCountMs); sin reloj, 0.
        const InventoryServices inventory = InventoryServicesOf(m_resources);
        const EventPacketContext context{
            .world = m_resources.world,
            .random = m_resources.cloth.random,
            .texts = m_resources.texts,
            .items = m_resources.items,
            .measure = m_resources.textMeasure,
            .screen = m_resources.screen ? m_resources.screen() : Core::PixelSize{640, 480},
            .tickCount = m_resources.clock != nullptr ? App::TickCountMs(m_resources.clock->Now()) : 0U,
            .playerSkeleton = m_resources.models.SkeletonOf(Tables::PlayerSkeletonModel),
            .models = &m_resources.models,
            .sendWindowClose = [&session] { session.SendWindowClose(); },
            // SendChat manda cualquier paquete de mundo, como el de los demás pasos.
            .send = [&session](Protocol::Outgoing outgoing) { session.SendChat(std::move(outgoing)); },
            .dataRoot = m_resources.dataRoot,
            .sendNpcWindow = [&session](Protocol::Outgoing outgoing)
            { return session.SendNpcWindowPacket(std::move(outgoing)); },
            .inventory = &inventory};
        const auto applied = ApplyEventPacket(*packet, context);
        if (!applied.HasValue())
            return Core::Unexpected(applied.Error());
        // Lo que el original hace fuera de sus tablas se descarta con un aviso, y la sesión sigue (Q-C174, Q-C778).
        if (applied.Value().discarded)
            m_resources.log.Warning("se descarta el resto del paquete de evento: {}", *applied.Value().discarded);
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
