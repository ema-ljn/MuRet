#pragma once

#include "Assets/ModelIds.h"
#include "Kernel/ActorSpirit.h"
#include "Math/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>

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
struct TerrainPolygon;
} // namespace Client::Terrain

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

// The raven: the bird of item 0x1c17 in the Dark Lord's hand, model 0xc0 (Skill\DarkSpirit.bmd). 0x420d60 creates it
// with 0x4167b0 (a 0x460-byte object with a CHARACTER at +0xc), 0x416a20 moves it on each owner step and 0x4182f0 draws
// it. Contract: 06 § El cuervo. States 0 and 1 (free flight, the only ones in the login) are checked bit for bit
// against the emulated real code (tests/vectors/world55/raven.txt); 3 (perched) and 4 (landing), the safe zone's, and
// 2, 5 and 6, the attack the A8 sets (Pets/PetOrders, 06 § El ataque del cuervo), are ported from the ASM.
namespace Client::Pets
{
inline constexpr std::uint16_t DarkSpiritModel = Assets::DarkSpiritModel;

using Kernel::DarkSpirit;

// The owner's position (+0x10) and flags (+0x78): with 0x1000 the step does nothing; with 0x4000 there is no spark. In
// the safe zone (+0x3e2 at 1), also its angles (+0x1c), own bones (+0x120, the last draw's; empty before the first,
// Q-C318) and its type's shared model scale and position (+0x68 and +0x6c of 0x5769ad8 + type · 0xe0): those of the
// last 0x20a that copied them (WorldState::PlayerModel, Q-C412). With the pet (+0x318) at 0x1a04, it flies at 350
// (Q-C413).
struct DarkSpiritOwner
{
    glm::vec3 position{};
    std::uint32_t flags = 0;
    glm::vec3 angles{};
    std::uint16_t pet = 0;
    std::uint8_t safeZone = 0;
    std::span<const Math::BoneTransform> pose{};
    float modelScale = 0.0f;
    glm::vec3 modelOrigin{};
};

// What the notice and the attack states read from other pool characters: the +8 one (`target`), which with +0x1ad at 1
// is the flight destination instead of the owner (0x4177b1..0x417811), and the +0x3b0 one: whether it is among the 400
// (0x416a88..0x416a9b, signed), its flags (+0x78, the 0x4000 of 0x416ab6), and its position, which state 6 reads with
// +0x3b0 other than 0xffff (0x417847).
struct DarkSpiritTargets
{
    glm::vec3 target{};
    bool attackInPool = false;
    std::uint32_t attackFlags = 0;
    glm::vec3 attackPosition{};
};

// The shout (0x41ed10(0x1d1)) and the A7 that stops the attack (C1 07 A7 00 78 FF FF, Protocol::Pets::WritePetCommand).
struct DarkSpiritStepped
{
    bool shouts = false;
    bool stopsTheAttack = false;
};

struct DarkSpiritBirthAccesses
{
    std::uint8_t cellX = 0;
    std::uint8_t cellY = 0;
    std::int32_t counter = 0;
};

// 0x4167b0 with 0x415be0: the 12 rand() with the counts closed, from the closes of the owner's cells and of the counter
// (Q-C125), and the raven at the center of the owner's cell (with `and 0xff`), at the terrain height there
// (`groundHeight`, that of 0x5f93c0 at (cell · 100 + 50), which 0x415ea7 stores as float) plus 300. In the login, cell
// (0, 0) (Q-C130).
[[nodiscard]] DarkSpirit CreateDarkSpirit(int cellX, int cellY, double groundHeight, Crt::CrtRandom& random,
                                          DarkSpiritBirthAccesses accesses = {});

// 0x416a20 with the raven's skeleton; `counter` is the value the shout reads from 0x7d3a0f0. With 0x1000 on the owner,
// nothing. In order: the notice (0x416a88..0x4176e6, Q-C1206), the safe zone (0x4176f0, Q-C414), the 0.4 animation and
// the action by state on wrapping (0x417f0c), then the state: 0 and 1 the free flight (Q-C131, Q-C413), 5 and 2 the
// charge (0x4179ef, 0x4e9810), 6 the shout (0x417847), 3 the perch (0x4178a2), 4 the landing (0x4178f4, 0x4e9810);
// last, the shout and the jump next to the owner when the far count passes 90.
[[nodiscard]] DarkSpiritStepped StepDarkSpirit(DarkSpirit& spirit, const Models::Skeleton& skeleton,
                                               const DarkSpiritOwner& owner, std::uint8_t map,
                                               Particles::Particles& particles, float worldClock,
                                               Crt::CrtRandom& random, std::int32_t counter,
                                               const DarkSpiritTargets& targets = {});
} // namespace Client::Pets
