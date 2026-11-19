#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/FaithfulPacer.h"
#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/CastleBattle.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Combat.h"
#include "Protocol/Duel.h"
#include "Protocol/Events.h"
#include "Protocol/FloorItems.h"
#include "Protocol/FriendChat.h"
#include "Protocol/Friends.h"
#include "Protocol/Guild.h"
#include "Protocol/Inventory.h"
#include "Protocol/Letters.h"
#include "Protocol/Messages.h"
#include "Protocol/Movement.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party.h"
#include "Protocol/PersonalShop.h"
#include "Protocol/Pet/PetInfo.h"
#include "Protocol/Pet/PetOrders.h"
#include "Protocol/Quest.h"
#include "Protocol/Skill.h"
#include "Protocol/Trade.h"
#include "Protocol/Viewport.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActionReply.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/CastleBattleReceive.h"
#include "Scenes/CastleSiegeReceive.h"
#include "Scenes/MapServerMoveApply.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/CombatReplies.h"
#include "Scenes/EventPackets.h"
#include "Scenes/FloorItemPackets.h"
#include "Scenes/GuildReceive.h"
#include "Scenes/GuildState.h"
#include "Scenes/GameFriends.h"
#include "Scenes/GameLetters.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/InventoryReply.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/MagicAttackCount.h"
#include "Scenes/MonsterViewport.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/PersonalShopLists.h"
#include "Scenes/PetOrders.h"
#include "Scenes/PetStats.h"
#include "Scenes/QuestReplies.h"
#include "Scenes/ReturnToSelection.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SkillReplies.h"
#include "Scenes/SocialEvents.h"
#include "Scenes/ViewportPacketRing.h"
#include "Scenes/ViewportPlayers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitions.h"
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
void ApplyWeather(const Protocol::World::Weather& received, World::LeafWeather& weather)
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

// 0x42a760 manda el B9 02 por el socket del GS al cargar el 30: el de la sesión que aplica la respuesta. Y 0x419900,
// al cerrar las misiones, y 0x657a90, al cerrar un NPC de evento, el C1 03 31 por la misma sesión, como el 3D del
// comercio (0x657fe1) y el 3F 07 de la tienda de otro (0x5eb17f) que mandan los mismos cierres, y las ventanas de los
// NPC los suyos (cerrarlas al cambiar de mapa, CL9 hito 6).
WorldEntryResources WithTheSession(const WorldEntryResources& resources, App::ServerSession& session)
{
    WorldEntryResources bound = resources;
    bound.requestCastleSiegeState = [&session] { session.RequestCastleSiegeState(); };
    bound.sendNpcWindowPacket = [&session](Protocol::Outgoing packet)
    { return session.SendNpcWindowPacket(std::move(packet)); };
    bound.sendWindowClose = [&session] { session.SendWindowClose(); };
    bound.sendGamePacket = [&session](Protocol::Outgoing outgoing) { session.SendGamePacket(std::move(outgoing)); };
    return bound;
}

// 04 § Las ventanas de los NPC (Scenes/NpcWindows.h).
template <typename Event> constexpr bool IsNpcWindowEvent(const Event&)
{
    namespace Npc = Protocol::NpcWindows;
    return std::is_same_v<Event, Npc::TalkResult> || std::is_same_v<Event, Npc::ItemList> ||
           std::is_same_v<Event, Npc::BuyResult> || std::is_same_v<Event, Npc::SellResult> ||
           std::is_same_v<Event, Npc::RepairResult> || std::is_same_v<Event, Npc::WarehouseMoney> ||
           std::is_same_v<Event, Npc::NpcWindowClosed> || std::is_same_v<Event, Npc::WarehouseState> ||
           std::is_same_v<Event, Npc::ChaosMixResult>;
}

std::optional<Core::Expected<void, std::string>>
ApplyNpcWindowEvent(const App::WorldEvent& event, const WorldEntryResources& unbound, App::ServerSession& session)
{
    namespace Npc = Protocol::NpcWindows;
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

// Lo que el chat que llega lee del juego y de la sesión: 0x6d5edc es el elegido de la lista.
ChatReceiveContext ChatContextOf(const WorldEntryResources& resources, App::ServerSession& session)
{
    const Protocol::Character::CharacterEntry* chosen = session.ChosenCharacter();
    return ChatReceiveContext{.world = resources.world,
                              .random = resources.cloth.random,
                              .measure = resources.textMeasure,
                              .screen = resources.screen ? resources.screen() : App::PixelSize{640, 480},
                              .scene = resources.scene ? resources.scene() : std::nullopt,
                              .texts = resources.texts,
                              .chosenSlot =
                                  chosen != nullptr ? std::optional<std::uint8_t>(chosen->slot) : std::nullopt,
                              .rooms = &session,
                              .worldEntry = &resources};
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
                               .screen = resources.screen ? resources.screen() : App::PixelSize{640, 480},
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
                                .screen = resources.screen ? resources.screen() : App::PixelSize{640, 480},
                                .texts = resources.texts,
                                .send = [&session](Protocol::Outgoing packet)
                                { session.SendCastlePacket(std::move(packet)); },
                                .dataRoot = resources.dataRoot,
                                .tick = resources.clock != nullptr ? App::TickCountMs(resources.clock->Now()) : 0u,
                                .entryResources = &resources};
}

// El 0x3d de la respuesta va a la sesión que la está aplicando.
class SessionPublisher final : public IWorldEntryPublisher, public IWorldTransitionPublisher
{
public:
    explicit SessionPublisher(App::ServerSession& session) : m_session(session) {}

    void PublishWorldEntry() override
    {
        m_session.PublishWorldEntry();
    }

    void ConfirmTeleport() override
    {
        m_session.ConfirmTeleport();
    }
    void RequestCrywolfState() override
    {
        m_session.RequestCrywolfState();
    }

private:
    App::ServerSession& m_session;
};
} // namespace

SessionEventApplier::SessionEventApplier(const WorldEntryResources& resources) : m_resources(resources) {}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_resources.selection.Apply(std::span(&event, 1), m_resources.camera, m_resources.cameraInput,
                                       &m_resources.cloth,
                                       m_resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel));
}

// Lo que las ventanas de los chats dejaron para sus salas (un chat que se soltó sale de la suya) sale al final de cada
// recepción.
Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent& event,
                                                                  App::ServerSession& session)
{
    auto applied = ApplyWorldEvent(event, session);
    FlushTheChatRoomOrders(m_resources.world.Runtime().oldWindows.chatRooms, &session);
    return applied;
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldEvent(const App::WorldEvent& event,
                                                                       App::ServerSession& session)
{
    if (const auto* walk = std::get_if<Protocol::Movement::Walk>(&event))
        return ApplyWalkReply(*walk, session.PlayerIndex(), m_resources);
    if (const auto* damage = std::get_if<Protocol::Movement::Damage>(&event))
        return ApplyDamageReply(*damage, session.PlayerIndex(), m_resources);
    if (const auto* attack = std::get_if<Protocol::Viewport::MagicAttackCount>(&event))
        return ApplyMagicAttackCount(*attack, session.PlayerIndex(), m_resources);
    // CL9 hito 3: el 0x16 y el 0x9C (0x4cf200, 0x4cfce0) y el 0x17 (0x4d0a30).
    if (const auto* experience = std::get_if<Protocol::Combat::KillExperience>(&event))
        return ApplyKillExperience(*experience, CombatContextOf(m_resources), ChatContextOf(m_resources, session));
    if (const auto* death = std::get_if<Protocol::Combat::Death>(&event))
        return ApplyDeath(*death, CombatContextOf(m_resources));
    if (const auto* action = std::get_if<Protocol::Movement::Action>(&event))
        return ApplyActionReply(*action, m_resources, [&session](std::uint8_t direction, std::uint8_t sent)
                                { session.SendAction(direction, sent); });
    if (const auto* teleport = std::get_if<Protocol::World::Teleport>(&event))
    {
        SessionPublisher publisher(session);
        return ApplyTeleport(*teleport, session.PlayerIndex(), WithTheSession(m_resources, session), publisher);
    }
    if (const auto* regen = std::get_if<Protocol::World::Regen>(&event))
        return ApplyRegen(*regen, session.PlayerIndex(), WithTheSession(m_resources, session));
    if (const auto* chat = std::get_if<Protocol::Messages::Chat>(&event))
    {
        // 0x4c6095: en el login, el 00 es el saludo del CS y pide la lista por el socket abierto.
        if (m_resources.scene && m_resources.scene() == SceneId::Login)
        {
            session.ResendServerListRequest();
            return {};
        }
        return ApplyChat(*chat, ChatContextOf(m_resources, session));
    }
    if (const auto* chat = std::get_if<Protocol::Messages::KeyChat>(&event))
    {
        // 0x4e345c: el 01 del Guild Master abre la ventana 2 del gremio, y lo que eso cierra manda por el GameServer.
        ChatReceiveContext context = ChatContextOf(m_resources, session);
        context.guildSend = [&session](Protocol::Outgoing packet) { session.SendGuildPacket(std::move(packet)); };
        return ApplyKeyChat(*chat, context);
    }
    if (const auto* whisper = std::get_if<Protocol::Messages::Whisper>(&event))
        return ApplyWhisper(*whisper, ChatContextOf(m_resources, session));
    if (const auto* result = std::get_if<Protocol::Messages::WhisperResult>(&event))
        return ApplyWhisperResult(*result, ChatContextOf(m_resources, session));
    if (const auto* notice = std::get_if<Protocol::Messages::Notice>(&event))
        return ApplyNotice(*notice, ChatContextOf(m_resources, session));
    if (const auto* options = std::get_if<Protocol::OptionData::OptionData>(&event))
        return ApplyOptionData(*options, ChatContextOf(m_resources, session));
    if (const auto* guild = std::get_if<Protocol::Guild::GuildPacket>(&event))
        return ApplyGuildPacket(*guild, GuildContextOf(m_resources, session));
    if (auto skill = ApplySkillEvent(event); skill)
        return std::move(*skill);
    if (auto viewport = ApplyViewport(event))
        return *viewport;
    if (const auto* list = std::get_if<Protocol::Friends::FriendList>(&event))
        return ApplyFriendList(*list, ChatContextOf(m_resources, session));
    if (const auto* added = std::get_if<Protocol::Friends::FriendAddResult>(&event))
        return ApplyFriendAddResult(*added, ChatContextOf(m_resources, session));
    if (const auto* request = std::get_if<Protocol::Friends::FriendRequest>(&event))
        return ApplyFriendRequest(*request, ChatContextOf(m_resources, session));
    if (const auto* deleted = std::get_if<Protocol::Friends::FriendDeleteResult>(&event))
        return ApplyFriendDeleteResult(*deleted, ChatContextOf(m_resources, session));
    if (const auto* state = std::get_if<Protocol::Friends::FriendState>(&event))
        return ApplyFriendState(*state, ChatContextOf(m_resources, session));
    // CL9 hito 10.
    if (const auto* sent = std::get_if<Protocol::Letters::LetterSendResult>(&event))
        return ApplyLetterSendResult(*sent, ChatContextOf(m_resources, session));
    if (const auto* row = std::get_if<Protocol::Letters::LetterRow>(&event))
        return ApplyLetterRow(*row, ChatContextOf(m_resources, session));
    if (const auto* letter = std::get_if<Protocol::Letters::LetterRead>(&event))
        return ApplyLetterRead(*letter, ChatContextOf(m_resources, session));
    if (const auto* deleted = std::get_if<Protocol::Letters::LetterDeleteResult>(&event))
        return ApplyLetterDeleteResult(*deleted, ChatContextOf(m_resources, session));
    if (const auto* answer = std::get_if<Protocol::FriendChat::RoomAnswer>(&event))
        return ApplyRoomAnswer(*answer, ChatContextOf(m_resources, session));
    if (const auto* invited = std::get_if<Protocol::FriendChat::InviteResult>(&event))
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
    if (const auto* request = std::get_if<Protocol::Trade::TradeRequest>(&event))
    {
        auto reply = ApplyTradeRequest(*request, m_resources);
        if (!reply.HasValue())
            return Core::Expected<void, std::string>(Core::Unexpected(std::move(reply).Error()));
        // 0x4d52bc..0x4d5553: con una tienda personal abierta, el C1 04 37 00 con el XOR32, en claro.
        if (reply.Value())
            session.SendGamePacket(std::move(*reply.Value()));
        return Core::Expected<void, std::string>{};
    }
    if (std::holds_alternative<Protocol::Trade::ClearTradeRequest>(event))
    {
        session.SendGamePacket(ApplyClearTradeRequest(m_resources.cloth.random));
        return Core::Expected<void, std::string>{};
    }
    if (const auto* response = std::get_if<Protocol::Trade::TradeResponse>(&event))
        return ApplyTradeResponse(*response, WithTheSession(m_resources, session));
    if (const auto* removed = std::get_if<Protocol::Trade::TradeItemRemoved>(&event))
        return ApplyTradeItemRemoved(*removed, m_resources);
    if (const auto* added = std::get_if<Protocol::Trade::TradeItemAdded>(&event))
        return ApplyTradeItemAdded(*added, m_resources);
    if (const auto* money = std::get_if<Protocol::Trade::TradeMoneyResult>(&event))
    {
        ApplyTradeMoneyResult(*money, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* other = std::get_if<Protocol::Trade::TradeOtherMoney>(&event))
    {
        ApplyTradeOtherMoney(*other, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* ok = std::get_if<Protocol::Trade::TradeOkState>(&event))
    {
        ApplyTradeOkState(*ok, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* result = std::get_if<Protocol::Trade::TradeResult>(&event))
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
    if (const auto* start = std::get_if<Protocol::Duel::DuelStart>(&event))
        return ApplyDuelStart(*start, m_resources);
    if (const auto* end = std::get_if<Protocol::Duel::DuelEnd>(&event))
        return ApplyDuelEnd(*end, m_resources);
    if (const auto* duel = std::get_if<Protocol::Duel::DuelRequest>(&event))
    {
        ApplyDuelRequest(*duel, world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* score = std::get_if<Protocol::Duel::DuelScore>(&event))
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
    if (const auto* attack = std::get_if<Protocol::Skill::MagicAttack>(&event))
        return ApplyMagicAttack(*attack, m_resources);
    if (const auto* magic = std::get_if<Protocol::Skill::DurationMagic>(&event))
        return ApplyDurationMagic(*magic, m_resources);
    if (const auto* cancel = std::get_if<Protocol::Skill::MagicCancel>(&event))
    {
        ApplyMagicCancel(*cancel, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* state = std::get_if<Protocol::Skill::ActorState>(&event))
        return ApplyActorState(*state, m_resources);
    if (const auto* effect = std::get_if<Protocol::Skill::ActorEffect>(&event))
        return ApplyActorEffect(*effect, m_resources);
    if (const auto* target = std::get_if<Protocol::Skill::SkillTarget>(&event))
        return ApplySkillTarget(*target, m_resources);
    if (const auto* list = std::get_if<Protocol::Skill::SkillList>(&event))
        return ApplySkillList(*list, m_resources);
    if (const auto* summon = std::get_if<Protocol::Skill::SummonLife>(&event))
    {
        ApplySummonLife(*summon, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* order = std::get_if<Protocol::Pet::PetOrder>(&event))
    {
        ApplyPetOrder(*order, m_resources.world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* attack = std::get_if<Protocol::Pet::PetAttack>(&event))
    {
        ApplyPetAttack(*attack, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* info = std::get_if<Protocol::Pet::PetInfoReply>(&event))
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

std::optional<Core::Expected<void, std::string>> SessionEventApplier::ApplyViewport(const App::WorldEvent& event)
{
    if (const auto* players = std::get_if<Protocol::Viewport::Players>(&event))
        return ApplyViewportPlayers(*players, m_resources);
    if (const auto* leave = std::get_if<Protocol::Viewport::Leave>(&event))
    {
        ApplyViewportLeave(*leave, m_resources.world);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* transformed = std::get_if<Protocol::Viewport::Transformed>(&event))
        return ApplyTransformed(*transformed, m_resources);
    if (const auto* shown = std::get_if<Protocol::Viewport::EquipmentShown>(&event))
        return ApplyEquipmentShown(*shown, m_resources);
    if (const auto* position = std::get_if<Protocol::Viewport::Position>(&event))
        return ApplyViewportPosition(*position, m_resources);
    if (const auto* monsters = std::get_if<Protocol::Viewport::Monsters>(&event))
        return ApplyViewportMonsters(*monsters, m_resources);
    if (const auto* summons = std::get_if<Protocol::Viewport::Summons>(&event))
        return ApplySummons(*summons, m_resources);
    if (const auto* objects = std::get_if<Protocol::Viewport::Objects>(&event))
        return ApplyViewportObjects(*objects, m_resources);
    return std::nullopt;
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldContent(const App::WorldEvent& event,
                                                                         App::ServerSession& session)
{
    if (const auto* weather = std::get_if<Protocol::World::Weather>(&event))
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
    if (const auto* battle = std::get_if<Protocol::CastleBattle::CastleBattlePacket>(&event))
        return ApplyCastleBattlePacket(*battle, CastleContextOf(m_resources, session));
    if (std::holds_alternative<App::MapServerLeft>(event))
    {
        const Protocol::Character::CharacterEntry* chosen = session.ChosenCharacter();
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
    if (const auto* moved = std::get_if<Protocol::Inventory::ItemMoveResult>(&event))
        return ApplyItemMoveResult(*moved, m_resources);
    if (const auto* life = std::get_if<Protocol::Inventory::LifeRefill>(&event))
        return ApplyLifeRefill(*life, m_resources.world, m_resources.cloth.random);
    if (const auto* mana = std::get_if<Protocol::Inventory::ManaRefill>(&event))
        return ApplyManaRefill(*mana, m_resources.world, m_resources.cloth.random);
    if (const auto* deleted = std::get_if<Protocol::Inventory::ItemDelete>(&event))
        return ApplyItemDelete(*deleted, m_resources);
    if (const auto* durability = std::get_if<Protocol::Inventory::Durability>(&event))
        return ApplyDurability(*durability, m_resources);
    if (const auto* change = std::get_if<Protocol::Inventory::EquipmentChange>(&event))
        return ApplyEquipmentChange(*change, m_resources);
    if (const auto* put = std::get_if<Protocol::Inventory::ItemPut>(&event))
        return ApplyItemPut(*put, m_resources);
    if (auto social = ApplySocial(event, session))
        return *social;
    // Las misiones (CL9 hito 2).
    if (const auto* flag = std::get_if<Protocol::Quest::QuestFlag>(&event))
    {
        ApplyQuestFlag(*flag, m_resources.world);
        return {};
    }
    if (const auto* list = std::get_if<Protocol::Quest::QuestList>(&event))
    {
        ApplyQuestList(*list, m_resources);
        return {};
    }
    if (const auto* state = std::get_if<Protocol::Quest::QuestStateChange>(&event))
        return ApplyQuestState(*state, WithTheSession(m_resources, session));
    if (const auto* set = std::get_if<Protocol::Quest::SetQuestResult>(&event))
        return ApplySetQuestResult(*set, WithTheSession(m_resources, session));
    if (const auto* result = std::get_if<Protocol::Quest::QuestResult>(&event))
        return ApplyQuestResult(*result, m_resources);
    // CL9 hito 5.
    if (const auto* created = std::get_if<Protocol::FloorItems::FloorItemsCreate>(&event))
        return ApplyFloorItemsCreate(*created, m_resources);
    if (const auto* deleted = std::get_if<Protocol::FloorItems::FloorItemsDelete>(&event))
    {
        ApplyFloorItemsDelete(*deleted, m_resources.world);
        return {};
    }
    if (const auto* picked = std::get_if<Protocol::FloorItems::PickUpResult>(&event))
        return ApplyPickUpResult(*picked, m_resources);
    if (const auto* dropped = std::get_if<Protocol::FloorItems::DropResult>(&event))
        return ApplyDropResult(*dropped, m_resources);
    if (const auto* fruit = std::get_if<Protocol::FloorItems::StatFruit>(&event))
        return ApplyStatFruit(*fruit, m_resources.world, m_resources.cloth.random, m_resources.texts);
    if (std::holds_alternative<Protocol::FloorItems::ShortPickUpResult>(event))
    {
        ApplyShortPickUpResult(m_resources.world);
        return {};
    }
    if (const auto* dropped = std::get_if<Protocol::FloorItems::ShortDropResult>(&event))
    {
        ApplyShortDropResult(*dropped, m_resources.world);
        return {};
    }
    if (std::holds_alternative<Protocol::FloorItems::ShortStatFruit>(event))
    {
        ApplyShortStatFruit(m_resources.world);
        return {};
    }
    if (const auto* inventory = std::get_if<Protocol::Character::InventoryList>(&event))
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
        const EventPacketContext context{
            .world = m_resources.world,
            .random = m_resources.cloth.random,
            .texts = m_resources.texts,
            .items = m_resources.items,
            .measure = m_resources.textMeasure,
            .screen = m_resources.screen ? m_resources.screen() : App::PixelSize{640, 480},
            .tickCount = m_resources.clock != nullptr ? App::TickCountMs(m_resources.clock->Now()) : 0U,
            .playerSkeleton = m_resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel),
            .models = &m_resources.models,
            .sendWindowClose = [&session] { session.SendWindowClose(); },
            // SendChat manda cualquier paquete de mundo, como el de los demás pasos.
            .send = [&session](Protocol::Outgoing outgoing) { session.SendChat(std::move(outgoing)); },
            .dataRoot = m_resources.dataRoot};
        const auto applied = ApplyEventPacket(*packet, context);
        if (!applied.HasValue())
            return Core::Unexpected(applied.Error());
        // Lo que el original hace fuera de sus tablas se descarta con un aviso, y la sesión sigue (Q-C174, Q-C778).
        if (applied.Value().discarded)
            m_resources.log.Warning("se descarta el resto del paquete de evento: {}", *applied.Value().discarded);
        return {};
    }
    const auto& reply = std::get<Protocol::Character::CharacterSelectResult>(event);
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
