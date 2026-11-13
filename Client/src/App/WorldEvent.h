#pragma once

#include "Network/Packet.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/FriendChat.h"
#include "Protocol/Friends.h"
#include "Protocol/Inventory.h"
#include "Protocol/Letters.h"
#include "Protocol/Messages.h"
#include "Protocol/OptionData.h"
#include "Protocol/World.h"
#include "Protocol/Movement.h"

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

// El F1 02 con 1 que llegó cifrado: lo que 0x4c2710 limpia del juego (0x4c3626..0x4c363a) y lo que hace
// 0x4c2480 (0x4c390a), sin los paquetes, que manda la sesión (06, "La vuelta a la selección"). Corre en
// cualquier escena: el 1 no mira cuál es.
struct ReturnToSelectionReceived
{
};

// Lo que llega por la conexión de la sala de un chat con un amigo (IChatRoomLinks.h): 0x49e510 pasa cada paquete de
// cada sala a 0x4e1d50 con 1 y la ventana del chat, que lo da a 0x4e6240.
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

using WorldEvent =
    std::variant<Protocol::Character::CharacterSelectResult, Protocol::Character::InventoryList, ClearInventoryReceived,
                 ReturnToSelectionReceived, Protocol::World::Weather, Protocol::Movement::Walk,
                 Protocol::Movement::Damage, Protocol::World::Teleport, Protocol::World::Regen,
                 Protocol::Messages::Chat, Protocol::Messages::KeyChat, Protocol::Messages::Whisper,
                 Protocol::Messages::WhisperResult, Protocol::Messages::Notice, Protocol::OptionData::OptionData,
                 Protocol::Inventory::ItemMoveResult, Protocol::Inventory::ItemDelete, Protocol::Inventory::Durability,
                 Protocol::Inventory::EquipmentChange, Protocol::Inventory::ItemPut, Protocol::Inventory::LifeRefill,
                 Protocol::Inventory::ManaRefill, Protocol::Friends::FriendList, Protocol::Friends::FriendAddResult,
                 Protocol::Friends::FriendRequest, Protocol::Friends::FriendDeleteResult,
                 Protocol::Friends::FriendState, Protocol::Letters::LetterSendResult, Protocol::Letters::LetterRow,
                 Protocol::Letters::LetterRead, Protocol::Letters::LetterDeleteResult, Protocol::FriendChat::RoomAnswer,
                 Protocol::FriendChat::InviteResult, ChatRoomPacket, ChatRoomClosed, ChatRoomPingDue>;
} // namespace Client::App
