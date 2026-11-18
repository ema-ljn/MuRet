#pragma once

#include "Core/Expected.h"
#include "Scenes/WorldRuntime.h"

#include <deque>
#include <string>

// Las barras de vida sobre el 0x1a8 (CL9 hito 2): el paso de los personajes las encola (0x42a2b0, desde 0x5053fc) y el
// HUD las dibuja (0x42a4d0, el 5 de 0x5a7320, en 0x5a7344). Contrato: Client/docs/research/06-render-escenas-y-ui.md,
// "Las barras de vida del 0x1a8".
namespace Client::Render
{
class SpriteBatch;
}

namespace Client::Scenes
{
struct ActorState;
struct GameProjection;

// 0x42a2b0(o): con el tipo 0x1a8 (el 278 de 0x42d4a1) y +0x93 debajo de 5 sin signo, encola (push_back) la posición
// con la z en +0x18 + +0x138 + 100 (0x6b86b4), en la x87 y con el FSTP en float, y +0x93. Con otro, nada.
void QueueTheHealthBar(const ActorState& actor, std::deque<QueuedHealthBar>& queue);

// 0x42a4d0: para i desde 0 mientras i sea menor que lo que queda en la cola, el primero: 0x6205b0 con su punto, la
// barra de 0x5a6af0(x − 25, y + 12, 50, 2, b · 10, 0, 1), en float desde el entero, y pop_front. Como la cuenta baja
// con cada uno, dibuja la mitad redondeada para arriba; el resto queda para el frame siguiente, delante de los
// nuevos. Sin `projection`, una cola con algo corta.
[[nodiscard]] Core::Expected<void, std::string>
DrawTheHealthBars(std::deque<QueuedHealthBar>& queue, const GameProjection* projection, Render::SpriteBatch& sprites);
} // namespace Client::Scenes
