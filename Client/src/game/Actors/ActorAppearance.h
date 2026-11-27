#pragma once

#include "Kernel/CharacterAppearance.h"

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The projection of the persistent record's equipment that the character drawing consumes.
namespace Client::Actors
{

[[nodiscard]] Kernel::CharacterAppearance AppearanceOfActor(const Kernel::ActorState& actor);
} // namespace Client::Actors
