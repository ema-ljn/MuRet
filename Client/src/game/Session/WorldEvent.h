#pragma once

#include "Network/Packet.h"
#include "Protocol/CastleSiege/CastleBattle.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Player/LevelUpPoint.h"
#include "Protocol/Player/PkLevel.h"
#include "Protocol/Combat/Combat.h"
#include "Protocol/Social/Duel.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Messenger/FriendChat.h"
#include "Protocol/Messenger/Friends.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Messenger/Letters.h"
#include "Protocol/Chat/Messages.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Party/Party.h"
#include "Protocol/PersonalShop/PersonalShop.h"
#include "Protocol/Pets/PetInfo.h"
#include "Protocol/Pets/PetOrders.h"
#include "Protocol/Quests/Quest.h"
#include "Protocol/Skills/Skill.h"
#include "Protocol/Social/Trade.h"
#include "Protocol/Actors/Viewport.h"
#include "Protocol/Movement/World.h"
#include "Protocol/Weather/Weather.h"

#include <cstdint>
#include <variant>

// Recepciones propietarias del wire de mundo, antes de aplicar héroe, equipo, mapa o clima.
namespace Client::App
{
// Un F3 10 que no llegó cifrado: 0x4c55a0 hace igual su inicialización común (0x4c55c4..0x4c56e0), antes de
// mirar el flag, y después no lee el paquete (06, "El F3 10, el inventario").
struct ClearInventoryReceived
{
};

// 0x4c38eb..0x4c390a: terminación del retorno después de que la sesión mandóF3 00.
struct SelectionWorldReset
{
};

// F1 02 con1 cifrado, prefijo4c3626..4c363f: devuelve cursor y suelta mundo, deja escena4.
// La sesión mandaF3 00; después SelectionWorldReset completa4c38eb..4c390a.
struct ReturnToSelectionReceived
{
};

// CL9 hito 13: el B1 00 con el login ya mandado ([0x5767928] distinto de 0), lo del juego de 0x412b30 entre que cierra
// el GameServer (0x4c1680) y conecta con el nuevo (0x4c1340): Scenes::ApplyMapServerLeave.
struct MapServerLeft
{
};

// El saludo del GameServer nuevo con el movimiento pendiente: lo del juego de 0x412bb0 antes de armar el B1 01
// (Scenes::ApplyMapServerJoin).
struct MapServerJoined
{
};

// 0x41372f y el acceso al serial: después del tick del B1 01, antes de enviarlo.
struct MapServerAuthenticationPrepared
{
};

// 0x4c1e54: todo saludo posterior al login limpia la tabla de gremios, incluso sin movimiento pendiente.
struct GuildTableReset
{
};

// 0x4c275c..0x4c2779: limpieza del motivo 2, antes de comprobar el cifrado y cerrar el socket.
struct ServerListLogoutCleanup
{
};

// 0x4c2f1a..0x4c2f51: motivo 2 cifrado, con el GS ya cerrado; orden de escena 2.
struct ReturnToServerListReceived
{
};

// Lo que llega por la conexión de la sala de un chat con un amigo (IChatRoomLinks.h): 0x49e510 pasa cada paquete de
// cada sala a 0x4e1d50 con 1 y la ventana del chat, que lo da por pkt[3] (el switch 0x4e2a60) a 0x4dd1b0 (00), 0x4dd1e0
// (01), 0x4dd350 (02), 0x4dd3d0 (04) y 0x4dd540 (0D). 0x4e6240, una copia con los mismos casos, no la llama nadie.
struct ChatRoomPacket
{
    std::uint32_t room = 0;
    std::int32_t window = 0;
    Network::Packet packet;
};

// El cierre de la conexión de una sala. Abierta, es el FD_CLOSE de 0x49e480 (el 402 al chat y 0x4e8380), y la sala
// sigue anotada; sin abrir, el connect de 0x4e8570 que falla, y 0x49de50 no la anota.
struct ChatRoomClosed
{
    std::uint32_t room = 0;
    std::int32_t window = 0;
    bool opened = false;
};

// El timer 1002 (WM_TIMER cada 15 s, 0x4b1a5b → 0x4a6550): un C1 03 05 a la sala de cada chat con un amigo. La sesión
// lo da solo con alguna sala anotada: sin ninguna, 0x4a6550 no manda nada.
struct ChatRoomPingDue
{
};

using WorldEvent = std::variant<
    Protocol::FrontEnd::CharacterSelectResult, Protocol::Items::InventoryList, ClearInventoryReceived,
    ReturnToSelectionReceived, SelectionWorldReset, ServerListLogoutCleanup, ReturnToServerListReceived,
    Protocol::Weather::Weather, Protocol::Movement::Damage, Protocol::Movement::Teleport, Protocol::Movement::Regen,
    Protocol::Chat::Chat, Protocol::Chat::KeyChat, Protocol::Chat::Whisper, Protocol::Chat::WhisperResult,
    Protocol::Chat::Notice, Protocol::Player::OptionData, Protocol::Items::ItemMoveResult, Protocol::Items::ItemDelete,
    Protocol::Items::Durability, Protocol::Items::EquipmentChange, Protocol::Items::ItemPut,
    Protocol::Items::LifeRefill, Protocol::Items::ManaRefill, Protocol::Npc::TalkResult, Protocol::Npc::ItemList,
    Protocol::Npc::BuyResult, Protocol::Npc::SellResult, Protocol::Npc::RepairResult, Protocol::Npc::WarehouseMoney,
    Protocol::Npc::NpcWindowClosed, Protocol::Npc::WarehouseState, Protocol::Npc::ChaosMixResult,
    Protocol::Items::FloorItemsCreate, Protocol::Items::FloorItemsDelete, Protocol::Items::PickUpResult,
    Protocol::Items::DropResult, Protocol::Items::StatFruit, Protocol::Items::ShortPickUpResult,
    Protocol::Items::ShortZenPickUp, Protocol::Items::ShortDropResult, Protocol::Items::ShortStatFruit,
    Protocol::Social::TradeRequest, Protocol::Social::ClearTradeRequest, Protocol::Social::TradeResponse,
    Protocol::Social::TradeItemRemoved, Protocol::Social::TradeItemAdded, Protocol::Social::TradeMoneyResult,
    Protocol::Social::TradeOtherMoney, Protocol::Social::TradeOkState, Protocol::Social::TradeResult,
    Protocol::Party::PartyRequest, Protocol::Party::PartyRequestResult, Protocol::Party::PartyList,
    Protocol::Party::PartyLeft, Protocol::Party::PartyLife, Protocol::Social::DuelStart, Protocol::Social::DuelEnd,
    Protocol::Social::DuelRequest, Protocol::Social::DuelScore, Protocol::PersonalShop::ShopTitles,
    Protocol::PersonalShop::ShopCreated, Protocol::PersonalShop::ShopClosed, Protocol::PersonalShop::ShopNotice,
    Protocol::PersonalShop::ShopTitle, Protocol::PersonalShop::ShopOwnerLeft, Protocol::PersonalShop::PriceResult,
    Protocol::PersonalShop::ShopItemList, Protocol::PersonalShop::ShopPurchase, Protocol::PersonalShop::ShopRefresh,
    Protocol::Skills::MagicAttack, Protocol::Skills::DurationMagic, Protocol::Skills::MagicCancel,
    Protocol::Skills::ActorState, Protocol::Skills::ActorEffect, Protocol::Skills::SkillTarget,
    Protocol::Skills::SkillList, Protocol::Skills::LevelUp, Protocol::Player::LevelUpPointResult,
    Protocol::Player::PkLevel, Protocol::Skills::SummonLife, Protocol::Pets::PetOrder, Protocol::Pets::PetAttack,
    Protocol::Pets::PetInfoReply, Protocol::Quests::QuestFlag, Protocol::Quests::QuestList,
    Protocol::Quests::QuestStateChange, Protocol::Quests::SetQuestResult, Protocol::Quests::QuestResult,
    Protocol::Movement::Action, Protocol::Actors::MagicAttackCount, Protocol::Messenger::FriendList,
    Protocol::Messenger::FriendAddResult, Protocol::Messenger::FriendRequest, Protocol::Messenger::FriendDeleteResult,
    Protocol::Messenger::FriendState, Protocol::Combat::KillExperience, Protocol::Combat::Death,
    Protocol::Events::EventPacket, Protocol::Guild::GuildPacket, Protocol::Messenger::LetterSendResult,
    Protocol::Messenger::LetterRow, Protocol::Messenger::LetterRead, Protocol::Messenger::LetterDeleteResult,
    Protocol::Messenger::RoomAnswer, Protocol::Messenger::InviteResult, ChatRoomPacket, ChatRoomClosed, ChatRoomPingDue,
    Protocol::CastleSiege::CastleSiegePacket, Protocol::CastleSiege::CastleBattlePacket, MapServerLeft, MapServerJoined,
    MapServerAuthenticationPrepared, GuildTableReset>;
} // namespace Client::App
