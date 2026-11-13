#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

// CL9 hito 3, fase 2e: la flecha del elfo con arco o ballesta y de los arcos en Uniria, Dinorant y Fenrir. El tiro
// (0x53e2b0 y 0x53d9a0), el alta de cada flecha en la tabla de efectos con modelo (los casos de 0x518cf0) y su paso
// (los casos de 0x5208a0 con el choque de 0x520170 y 0x517850). 06, "La flecha".
namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::World
{
class FrameSprites;
class Particles;
struct ModelEffect;
struct TerrainLighting;
} // namespace Client::World

namespace Client::Scenes
{
struct ActorState;
struct CharacterStepContext;
class WorldState;

// Los tipos de flecha que crea 0x53d9a0: 0xdb..0xe3, 0xf7..0xf9, 0x10e, 0x111 y 0x11c.
[[nodiscard]] bool IsArrowEffect(std::int16_t type);

// 0x53e2b0(c, o, blanco, lugar, bandera, skill), como lo llama el paso del ataque (0x50377d, 0x5037d0, 0x504060 y
// 0x5040a9). Con la skill 0x34, 0x33 o 0x2e (0x53e741) y con la bandera en 1 (la skill 0x18 sin blanco, 0x53e2e0)
// corta: son del hito 4. Si no, 0x53d9a0: las armas (con el héroe, las del registro propio, que abre y cierra; con
// otro, las de +0x2b8 y +0x2d8), la skill en [0x5767948] (WorldRuntime::lastShotSkill) y una flecha por mano con arco
// o ballesta, cada una con su alta. Después, con 0xa25 en +0x2b8, corta (0x53e642). El blanco no lo lee nadie
// portado: no se pasa. Lo que hizo antes de un corte queda, con sus rand().
[[nodiscard]] Core::Expected<void, std::string> FireTheArrow(ActorState& actor, std::size_t slot,
                                                             std::int32_t skillIndex, std::uint8_t flag,
                                                             std::uint8_t skill, const CharacterStepContext& context);

// Lo que el paso de una flecha lee y escribe afuera de su lugar: el pool de personajes, el registro propio y el
// héroe (0x520170), las partículas (0x53fe70), los sprites del frame (0x5491e0), la luz dinámica (0x5f9780) y el
// reloj del mundo (0x5767b88).
struct ArrowStepContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    World::Particles* particles = nullptr;
    World::FrameSprites* sprites = nullptr;
    World::TerrainLighting* lighting = nullptr;
    float worldClock = 0.0f;
};

// 0x5208a0 sobre una flecha viva: el rand() de la luz local (0x5208c5), el caso de su tipo, el choque (0x520170) y la
// cola común (0x52cb98: la animación con `skeleton`, la velocidad girada y una vida menos), que corre también si el
// choque la apagó. Corta, con lo de antes y sus rand(), donde pide algo sin portar.
[[nodiscard]] Core::Expected<void, std::string>
StepArrow(World::ModelEffect& effect, const Render::Models::Skeleton* skeleton, const ArrowStepContext& context);
} // namespace Client::Scenes
