#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActorState.h"
#include "Scenes/ActorCooldownServices.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SceneId.h"
#include "World/TerrainCulling.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::World
{
class Particles;
struct WorldTerrain;
} // namespace Client::World

// El paso de los personajes (0x508f00, desde el juego en 0x63710f, la selección en 0x6342f9 y el login en
// 0x634d13): el TerrainWall, si cada uno se ve y, por cada uno, 0x508ea0: 0x508ba0 con los que no son el
// héroe, el cuerpo (0x4fd5f0) por el camino del 0x20a quieto y 0x505de0 hasta su final, con la cabeza que
// sortea en los que no son el héroe. Corta con error donde el original lee algo que MuRet no tiene. Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "El paso de los personajes", "El paso de los personajes en
// la selección y el login", "El cuerpo de los personajes (0x4fd5f0)" y "Los efectos de los personajes
// (0x505de0)".
namespace Client::Scenes
{
class IModelLibrary;
class WorldState;
struct WorldRuntime;
class PlayerActionSpeeds;

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
    // DAT_006d5dc8: la selección (4) decide el final de la animación con el frame + 2 (Q-C21), y el 0x607
    // del mapa 7 de 0x505de0 es solo de la 5 (0x506cf8).
    SceneId scene = SceneId::Game;
    // Player.bmd, el modelo 0x5769ad8 + 0x20a · 0xe0 de las acciones que leen 0x4f9580 y 0x4ec200. Sin él,
    // el cuerpo corta en 0x4f9580.
    const Render::Models::Skeleton* playerSkeleton = nullptr;
    // *[0x7758550], Item.bmd en memoria: lo leen los helpers de 0x4fc450 y la acción de parado (0x4f2250)
    // con un arma o un ítem puesto. nullptr si no se cargó: cortan donde lo leen.
    const Data::Tables::ItemTable* items = nullptr;
    // *[0x76bd1d8], Skill.bmd en memoria: el alcance (0x5526f0) del lazo de la skill 0x4c del paso del ataque
    // (0x50243d, CL9 hito 4). nullptr: corta ahí con un blanco que mirar.
    const Data::Tables::SkillTable* skills = nullptr;
    // Los esqueletos de los demás modelos, para 0x4f5660 (SetActorDie) con el que se muere desde el paso 15 (CL9 hito
    // 3). nullptr: un modelo que no es el 0x20a corta ahí.
    const IModelLibrary* models = nullptr;
    // La carga de la escena 2 o 4; vacío, el mapa del juego. El TerrainWall se escribe ahí mismo.
    const World::WorldTerrain* terrain = nullptr;
    const Render::Models::Skeleton* darkSpiritSkeleton = nullptr;
    World::Particles* particles = nullptr;
    // Los joints del juego (0x74a8d78, GameDrawTailState::joints), para el 0x618/0 y /1 del que mató (0x505169). Vacío,
    // el paso del ataque corta ahí.
    std::span<JointState> joints{};
    float worldClock = 0.0f;
    std::int32_t counter = 0;
    // El objeto suelto de 0x7437050 (NewFace 0xa8..0xac), fuera de los 400 del pool.
    const Render::Models::Skeleton* newFaceSkeleton = nullptr;
    // Si está vacío, el overlay durable de world. Lo escribe cada Walk antes de animar ese actor.
    PlayerActionSpeeds* actionSpeeds = nullptr;
    float fixedHeight = 1200.0f;
    ActorCooldownServices cooldown{};
    // Solicitudes de sonido: el consumidor CL10 decide cómo reproducirlas.
    std::function<void(std::int32_t)> soundRequest{};
    // Lo que el paso manda por send() (el A7 del cuervo, 0x416a20). Vacío, sin la sesión: no sale.
    std::function<void(Protocol::Outgoing)> send{};
    // GetAsyncKeyState(0x11) con 0x80 en el byte de arriba: Ctrl abajo, que 0x558eb0 lee en la habilidad del héroe
    // (0x558fd6, CL9 hito 3).
    bool controlDown = false;
};

// 0x508f00. Corta con error, en el orden del original, en el mapa 30 (0x412140), sin el mapa cargado
// (0x508f14), en las ramas de 0x508ba0 que MuRet no tiene, en las de 0x4fd5f0 fuera del 0x20a quieto (06, su
// tabla de cortes) y en las de 0x505de0 que crean efectos o partículas que MuRet no tiene (06, "Los efectos
// de los personajes (0x505de0)"); las luces de las manos van al buffer dinámico de `world`. Lo que hizo antes
// del corte queda, con los rand() que sacó.
[[nodiscard]] Core::Expected<void, std::string> StepCharacters(const CharacterStepContext& context);

// 0x412140 (el personaje que recibe no lo mira). Fuera del mapa 30, 0x412560 suelta el panel de 0x5763530 si lo hay.
// En el 30, con 0x6c649a en 0xff (0x412191 y 0x4123f9), arma el de la clase 0 si no hay panel (0x412195) o si el que
// hay es de otra clase (0x412402..0x41240f), y deja el que ya es de la clase 0. Los otros puestos (0x80, 0x40, 0x20 y
// 0) arman los paneles 1 y 2 según el gremio de 0x6c6498, y solo los trae el paquete del gremio (CL9): cortan. Lo
// llaman el paso de los personajes con el héroe (0x508f06) y el 0x12 con cada uno (0x4c9256).
[[nodiscard]] Core::Expected<void, std::string> StepTheSiegePanel(WorldRuntime& runtime);

// 0x508ea0 para un objeto fuera del pool: no borra ni marca TerrainWall y nunca es el héroe publicado.
[[nodiscard]] Core::Expected<void, std::string> StepCharacter(ActorState& actor, const CharacterStepContext& context);
} // namespace Client::Scenes
