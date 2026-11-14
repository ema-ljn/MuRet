#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <optional>
#include <string>

// Las banderas de efectos de otro personaje (+0x78 y +0x7c) que traen el 0x12 y el 0x45: 0x4e6380 y 0x42ab40.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". *Estático*.
namespace Client::Scenes
{
struct ActorState;

// 0x4e6380(banderas, personaje), bit por bit en el orden del original: los que solo prenden su bit en +0x78
// (1, 2, 4, 8, 0x80, 0x200, 0x4000, 0x8000 y 0x1000000), 0x800000 y 0x400000 (que apagan antes el 0x40000),
// 0x10000000 en +0x7c, y 0x10000, 0x20000, 0x40000 (que apaga antes el primero de 0x80000, 0x400000 y 0x800000),
// 0x80000 (que apaga el 0x40000) y 0x100000, que con los tipos 0x204 a 0x208 pisan +0x7c entero en vez de prender su
// bit. Los que crean efectos cortan con su dirección, con lo de antes ya hecho: 0x100 en un 0x20a sin el bit (el
// sonido 0x84 queda en `lastSound`; 0x534f90 y los cinco joints 0x10f son de la CL11), 0x1000 (con su bit prendido;
// los tres joints 0x10f), 0x400 y 0x10 (0x518ac0 y el efecto 0x5ac/2 o /1), 0x20 y 0x40 sin su bit (0xc1/1 y /2;
// 0x628/0 y el sonido 0x85) y 0x2000000 en el héroe publicado (0x555190 con los textos de 0x5767b6c y 0x774300c,
// sin leer). Sin el bit, cada uno de esos no hace nada.
[[nodiscard]] Core::Expected<void, std::string>
ApplyEffectFlags(ActorState& actor, std::uint32_t flags, bool publishedHero, std::optional<std::int32_t>& lastSound);

// 0x42ab40(personaje, banderas): solo en el mapa 30. Con 0x80000, 0x400000 u 0x800000 (el primero que esté, en ese
// orden), +0x360 en 6, 7 u 8, soltando antes la capa (0x414340) si estaba en 0xc; si no, con 0x40000, +0x360 en 0xc,
// soltándola antes si estaba en 6, 7 u 8; con ninguno, suelta la capa si +0x360 es 6, 7, 8 o 0xc. Lo que devuelve no
// lo lee nadie (0x4c9250, 0x4c9396).
void ApplyCastleSiegeFlags(ActorState& actor, std::uint32_t flags, std::int32_t map);
} // namespace Client::Scenes
