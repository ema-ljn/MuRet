#pragma once

#include "GameLogic/CharacterAppearance.h"

// La proyección del equipo del registro persistente que consume el dibujo del personaje.
namespace Client::Scenes
{
struct ActorState;

[[nodiscard]] GameLogic::CharacterAppearance AppearanceOfActor(const ActorState& actor);
} // namespace Client::Scenes
