#pragma once

#include "Render/Pass3D.h"

#include <glm/ext/vector_float3.hpp>

// El pase del mundo de las escenas sin terreno todavía y sus cámaras fijas (06 § Cámara, CL6.R).
// Las rutas de 0x633920 corren en la lógica y escriben otra posición, otros ángulos y un fov de 45,
// pero 0x635ae0 las pisa antes de que lleguen a 0x620db0: lo que dibuja es esto.
namespace Client::Scenes
{
// 0x620db0(0, 25, 640, 430), en 0x6350b0 (el login) y en 0x634680 (la selección).
inline constexpr Render::ViewportRect WorldViewport{0, 25, 640, 430};

// 0x635ae0 en la escena 4: fov 35 (0x6d5d48), near 20 (0x6d5d40), el far de 0x6d5d44 con
// DAT_07d3a0ec en 0 (11.519,9), x −84,5° (0x6ba7bc más DAT_07d325c8, en 0), z −45° y la posición
// (13.909; 14.482; 543). Verificada contra la traza (06 § Cámara).
[[nodiscard]] Render::Camera3D SelectionCamera();

// 0x635ae0 en la escena 2: lo mismo con el far 20.400, x −96,5° (0x6ba7c0 más DAT_07d325c8), z −36° y
// la posición (12.778; 12.678; 579). *Estático*: no hay traza del login (P-C13).
[[nodiscard]] Render::Camera3D LoginCamera();

// El héroe del login (DAT_0743a170, el personaje 0): 0x6350b0 le escribe esta posición en cada frame
// (0x6350d3..0x6350e1). Es el centro del trapecio del terreno (0x5fb570) y de las brasas (CL7.6 B).
inline constexpr glm::vec3 LoginHeroPosition{11679.0f, 14191.0f, 333.0f};
} // namespace Client::Scenes
