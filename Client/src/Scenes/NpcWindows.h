#pragma once

#include "Core/Expected.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/NpcWindowState.h"
#include "UI/PanelRegistry.h"

#include <cstdint>
#include <functional>
#include <string>

// Las ventanas de los NPC del lado de la escena (CL9 hito 6): lo que abrir y cerrar el almacén, la caja del caos y la
// tienda hace fuera de los paneles (0x6599a0 y 0x657ab0), los paquetes que llegan (el dispatcher 0x4e2b10, 0x4d1e00,
// 0x4c5f60 y 0x4da280) y lo que mandan las ventanas. Contrato: Client/docs/research/04-protocolo.md, "Las ventanas de
// los NPC".
namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::Scenes
{
class GamePanels;
class WorldState;
struct WorldEntryResources;

// El envío de un paquete de las ventanas por la sesión (WorldEntryResources::sendNpcWindowPacket). Los rand() del C3
// los saca quien lo arma, antes.
using NpcWindowSender = std::function<Core::Expected<void, std::string>(Protocol::Outgoing)>;

// Lo que los cables de 0x6599a0 y 0x657ab0 leen: el mundo, el rand() y, para 0x5be150 y 0x555190, los servicios del
// inventario.
struct NpcWindowContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const InventoryServices& services;
    const NpcWindowSender& send;
};

// UI::PanelContext::npcWindowOpened. 0x40: 0x6c932f y 0x78abb20 en 0 (0x659f8d..0x659f94). 0x100: 0x7d2dc9d en 0, el
// C1 04 BD 09 con el XOR32 en claro (0x65a94f..0x65ac07) y 0x7d2dc80 en 0 (0x65ac28).
[[nodiscard]] Core::Expected<void, std::string> NpcWindowOpened(const NpcWindowContext& context, UI::PanelId id);

// UI::PanelContext::npcWindowClosing. 0x40 (0x65852b..0x65871d): con la traba del 24 (0x78abb39), false; si no, la
// grilla de 0x78a7070 vacía, 0x5be150 con un ítem en el cursor, el C1 03 82 en claro y 0x6c932f en 0. 0x100
// (0x658741..0x658961): con algo en la caja (0x5e3c20) o en el cursor, el texto 593 de error y false; si no, el C1 03
// 87 en claro y la grilla de 0x7d2d380 vacía. 0x10000 (0x659063..): el C1 03 31 en claro y la grilla de 0x78a7070
// vacía; el byte protegido ya lo bajó el registro.
[[nodiscard]] Core::Expected<bool, std::string> NpcWindowClosing(const NpcWindowContext& context, UI::PanelId id);

// 0x4d28a9..0x4d28f6: adónde manda 0x4d1e00 el cursor con SetCursorPos, desde el mouse en 640 × 480 (0x7d3682c y
// 0x7d36828) y el tamaño de la pantalla (0x6d5d60 y 0x6d5d64): x = (260 · mx / 640, con signo) · ancho / 640 e
// y = alto · my / 480, sin signo.
[[nodiscard]] CursorWarp NpcTalkCursorWarp(std::int32_t mouseX, std::int32_t mouseY, std::int32_t screenWidth,
                                           std::int32_t screenHeight);

// El 30 cifrado (0x4d250c..0x4d28f6): 0x657a90 (cerrar todo), lo que abre el NPC según pkt[3] (la tabla de 0x4d2914),
// los dos sonidos (0x19 y 0x1c, CL10) y el pedido del cursor (NpcWindowState::cursorWarpRequested). Con 3, las seis
// tasas de pkt[4..9]; las que el paquete no trae quedan en 0 (Q-C920).
[[nodiscard]] Core::Expected<void, std::string> ApplyTalkResult(const Protocol::NpcWindows::TalkResult& talk,
                                                                const WorldEntryResources& resources);

// El 31 (0x4c5f60): con la grilla 3, 0x7d2dc80 en 2, los sonidos 0x57 y 0x44, la caja del caos vacía y cada entrada
// con 0x5bc740(0x7d2d380, 8, 4, lugar, datos, 1); con la 5, la caja del entrenador (corta); con otra, la grilla de
// 0x78a7070 vacía y cada entrada con 0x5bc150.
[[nodiscard]] Core::Expected<void, std::string> ApplyItemList(const Protocol::NpcWindows::ItemList& list,
                                                              const WorldEntryResources& resources);

// El 32 (0x4e3c2a..0x4e3c58): con un lugar, 0x5bc740(0x78a9bb0, 8, 8, lugar, datos, 0) y el sonido 0x1d; siempre,
// 0x5767950 en 0.
[[nodiscard]] Core::Expected<void, std::string> ApplyBuyResult(const Protocol::NpcWindows::BuyResult& result,
                                                               const WorldEntryResources& resources);

// El 33 (0x4e3c6e..0x4e3cc6): con 0, 0x5be150; si no, el cursor vacío y el zen nuevo en +0xecc del registro propio,
// abierto (0x4e79a0 y 0x40f370), y el sonido 0x1d.
[[nodiscard]] Core::Expected<void, std::string> ApplySellResult(const Protocol::NpcWindows::SellResult& result,
                                                                const WorldEntryResources& resources);

// El 34 (0x4e3cd7..0x4e3d24): con zen, en +0xecc del registro propio abierto, 0x550d10 y el sonido 0x25.
[[nodiscard]] Core::Expected<void, std::string> ApplyRepairResult(const Protocol::NpcWindows::RepairResult& result,
                                                                  const WorldEntryResources& resources);

// El 81 (0x4e4692..0x4e46ce): con pkt[3] distinto de 0, el zen del almacén (+0xed0) y el del inventario (+0xecc) con
// el registro propio abierto.
[[nodiscard]] Core::Expected<void, std::string> ApplyWarehouseMoney(const Protocol::NpcWindows::WarehouseMoney& money,
                                                                    const WorldEntryResources& resources);

// El 82 y el 87 (0x4e4819..0x4e485d): el cursor vacío y, con el diálogo 0x73, su cierre y la entrada del chat.
[[nodiscard]] Core::Expected<void, std::string> ApplyNpcWindowClosed(const Protocol::NpcWindows::NpcWindowClosed&,
                                                                     const WorldEntryResources& resources);

// El 83 (0x4e46df..0x4e47f2, las tablas de 0x4e60c0 y 0x4e60dc): 0 y 1 la traba del almacén, 0xa, 0xb y 0xd sus
// diálogos, 0xc lo que esperaba la contraseña.
[[nodiscard]] Core::Expected<void, std::string> ApplyWarehouseState(const Protocol::NpcWindows::WarehouseState& state,
                                                                    const WorldEntryResources& resources);

// El 86 (0x4da280, las tablas de 0x4da38c y 0x4da3a8): el resultado de la mezcla.
[[nodiscard]] Core::Expected<void, std::string> ApplyChaosMixResult(const Protocol::NpcWindows::ChaosMixResult& result,
                                                                    const WorldEntryResources& resources);

// 0x5c4472..0x5c4a89, desde 0x5c4110 con el modo de reparar: el clic consumido, los dos rand() del C3 y el C1 05 34
// con el lugar (fila · columnas + columna + 12) y 0x78abb10.
[[nodiscard]] Core::Expected<void, std::string> SendRepair(Render::Models::CrtRandom& random,
                                                           const NpcWindowSender& send, std::uint8_t position,
                                                           std::uint8_t requestPosition);
// Los cables de 0x6599a0 y 0x657ab0 de `resources` en `panels`: el mundo, el rand() de la tela, los servicios del
// inventario y el envío.
void LendTheNpcWindows(GamePanels& panels, const WorldEntryResources& resources);
} // namespace Client::Scenes
