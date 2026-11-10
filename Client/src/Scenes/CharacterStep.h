#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorState.h"
#include "Scenes/SceneId.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// El paso de los personajes (0x508f00, desde el juego en 0x63710f, la selección en 0x6342f9 y el login en
// 0x634d13): el TerrainWall, si cada uno se ve y, por cada uno, 0x508ea0. MuRet lo corre en el juego con el
// héroe, hasta la zona segura de 0x505de0 y lo que sigue hasta su final, y con los demás hasta 0x508ba0; corta
// con error donde el original lee algo que MuRet no tiene. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El paso de los personajes" y "El paso de los personajes en
// la selección y el login".
namespace Client::Scenes
{
class WorldState;

// 0x5fba90 como lo llaman 0x508f00 (0x50916d..0x50918c) y 0x508ea0 (0x508eac..0x508ecb): el lugar del
// personaje por 0,01f (0x6b88d4), guardado en float, con el margen de −20 (0xc1a00000).
[[nodiscard]] bool CharacterSeen(const World::TerrainPolygon& polygon, const glm::vec3& position);

// La celda lógica de un personaje: la X de +0x41c y la Y de +0x420.
struct LogicalCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// La celda lógica por la tabla de protección, como la leen 0x508f00, 0x505de0 y la cámara (0x635ae0):
// la Y y después la X, cada una en un acceso que se cierra y gasta 4 rand() si lo deja en 0.
[[nodiscard]] LogicalCell ReadLogicalCell(ActorState& actor, Render::Models::CrtRandom& random);

struct CharacterStepContext
{
    // Con el trapecio global que dejó el último 0x5fb570 (WorldRuntime::terrainPolygon): antes del primer
    // dibujo del juego, el del último dibujo de la selección (Q-C216).
    WorldState& world;
    Render::Models::CrtRandom& random;
    // DAT_006d5dc8. Solo en el juego el cuerpo del héroe (0x4fd5f0) es frontera (08 P-C44 a); en las demás
    // escenas corta ahí, como con cualquier otro personaje.
    SceneId scene = SceneId::Game;
};

// 0x508f00. Corta con error, en el orden del original, en el mapa 30 (0x412140), sin el mapa cargado
// (0x508f14), en las ramas de 0x508ba0 que MuRet no tiene, en el cuerpo (0x4fd5f0) del primer vivo que no es
// el héroe del juego y en las ramas de 0x505de0 que el héroe todavía no tiene. Lo que hizo antes del corte
// queda, con los rand() que sacó.
[[nodiscard]] Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context);
} // namespace Client::Scenes
