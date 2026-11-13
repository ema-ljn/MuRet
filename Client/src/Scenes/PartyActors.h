#pragma once

#include <cstddef>

// Los miembros de la party en el viewport (CL9 hito 7, fase 2): el +0x1c y el +0x20 de cada lugar de 0x77f3cd0, que
// escriben el picking del frame (0x59e700 y 0x59e3b0) y la lista de la derecha (0x5e4140), y que leen las barras del
// HUD (0x5a89b0) y la lista dibujada (0x5f130a). Contrato: 06 § Lo social en pantalla. *Estático*.
namespace Client::UI
{
class PanelRegistry;
}

namespace Client::Scenes
{
class WorldState;
struct FramePickingContext;

// 0x59e890..0x59e948 (en 0x59e700) y 0x59e439..0x59e4ed (en 0x59e3b0), con el personaje `index`, que el que llama ya
// eligió: el primer lugar de la party (hasta 0x78aba48) con +0x1c en −2 cuyo nombre es el suyo (strncmp con el mayor
// de los dos largos, por lo menos 1: la igualdad de los textos) se queda con `index` y su +0x78; cada lugar en −2 que
// compara pone antes +0x20 en 0.
void MatchThePartyMembers(WorldState& world, std::size_t index);

// 0x59ea9b..0x59eb66 (al final de 0x59e700) y 0x59e50f..0x59e5da (de 0x59e3b0): cada lugar con +0x1c negativo pone
// +0x20 en 0 y +0x1c en −3 si su nombre es el del héroe publicado (0x743a170) y en −1 si no.
void MarkTheMissingMembers(WorldState& world);

// 0x59e3b0, desde 0x59f52a: los 400 primeros de tipo 0x20a, jugadores (+0x88 en 1), vivos, a la vista (+0x16c), con
// el alpha sobre 0 y sin morirse (+0x389), cada uno con MatchThePartyMembers; después, MarkTheMissingMembers. Sin
// rand().
void FindThePartyInTheViewport(WorldState& world);

// 0x5e4140, desde 0x59f48d: con la party cerrada (IsOpen(4)), la lista de la derecha del mundo. El ancho del mundo
// (0x5bb8d0, con sus consultas) menos 50 es su x y cada renglón mide 19 desde y 5. Un lugar con personaje (+0x1c en 0
// o más) ocupa su renglón y, con el Dark Wizard o el Elf (+0x1d4 & 7 en 0 o 2), abre el registro propio para leer la
// skill de su lugar (+0x425; el cierre gasta los 3.848 rand() de 0x40b4d0 si deja la cuenta en 0): con 0xf, 0x10 o 0x1a
// a 0x1c y el mouse en [x − 3, x + 50] × [y, y + 19], el elegido (0x6c8e30) es ese personaje y devuelve true. Uno en −1
// también ocupa su renglón; −2 y −3, no. Todos quedan en −2. La última que acierta gana.
[[nodiscard]] bool PickFromThePartyList(const FramePickingContext& context, UI::PanelRegistry& panels);
} // namespace Client::Scenes
