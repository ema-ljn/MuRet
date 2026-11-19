#pragma once

#include "Network/Packet.h"
#include "Protocol/CastleBattle.h"
#include "Protocol/CastleSiege.h"
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
#include "Protocol/NpcWindows.h"
#include "Protocol/Movement.h"
#include "Protocol/OptionData.h"
#include "Protocol/Party.h"
#include "Protocol/PersonalShop.h"
#include "Protocol/Pet/PetInfo.h"
#include "Protocol/Pet/PetOrders.h"
#include "Protocol/Quest.h"
#include "Protocol/Skill.h"
#include "Protocol/Trade.h"
#include "Protocol/Viewport.h"
#include "Protocol/World.h"

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
    Protocol::Character::CharacterSelectResult, Protocol::Character::InventoryList, ClearInventoryReceived,
    ReturnToSelectionReceived, SelectionWorldReset, ServerListLogoutCleanup, ReturnToServerListReceived,
    Protocol::World::Weather, Protocol::Movement::Walk, Protocol::Movement::Damage, Protocol::World::Teleport,
    Protocol::World::Regen, Protocol::Messages::Chat, Protocol::Messages::KeyChat, Protocol::Messages::Whisper,
    Protocol::Messages::WhisperResult, Protocol::Messages::Notice, Protocol::OptionData::OptionData,
    Protocol::Inventory::ItemMoveResult, Protocol::Inventory::ItemDelete, Protocol::Inventory::Durability,
    Protocol::Inventory::EquipmentChange, Protocol::Inventory::ItemPut, Protocol::Inventory::LifeRefill,
    Protocol::Inventory::ManaRefill, Protocol::NpcWindows::TalkResult, Protocol::NpcWindows::ItemList,
    Protocol::NpcWindows::BuyResult, Protocol::NpcWindows::SellResult, Protocol::NpcWindows::RepairResult,
    Protocol::NpcWindows::WarehouseMoney, Protocol::NpcWindows::NpcWindowClosed, Protocol::NpcWindows::WarehouseState,
    Protocol::NpcWindows::ChaosMixResult, Protocol::FloorItems::FloorItemsCreate,
    Protocol::FloorItems::FloorItemsDelete, Protocol::FloorItems::PickUpResult, Protocol::FloorItems::DropResult,
    Protocol::FloorItems::StatFruit, Protocol::FloorItems::ShortPickUpResult, Protocol::FloorItems::ShortDropResult,
    Protocol::FloorItems::ShortStatFruit, Protocol::Trade::TradeRequest, Protocol::Trade::ClearTradeRequest,
    Protocol::Trade::TradeResponse, Protocol::Trade::TradeItemRemoved, Protocol::Trade::TradeItemAdded,
    Protocol::Trade::TradeMoneyResult, Protocol::Trade::TradeOtherMoney, Protocol::Trade::TradeOkState,
    Protocol::Trade::TradeResult, Protocol::Party::PartyRequest, Protocol::Party::PartyRequestResult,
    Protocol::Party::PartyList, Protocol::Party::PartyLeft, Protocol::Party::PartyLife, Protocol::Duel::DuelStart,
    Protocol::Duel::DuelEnd, Protocol::Duel::DuelRequest, Protocol::Duel::DuelScore, Protocol::PersonalShop::ShopTitles,
    Protocol::PersonalShop::ShopCreated, Protocol::PersonalShop::ShopClosed, Protocol::PersonalShop::ShopNotice,
    Protocol::PersonalShop::ShopTitle, Protocol::PersonalShop::ShopOwnerLeft, Protocol::PersonalShop::PriceResult,
    Protocol::PersonalShop::ShopItemList, Protocol::PersonalShop::ShopPurchase, Protocol::PersonalShop::ShopRefresh,
    Protocol::Skill::MagicAttack, Protocol::Skill::DurationMagic, Protocol::Skill::MagicCancel,
    Protocol::Skill::ActorState, Protocol::Skill::ActorEffect, Protocol::Skill::SkillTarget, Protocol::Skill::SkillList,
    Protocol::Skill::LevelUp, Protocol::Skill::SummonLife, Protocol::Pet::PetOrder, Protocol::Pet::PetAttack,
    Protocol::Pet::PetInfoReply, Protocol::Quest::QuestFlag, Protocol::Quest::QuestList,
    Protocol::Quest::QuestStateChange, Protocol::Quest::SetQuestResult, Protocol::Quest::QuestResult,
    Protocol::Movement::Action, Protocol::Viewport::Players, Protocol::Viewport::Leave, Protocol::Viewport::Transformed,
    Protocol::Viewport::EquipmentShown, Protocol::Viewport::Position, Protocol::Viewport::Monsters,
    Protocol::Viewport::Summons, Protocol::Viewport::Objects, Protocol::Viewport::MagicAttackCount,
    Protocol::Friends::FriendList, Protocol::Friends::FriendAddResult, Protocol::Friends::FriendRequest,
    Protocol::Friends::FriendDeleteResult, Protocol::Friends::FriendState, Protocol::Combat::KillExperience,
    Protocol::Combat::Death, Protocol::Events::EventPacket, Protocol::Guild::GuildPacket,
    Protocol::Letters::LetterSendResult, Protocol::Letters::LetterRow, Protocol::Letters::LetterRead,
    Protocol::Letters::LetterDeleteResult, Protocol::FriendChat::RoomAnswer, Protocol::FriendChat::InviteResult,
    ChatRoomPacket, ChatRoomClosed, ChatRoomPingDue, Protocol::CastleSiege::CastleSiegePacket,
    Protocol::CastleBattle::CastleBattlePacket, MapServerLeft, MapServerJoined, MapServerAuthenticationPrepared,
    GuildTableReset>;
} // namespace Client::App
