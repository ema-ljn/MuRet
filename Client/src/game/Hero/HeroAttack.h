#pragma once

#include "Core/Expected.h"
#include "Tables/SkillTable.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Combat/ActorCombat.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Hero
{

// Lo que 0x5650c0 necesita además del mundo: los intentos del D3 de girar (C1 06 D3 X Y rumbo, como el SendMove de un
// paso) y del D7 de pegar. Son best-effort como el socket original: que el transporte esté cerrado no corta la llegada.
struct HeroArrivalContext
{
    Combat::ActorCombatContext combat;
    std::function<void(std::span<const Protocol::Movement::Cell>, Protocol::Movement::Cell, float)> sendWalk{};
    std::function<void(std::uint16_t key, std::uint8_t action, std::uint8_t direction)> sendAttack{};
    // CL9 hito 4, fase 2c: lo que piden las skills del caso 5 (0x56601f), como en HeroSkillContext: Skill.bmd
    // (0x5526f0; nullptr corta), el GetTickCount del freno de 300 ms (vacío: el envío que lo mira corta), los servicios
    // de 0x561c50, Ctrl (0x558eb0) y lo que mandan por send() (los C3 con sus dos rand() ya sacados; vacío, no sale).
    const Tables::SkillTable* skills = nullptr;
    std::function<std::uint32_t()> tickCount{};
    const Items::InventoryServices* inventory = nullptr;
    bool controlDown = false;
    std::function<void(Protocol::Outgoing)> sendSkill{};
};

// 0x5650c0(héroe, héroe, 0) al llegar (0x59b2d2): por +0x379 (MOVSX, del 1 al 5; con otro, nada). El 3 pega
// (0x565107..0x56601e): el alcance por las armas del registro propio, el blanco de 0x6c8ebc, la distancia a su celda
// y, en alcance, 0x4f4a10, el giro (D3) y el D7. El 5 (0x56601f..0x566754) usa la skill del arma pendiente
// (0x7758580..0x7758588): la skill (el lugar del registro propio con 0x7758580, o 0x7758584 tal cual) y su alcance
// (0x5526f0); por las tablas 0x56ab4c y 0x56ab34, con el blanco vivo (+0x389 en 0) y su celda en 0x77e1ba8/0x77e1bac:
// - 1 a 4, 7, 0xb, 0x11, 0x2d y 0x49: en alcance (· 100) y fuera de la zona segura, con la línea de vista (0x558800),
//   0x55cbc0 (0x5663b2); sin ella, nada.
// - 0x13 a 0x17, 0x2b, 0x2c, 0x31, 0x39, 0x3c, 0x3d y 0x4a, y la 0x2f con el héroe publicado en Uniria: en alcance
//   (· [0x6ba448], 120,0000076) y fuera de la zona segura, 0x55b6f0 (0x5661f4), sin mirar la línea de vista.
// - 0x2e y 0x33 (0x56654a) y 0x1a a 0x1c (0x5666db): como las primeras, con 0x55faa0.
// Fuera de alcance o en la zona segura, el camino (0x4eac00) hasta el alcance (· 1,2 en el segundo grupo) y, con él,
// +0x378 en 1; +0x379 sigue en 5. Las demás skills, nada. Los casos 1, 2 y 4 son de otros hitos y cortan con su
// dirección.
[[nodiscard]] Core::Expected<void, std::string> InteractOnArrival(Kernel::ActorState& hero,
                                                                  const HeroArrivalContext& context);
} // namespace Client::Hero
