#pragma once

#include "Kernel/CharacterAppearance.h"

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// La proyección del equipo del registro persistente que consume el dibujo del personaje.
namespace Client::Actors
{

[[nodiscard]] Kernel::CharacterAppearance AppearanceOfActor(const Kernel::ActorState& actor);
} // namespace Client::Actors
