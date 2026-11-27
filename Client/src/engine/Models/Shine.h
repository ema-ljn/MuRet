#pragma once

#include "Core/IClock.h"
#include "EngineApi.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// The UVs of a model's glow passes, like 0x4ecc30: from each vertex's world normal, rotated by its bone and without
// the object's scale (SkinnedMesh::normals), and from the frame clock (Client/docs/research/06-render-escenas-y-ui.md,
// "El personaje, vértice a vértice").
namespace Client::Models
{
// The level passes (ItemGlow), with their 0x4ecc30 draw flag: the three the trace shows, with their texture in it,
// and the two of levels 11 and 12 and of a set item, *static*. 0x4000 is requested by the 10 path of 0x6002e0
// (Actors::Drawing::DrawChromeShell), *static*; the other two modes of 06's table (0x8000 and 0x10000) come in when
// something requests them.
enum class ShinePass : std::uint8_t
{
    Shiny,   // 0x8, GL texture 115 (slot 0x5fc): u = 0.5·nz + 0.2, v = 0.5·(ny + 1)
    Chrome,  // 0x4, texture 120 (slot 0x5c0): u = 0.5·nz + t, v = 0.5·ny + 2t
    Chrome4, // 0x1000, texture 141 (slot 0x620): u = c·nx + (s + 0.5)·ny + nz + 3s,
             //                                   v = 1 − c·nx − s·ny − 1.5·nz − 3t
    Chrome2, // 0x200 (slot 0x620): u = (nz + nx) · 0.8 + 2f, v = nx + ny + 3f,
             //                     f = (T mod 5000) · 0.00024 − 0.4
    Chrome3, // 0x800 (slot 0x620): u = L·n, v = 1 − L·n, with L = (0; −0.1; −0.8)
    Chrome5, // 0x4000 (the mesh's slot, unless a texture is forced): u = c·nx + s·ny + nz + 5s + 3ny,
             //                                                       v = 1 − (c·nx + s·ny + nz) − (2.5·nz + t)
};

// A vertex's UVs from its world normal and the frame clock, with t = (T mod 10000) · 0.0001, c = cos(0.001·T) and
// s = sin(0.002·T), T in ms. The original stores T in a float, which from 2²⁴ ms (4.7 hours) no longer counts by one
// (Q-C22): here T is the integer clock, and a negative one counts as 0.
[[nodiscard]] ENGINE_API glm::vec2 ShineTexCoords(ShinePass pass, const glm::vec3& worldNormal,
                                                  Core::Duration frameClock);
} // namespace Client::Models
