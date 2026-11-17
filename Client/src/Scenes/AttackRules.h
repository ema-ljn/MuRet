#pragma once

#include <cstdint>

// CL9 hito 3: los globales de 0x558bb0 que escriben otros paquetes (la guerra de gremios, del hito 8). Sin esos
// paquetes, los valores de la .bss: todo en 0. El duelo (0x78abad8, 0x78abadc y 0x78abae0) es SocialState::duel, el
// que escriben los paquetes del hito 7 (08 P-C206 b).
namespace Client::Scenes
{
struct AttackRules
{
    // 0x576796c: con algo distinto de 0, la guerra de gremios: un monstruo se ataca sólo con 0x576796d (0x558c07), y un
    // jugador, si es del gremio rival (+0x374 en 2, 0x558e61) o, con el PK de 6 o más, si su gremio no es el del héroe
    // (0x558d8e). Qué son exactamente, sin leer: los escribe el hito 8.
    std::uint8_t byte576796c = 0;
    std::uint8_t byte576796d = 0;
};
} // namespace Client::Scenes
