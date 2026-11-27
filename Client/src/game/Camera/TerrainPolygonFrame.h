#pragma once

#include "Core/Expected.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"

#include <string>

namespace Client::Kernel
{
struct WorldRuntime;
} // namespace Client::Kernel

// El trapecio global de 0x5fb570 y su nivel efectivo (WorldRuntime::terrainPolygon y terrainPolygonState),
// como los dejan los dibujos de la selección (0x63475c) y del juego (0x637366): 0x5fbb00 → 0x5fb570 una vez
// por frame del original, después de la cámara (0x635ae0). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El trapecio completo" y "El paso de los personajes".
namespace Client::Camera
{

// 0x5fb570 en un frame del original: la transición del nivel efectivo con el nivel pedido (0x7d3a0ec), la D
// (0x7d36ba4) y la meta (0x6d5ee0) que dejó la cámara de ese frame, y el trapecio de `input`. Deja los dos en
// `runtime`. Con error no deja nada (ADR 0009) y el texto nombra 0x5fb570.
[[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
StepTheTerrainPolygon(Kernel::WorldRuntime& runtime, const Terrain::TerrainPolygonInput& input,
                      const Camera::GameCamera& camera);

// Un dibujo intermedio de MuRet, que no es un frame del original: el trapecio de `input` con el nivel
// efectivo que hay, sin la transición y sin escribir nada.
[[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
TerrainPolygonBetweenFrames(const Kernel::WorldRuntime& runtime, const Terrain::TerrainPolygonInput& input);
} // namespace Client::Camera
