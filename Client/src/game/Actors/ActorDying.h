#pragma once

#include "Core/Expected.h"
#include "Kernel/SceneId.h"
#include "Audio/SoundId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Particles
{
class Particles;
}

namespace Client::Kernel
{
struct ActorState;
class WorldState;
} // namespace Client::Kernel

// The step of the dying (+0x389 other than 0), 0x4fb9e0(c, o, b), which the characters' body (0x4fd5f0) calls at
// 0x4fd722. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Morirse". *Static*.
namespace Client::Actors
{

struct ActorDyingContext
{
    std::int32_t map = 0;
    // DAT_006d5dc8: the bubbles of map 7 belong to scene 5 only.
    Kernel::SceneId scene = Kernel::SceneId::Game;
    // c == [0x743a170]: the hero sinks but is not erased.
    bool hero = false;
    Crt::CrtRandom& random;
    Particles::Particles* particles = nullptr;
    float worldClock = 0.0f;
    Kernel::WorldState* world = nullptr;
    std::function<void(Audio::SoundId)> soundRequest{};
};

// 0x4fb9e0: turns off 0x1000, 0x4000 and 0x20 of +0x78; +0x3c8 rises 0.02 per step (0.05 for 0x172 on map 7 and 0.01
// for 0x192 with the object's +0x60 at 100 or more, ActorState::dword60, which its creation writes); from 1, the alpha
// is 2 − +0x3c8 and, while it does not drop below 0.01, the Z drops 0.4; below that, anyone but the hero is erased
// (+0 at 0 and +0x391, +0x1a1 and +0x1bc at 0; 0x401040 takes out its named bones and 0x4fb1a0 frees its cloths).
// Then the push flight: in Blood Castle (11 to 17) with +0x1a1, 0x4fb2b0; with skill 0x28 or 0x3b (outside the
// castles), before step 15, 0x51fd20. The 0x607 bubbles of map 7 in scene 5 (four, with three rand() each). In Chaos
// Castle it completes 0x51fe10; skill 0x28, up to step 30, releases ten 0x5ac/5 (two rand() each: bone and scale).
[[nodiscard]] Core::Expected<void, std::string> StepActorDying(Kernel::ActorState& actor, std::size_t slot,
                                                               const ActorDyingContext& context);

// 0x4fb2b0(c, o): the flight of one who dies in a castle: (0, +0xd0, 0) turned by +0x1a4 (0x5fc8f0 and 0x5fcbd0)
// from +0x1b0, +0xd0 plus +0xcc, +0xe4 plus +0xd8, +0xd8 minus +0xd4, the X angle minus 5 and the Z at +0x1b8 + +0xe4.
void FlyFromTheCastle(Kernel::ActorState& actor);

// 0x51fd20(o, o + 0x28): the position plus (+0xcc, +0xd0, +0xd4) turned by the angles of +0x28 (those of the head and
// +0x30).
void FlyFromThePush(Kernel::ActorState& actor);
} // namespace Client::Actors
