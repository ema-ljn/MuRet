#pragma once

#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"

#include <cstdint>

// El caballo oscuro: el ayudante 0xbf (Skill\DarkHorse.bmd) que monta el personaje con la mascota
// 0x1c16. Lo crea 0x443d70 en el lugar del jinete, lo mueve 0x444010 en cada paso y lo dibuja
// 0x445260 → 0x602e90 (Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro"). Sin la
// zona segura de la celda del jinete, que lo apaga (el mundo 56 no tiene ninguna), sin el polvo y los
// efectos de sus acciones, que son de la ola de los efectos, y sin la cabeza del jinete en su hueso 7
// (en DarkHorse.bmd, una raíz que ningún vértice usa).
namespace Client::Scenes
{
inline constexpr std::uint16_t DarkHorseModel = 0xbf;
// La escala que le pone 0x445260 con el jinete en el login (la escena 2). En la selección (la 4), 1.
inline constexpr float DarkHorseLoginScale = 1.6f;

struct DarkHorse
{
    Render::Models::AnimationState animation;
    // +0xd8: 0,5 de 0x443d70, hasta que el primer paso le pone la de su acción.
    float speed = 0.5f;
    // +0x174: 0 al crearlo (0x443d70).
    float alpha = 0.0f;
};

// La acción del caballo por la del jinete (+0x111), con su velocidad (0x444010 con el 0xbf).
struct DarkHorseGait
{
    std::uint8_t action = 0;
    float speed = 0.0f;
};

// 0x53 → 3, 0x4b → 1 y 0x4e a 0x52 → 2, a 0,34; 0x54 → 5 y 0x55 → 6, a 1; cualquier otra → 0, a 0,3.
[[nodiscard]] DarkHorseGait DarkHorseGaitOf(std::uint8_t riderAction);

// Un paso de 0x444010 con el 0xbf: el alpha sube hacia 1 un décimo de lo que le falta (0x4e9980), el
// caballo toma la acción y la velocidad que le da la del jinete (0x4ea030) y avanza con ella
// (0x4ec200).
void StepDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction);

// Lo que se dibuja del caballo entre dos pasos (`stepAlpha` de un paso más), en el lugar y con los
// ángulos del jinete y la escala que le pone 0x445260: blanco, con la luz por vértice y su alpha
// (DrawDarkHorse).
[[nodiscard]] DrawnObject DrawnDarkHorse(const DarkHorse& horse, const Render::Models::Skeleton* skeleton,
                                         const Render::Models::ObjectTransform& rider, float scale, float stepAlpha);
} // namespace Client::Scenes
