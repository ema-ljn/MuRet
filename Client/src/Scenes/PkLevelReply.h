#pragma once

#include "Core/Expected.h"
#include "Protocol/Player/PkLevel.h"
#include "Scenes/WorldEntry.h"

#include <string>

// 0x4d3b50, el F3 08: el estado de PK de un personaje (CL9). Lo llama 0x4e30cd con el paquete.
namespace Client::Scenes
{
// El personaje de la clave (0x5110d0, sin mirar el resultado: el 400 si no hay nadie) guarda el estado en +0x375 y, en
// +0x1d6 (el word que la lista y el F3 03 escriben como nivel), 1 desde el 6 (JC) y 0 si no. Del 1 al 6, un aviso al
// chat (0x555190) con el nombre del personaje (+0x1d9, de otra llamada a 0x5110d0): el 487 con el 1 y el 2 y el 488 con
// el 3, en el tipo 1; el 489 con el 4, y "%s %d%s" (0x6c843c y 0x6c8444) con el 490, 1 o 2 y el 491 con el 5 y el 6, en
// el tipo 2. Ese sprintf va a un buffer de 0x20 bytes de la pila: si no entra, corta. Un texto que no está corta.
[[nodiscard]] Core::Expected<void, std::string> ApplyPkLevel(const Protocol::Character::PkLevel& packet,
                                                             const WorldEntryResources& resources);
} // namespace Client::Scenes
