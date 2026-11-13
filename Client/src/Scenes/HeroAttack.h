#pragma once

#include "Core/Expected.h"
#include "Protocol/Movement.h"
#include "Scenes/ActorCombat.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

// CL9 hito 3: pegar. Si el personaje elegido se puede atacar (0x558bb0), el freno de 300 ms entre acciones
// (0x576791c) y lo que hace el héroe al llegar al final del camino (0x5650c0), con el ataque del caso 3.
namespace Client::Scenes
{
class WorldState;
struct ActorState;

// 0x558bb0: si el personaje elegido (0x6c8e30, selectedTargets[2]) se puede atacar. Falso con un ítem en el cursor
// ([0x7879fbc] > 0, con signo) o sin elegido; verdadero en Chaos Castle (18 a 23) con otro que el héroe. Un monstruo
// (+0x88 en 2), salvo en la guerra de gremios; un jugador (+0x88 en 1) por el asedio (0x4294b0), +0x370, la guerra
// de gremios, el duelo, el PK de 6 o más o, si no, con Ctrl abajo (GetAsyncKeyState(0x11) en 0x558e7f, `controlDown`)
// y que no sea el héroe. Corta donde compara los nombres de gremio (0x787aa55, del hito 8).
[[nodiscard]] Core::Expected<bool, std::string> IsAttackable(const WorldState& world, bool controlDown);

// El freno de 300 ms (0x57f2d1..0x57f2f9, y el mismo en 0x559890, 0x55b6f0, 0x55cbc0 y 0x55faa0): salvo con la skill
// 0x28, si |GetTickCount() − [0x576791c]| (resta de 32 bits con signo, CDQ/XOR/SUB) es 300 o menos, no pasa; si
// pasa, anota en `last` un GetTickCount nuevo (el original lo llama dos veces).
[[nodiscard]] bool PassesTheActionBrake(std::uint32_t& last, std::int32_t skill,
                                        const std::function<std::uint32_t()>& tickCount);

// Lo que 0x5650c0 necesita además del mundo: los envíos del D3 de girar (C1 06 D3 X Y rumbo, como el SendMove de un
// paso) y del D7 de pegar. Vacíos, no manda nada (el original los manda por el socket, que puede estar cerrado).
struct HeroArrivalContext
{
    ActorCombatContext combat;
    std::function<Core::Expected<void, std::string>(std::span<const Protocol::Movement::Cell>, Protocol::Movement::Cell,
                                                    float)>
        sendWalk{};
    std::function<Core::Expected<void, std::string>(std::uint16_t key, std::uint8_t action, std::uint8_t direction)>
        sendAttack{};
};

// 0x5650c0(héroe, héroe, 0) al llegar (0x59b2d2): por +0x379 (MOVSX, del 1 al 5; con otro, nada). El 3 pega
// (0x565107..0x56601e): el alcance por las armas del registro propio, el blanco de 0x6c8ebc, la distancia a su celda
// y, en alcance, 0x4f4a10, el giro (D3) y el D7. Los demás casos son de otros hitos y cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> InteractOnArrival(ActorState& hero, const HeroArrivalContext& context);
} // namespace Client::Scenes
