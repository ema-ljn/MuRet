#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}
namespace Client::World
{
class Particles;
}

// El brillo de la mano de la skill que pega a varios (CL9 hito 2, el 1A): 0x53d810 y 0x53fdb0 sobre la 5fd de
// World::Particles (06, "La skill que pega a varios").
namespace Client::Scenes
{
struct ActorState;

// 0x53fdb0 con la 5fd: el punto (0, 0, 0) de un hueso en el mundo, 0x4ec110 con Translate en 1 sobre los huesos propios
// (+0x120) del dueño `owner`, con su posición (que 0x53fdb0 copia al +0x6c del modelo) y su escala (la del +0x68 del
// modelo, que dejó el último que lo usó: Q-C853). El hueso es el de la mano (subtipo % 2) del héroe `hero`
// (0x743a170 + 0x2bd + (subtipo % 2) · 0x20), no el del dueño (Q-C854). Sin huesos propios (antes del primer dibujo
// del dueño), la matriz en cero (Q-C318).
[[nodiscard]] glm::vec3 HandShinePoint(std::span<const ActorState> actors, std::size_t hero, std::size_t owner,
                                       std::int32_t subtype);

// El resolvedor del paso de la 5fd (World::ParticleActorHands): HandShinePoint del lugar del pool entre `actors`, que
// tienen que vivir más que él; nullopt fuera del pool.
[[nodiscard]] std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>
ParticleActorHandsAmong(std::span<const ActorState> actors, std::size_t hero);

// 0x53d810(personaje, 0): el punto (0, 0, 0) del hueso de la mano derecha del personaje `slot` (+0x2bd, con sus
// huesos propios, su posición y su escala: Q-C853) y, desde ahí, con sus ángulos (+0x1c), la luz (1; 0,5; 0,2) y la
// escala 0, la 5fd/0 y la 5fd/2 de dueño el personaje (0x53fe70). Cada una que nace va a la mano con 0x53fdb0
// (HandShinePoint); con el pool lleno no nace y no hay más. Sin rand().
[[nodiscard]] Core::Expected<void, std::string> RaiseTheHandShine(std::span<const ActorState> actors, std::size_t hero,
                                                                  std::size_t slot, World::Particles& particles,
                                                                  Render::Models::CrtRandom& random);
} // namespace Client::Scenes
