#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Pass3D.h"

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>

#include <cstdint>
#include <string>

// Caché de6203c0 y consumo6205b0. La proyección usa los escalares guardados, no multiplica una
// matriz GLM ni redondea después de agregar el centro. Contrato estático P-C55(f), informe local.
namespace Client::Scenes
{
struct GameProjection
{
    Render::Models::OriginalMatrix view{};
    float horizontalScale = 0;
    float verticalScale = 0;
    std::int32_t centerX = 0;
    std::int32_t centerY = 0;
    std::int32_t screenWidth = 0;
    std::int32_t screenHeight = 0;
};

// Preparación matemática de los dos stores binary32 de6203c0. tan de la biblioteca no acredita
// equivalencia de FPTAN para todos los argumentos; los escalares se pueden prestar explícitamente.
[[nodiscard]] Core::Expected<GameProjection, std::string> MakeGameProjection(const Render::Pass3D& pass,
                                                                             float fieldOfView);
[[nodiscard]] Core::Expected<glm::ivec2, std::string> ProjectGamePoint(const GameProjection& projection,
                                                                       const glm::vec3& point);
} // namespace Client::Scenes
