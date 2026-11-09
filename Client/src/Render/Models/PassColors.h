#pragma once

#include "Core/IClock.h"
#include "Render/Models/Shine.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

// El color de cada pase de brillo de una parte o de lo que cuelga, como lo pone 0x60b3d0 antes de
// cada RenderBody (Client/docs/research/06-render-escenas-y-ui.md, "Los pases de brillo"). Las tres
// tablas salen de emular 0x608e00, 0x6093d0 y 0x609560 sobre el main.exe de c32bbd5f para cada
// modelo de la tabla, con sus argumentos en 1.
namespace Client::Render::Models
{
// El de los pases 0x44 y 0x48 (0x60b130 → 0x608e00, con el alpha del objeto en 1): uno por ítem, que
// no mira la luz. Casi todos (1; 0,5; 0); las Male17, (0,75; 0,65; 0,5).
[[nodiscard]] Color ShineColorOf(std::uint16_t model);

// Por qué multiplica 0x6093d0 (desde 0x60b300) el color base en los pases 0x240 y 0x1040: casi todos
// por 1, y el pase queda con la luz por el factor del nivel.
[[nodiscard]] glm::vec3 MetalFactorOf(std::uint16_t model);

// El del pase de set, 0x840 (0x60b300 → 0x609560): (0,1; 0,6; 1), salvo 15 partes (1; 0,7; 0,2).
[[nodiscard]] Color SetColorOf(std::uint16_t model);

// El del pase 0x42 del excelente: k = (sin(0,002 · T) + 1) · 0,5 y (k, 0,3k, 1 − k), con T el reloj
// del mundo en ms (DAT_05767b88).
[[nodiscard]] Color ExcellentColorAt(Core::Duration clock);

// El alpha con que 0x60b3d0 llama al pase de set: sin(0,001 · T) · 0,5 + 0,4. Como es menor que 0,99,
// 0x4ecc30 multiplica por él, en el lugar, el color del modelo en cada malla que dibuja ese pase: la
// primera va por a, la segunda por a² (Q-C95).
[[nodiscard]] float SetAlphaAt(Core::Duration clock);

// La malla que 0x60b130 (los pases 0x48 y 0x44) y 0x60b300 (el 0x240, el 0x1040 y el de set) le
// pasan a RenderBody como la oculta: la 1 de 0xc17 y 0x226 y la 0 de 0x227 en todos; y en los de
// 0x60b130, la 2 de 0x619 y la del objeto (`objectHidden`, su +0x58) con 0x231, 0x81c, 0xe22 y
// 0x61a..0x61f. Las demás, ninguna (−1).
[[nodiscard]] std::optional<std::size_t> PassHiddenMesh(std::uint16_t model, ShinePass pass, std::int32_t objectHidden);
} // namespace Client::Render::Models
