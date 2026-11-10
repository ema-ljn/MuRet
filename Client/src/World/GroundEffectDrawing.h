#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainHeights;
}
namespace Client::Render
{
class ModelBatch;
}
namespace Client::Render::Models
{
class CrtRandom;
}

// Los casos de 0x52dc20 después del filtro vivo/visto, el rand() común y la mezcla aditiva.
// Contrato estático: 06 § Después de los objetos, P-C55 (b).
namespace Client::World
{
struct ModelEffect;

struct GroundEffectResources
{
    const Data::Terrain::TerrainHeights& heights;
    Render::Models::CrtRandom& random;
    float worldClock = 0.0f;
    std::uint8_t map = 0;
};

// 0x620a90 antes del caso: publica el perfil aditivo sin ligar una textura.
void ApplyGroundEffectAdditive(Render::ModelBatch& batch);

// Conserva el orden de los calcos y las mutaciones que hace el dibujo. No vuelve a mirar alive/seen
// ni consume el rand() de 0x52dc46: le corresponden al recorrido de la tabla.
[[nodiscard]] Core::Expected<void, std::string> DrawGroundEffectCase(Render::ModelBatch& batch, ModelEffect& effect,
                                                                     const GroundEffectResources& resources);
} // namespace Client::World
