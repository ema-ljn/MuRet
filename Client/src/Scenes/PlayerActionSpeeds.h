#pragma once

#include <array>
#include <optional>

namespace Client::Scenes
{
// Overrides compartidos de la tabla020a: no se reinician al cambiar escena ni por actor.
// nullopt conserva la velocidad cargada del modelo; no afirma su origen nativo de arranque.
using PlayerActionSpeeds = std::array<std::optional<float>, 256>;
} // namespace Client::Scenes
