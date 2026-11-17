#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/HeroSkills.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// CL9 hito 4, fase 2c: lo que las ramas de las clases de 0x57cfc0 y 0x55cbc0 hacen en línea, sin dirección propia, al
// mandar (06, "Usar una skill"). Todo con el contexto de UseTheHeroSkill.
namespace Client::Scenes
{
class WorldState;
struct ActorState;

// Las cadenas "webzen" (0x6c91ac..0x6c9254 en 0x57cfc0 y 0x6c8fb0..0x6c8fc0 en 0x55cbc0): el strstr en línea sobre el
// nombre del héroe publicado (+0x1d9, con el strlen que sigue a +0x1e3). Con el nombre, la rama no manda y sigue con lo
// de después del envío.
[[nodiscard]] bool HasTheSilentName(const WorldState& world);

// El D3 de girar en línea (0x55d4ef.., como el SendMove de un paso): C1 06 D3 X Y (rumbo << 4), con el XOR32 y sin C3
// (SendTheSkillPacket, sin rand()), el rumbo de `angle` como DirectionFromAngle. Un ángulo que no se puede escribir
// corta con `where`.
[[nodiscard]] Core::Expected<void, std::string> SendTheTurn(std::uint8_t x, std::uint8_t y, float angle,
                                                            const HeroSkillContext& context, std::string_view where);

// Lo que leen 0x4f5290, 0x4f3f80 y 0x4ea030 desde las ramas: el mundo, el rand(), Player.bmd e Item.bmd (sin la
// biblioteca de modelos, como el contexto de 0x5650c0 desde MoveHero).
[[nodiscard]] ActorCombatContext CombatContextOf(const HeroSkillContext& context);

// El lugar de `actor` en el pool (0x743a16c), o el tamaño del pool si no es uno de sus personajes (0x558eb0 lo compara
// con el héroe publicado).
[[nodiscard]] std::size_t SlotOf(const WorldState& world, const ActorState& actor);

// La celda lógica del personaje (+0x41c o +0x420) abierta, leída y cerrada: 4 rand() si su cuenta vuelve a 0 (0x40b570
// y 0x412890 o 0x40b800, 0x412810 y 0x4e7e40 en línea).
[[nodiscard]] std::uint8_t ReadTheSkillCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random);
} // namespace Client::Scenes
