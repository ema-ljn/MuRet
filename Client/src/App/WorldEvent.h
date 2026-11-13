#pragma once

#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Duel.h"
#include "Protocol/Inventory.h"
#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"
#include "Protocol/Party.h"
#include "Protocol/Trade.h"
#include "Protocol/World.h"
#include "Protocol/Movement.h"

#include <variant>

// Recepciones propietarias del wire de mundo, antes de aplicar héroe, equipo, mapa o clima.
namespace Client::App
{
// Un F3 10 que no llegó cifrado: 0x4c55a0 hace igual su inicialización común (0x4c55c4..0x4c56e0), antes de
// mirar el flag, y después no lee el paquete (06, "El F3 10, el inventario").
struct ClearInventoryReceived
{
};

// El F1 02 con 1 que llegó cifrado: lo que 0x4c2710 limpia del juego (0x4c3626..0x4c363a) y lo que hace
// 0x4c2480 (0x4c390a), sin los paquetes, que manda la sesión (06, "La vuelta a la selección"). Corre en
// cualquier escena: el 1 no mira cuál es.
struct ReturnToSelectionReceived
{
};

using WorldEvent =
    std::variant<Protocol::Character::CharacterSelectResult, Protocol::Character::InventoryList, ClearInventoryReceived,
                 ReturnToSelectionReceived, Protocol::World::Weather, Protocol::Movement::Walk,
                 Protocol::Movement::Damage, Protocol::World::Teleport, Protocol::World::Regen,
                 Protocol::Messages::Chat, Protocol::Messages::KeyChat, Protocol::Messages::Whisper,
                 Protocol::Messages::WhisperResult, Protocol::Messages::Notice, Protocol::OptionData::OptionData,
                 Protocol::Inventory::ItemMoveResult, Protocol::Inventory::ItemDelete, Protocol::Inventory::Durability,
                 Protocol::Inventory::EquipmentChange, Protocol::Inventory::ItemPut, Protocol::Inventory::LifeRefill,
                 Protocol::Inventory::ManaRefill, Protocol::Trade::TradeRequest, Protocol::Trade::ClearTradeRequest,
                 Protocol::Trade::TradeResponse, Protocol::Trade::TradeItemRemoved, Protocol::Trade::TradeItemAdded,
                 Protocol::Trade::TradeMoneyResult, Protocol::Trade::TradeOtherMoney, Protocol::Trade::TradeOkState,
                 Protocol::Trade::TradeResult, Protocol::Party::PartyRequest, Protocol::Party::PartyRequestResult,
                 Protocol::Party::PartyList, Protocol::Party::PartyLeft, Protocol::Party::PartyLife,
                 Protocol::Duel::DuelStart, Protocol::Duel::DuelEnd, Protocol::Duel::DuelRequest,
                 Protocol::Duel::DuelScore>;
} // namespace Client::App
