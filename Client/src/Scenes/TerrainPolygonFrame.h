#pragma once

#include "Core/Expected.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <string>

// El trapecio global de 0x5fb570 y su nivel efectivo (WorldRuntime::terrainPolygon y terrainPolygonState),
// como los dejan los dibujos de la selección (0x63475c) y del juego (0x637366): 0x5fbb00 → 0x5fb570 una vez
// por frame del original, después de la cámara (0x635ae0). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El trapecio completo" y "El paso de los personajes".
namespace Client::Scenes
{
struct WorldRuntime;

// 0x5fb570 en un frame del original: la transición del nivel efectivo con el nivel pedido (0x7d3a0ec), la D
// (0x7d36ba4) y la meta (0x6d5ee0) que dejó la cámara de ese frame, y el trapecio de `input`. Deja los dos en
// `runtime`. Con error no deja nada (ADR 0009) y el texto nombra 0x5fb570.
[[nodiscard]] Core::Expected<World::TerrainPolygon, std::string>
StepTheTerrainPolygon(WorldRuntime& runtime, const World::TerrainPolygonInput& input, const World::GameCamera& camera);

// Un dibujo intermedio de MuRet, que no es un frame del original: el trapecio de `input` con el nivel
// efectivo que hay, sin la transición y sin escribir nada.
[[nodiscard]] Core::Expected<World::TerrainPolygon, std::string>
TerrainPolygonBetweenFrames(const WorldRuntime& runtime, const World::TerrainPolygonInput& input);
} // namespace Client::Scenes
