#pragma once

#include "World/WorldObjects.h"

#include <cstdint>
#include <optional>

namespace Client::World
{
// La cadena default de 0x606660: cada handler conserva su guarda/retorno y lee el mapa al entrar.
// Unsupported es un gate de integración; no acredita el comportamiento transitivo faltante.
[[nodiscard]] std::optional<WorldObjectCreationError>
ApplyDefaultWorldObjectCreation(WorldObject& object, WorldObjectCreationContext& context);

// 0x42b480, del mapa 30, en la creación (0x42c6b0) y en cada paso de un objeto visto (0x42c500): con el byte
// 0x7239ec que lee 0x4294b0 en `siegeState`, el 4, el 0xb, el 0x25, el 0x29, del 0x2b al 0x30, del 0x43 al
// 0x45, del 0x47 al 0x4c y el 0x58 quedan con +0x58 en −1 si es distinto de 0 y en −2 si es 0; el 0x10, del
// 0x26 al 0x28, el 0x32, el 0x3a, el 0x3b, el 0x4e y del 0x50 al 0x57, al revés. Los demás tipos, nada.
void ApplyCastleSiegeObjectState(WorldObject& object, std::uint8_t siegeState);
} // namespace Client::World
