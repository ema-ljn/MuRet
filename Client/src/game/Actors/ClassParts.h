#pragma once

#include "Core/Expected.h"
#include "Actors/ActorActions.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The new class's clothes: 0x513100, which a quest's prize (A3 0xc9..0xcb, 0x4ddb80) calls after the joints and the
// decal. Contract: Client/docs/research/04-protocolo.md, "Las misiones y los NPC" (the A3), and 07, Q-C1024.
namespace Client::Actors
{

// 0x513100: only with type 0x20a. With the action (+0x111) outside [0x22, 0x97] and [0xc5, 0xcc], 0x4f2250 with
// `stop`. Then each body part with the base class's model (class & 7) goes to the whole class's, with the level and
// bytes +3 and +4 at 0; the helmet compares its own model, and the other four that of the next part (the last one,
// that of the right hand, +0x2b8): Q-C1024. At the end, 0x512930 (the head and the scale). Returns what 0x4f2250
// asked for, if it ran; its error, without touching the parts.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> ApplyTheClassParts(Kernel::ActorState& actor,
                                                                              const ActorStopContext& stop);
} // namespace Client::Actors
