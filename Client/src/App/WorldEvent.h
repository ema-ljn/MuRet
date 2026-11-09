#pragma once

#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Movement.h"
#include "Protocol/World.h"

#include <variant>

// Recepciones propietarias del wire de mundo: entrada, inventario, caminar, daño, warp y revivir.
// El sink aplica cada una antes de la siguiente recepción, con el contexto vivo.
namespace Client::App
{
using WorldEvent = std::variant<Protocol::Character::CharacterSelectResult, Protocol::Character::InventoryList,
                                Protocol::Movement::Walk, Protocol::Movement::Damage, Protocol::World::Teleport,
                                Protocol::World::Regen>;
} // namespace Client::App
