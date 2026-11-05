#pragma once

#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>

// El billboard del original (CL7.6 B1): 0x621400(textura, posición, ancho, alto, luz, rotación, u, v,
// du, dv), el cuadrado de los sprites y las partículas, en coordenadas de ojo (lo que sigue a 0x6213e0,
// glLoadIdentity en la modelview), como la x87. Verificado contra el código real emulado en 300 casos
// (tests/vectors/world55/billboard.txt). Contrato: 06 § Los efectos del login.
namespace Client::Render
{
struct BillboardCorner
{
    glm::vec3 position{}; // en ojo
    glm::vec2 uv{};
};

struct Billboard
{
    // En el orden de GL_QUADS: abajo a la izquierda, abajo a la derecha, arriba a la derecha y arriba a
    // la izquierda, con la fila 0 de la textura arriba.
    std::array<BillboardCorner, 4> corners{};
    Color color;
};

// La textura `0x5e4`: con 4 componentes va con alpha 1, no con el rojo.
inline constexpr std::uint16_t OpaqueAlphaBillboardSlot = 0x5e4;

// La vista de la cámara como la guarda 0x620370 en 0x7d366f0: la modelview de 3 × 4, por filas.
[[nodiscard]] Models::OriginalMatrix ViewRows(const glm::mat4& view);

// 0x621400: `position` pasa por `view` (0x5fccc0); el cuadrado de `width` × `height` va en el plano de
// la pantalla, girado `rotation` grados alrededor del eje de la vista (0x5fc8f0 y 0x5fcbd0 con (0, 0,
// rotación)), con las UV de (u, v) a (u + du, v + dv). El color: con una textura de 3 componentes,
// `light` (glColor3fv); con 4, (r, g, b, r), salvo la 0x5e4, con alpha 1.
[[nodiscard]] Billboard MakeBillboard(const Models::OriginalMatrix& view, const glm::vec3& position, float width,
                                      float height, const glm::vec3& light, float rotation, glm::vec2 uv,
                                      glm::vec2 uvSize, std::uint16_t slot, int components);
} // namespace Client::Render
