#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>

// The body of the models other than 0x20a (CL9 milestone 2, P-C52 d): what 0x4f9580, 0x4fcd00, 0x4fcee0 and 0x505de0 do
// by type for a monster, an NPC or a NewFace. *Static*. Contract: Client/docs/research/06-render-escenas-y-ui.md, "El
// cuerpo de los monstruos y los NPC".
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Models
{
struct Skeleton;
} // namespace Client::Models

namespace Client::Kernel
{
struct ActorState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Monsters
{

// 0x4f96d4..0x4f9737, after the common factors: the NewFace (0xa8..0xac) times 2 (FADD), 0x192 in action 6 past frame 6
// times 4 (0x6b8a44) and 0x1a9 in action 6 times 0.5. Each one is stored as a float.
[[nodiscard]] float ModelSpeedOf(std::uint16_t type, std::uint8_t action, float frame, float speed);

// What 0x4fcbb0 asks for afterwards.
enum class ModelActionEnd : std::uint8_t
{
    Done,         // sale
    AdvanceAgain, // 0x42d890 returned 1: 0x4f9580 again (0x4fcbe3)
    LeaveRemains, // action 6 of a monster with +0x38a at 0, which is now 1: 0x52e810 (0x4fcc3c, LeaveTheRemains)
};

// 0x4fcbb0(c, c), when a model other than 0x20a ends its action (0x4fce60). `skeleton` is the model's, which 0x4ea030
// reads. In order: 0xc5 sets +0x111 to 1; 0x42d890 in map 30, by +0x376 (221 and 222 to action 0; 277 by +0x78; 286 and
// 287 by the action, with the byte 0x7239d8, `runtime.castleSiege.castleByte7239d8`); in map 1, 0x28 to 0; 0x152..0x1c9
// (the monsters): action 6 with +0x38a at 0 sets it to 1 and asks for the remains (LeaveRemains:
// 0x52e810, CL9 milestone 3; with +0x38a at 1, nothing), actions 1, 3, 4, 5, 8, 9 and 0xb to 0; 0x1ce and 0x1cf, rand()
// % 16 under 12 to 0 and otherwise to 1 + rand() % 2; the NewFace nothing; the others to rand() % 2.
[[nodiscard]] Core::Expected<ModelActionEnd, std::string> FinishTheModelAction(Kernel::ActorState& actor,
                                                                               const Models::Skeleton& skeleton,
                                                                               Kernel::WorldRuntime& runtime,
                                                                               Crt::CrtRandom& random);

// 0x4ea190(c) from 0x5068e3, with a model other than 0x20a: action 6 does not touch the head's target; action 0 draws
// one (rand() % 32 at 0: x = rand() % 128 − 64 and y = rand() % 48 − 16, plus 360 if negative); action 2 with +0x3a4
// other than −1 looks at that pool character (0x4ea0e0); the rest sets it to 0. A +0x3a4 outside the pool cuts.
[[nodiscard]] Core::Expected<void, std::string>
SetTheModelHeadTarget(Kernel::ActorState& actor, std::span<const Kernel::ActorState> pool, Crt::CrtRandom& random);

// 0x4ea0e0(c, target): with the heading from c to the target (0x4e93f0, as a float) under 90 degrees from +0x24
// (0x4e96f0 unsigned), the head's target is +0x24 minus the heading and (the target's z − (z + 50)) · 0.2f, plus 360 if
// negative; otherwise, (0, 0).
void LookAtTheTarget(Kernel::ActorState& actor, const Kernel::ActorState& target);

} // namespace Client::Monsters
