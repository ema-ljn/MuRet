#pragma once

#include "GameLogic/CharacterAppearance.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Pose.h"
#include "Scenes/CharacterDrawing.h"

// Lo que avanza de un personaje en cada paso de lógica (Client/docs/research/06-render-escenas-y-ui.md,
// "Cuánto avanza la animación"): el cuerpo con la velocidad de su acción, y la mascota y las alas por
// tiempo (Q-C20). Lo comparten la selección y el login.
namespace Client::Scenes
{
struct CharacterAnimation
{
    Render::Models::AnimationState body;
    Render::Models::TimedAnimation pet;
    Render::Models::TimedAnimation wings;
};

// Un paso: 0x4ec200 con el cuerpo de Player.bmd a la velocidad de su acción (con la regla de `scene`:
// la selección decide "terminó" con el frame + 2, Q-C21), y la mascota y las alas un paso de tiempo,
// las alas a la velocidad que 0x50ba30 les pone en +0x308 por la acción del cuerpo. Lo que la
// biblioteca no tiene no avanza. Devuelve si la acción del cuerpo terminó: con eso, 0x4fcd00 le pone
// la de parado otra vez (0x4fcb10 → 0x4f2250).
[[nodiscard]] bool StepCharacterAnimation(CharacterAnimation& animation,
                                          const GameLogic::CharacterAppearance& appearance, const IModelLibrary& models,
                                          Render::Models::AnimationScene scene);

// Solo las dos animaciones colgantes: el cuerpo ya vino del ActorState que avanzó 0x508f00.
void StepCharacterAttachments(CharacterAnimation& animation, const GameLogic::CharacterAppearance& appearance,
                              const IModelLibrary& models);

// El cuerpo que se posa entre dos pasos: `alpha` de un paso más (Render::Models::Interpolated). Sin la
// acción en Player.bmd, el del paso.
[[nodiscard]] Render::Models::AnimationState BodyBetweenSteps(const CharacterAnimation& animation,
                                                              const IModelLibrary& models, float alpha);
} // namespace Client::Scenes
