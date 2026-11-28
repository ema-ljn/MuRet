#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <glm/ext/vector_float3.hpp>

// CL9 hito 3, fase 2e: la flecha del elfo con arco o ballesta y de los arcos en Uniria, Dinorant y Fenrir. El tiro
// (0x53e2b0 y 0x53d9a0), el alta de cada flecha en la tabla de efectos con modelo (los casos de 0x518cf0) y su paso
// (los casos de 0x5208a0 con el choque de 0x520170 y 0x517850). 06, "La flecha". CL9 hito 4: las skills de tiro
// (0x53e741, la bandera de 0x53e2e0, el 0x13a, el 0x105 y las flechas 0xdb/3 y /4 y 0xe1/1) y el choque de las
// flechas de skill (0x5201ed, con 0x517a20, Scenes/SkillHits).
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Terrain
{
struct TerrainLighting;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ModelEffectArguments;
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Effects
{

// Los tipos de flecha que crea 0x53d9a0: 0xdb..0xe3, 0xf7..0xf9, 0x10e, 0x111 y 0x11c.
[[nodiscard]] bool IsArrowEffect(std::int16_t type);

// 0x518cf0 con un tipo de flecha: el prefijo (Effects::ModelEffects::Begin) y el alta del tipo (0x51c43b, 0x51a41e,
// 0x51a1e6..0x51a405, 0x51be81 y la salida 0x51a1b6, que copia +0xf04 del registro propio a +0x8c). La llama 0x53d9a0
// por cada mano; la 0xdb con el subtipo 3 o 4 la crea el asedio (0x42c319 y 0x42c4e0, CL9.5) y la 0xe1/1 la fortaleza
// y Crywolf (0x42fd32 y 0x43252d); su 610/13 conserva al efecto de dueño. Con el subtipo 2 (la skill 0x34), antes el
// 0x105 con la flecha de dueño y cuatro intentos de joint 0x610 (0x51a08a). Con la tabla llena, nada y sin rand().
[[nodiscard]] Core::Expected<void, std::string> CreateTheArrowEffect(const Kernel::ModelEffectArguments& arguments,
                                                                     Kernel::WorldState& world, Crt::CrtRandom& random,
                                                                     glm::vec3* callerAngles = nullptr);

// 0x53e2b0(c, o, blanco, lugar, bandera, skill), como lo llama el paso del ataque (0x50377d, 0x5037d0, 0x504060 y
// 0x5040a9) y el caso 0x4f814e de los monstruos (0x4f816f). `actor` es c y o; `target`, el blanco (nullptr sin él),
// que solo lee el efecto 0x13a de la skill 0x2e como dueño. Con la skill 0x34, 0x33 o 0x2e (0x53e741), un tiro de
// 0x53d9a0 y la cuenta de la tanda (+0xf04 del registro propio, que abre y cierra), y sale. Con la bandera en 1 (la
// skill 0x18 sin blanco, 0x53e2e0), tres tiros con +0x24 de o en a, a + 15 y a − 15 (cuatro, en a + 5, a + 15, a − 5 y
// a − 15, con 0xa24 o 0xa25 en +0x2b8: +0x24 queda en a + 15, Q-C1218) y la cuenta de la tanda. Si no, un tiro. Cada
// tiro de 0x53d9a0: las armas (con el héroe, las del registro propio, que abre y cierra; con otro, las de +0x2b8 y
// +0x2d8), la skill en [0x5767948] (WorldRuntime::lastShotSkill) y una flecha por mano con arco o ballesta, cada
// una con su alta; con la 0x2e, solo el efecto 0x13a, con su joint 0x632/4 (0x51e01a). Después de la bandera en 1 o
// de un tiro, con 0xa25 en +0x2b8, el brillo de la mano (0x53e642). Lo que hizo antes de un corte queda, con sus
// rand().
[[nodiscard]] Core::Expected<void, std::string> FireTheArrow(Kernel::ActorState& actor, Kernel::ActorState* target,
                                                             std::size_t slot, std::int32_t skillIndex,
                                                             std::uint8_t flag, std::uint8_t skill,
                                                             const Actors::CharacterStepContext& context);

// 0x518cf0 de la flecha 0xe2, con los campos nativos de 0x4f7f67.
[[nodiscard]] Core::Expected<void, std::string> CreateAquagoldArrow(Kernel::ActorState& owner, const glm::vec3& angles,
                                                                    const Actors::CharacterStepContext& context);

// 0x51fff0 con el estallido de combate 0x51fe10 y el sonido 0x40 de 0x542a7c.
[[nodiscard]] Core::Expected<void, std::string> CreateCombatBurstAt(const glm::vec3& position,
                                                                    const Actors::CharacterStepContext& context);

// 0x51fff0(posición,0): ocho grupos con 0x51fe10 y un selector por intento de piedra c8/c9, primero /1 y después /0.
[[nodiscard]] Core::Expected<void, std::string> ScatterCombatBurstAt(const glm::vec3& position,
                                                                     const Actors::CharacterStepContext& context);

// Lo que el paso de una flecha lee y escribe afuera de su lugar: el pool de personajes, el registro propio y el
// héroe (0x520170), las partículas (0x53fe70), los sprites del frame (0x5491e0), la luz dinámica (0x5f9780) y el
// reloj del mundo (0x5767b88). CL9 hito 4: la altura del terreno (0x5f93c0, la flecha 0xdb/3 y /4; los atributos los
// lee de `terrain`), los sonidos (0x41ed10, CL10: quedan en WorldRuntime::lastSoundId) y lo que manda el choque
// de las flechas de skill (0x517a20, el C1 10). Vacíos, la altura corta, el sonido no se pide y el paquete no sale
// (sus rand() sí).
struct ArrowStepContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    Particles::Particles* particles = nullptr;
    Particles::FrameSprites* sprites = nullptr;
    Terrain::TerrainLighting* lighting = nullptr;
    float worldClock = 0.0f;
    // The map's load (0x7d0d348); without it, an empty one.
    const Terrain::WorldTerrain* terrain = nullptr;
    std::function<double(float, float)> terrainHeight{};
    std::function<void(Audio::SoundId)> soundRequest{};
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x5208a0 sobre una flecha viva: el rand() de la luz local (0x5208c5), el caso de su tipo, el choque (0x520170) y la
// cola común (0x52cb98: la animación con `skeleton`, la velocidad girada y una vida menos), que corre también si el
// choque la apagó. Corta, con lo de antes y sus rand(), donde pide algo sin portar.
[[nodiscard]] Core::Expected<void, std::string>
StepArrow(Effects::ModelEffect& effect, const Models::Skeleton* skeleton, const ArrowStepContext& context);

// 0x520170(efecto) fuera del paso de la flecha: el 0x60f/0xb de la 0xf8 con el skill 1 la pone en su posición y la
// hace chocar desde el paso de los joints (0x53ae6d).
[[nodiscard]] Core::Expected<void, std::string> HitTheArrow(Effects::ModelEffect& effect,
                                                            const ArrowStepContext& context);
} // namespace Client::Effects
