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

// 0x620480(x, y, salida, 0): lo que deja la vuelta atrás de un punto de 640 × 480.
struct UnprojectedPoint
{
    // 0x7d36834: la cámara, la traslación de 0x7d366f0 cambiada de signo por la transpuesta de su giro (0x5fcc30).
    glm::vec3 camera{};
    // La salida: la cámara más el punto a 2.000 de la cámara, también por la transpuesta del giro.
    glm::vec3 point{};
};

// 0x620480 con el último argumento en 0: (x · W) / 640 y (y · H) / 480 sin signo (W y H, la pantalla), menos el centro
// del caché; cada uno por su escala y por ±2.000 (0x6ba148 y 0x6ba768) en la x87, con el fstp en float, y −2.000 en la
// z. El far del caché (0x6d5d44) es del último argumento en 1, que MuRet no usa.
[[nodiscard]] UnprojectedPoint UnprojectGamePoint(const GameProjection& projection, std::int32_t x, std::int32_t y);
} // namespace Client::Scenes
