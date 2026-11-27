#pragma once

#include <glm/ext/vector_float2.hpp>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

// A character's head (end of the login): the stretch of 0x505de0 that moves it (0x5068eb..0x5069ab), on each step of
// 0x508ea0, after 0x4fd5f0 (the body and the raven). The pose reads it at 0x4eb860 (Models::PoseSkeleton,
// `headAngles`). The characters' step runs it (Actors::StepCharacters, with ActorState::headTurn) and, on its own, the
// login. Contract: 06 § Lo demás del login and § Los efectos de los personajes (0x505de0). Verified bit for bit against
// the emulated original (tests/vectors/world55/head.txt).
namespace Client::Kernel
{
struct CharacterHead
{
    glm::vec2 angles{}; // +0x28, +0x2c: the head's, in degrees
    glm::vec2 target{}; // +0x34, +0x38: where it turns to
};

// With `sorts` (the character is neither the hero of 0x743a170 nor dying, +0x389), a rand() % 32 of 0 draws the
// target: x = rand() % 128 − 64 and y = rand() % 32 − 16, plus 360 if negative. Then each angle turns toward its target
// (0x4e9560) by at most 20 % of the gap (0x4e96f0 unsigned, times 0.2f and stored as a float). What is skipped before
// (flags 0x20 and 0x1000 of +0x78, and whether it is visible, +0x16c) is the caller's check. This is the stretch of
// model 0x20a, the players': with another model, 0x505de0 first calls 0x4ea190 (0x5068da..0x5068e8), which by +0x111
// draws another target (with 0: a rand() % 32 of 0, x = rand() % 128 − 64 and y = rand() % 48 − 16), looks at another
// character (with 2 and +0x3a4 other than −1, 0x4ea0e0), does nothing (with 6) or sets it to 0; an NPC can draw twice
// per step (08, P-C17).
void StepCharacterHead(CharacterHead& head, bool sorts, Crt::CrtRandom& random);
} // namespace Client::Kernel
