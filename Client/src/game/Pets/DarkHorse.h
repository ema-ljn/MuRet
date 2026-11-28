#pragma once

#include "Assets/ModelIds.h"
#include "Kernel/HelperTable.h"
#include "Models/Animation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Actors/Drawing/CharacterDrawing.h"

#include <cstdint>

// El caballo oscuro: el ayudante 0xbf (Skill\DarkHorse.bmd) que monta el personaje con la mascota
// 0x1c16. Lo crea 0x443d70 en el lugar del jinete, lo mueve 0x444010 en cada paso y lo dibuja
// 0x445260 → 0x602e90 (Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro"). Acá va lo
// del caballo solo: el alpha, la acción, el avance y la celda segura del jinete. Lo que hace con la
// tabla de los ayudantes en el juego (el jinete, el polvo, el aliento y los efectos) está en
// Scenes/HelperTable.h. Sin la cabeza del jinete en su hueso 7 (en DarkHorse.bmd, una raíz que ningún
// vértice usa).
namespace Client::Pets
{
inline constexpr std::uint16_t DarkHorseModel = Assets::DarkHorseModel;
// La escala que le pone 0x445260 con el jinete en el login (la escena 2). En la selección (la 4) y en el
// juego (la 5), 1.
inline constexpr float DarkHorseLoginScale = 1.6f;

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
void FadeInDarkHorse(Kernel::DarkHorse& horse);

// 0x444145..0x444535: la acción que le da la del jinete (0x4ea030) y su velocidad en +0xd8; con una acción del
// jinete que no es ninguna de las del caballo, también la cuenta del terremoto (+0x8e) en 0 (0x44452e).
void TakeTheRidersGait(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction);

// 0x445006..0x445039: el avance de su acción a +0xd8 (0x4ec200), el final del paso, también el de los otros
// ayudantes. En la escena 4 (`scene`) decide el final con el frame + 2 (Q-C21).
void AdvanceDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton,
                      Models::AnimationScene scene = Models::AnimationScene::Other);

// Un paso de 0x444010 con el 0xbf, sin lo que saca efectos: el alpha sube (FadeInDarkHorse) y, en la
// celda segura del jinete (0x4440b8..0x4440f8), vuelve a 0 y el caballo avanza con la velocidad que
// tenía, sin mirar la acción del jinete; si no, toma la acción y la velocidad que le da la del jinete
// y avanza con ellas.
void StepDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction,
                   bool riderInSafeCell);

// El paso del login (la escena 2): el mundo 56 no tiene ninguna celda con el bit 0 del atributo (06, "El
// caballo oscuro"), así que la celda del jinete nunca lo oculta.
void StepDarkHorse(Kernel::DarkHorse& horse, const Models::Skeleton& skeleton, std::uint8_t riderAction);

// Lo que se dibuja del caballo entre dos pasos (`stepAlpha` de un paso más), en `place` (el lugar y los
// ángulos del jinete en el login, los suyos en el juego) y con la escala que le pone 0x445260: blanco, con
// la luz por vértice y su alpha (DrawDarkHorse).
[[nodiscard]] Actors::Drawing::DrawnObject DrawnDarkHorse(const Kernel::DarkHorse& horse,
                                                          const Models::Skeleton* skeleton,
                                                          const Models::ObjectTransform& place, float scale,
                                                          float stepAlpha);
} // namespace Client::Pets
