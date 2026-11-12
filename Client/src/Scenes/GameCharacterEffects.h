#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Render/Models/HalfLifeMath.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameDrawTail.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
namespace Models
{
class CrtRandom;
}
} // namespace Client::Render

namespace Client::World
{
class FrameSprites;
struct ModelEffect;
class ModelEffects;
class Particles;
struct TerrainLighting;
struct TerrainPolygon;
struct WorldTerrain;
} // namespace Client::World

// Los efectos de un jugador en el dibujo del juego: la cola de 0x50ba30 después de lo que cuelga, del caso
// del 0x20a del switch de 0x50e9fa (0x50ea20) al return, y el cuervo de 0x420df0. Lo que pide un efecto,
// un joint, una partícula o un sonido que MuRet no tiene corta con su dirección, y sólo cuando la rama de
// verdad correría. Contrato: 06 § El dibujo del juego, "Los efectos del jugador".
namespace Client::Scenes
{
class IModelLibrary;
struct ActorState;
struct FaunaDrawCarry;

struct GameCharacterEffectsPass
{
    // Los huesos del cuerpo de este frame: el buffer propio del personaje (+0x120).
    std::span<const Render::Models::BoneTransform> body;
    // El rand() global de 0x69fd70.
    Render::Models::CrtRandom& random;
    // La tabla de sprites del frame (0x5491e0). Hace falta sólo si una rama pone sprites.
    World::FrameSprites* sprites = nullptr;
    // Las partículas (0x53fe70), los joints (0x530860) y los efectos con modelo (0x518cf0) del frame, para lo que
    // crea el conjunto completo. Hacen falta sólo si una rama crea, en un frame del original.
    World::Particles* particles = nullptr;
    std::span<JointState> joints{};
    World::ModelEffects* effects = nullptr;
    // El lugar del personaje en el pool: la identidad del dueño (+0x40) de la órbita 60f/0.
    std::size_t slot = 0;
    // Lo que devolvió 0x505820 en este dibujo (0x50c904) y lo que dejó en [0x743a178].
    FullSetScan scan;
    // DAT_05767b88: el reloj del mundo en ms, en float.
    float worldClock = 0.0f;
    std::int32_t map = 0;        // 0x6c9dd0
    std::int32_t logicSteps = 0; // 0x7d3a0f0
    // 0x510a80: la escena (0x6d5dc8) es la 5. En el login y en la selección, 0x510ef0 no corre.
    bool gameScene = true;
    // En un frame del original corren los contadores y lo que nace en las tablas durables; entre frames,
    // sólo los sprites, sobre una copia del personaje y del rand().
    bool originalFrame = true;
};

// 0x50ea20..0x510ab6 sobre un 0x20a con lo que cuelga ya dibujado, en el orden del ASM: los sprites de la
// piel especial del mago (0x50ea38, que deja el corrimiento (−4, 11, 0) para las manos, Q-C350), los de la
// acción 0x94 fuera del Dark Lord y los efectos de las ventanas de las acciones 0x94, 0x51 y 0x5b (0x50f828),
// los de las banderas +0x78 & 4 o 0x2000000 (0x50fba4); fuera de Chaos Castle, el contador de +0x35c con
// +0x358 (0x50fd74, con el efecto 0x5ac/3 de cada 100) y, con el conjunto completo de 0x505820, los sprites 0x5ac
// de las manos, las partículas 0x6f1/3 de las botas de 0x182f a 0x1833 y, por encima de +9, la órbita 0x60f/0 y los
// joints 0x60f/0, 0x60f/0x12 y 0x610/7 (0x50fddb, con sus rand() en orden); y al final, sólo en la escena 5 (0x510a80),
// 0x510ef0 con +0x369. No hay otra puerta de escena: lo llaman el juego, el login y la selección. Entre frames, los
// sprites, el contador y los rand() que eligen sobre la copia del personaje y del rand() que da el caller; lo que
// nace en las tablas durables, sólo en un frame del original.
[[nodiscard]] Core::Expected<void, std::string> DrawGameCharacterEffects(ActorState& actor,
                                                                         const GameCharacterEffectsPass& pass);

// 0x5208a0 sobre el 0x5ac/3 del conjunto completo (0x52a9ad): el rand() de la luz de todo efecto vivo (0x5208c5), +0xd8
// un paso más y, con la vida múltiplo de 15 y dueño, el giro z 180 más; la posición a 50 del personaje dueño (+0x108,
// leído en vivo) con el seno del giro sin redondear y el coseno del giro guardado (Q-C380), 20 más arriba, y el joint
// 619/9 ahí. Después la cola común (0x52cb98): sin animación ni velocidad, una vida menos.
[[nodiscard]] Core::Expected<void, std::string>
StepFullSetFlare(World::ModelEffect& effect, std::span<JointState> joints, Render::Models::CrtRandom& random);

// Lo que lee el cuervo del juego (0x420df0 → 0x4182f0) fuera de su dueño.
struct GameRavenPass
{
    Render::ModelBatch& batch;
    const IModelLibrary& models;
    // El trapecio de este dibujo (0x5fba90) y el buffer dinámico de la luz del terreno (0x5ffce0).
    const World::TerrainPolygon& polygon;
    const World::TerrainLighting* lighting = nullptr;
    // Las alturas del terreno de 0x5f93c0, para la sombra, con la altura fija de la cámara.
    const World::WorldTerrain& terrain;
    float fixedHeight = 0.0f;
    // El buffer global de la pose (0x68b181c), donde 0x5ffdf0 deja la del cuervo. Opcional.
    FaunaDrawCarry* carry = nullptr;
    Core::Duration frameClock{};
    std::int32_t map = 0;
    // +0x1a1 del héroe, que 0x5ffdf0 lee en los mapas 18 a 23.
    std::uint8_t heroByte1a1 = 0;
    bool originalFrame = true;
};

// 0x420df0: con un cuervo en +0x430, 0x4182f0. Sin pasar el trapecio (su (x, y) por 0,01f, con −20),
// MissTheView. Si lo pasa, 0x602e90: 0x5ffdf0 (el alpha de Chaos Castle y, por encima de 0,01, la pose con
// la luz del terreno) y 0x6002e0 (las mallas 3 y 2, y la sombra salvo en los mapas 10, 24 a 29 y 36); con
// +0x78 & 0x4000 del dueño, 0x6002e0 recibe 10 y corta. Después 0x418920, que sólo hace algo en los estados
// 5 y 6 (las órdenes del A7). Se dibuja como lo dejó el paso, como el personaje. En un frame del original le
// quedan los huesos de la pose para la chispa del paso.
[[nodiscard]] Core::Expected<void, std::string> DrawGameRaven(ActorState& owner, const GameRavenPass& pass);
} // namespace Client::Scenes
