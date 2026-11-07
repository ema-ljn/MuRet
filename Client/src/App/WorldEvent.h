#pragma once

#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"

#include <variant>

// Recepciones propietarias del wire de mundo, antes de aplicar héroe, equipo o mapa.
namespace Client::App
{
using WorldEvent = std::variant<Protocol::Character::CharacterSelectResult, Protocol::Character::InventoryList>;
} // namespace Client::App
