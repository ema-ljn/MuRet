#pragma once

#include <glm/ext/vector_float3.hpp>

namespace Client::World
{
// Estado persistente de 0x5fc260/0x5fac90/0x622440. Los defaults son del DTO de MuRet: estos
// cuerpos no demuestran el estado nativo anterior al primer picking.
struct TerrainPickingState
{
    bool selected = false;
    float selectedX = 0.0f;
    float selectedY = 0.0f;
    // Parámetro del segmento, no distancia en unidades de mundo.
    float maxT = 0.0f;
    glm::vec3 hit{};
};
} // namespace Client::World
