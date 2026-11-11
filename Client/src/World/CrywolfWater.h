#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class Particles;
struct TerrainView;

// Lo que 0x5fa5c0 lee fuera de la celda en el mapa 34. Quien llama lo presta durante el pase.
struct CrywolfWaterContext
{
    // +0x18 y +0x1c del héroe (0x743a170): la z de la burbuja y sus ángulos.
    float heroZ = 0.0f;
    glm::vec3 heroAngles{0.0f};
    Render::Models::CrtRandom* random = nullptr;
    Particles* particles = nullptr;
    std::reference_wrapper<const float> worldClock;
};

// 0x5fa695..0x5fa731: en el 34, cada vez que 0x5fa830 pide a 0x5fa5c0 las UV del agua (la capa de abajo cuando es la
// 1 en 5, con alguna esquina de alfa por debajo de 1; y la de alfa cuando además la 2 es 5 y alguna esquina pasa de
// 0, como World::DrawTerrainCell), un rand() y, con 0 módulo 50, la burbuja 698/0 en (x · 100 + rand() % 100 + 1;
// y · 100 + rand() % 100 + 1; z del héroe + 10), con los ángulos del héroe, luz (0,3; 0,4; 0,2), escala 1 y sin
// dueño. Va antes de dibujar la celda (x, y), que ya pasó el bit 0x08: el dibujo no saca rand(). Corta sin el azar o
// sin el pool cuando los lee.
[[nodiscard]] Core::Expected<void, std::string>
StepCrywolfWaterCell(const TerrainView& view, std::uint8_t x, std::uint8_t y, const CrywolfWaterContext& context);
} // namespace Client::World
