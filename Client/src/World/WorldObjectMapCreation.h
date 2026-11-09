#pragma once

#include "World/WorldObjects.h"

#include <optional>

namespace Client::World
{
// La cadena default de 0x606660: cada handler conserva su guarda/retorno y lee el mapa al entrar.
// Unsupported es un gate de integración; no acredita el comportamiento transitivo faltante.
[[nodiscard]] std::optional<WorldObjectCreationError>
ApplyDefaultWorldObjectCreation(WorldObject& object, WorldObjectCreationContext& context);
} // namespace Client::World
