#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::World
{
class FrameSprites;
class Particles;
} // namespace Client::World

// 0x505de0 de un personaje que no es el 0x20a, desde el rand() & 7 de 0x506cac hasta 0x50862f (06, "Los efectos de los
// personajes (0x505de0)"): el caso de su tipo (la tabla de bytes 0x508884 sobre la de saltos 0x50880c) o, sin caso, los
// seis helpers de 0x5085db en orden hasta el primero que devuelve 1 (0x43bdd0, 0x42de90, 0x441210, 0x42f550, 0x431470 y
// 0x426ce0). Lo que escriben del personaje, sus luces, sus rand(), sus sonidos (en WorldRuntime::lastSoundId, CL10) y
// las partículas, los joints y los sprites que MuRet ya tiene; corta con la dirección de lo demás (los efectos de
// 0x518cf0 y las partículas y los joints que el pool no conoce, CL11; los huesos por nombre, el comportamiento
// indefinido del original), siempre después de los rand() que el original saca antes.
namespace Client::Scenes
{
struct ActorState;
class WorldState;

struct ModelEffectsPass
{
    ActorState& actor;
    std::size_t slot = 0;
    WorldState& world;
    Render::Models::CrtRandom& random;
    // El modelo del tipo (0x5769ad8 + tipo · 0xe0): la cantidad de huesos de +0x22 y la velocidad de las acciones.
    const Render::Models::Skeleton& skeleton;
    World::Particles* particles = nullptr;
    // La tabla de sprites del frame (0x5491e0), la del paso de los efectos del mapa.
    World::FrameSprites* sprites = nullptr;
    float worldClock = 0.0f;
    std::int32_t map = 0;
};

[[nodiscard]] Core::Expected<void, std::string> StepTheModelEffects(ModelEffectsPass& pass);
} // namespace Client::Scenes
