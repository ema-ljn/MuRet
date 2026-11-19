#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// CL11, el lote de los tipos altos: las altas de 0x518cf0 de los tipos que salen del switch de 9..0x113 por otras
// comparaciones (0x518e3c..0x518f31, 0x51e349 y 0x51f47f) y sus pasos de 0x5208a0 (0x525e6e, 0x52a688 y 0x52b56d), con
// el prefijo de World::ModelEffects::Begin. 06, "Los efectos con modelo de los tipos altos". El dibujo de estos tipos
// va por el camino común de 0x52d620 (0x52da84 → 0x602e90, GameDrawTail) o no se dibuja: los de 0x151 en adelante,
// salvo 0x5e8..0x5ea y 0xc1a, no tienen caso en 0x52d620 ni en el pase del suelo (0x52dc20).
namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::World
{
struct ModelEffect;
struct ModelEffectArguments;
struct ModelEffectStepServices;
} // namespace Client::World

namespace Client::Scenes
{
class IModelLibrary;
class WorldState;
struct ActorState;
struct CharacterStepContext;

// Lo que las altas de estos tipos leen y escriben fuera de su lugar: la tabla de efectos (World::ModelEffects), los
// joints (0x530860, GameDrawTailState::joints), las partículas (0x53fe70), el sonido (0x41ed10, que queda en
// WorldRuntime), el héroe ([0x743a170]) y el registro propio, todo de `world`, y el reloj del mundo (0x5767b88). `send`
// es lo que manda 0x517a20 (el C1 10) desde el alta del 0x5e8; vacío, no sale (sus rand() sí). `models`, si está, da el
// BMD que 0x4ea030 no encuentra en world.FaunaResources() (los modelos del mapa), como MonsterSkeletonOf.
struct HighEffectContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    float worldClock = 0.0f;
    std::function<void(Protocol::Outgoing)> send{};
    const IModelLibrary* models = nullptr;
};

// El contexto de las altas desde el paso de un personaje (0x4f5b70, 0x4fd5f0, 0x4f97b0 y sus helpers).
[[nodiscard]] HighEffectContext HighContextOf(const CharacterStepContext& context);

// 0x4f5b10(personaje, x, y, z, punto): `offset` girado por los ángulos del personaje (+0x1c) más su posición, un fstp
// por eje.
[[nodiscard]] glm::vec3 PointAround(const ActorState& actor, const glm::vec3& offset);

// 0x4ec110(modelo del tipo, huesos propios (+0x120) + hueso · 0x30, offset, punto, 1): el punto de un hueso del
// personaje en el mundo, con la escala y el origen de su modelo (los que 0x4fd69b le copia antes del paso). Sin huesos
// propios (antes del primer dibujo), la matriz en cero (Q-C318), como OwnBonePoint de CharacterStep.cpp.
[[nodiscard]] glm::vec3 ActorBonePoint(const ActorState& actor, std::size_t bone, const glm::vec3& offset,
                                       const WorldState& world);

// Los tipos cuya alta está en este archivo.
[[nodiscard]] bool IsHighModelEffect(std::int16_t type);

// 0x518cf0(tipo, posición, ángulos, luz, subtipo, dueño, clave, +0x89, +0x88, +0x1bd, escala) con uno de esos tipos: el
// prefijo (World::ModelEffects::Begin) y el caso del tipo, con sus rand(), joints, partículas y sonidos. Con la tabla
// llena, nada y sin rand(). Corta, con lo de antes y sus rand(), donde el caso pide algo que MuRet no tiene (un joint
// o una partícula de otro lote de la CL11). Devuelve el lugar.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateHighModelEffect(const World::ModelEffectArguments& arguments, const HighEffectContext& context);

// Lo que el paso de estos tipos pide: los servicios del paso común (la altura del terreno, la luz dinámica, el
// sonido, la acción publicada del modelo) y el mundo y el CRT del juego.
struct HighEffectStepContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const World::ModelEffectStepServices& services;
    float worldClock = 0.0f;
};

// Los tipos cuyo paso está en este archivo.
[[nodiscard]] bool StepsAsHighModelEffect(const World::ModelEffect& effect);

// 0x5208a0 sobre un efecto vivo de esos tipos: el rand() de la luz local (0x5208c5), el caso del tipo y la cola común
// (0x52cb98: la acción publicada y la animación de los BMD de [0xad, 0x151), el movimiento de 0x51fc80 salvo los
// tipos que lo saltan, una vida menos y, con 0 o menos, el lugar libre). Corta, con lo de antes y sus rand(), donde el
// paso pide algo sin portar.
[[nodiscard]] Core::Expected<void, std::string> StepHighModelEffect(World::ModelEffect& effect,
                                                                    const Render::Models::Skeleton* skeleton,
                                                                    const HighEffectStepContext& context);

// La posición (+0x10) de un personaje de `world` que es el dueño (+0x108) de un efecto, para el pase del suelo (el
// 0x61e/7, 0x52ded0); nullopt si el dueño no es uno de sus personajes. `world` tiene que vivir más que la función.
[[nodiscard]] std::function<std::optional<glm::vec3>(const void*)> ActorPositionOf(const WorldState& world);
} // namespace Client::Scenes
