#pragma once

#include "Core/Expected.h"

#include <string>

// Los pasos de la UI del comercio y del duelo (CL9 hito 7; 04 § El comercio y § El duelo): la ventana del comercio
// (0x5ecde0, una de las hermanas de 0x5e4ce0) y el pedido de duelo (0x5ebca0, desde 0x636ed6). Lo que mandan sale por
// IGameUiRequests::SendGamePacket, con los dos rand() de cada C3 antes.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
struct GameUiContext;

// 0x5ecde0: con el comercio abierto (su IsOpen saca el rand() del byte protegido), los tres botones del pie, desde la
// esquina 0x78a5a1c/0x78a5a18, de 0x18 × 0x18 en y + 0x186:
// - el zen (x + 0x1a): el diálogo 0x73, los campos de texto vacíos (0x553570(0)), el largo 8, un campo de Tab, el
//   chat cerrado, el modo del zen en 1 y su valor en 0, 0x78abacc en 2 y el sonido 0x19;
// - el OK (x + 0x61), con la espera 0x78abac8 (que baja de a uno) en 0 y sin ítem en el cursor: suena, y con la grilla
//   del otro cambiada y el OK propio apagado, el aviso 0x633300(4, 0x173, 0x94); si no, el OK propio da vuelta,
//   0x78abaa5 en 1 y el C1 04 3C con el OK nuevo, en C3;
// - cancelar (x + 0x89): suena y, sin ítem en el cursor, 0x78abaa5 en 0, el C1 03 3D en C3 y los botones (0x77f3b10
//   en 0 y 0x6c8dcc en 6).
// Cada botón gasta el clic (0x7d366d4).
[[nodiscard]] Core::Expected<void, std::string> StepTheTradeWindow(const GameUiContext& context,
                                                                   UI::PanelRegistry& panels);

// 0x5ebca0: con el pedido de duelo (0x78abad9), el mouse sobre la caja (0xd5..0x1aa × 0x6e..0x118) es de la UI; el
// clic en sí (0xed..0x133) o en no (0x14c..0x192), los dos en y 0xfa..0x10f, manda el C1 10 AC con la respuesta, la
// clave de 0x78abadc y el nombre de 0x78abae0 en C3 y baja el pedido. No gasta el clic ni suena.
[[nodiscard]] Core::Expected<void, std::string> StepTheDuelRequest(const GameUiContext& context);
} // namespace Client::Scenes
