#pragma once

#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"

#include <variant>

// Recepciones propietarias del wire de mundo, antes de aplicar héroe, equipo o mapa.
namespace Client::App
{
// Un F3 10 que no llegó cifrado: 0x4c55a0 hace igual su inicialización común (0x4c55c4..0x4c56e0), antes de
// mirar el flag, y después no lee el paquete (06, "El F3 10, el inventario").
struct ClearInventoryReceived
{
};

using WorldEvent = std::variant<Protocol::Character::CharacterSelectResult, Protocol::Character::InventoryList,
                                ClearInventoryReceived>;
} // namespace Client::App
