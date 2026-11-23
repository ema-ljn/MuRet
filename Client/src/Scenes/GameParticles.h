#pragma once

#include "Core/Expected.h"
#include "Render/Models/OriginalRotation.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Render::Models
{
class CrtRandom;
class IModelLibrary;
} // namespace Client::Render::Models
namespace Client::World
{
class FrameSprites;
struct TerrainLighting;
struct WorldObject;
} // namespace Client::World

// Emisión, paso y dibujo de las partículas del juego, sobre la tabla compartida de 0x743a198.
// Contrato estático: 06 § Después de los objetos, P-C55 (a).
namespace Client::Scenes
{
struct ActorState;
class WorldState;
struct DrawnObject;
struct FaunaDrawCarry;

// 0x4ec110 con Translate=0, después de los dos sorteos del desplazamiento de cada gota.
// El caller presta el buffer global de la pose vigente y conserva sus huesos anteriores/dummy.
using WaterspoutBonePoint =
    std::function<Core::Expected<glm::vec3, std::string>(std::size_t bone, const glm::vec3& offset)>;

// 0x603011..0x60313f, después del rand() común de 0x602ef4: luz y V, guard de mitad de los frames,
// y dos 5f2/0 con sus rand() intercalados y huesos 1 y 4. Pool lleno no consume rand() de Create.
[[nodiscard]] Core::Expected<void, std::string>
StepGameWaterspoutFrame(World::WorldObject& object, World::Particles& particles, Render::Models::CrtRandom& random,
                        const float& worldClock, const WaterspoutBonePoint& bonePoint);

// Con préstamo, 4ec110(Translate=0) lee la matriz global exacta, incluso dummy/alpha bajo.
// Sin él sólo se acredita el hueso escrito por este modelo; falta de matriz corta tras X/Z.
[[nodiscard]] Core::Expected<void, std::string>
StepGameWaterspoutFrame(World::WorldObject& object, World::Particles& particles, Render::Models::CrtRandom& random,
                        const float& worldClock, const Render::Models::IModelLibrary& models, const DrawnObject& drawn,
                        const FaunaDrawCarry* retainedPose = nullptr);

// 0x637128 → 0x5446e0: también con el pool vacío consume los dos rand() del viento.
// worldClock presta el lvalue del caller durante la pasada; no retiene ni copia ese owner. `owners`
// resuelve el dueño de 622/1 entre los objetos del mapa (World::ParticleObjectOwners), `actorActions` da la acción
// del personaje dueño de la órbita 60f/0 (World::ParticleActorActions), `textureHeight`, el alto de la textura
// que lee la 5f3 (World::ParticleTextureHeight), `lighting`, la luz dinámica (0x7b4ccc4) de la 5ed, y `ownerBones`, el
// hueso del dueño de la 5fe (World::ParticleOwnerBones, 0x53fdb0).
// que lee la 5f3 (World::ParticleTextureHeight), `lighting`, la luz dinámica (0x7b4ccc4) de la 5ed, y `actorHands`,
// la mano del dueño de la 5fd (0x53fdb0, World::ParticleActorHands).
[[nodiscard]] Core::Expected<void, std::string>
StepGameParticles(World::Particles& particles, Render::Models::CrtRandom& random, const std::int32_t& map,
                  const std::function<double(float, float)>& terrainHeight = {}, const float& worldClock = 0.0f,
                  const std::function<World::WorldObject*(std::uint64_t)>& owners = {},
                  const std::function<std::optional<std::uint8_t>(std::uint64_t)>& actorActions = {},
                  const std::function<std::optional<float>(std::int32_t)>& textureHeight = {},
                  World::TerrainLighting* lighting = nullptr,
                  const std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>& actorHands = {},
                  const std::function<std::optional<glm::vec3>(const World::Particle&)>& ownerBones = {});

// `sprites` es la tabla de los sprites del frame (0x5491e0) donde la 5fc/2 deja su destello en cada paso; sin ella,
// una 5fc/2 viva corta (World::ParticleError::MissingSprites).
[[nodiscard]] Core::Expected<void, std::string> StepGameParticles(
    World::Particles& particles, Render::Models::CrtRandom& random, const std::int32_t& map,
    const std::function<double(float, float)>& terrainHeight, const float& worldClock,
    const std::function<World::WorldObject*(std::uint64_t)>& owners,
    const std::function<std::optional<std::uint8_t>(std::uint64_t)>& actorActions,
    const std::function<std::optional<float>(std::int32_t)>& textureHeight, World::TerrainLighting* lighting,
    const std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>& actorHands,
    const std::function<std::optional<glm::vec3>(const World::Particle&)>& ownerBones,
    const std::function<void(std::int32_t)>& sound,
    const std::function<std::optional<World::ParticleOwnerState>(const World::ParticleOwner&)>& ownerStates = {},
    World::FrameSprites* sprites = nullptr);

// La acción (+0x111) del personaje de un lugar del pool entre `actors`, que tienen que vivir más que el
// resolvedor: lo que el paso de la órbita 60f/0 lee por el puntero +0x40 (0x54735f).
[[nodiscard]] std::function<std::optional<std::uint8_t>(std::uint64_t)>
ParticleActorActionsAmong(std::span<const ActorState> actors);

// El alto de la textura de cada lugar (+0x24 de 0x7d3a198) entre las que subió `textures`, como lo lee el paso de
// la 5f3; `textures` tiene que vivir más que el resolvedor.
[[nodiscard]] std::function<std::optional<float>(std::int32_t)>
ParticleTextureHeightsOf(const Render::Models::IModelLibrary& textures);

// El dueño de una partícula por la identidad de su objeto (WorldObject::identity) entre `objects`, que
// tienen que vivir más que el resolvedor: lo que el original lee por el puntero +0x40.
[[nodiscard]] std::function<World::WorldObject*(std::uint64_t)>
ParticleOwnersAmong(std::span<World::WorldObject> objects);

// 5ac/4 (0x544a9d): dueño tipo20a y byte88=1, hueso ftol(rotation) de la pose propia, vector local `angles` más
// posición del dueño, sin escala ni publicación de modelo. Un dueño inválido devuelve nullopt para apagar la luz.
// Los demás: 0x53fdb0(partícula) con un dueño del pool de personajes (ParticleOwnerKind::Actor): el hueso es el byte +5
// de la parte del héroe publicado ([0x743a170] + 0x2bd + (subtipo % 2, con signo) · 0x20: la mano derecha con 0, la
// izquierda con 1 y la quinta del cuerpo con −1) y el punto local (0; −120; 0) con la 5fe y la 62d, cero con las demás.
// 0x4ec110 con el modelo del dueño (+2): su origen (+0x6c) pasa a ser la posición del dueño y la escala (+0x68) es la
// que quedó en el modelo; con los huesos propios (+0x120) del último dibujo, una matriz en cero antes del primero
// (Q-C318). MuRet guarda el modelo de Player.bmd (WorldState::PlayerModel): con otro tipo, nullopt, que la partícula
// devuelve como MissingOwner. Es el corte de los anillos de transformación (08, P-C358): la 5fe de 0x55be23 corta antes
// con su dirección (Scenes::ShineOnTheHero).
[[nodiscard]] std::optional<glm::vec3> OwnerBonePointOf(WorldState& world, const World::Particle& particle);
// Lo que el paso lee por +0x40 de las partículas que siguen a su dueño (5c6/4, 5d5/2, el fuego /9 y 5de/4): de un
// personaje del pool, su byte vivo y su posición; de un efecto con modelo, además su partida (+0x17c). De los dos, la
// acción (+0x111), que la órbita 60f/0 lee también del 0x61d que la suelta (0x54735f). nullopt si la identidad no es
// de ninguno de los dos pools. `world` tiene que vivir más que el resolvedor.
[[nodiscard]] std::function<std::optional<World::ParticleOwnerState>(const World::ParticleOwner&)>
ParticleOwnerStatesOf(WorldState& world);
// El resolvedor de 0x53fdb0 sobre `world`, que tiene que vivir más que él.
[[nodiscard]] std::function<std::optional<glm::vec3>(const World::Particle&)> ParticleOwnerBonesOf(WorldState& world);

// 0x637558 → 0x548550(0), en el pase de ojo después de los sprites. Conserva la geometría y
// las operaciones anteriores a un fallo y devuelve la dirección y el lugar que falló.
[[nodiscard]] Core::Expected<void, std::string>
DrawGameParticles(Render::ModelBatch& batch, World::Particles& particles, const Render::Models::IModelLibrary& textures,
                  const Render::Models::OriginalMatrix& view, float worldClock, std::uint8_t mode = 0);
} // namespace Client::Scenes
