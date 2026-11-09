#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// Un registro del pool durable de personajes, de 0x438 bytes en el original (06, "Los personajes de
// la lista"). Es una proyección con lo que MuRet lee hoy, no el layout. Arranca en cero, como lo deja
// el REP STOSD de WinMain (0x4bafa6..0x4bafb3).
namespace Client::Scenes
{
struct ActorState
{
    // +0x10, +0x14 y +0x18. 0x511290 calcula la Z con XY 50/50 y el mundo vigente, y después
    // 0x517270 escribe la X y la Y del lugar (CL7.3 B).
    glm::vec3 position{};
    // Si el word318 (+0x318) es 0x1c15. Es lo único que se lee de él: el próximo 0x511290 suma el
    // corrimiento de la mascota con el word318 anterior. El login lo deja en 0x1c16 en el lugar 2.
    bool previousHad1c15 = false;
    // Propio de MuRet: cuántas veces lo construyó 0x511290. La vista rearma lo visual con cada una,
    // aunque los bytes sean los mismos (06, "Cada F3/00 aceptado y alta F3/01 exitosa reconstruye").
    std::uint64_t revision = 0;
};
} // namespace Client::Scenes
