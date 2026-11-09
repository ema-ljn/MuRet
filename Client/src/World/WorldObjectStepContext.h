#pragma once

#include "World/IWorldObjectStepModels.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
enum class ObjectScene : std::uint8_t;
struct TerrainLighting;
class IWorldObjectStepServices;

// Estado vivo prestado durante 604ab0. Mapa/escena/tiempo se releen en las fases nativas.
// Reloj de las ramas temporales: finito y en [-2^63, 2^63), el dominio de FtolLowChecked.
// El caller conserva almacenamiento y sincronización, y no realoca objetos/modelos desde servicios.
struct WorldObjectStepContext
{
    std::int32_t& map;
    const ObjectScene& scene;
    const float& worldTime;
    const std::int32_t& currentHeroTile1;
    const glm::vec3& heroPosition;
    const std::int32_t& editorState; // 077f3b18: guarda de movimiento del mapa2.
    const std::uint32_t& state7d3a0cc;
    const std::int32_t& value6d5e30;
    Render::Models::CrtRandom& random;
    TerrainLighting& terrainLight;
    IWorldObjectStepServices& services;
    // Préstamo obligatorio al dueño canónico; action/flag88 conservan historia entre pases.
    std::reference_wrapper<IWorldObjectStepModels> models;
    const std::uint8_t& heroAction; // +0x111 del Hero vivo para52f550.
};
} // namespace Client::World
