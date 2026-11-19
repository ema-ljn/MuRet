#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// Las banderas de efectos de otro personaje (+0x78 y +0x7c) que traen el 0x12 y el 0x45: 0x4e6380 y 0x42ab40.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". *Estático*.
namespace Client::Scenes
{
struct ActorState;
struct WorldEntryResources;

// 0x4e6380(banderas, personaje), bit por bit en el orden del original: los que solo prenden su bit en +0x78
// (1, 2, 4, 8, 0x80, 0x200, 0x4000, 0x8000 y 0x1000000), 0x800000 y 0x400000 (que apagan antes el 0x40000),
// 0x10000000 en +0x7c, y 0x10000, 0x20000, 0x40000 (que apaga antes el primero de 0x80000, 0x400000 y 0x800000),
// 0x80000 (que apaga el 0x40000) y 0x100000, que con los tipos 0x204 a 0x208 pisan +0x7c entero en vez de prender su
// bit. 0x400 y 0x10 apagan el 0x5ac/2 o /1 de `actor` en `effects` (0x518ac0) y crean otro con él de dueño
// (0x518cf0, `CreateSkillFlare`; con la tabla llena, ninguno), siempre, y prenden su bit (CL9 hito 4; en el hito 1
// cortaban). 0x20 sin su bit apaga los 0xc1/1 del personaje (0x518ac0) y crea el 0xc1/1 y el /2 con él de dueño
// (World::CreateWizardModelEffect, que pisa su luz +0xf4). 0x40 sin su bit apaga el 0x628/0 de `actor`, crea otro con
// él de dueño (PutTheSkullOn) y pide el sonido 0x85 en él. 0x100 en un 0x20a sin el bit (el sonido 0x84 queda en
// `lastSound`) apaga sus 0x10f/0 (0x534f90) y crea los cinco joints 0x10f que giran a su alrededor; 0x1000 prende su
// bit y crea los tres 0x10f/8 (los dos, 0x530860 de AuraJoints).
// Con 0x2000000 y el héroe publicado, 0x555190 añade Text[1828] con nombre vacío y tipo 1 antes de prender el bit,
// incluso si ya estaba puesto. Necesita Text.bmd y el medidor; sin el bit no los consulta.
[[nodiscard]] Core::Expected<void, std::string>
ApplyEffectFlags(ActorState& actor, std::uint32_t flags, bool publishedHero, const WorldEntryResources& resources);

// Los dos sitios de 0x518cf0 de un hielo (para el error).
struct IceCalls
{
    std::uint32_t first = 0;
    std::uint32_t second = 0;
};

// El hielo de un personaje: 0x518ac0(0xc1, personaje, 1) y el 0xc1/1 y el /2 (z + 180) con él de dueño, su posición,
// sus ángulos y su luz +0xf4, que cada alta pisa (World::CreateWizardModelEffect). Lo usan la bandera 0x20 (0x4e67bc)
// y la respuesta de la 0x33 (0x4cd6bc). Con la tabla llena, nada y sin rand().
[[nodiscard]] Core::Expected<void, std::string> WrapInIce(ActorState& actor, const WorldEntryResources& resources,
                                                          const IceCalls& calls);

// 0x42ab40(personaje, banderas): solo en el mapa 30. Con 0x80000, 0x400000 u 0x800000 (el primero que esté, en ese
// orden), +0x360 en 6, 7 u 8, soltando antes la capa (0x414340) si estaba en 0xc; si no, con 0x40000, +0x360 en 0xc,
// soltándola antes si estaba en 6, 7 u 8; con ninguno, suelta la capa si +0x360 es 6, 7, 8 o 0xc. Lo que devuelve no
// lo lee nadie (0x4c9250, 0x4c9396).
void ApplyCastleSiegeFlags(ActorState& actor, std::uint32_t flags, std::int32_t map);
} // namespace Client::Scenes
