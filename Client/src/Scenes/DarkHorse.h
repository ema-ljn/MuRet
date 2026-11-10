#pragma once

#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/CharacterDrawing.h"

#include <cstdint>

// El caballo oscuro: el ayudante 0xbf (Skill\DarkHorse.bmd) que monta el personaje con la mascota
// 0x1c16. Lo crea 0x443d70 en el lugar del jinete, lo mueve 0x444010 en cada paso y lo dibuja
// 0x445260 → 0x602e90 (Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro"). Acá va lo
// del caballo solo: el alpha, la acción, el avance y la celda segura del jinete. Lo que hace con la
// tabla de los ayudantes en el juego (el jinete, el polvo, el aliento y los efectos) está en
// Scenes/HelperTable.h. Sin la cabeza del jinete en su hueso 7 (en DarkHorse.bmd, una raíz que ningún
// vértice usa).
namespace Client::Scenes
{
inline constexpr std::uint16_t DarkHorseModel = 0xbf;
// La escala que le pone 0x445260 con el jinete en el login (la escena 2). En la selección (la 4) y en el
// juego (la 5), 1.
inline constexpr float DarkHorseLoginScale = 1.6f;

struct DarkHorse
{
    // +0x111 la acción, +0x112 la anterior, +0x114 el frame y +0x118 la key anterior.
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

// 0x4e9980 con +0x16d en 0 (0x443d70): el alpha sube hacia +0x170 (1, de 0x443d70) un décimo de lo
// que le falta, con un solo redondeo a float. Corre al empezar el paso, también en la celda segura.
void FadeInDarkHorse(DarkHorse& horse);

// 0x444145..0x444535: la acción que le da la del jinete (0x4ea030) y su velocidad en +0xd8.
void TakeTheRidersGait(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction);

// 0x445006..0x445039: el avance de su acción a +0xd8 (0x4ec200), el final del paso.
void AdvanceDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton);

// Un paso de 0x444010 con el 0xbf, sin lo que saca efectos: el alpha sube (FadeInDarkHorse) y, en la
// celda segura del jinete (0x4440b8..0x4440f8), vuelve a 0 y el caballo avanza con la velocidad que
// tenía, sin mirar la acción del jinete; si no, toma la acción y la velocidad que le da la del jinete
// y avanza con ellas.
void StepDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction,
                   bool riderInSafeCell);

// El paso del login (la escena 2): el mundo 56 no tiene ninguna celda con el bit 0 del atributo (06, "El
// caballo oscuro"), así que la celda del jinete nunca lo oculta.
void StepDarkHorse(DarkHorse& horse, const Render::Models::Skeleton& skeleton, std::uint8_t riderAction);

// Lo que se dibuja del caballo entre dos pasos (`stepAlpha` de un paso más), en `place` (el lugar y los
// ángulos del jinete en el login, los suyos en el juego) y con la escala que le pone 0x445260: blanco, con
// la luz por vértice y su alpha (DrawDarkHorse).
[[nodiscard]] DrawnObject DrawnDarkHorse(const DarkHorse& horse, const Render::Models::Skeleton* skeleton,
                                         const Render::Models::ObjectTransform& place, float scale, float stepAlpha);
} // namespace Client::Scenes
