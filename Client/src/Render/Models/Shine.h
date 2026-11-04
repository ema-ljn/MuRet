#pragma once

#include "Core/IClock.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// Las UV de los pases de brillo de un modelo, como 0x4ecc30: salen de la normal de cada vértice en el
// mundo, girada por su hueso y sin la escala del objeto (SkinnedMesh::normals), y del reloj del frame
// (Client/docs/research/06-render-escenas-y-ui.md, "El personaje, vértice a vértice").
namespace Client::Render::Models
{
// Los pases de los niveles (ItemGlow), con su bandera del dibujo de 0x4ecc30: los tres que muestra
// la traza, con su textura en ella, y los dos de los niveles 11 y 12 y de un ítem de set, *estáticos*.
// Los otros tres modos de la tabla del 06 (0x4000, 0x8000 y 0x10000) entran cuando algo los pida.
enum class ShinePass : std::uint8_t
{
    Shiny,   // 0x8, la textura GL 115 (slot 0x5fc): u = 0,5·nz + 0,2, v = 0,5·(ny + 1)
    Chrome,  // 0x4, la 120 (slot 0x5c0): u = 0,5·nz + t, v = 0,5·ny + 2t
    Chrome4, // 0x1000, la 141 (slot 0x620): u = c·nx + (s + 0,5)·ny + nz + 3s,
             //                              v = 1 − c·nx − s·ny − 1,5·nz − 3t
    Chrome2, // 0x200 (slot 0x620): u = (nz + nx) · 0,8 + 2f, v = nx + ny + 3f,
             //                     f = (T mod 5000) · 0,00024 − 0,4
    Chrome3, // 0x800 (slot 0x620): u = L·n, v = 1 − L·n, con L = (0; −0,1; −0,8)
};

// Las UV de un vértice con su normal en el mundo y el reloj del frame, con t = (T mod 10000) · 0,0001,
// c = cos(0,001·T) y s = sin(0,002·T), T en ms. El original guarda T en un float, que desde 2²⁴ ms
// (4,7 horas) ya no cuenta de a uno (Q-C22): acá T es el reloj entero, y uno negativo cuenta 0.
[[nodiscard]] glm::vec2 ShineTexCoords(ShinePass pass, const glm::vec3& worldNormal, Core::Duration frameClock);
} // namespace Client::Render::Models
