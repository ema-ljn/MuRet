#pragma once

#include "Core/Expected.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainHeights;
}
namespace Client::Render
{
class ModelBatch;
}
namespace Client::Crt
{
class CrtRandom;
}

// Los casos de 0x52dc20 después del filtro vivo/visto, el rand() común y la mezcla aditiva.
// Contrato estático: 06 § Después de los objetos, P-C55 (b).
namespace Client::World
{
class KalimaWorld;

struct GroundEffectResources
{
    const Data::Terrain::TerrainHeights& heights;
    Crt::CrtRandom& random;
    float worldClock = 0.0f;
    std::uint8_t map = 0;
    const KalimaWorld* kalima = nullptr;
    // La tabla que recorre el pase: el dibujo del 0x61e/6 y del /8 crea ahí los /7 y /9 (0x52e04c, 0x52e0a2).
    std::span<ModelEffect> table{};
    // La posición (+0x10) del dueño (+0x108) de un efecto, leída en vivo: la del 0x61e/7 (0x52ded0). nullopt si el
    // dueño no es un personaje.
    std::function<std::optional<glm::vec3>(const void*)> ownerPosition{};
};

// 0x620a90 antes del caso: publica el perfil aditivo sin ligar una textura.
void ApplyGroundEffectAdditive(Render::ModelBatch& batch);

// Conserva el orden de los calcos y las mutaciones que hace el dibujo. No vuelve a mirar alive/seen
// ni consume el rand() de 0x52dc46: le corresponden al recorrido de la tabla.
[[nodiscard]] Core::Expected<void, std::string> DrawGroundEffectCase(Render::ModelBatch& batch, ModelEffect& effect,
                                                                     const GroundEffectResources& resources);
} // namespace Client::World
