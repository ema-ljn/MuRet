#pragma once

#include "Models/Pose.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// The game's helpers' table: the 10 places of 0x1d4 bytes at 0x7d335d0, in the .bss. The world holds it
// (WorldState::Helpers): the character set and the removal of a character (game/Actors) write it, and the helpers'
// logic (game/Pets, Scenes/HelperTable.h) creates, steps and draws them.
namespace Client::Kernel
{
inline constexpr std::size_t HelperCount = 10;

// What a helper animates with, the same fields in every helper; the login's Dark Horse is one on its own.
struct DarkHorse
{
    // +0x111 the action, +0x112 the previous one, +0x114 the frame and +0x118 the previous key.
    Models::AnimationState animation;
    // +0xd8: 0.5 from 0x443d70, until the first step sets its action's.
    float speed = 0.5f;
    // +0x174: 0 when it is created (0x443d70).
    float alpha = 0.0f;
    // +0x8e: the earthquake count of action 3 (0x4453e0 raises it each step with that action). The branch of the
    // rider's other actions sets it to 0 (0x44452e); 0x443d70 does not write it, so a reused place keeps the previous
    // helper's, and the .bss leaves 0 to the first one.
    std::uint8_t effectCount = 0;
};

// A place: a projection with what MuRet reads, not the layout. It starts at zero, like the .bss.
// 0x443d70 also writes +4 (the fifth argument, 0), the hidden mesh (+0x58) at −1, the blendMesh light (+0x68) at 1,
// the alpha goal (+0x170) at 1, the vertex light (+0xe8) at 1, +0xf0, +0x16d and +0x11c at 0 and +0x60 at 30: MuRet
// keeps them fixed (DarkHorse, DrawDarkHorse and the draw of the others in Scenes/HelperTable.cpp) or nothing ported
// reads them. Nobody writes +0x74: at 0, the 0x6004aa of 0x6002e0 does not take its 0x80 path.
struct HelperState
{
    bool alive = false;     // +0
    std::uint16_t type = 0; // +2, the model
    float scale = 0.0f;     // +0xc, 1 for the 0xbf (0x443e82) and 1 in scene 5's draw (0x445309)
    glm::vec3 position{};   // +0x10
    glm::vec3 angles{};     // +0x1c
    // +0xcc..+0xd4: the flight of the 0x1c12 (0x445052..0x445221). 0x443d70 does not write it: a reused place keeps
    // the previous helper's (Q-C420).
    glm::vec3 velocity{};
    // +0xf4..+0xfc, its own light: 3 from 0x443d70 (0x443e2f..0x443e45), and 1 in the draw of the letters' portrait
    // (0x495987..0x495993). The 60f/0 the horse drops with +8 of the rider at 1 (0x444740 → 0x53429b) reads it, and
    // so does the draw, which adds it to the terrain's (0x5ffce0, 0x5ffd6a..0x5ffd8b); the breath (0x4445b5) keeps it
    // fixed at 3 (HorseLight in Scenes/HelperTable.cpp).
    glm::vec3 light{};
    // +0x64: −1 from 0x443d70, and 1 with the 0x1c12 (0x443f1a), which draws its mesh 1 as blendMesh.
    std::int16_t blendMesh = -1;
    std::size_t rider = 0; // +0x108, the rider: a pointer into the pool in the original, the pool slot in MuRet
    bool seen = false;     // +0x16c, from the draw (0x5fba90)
    // +0x111..+0x118 the animation, +0xd8 the speed and +0x174 the alpha: the horse's, which are the same fields in
    // every helper.
    DarkHorse horse{};
};

using HelperTable = std::array<HelperState, HelperCount>;

// 0x443d40(rider): turns off the live byte of the live places of that rider. The rest stays.
void TurnOffHelpersOf(HelperTable& helpers, std::size_t rider);
} // namespace Client::Kernel
