#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Client::Protocol::Character
{
struct InventoryList;
}

// El F3 10, 0x4c55a0 desde 0x4e309c. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El F3 10, el
// inventario", y 04-protocolo.md, "La entrada al mundo: la respuesta del F3 03 y el F3 10".
namespace Client::Scenes
{
struct WorldEntryResources;

// Lo que el F3 10 pide afuera y MuRet todavía no hace (sin audio hasta la CL10): el sonido de Fenrir de cada
// 0x4f2250 que corre dentro de 0x512a30, en el orden en que los pide.
struct InventoryReplyResult
{
    std::vector<std::int32_t> sounds;
};

// La inicialización común de 0x4c55a0 (0x4c55c4..0x4c56e0), que corre antes de mirar si llegó cifrado y
// también con la cantidad en 0: abre el registro propio, vacía su equipo (ClearEquipmentForInventory), lo
// cierra (3.848 rand() si lo deja sin accesos) y vacía las dos grillas del inventario.
void ClearForInventoryReply(const WorldEntryResources& resources);

// 0x4c55a0 con el paquete cifrado: la inicialización común; 0x443d40 y 0x421d60 con el héroe publicado,
// que en MuRet no sueltan nada; y 0x5bc740 por cada entrada, en orden y con las repetidas
// (Scenes::PlaceInventoryItem). La primera que corta deja lo que hicieron las de antes.
[[nodiscard]] Core::Expected<InventoryReplyResult, std::string>
ApplyInventory(const Protocol::Character::InventoryList& list, const WorldEntryResources& resources);
} // namespace Client::Scenes
