#pragma once

#include "Core/Expected.h"
#include "Scenes/WorldRuntime.h"

#include <array>
#include <cstdint>
#include <string>

namespace Client::Protocol::Pet
{
struct PetInfoReply;
}

namespace Client::Render::Models
{
class CrtRandom;
}

// Lo que llega con el A9 de las mascotas (CL9 hito 4): la tabla de 0x723960 (WorldRuntime::pets), el cuervo del héroe
// y el caballo del registro propio. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "El A9 de las mascotas".
// Cada acceso al registro propio gasta sus 3.848 rand() al cerrarlo en 0.
namespace Client::Scenes
{
class WorldState;
struct DarkSpirit;
struct WorldEntryResources;

// 0x420cd0, desde 0x657670 en la vuelta a la selección: 0x723910 (la pestaña), 0x723911 (UI::DelegatedPanels::pet)
// y los quince dwords de las tres mascotas en 0. Sin rand().
void ResetThePets(WorldRuntime& runtime);

// 0x4221d0(tipo): con el registro propio abierto y cerrado, el liderazgo (+0x36 + +0x20) y, para el caballo, la fuerza
// (+0x18) y +0x2a; después, con el tipo 0 o 1, la experiencia que sigue y +0xc..+0x12 de esa mascota. Con otro tipo,
// solo el acceso.
void UpdatePetStats(std::uint8_t pet, WorldState& world, Render::Models::CrtRandom& random);
// Lo mismo sobre `pets` (WorldRuntime::pets o una copia) y con la cuenta de accesos del registro propio dada: el
// tooltip de las mascotas (0x422a35) lo llama con el registro abierto alrededor (0x5f7bc0), o con las copias de un
// dibujo intermedio.
void UpdatePetStats(std::uint8_t pet, const WorldState& world, std::array<PetStats, PetStatsCount>& pets,
                    std::uint8_t& ownRecordDepth, Render::Models::CrtRandom& random);

// 0x417f30(nivel, experiencia), la vtable + 8 del cuervo: dos accesos al registro propio (el liderazgo y la
// durabilidad del lugar 1, +0xbce) y lo que sale de ellos en el cuervo (DarkSpirit::word1e2 a word45a).
void ApplyRavenInfo(DarkSpirit& raven, std::uint8_t level, std::uint32_t experience, WorldState& world,
                    Render::Models::CrtRandom& random);

// 0x4e4ca1: 0x421db0(tipo, clase, casilla, nivel, experiencia) y, con el registro propio abierto, 0x550d10. Con la
// ventana de la mascota abierta (0x420cc0), el cuervo del héroe y la tabla con la durabilidad del caballo; si no, la
// tabla con la del ítem del cartel (0x787a518) salvo con el entrenador abierto y la casilla debajo de 12, y con la
// clase 0, 0xfe o 0xff, el cuervo del héroe (la casilla 1) o el caballo (la casilla 8: su nivel, su +8 en el héroe y su
// opción, 0x4225d0). Un tipo de 3 o más que el original escribe fuera de 0x723960 se descarta entero (Q-C1208); sin
// escribir la tabla (la ventana cerrada sin ítem en el cartel, o con el entrenador), solo el acceso de 0x4221d0. Corta
// con la dirección del original: los joints del cuervo con 0xfe y 0xff (0x418390 y 0x418400, CL11) y lo que corta
// 0x550d10.
[[nodiscard]] Core::Expected<void, std::string> ApplyPetInfo(const Protocol::Pet::PetInfoReply& reply,
                                                             const WorldEntryResources& resources);
} // namespace Client::Scenes
