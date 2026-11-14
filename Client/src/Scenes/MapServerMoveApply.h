#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <optional>
#include <string>

// Lo del juego en el cambio de servidor del mapa (CL9 hito 13; 04 § Castle Siege, la batalla): el B1 00 (0x412b30) y
// el saludo del GameServer nuevo (0x4c1e20 → 0x412bb0). Los sockets, el saludo y los paquetes que salen son de la
// sesión (App::ServerSession).
namespace Client::Scenes
{
struct WorldEntryResources;

// 0x412b30 con [0x5767928] distinto de 0, entre 0x4c1680 (que cierra el socket del GameServer) y 0x4c1340 (que
// conecta con el nuevo): 0x61c210 arma las opciones (el registro propio: 3.848 rand() al cerrarse sin accesos) y no
// puede mandarlas, y 0x61c170 escribe Data\Macro.txt. `chosenSlot` es 0x6d5edc, el elegido de la lista.
[[nodiscard]] Core::Expected<void, std::string> ApplyMapServerLeave(const WorldEntryResources& resources,
                                                                    std::optional<std::uint8_t> chosenSlot);

// 0x412bb0 hasta el paquete, con el movimiento pendiente: 0x510f70(−1), 0x4c2480 (sin sus dos C1 03 31, de la sesión)
// y los dos rand() del C3 del B1 01, el relleno (0x41372f) y el cierre del serial (0x5767913).
void ApplyMapServerJoin(const WorldEntryResources& resources);
} // namespace Client::Scenes
