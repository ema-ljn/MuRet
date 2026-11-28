#pragma once

#include "Core/Expected.h"
#include "Kernel/WorldTextQueue.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Tables
{
class TextTable;
}

namespace Client::Models
{
struct Skeleton;
}

namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

// The per-type cases of the character step that are not the 0x20a (0x4fd5f0 and 0x508ea0): the name of the 0xc5, the
// fall of the 0xf1 and the siege objects of 0x42da90. Contract: 06 § El cuerpo de los personajes (0x4fd5f0) and § Los
// objetos del mapa 30 en el paso (0x42da90).
namespace Client::Monsters
{

// 0x4fd603..0x4fd698, the 0xc5 (the Kalima gates): "%s ." (0x6c88bc) with the name (+0x1d9, as a C string),
// Text.bmd[1176] (0x77133fc) with +0x1d6 unsigned, the strcat of both into the 100-byte buffer and 0x4381f0 with the
// position (+0x10). No rand(). Without Text.bmd, or if the strcat would go past 100 bytes, it cuts.
[[nodiscard]] Core::Expected<void, std::string>
QueueTheGateName(const Kernel::ActorState& actor, const Tables::TextTable* texts, Kernel::WorldTextQueue& queue);

// 0x4fb910..0x4fb9c7, the 0xf1 from 0x4fb380: z rises by +0xe4 and +0xe4 drops by 6; if the terrain height (0x5f93c0)
// plus 30 is above, z takes it and +0xe4 is multiplied by −0.4f; the angles add +0xcc, +0xd0 and +0xd4, which shrink by
// 0.8f. `height` is 0x5f93c0(x, y).
void StepTheFallingBall(Kernel::ActorState& actor, const std::function<double(float, float)>& height);

// 0x42a020(object, mode, range): whether any player of the pool (+0x88 at 1, type 0x20a and +0x16c) is near the object.
// Mode 0: dx and dy (object minus player) below the range and below 100; 1: |dx| below 1 and dy between 0 and the
// range; 2: the planar distance below the range. A NaN passes the C0 comparisons (the "below" ones).
[[nodiscard]] bool PlayerNear(const Kernel::ActorState& object, std::span<const Kernel::ActorState> pool,
                              std::uint8_t mode, float range);

struct CastleObjectStep
{
    Kernel::WorldRuntime& runtime;
    // The pool of 0x743a16c, for 0x42a020.
    std::span<const Kernel::ActorState> pool;
    // 0x5f93c0(x, y); empty, the 216 cuts.
    std::function<double(float, float)> height{};
    // The character's model, for 0x4ea030; without it, 286 and 287 cut when they change the action.
    const Models::Skeleton* skeleton = nullptr;
};

// 0x42da90(c, c), from 0x508eed after 0x505de0: outside map 30, nothing. In 30, by +0x376: 216 (0xd8) rests 200 above
// the terrain, copies +0x78 to 0x7239e4 and +0x42c is +0x78 nonzero; 277 (0x115) sets 0x7239d9 to 1 (the gates do not
// open in 0x42da60); 278 (0x116) with +0x42d at 0x80 stores its x and y in 0x7239dc and 0x7239e0; 283 (0x11b) with the
// siege sets +0x58 to −1 and +0x42c to 1, without it −2 and 0; 286 and 287 (0x11e and 0x11f) open with a player right
// in front (0x42a020 with mode 1 and 500) or with 0x7239d8 at 2 (action 4, 0x7239d8 at 2, +0x110 at 1 and +0x100 at
// 300) and close when +0x100 runs out with nobody there (action 0), and +0x100 drops by 1 each step. No rand().
[[nodiscard]] Core::Expected<void, std::string> StepTheCastleObject(Kernel::ActorState& actor,
                                                                    const CastleObjectStep& step);
} // namespace Client::Monsters
