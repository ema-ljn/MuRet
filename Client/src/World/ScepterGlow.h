#pragma once

#include "Render/Models/OriginalRotation.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// El brillo del cetro del Dark Lord del login (CL7.6 B1): el caso 0x620 (HDK_Mace) de 0x509340
// (0x50a169..0x50a398), que pide cinco sprites por frame después de dibujar el ítem. Contrato: 06 § Los
// efectos del login. Verificado contra el código real emulado en 300 casos
// (tests/vectors/world55/scepter.txt).
namespace Client::World
{
class FrameSprites;

inline constexpr std::uint16_t HdkMaceModel = 0x620;
inline constexpr std::size_t ScepterSpriteCount = 5;

// Los cinco, aditivos y con la luz (1; 0,3; 0), en TransformPosition(hueso 0 del ítem, desplazamiento)
// con la escala y el origen del cuerpo: dos Shiny02 (0x5fd) y un flare01 (0x5ac) en el tridente, y dos
// Shiny02 en el pomo, con la posición y la escala de seis rand() del frame. Con la tabla llena no se
// crean, pero los rand() salen igual.
void CreateScepterGlow(FrameSprites& sprites, const Render::Models::OriginalMatrix& bone, float bodyScale,
                       const glm::vec3& bodyOrigin, Render::Models::CrtRandom& random);
} // namespace Client::World
