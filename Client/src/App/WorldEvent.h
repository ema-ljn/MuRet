#pragma once

#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
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
                 Protocol::Movement::Damage, Protocol::World::Teleport, Protocol::World::Regen>;
} // namespace Client::App
