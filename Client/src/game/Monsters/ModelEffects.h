#pragma once

#include "Core/Expected.h"
#include "Terrain/GroundMarks.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
struct TerrainLighting;
} // namespace Client::Terrain

// 0x505de0 of a character other than 0x20a, from the rand() & 7 of 0x506cac up to 0x50862f (06, "Los efectos de los
// personajes (0x505de0)"): the case of its type (the byte table 0x508884 over the jump table 0x50880c) or, without a
// case, the six helpers of 0x5085db in order up to the first that returns 1 (0x43bdd0, 0x42de90, 0x441210, 0x42f550,
// 0x431470 and 0x426ce0). What they write to the character, its lights, its rand(), its sounds (in
// WorldRuntime::lastSoundId, CL10) and the particles, joints and sprites MuRet already has; it cuts with the address of
// the rest (the effects of 0x518cf0 and the particles and joints the pool does not know, CL11; the bones by name, the
// original's undefined behavior), always after the rand() the original takes before.
namespace Client::Monsters
{

struct ModelEffectsPass
{
    Kernel::ActorState& actor;
    std::size_t slot = 0;
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    // The type's model (0x5769ad8 + type · 0xe0): the bone count of +0x22 and the speed of the actions.
    const Models::Skeleton& skeleton;
    Particles::Particles* particles = nullptr;
    // The frame's sprite table (0x5491e0), the one of the map effects' step.
    Particles::FrameSprites* sprites = nullptr;
    float worldClock = 0.0f;
    std::int32_t map = 0;
    // The ground marks the 0x182 adds to (0x507486). nullptr: it stops there.
    Terrain::GroundMarkTable* groundMarks = nullptr;
    // The dynamic light (0x7b4ccc4) 0x5f9780 adds to. nullptr: it stops there.
    Terrain::TerrainLighting* dynamicLighting = nullptr;
    // The models a creation loads (Assets::AssetsState::fauna). nullptr: readers find no model, writers stop.
    Assets::FaunaModels* faunaModels = nullptr;
};

[[nodiscard]] Core::Expected<void, std::string> StepTheModelEffects(ModelEffectsPass& pass);
} // namespace Client::Monsters
