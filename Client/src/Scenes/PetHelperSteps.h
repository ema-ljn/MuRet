#pragma once

#include "Core/Expected.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float2.hpp>

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// Los cuerpos de 0x444010 de las mascotas que no son el caballo oscuro: el Uniria (0xc6), el Dinorant (0x110), los
// Fenrir (0x14a..0x14c) y el Guardian Angel (0x1c12). Corren después del alpha de 0x4e9980 y antes del avance de
// 0x445006, que pone Scenes/HelperTable.cpp. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las
// mascotas".
namespace Client::Scenes
{
struct ActorState;
struct HelperState;
struct HelperStepContext;

inline constexpr std::uint16_t UniriaModel = 0xc6;
inline constexpr std::uint16_t DinorantModel = 0x110;
inline constexpr std::uint16_t FirstFenrirModel = 0x14a;
inline constexpr std::uint16_t LastFenrirModel = 0x14c;
inline constexpr std::uint16_t GuardianAngelModel = 0x1c12;

// 0x4447c4..0x444ac4, el Uniria y el Dinorant: en la celda segura del jinete el alpha a 0. Si no, el alpha con +0x80
// del jinete, su posición (el Dinorant a −10 en los mapas 8 y 10 y a −30 en los demás, salvo el −1) y sus ángulos,
// la acción por la del jinete (0x4ea030), con el galope el polvo (rand() % 2, fuera del 10 y sin el 0x4000 del
// jinete), la velocidad en 0,34 y el byte vivo del jinete.
[[nodiscard]] Core::Expected<void, std::string> StepRidingHelper(HelperState& helper, const ActorState& rider,
                                                                 const Render::Models::Skeleton& skeleton,
                                                                 const HelperStepContext& context);

// 0x444af0..0x444f25, los Fenrir: la celda segura igual; el alpha con +0x80 del jinete, su cabeza, posición y
// ángulos, la acción y la velocidad por la del jinete, y con la carrera o el trote el polvo: en el 10, desde el
// hueso 22 del buffer global con el frame entre 1 y 1,2, o desde el 44 (y 700 más arriba, Q-C421) entre 4,8 y 5, la
// onda 0x6eb/1 y las 5f2; en los demás, rand() % 3, fuera de Kalima y sin el 0x4000 del jinete.
[[nodiscard]] Core::Expected<void, std::string> StepFenrir(HelperState& helper, const ActorState& rider,
                                                           const Render::Models::Skeleton& skeleton,
                                                           const HelperStepContext& context);

// 0x444f2a..0x444fff, el Guardian Angel: sin el 0x4000 del jinete, cuatro 5c5/1 con la luz 0,4 a (rand() & 15) − 8
// del ayudante en cada eje.
[[nodiscard]] Core::Expected<void, std::string>
SparkleTheGuardianAngel(const HelperState& helper, const ActorState& rider, const HelperStepContext& context);

// 0x445052..0x445221, después del avance: el Guardian Angel gira 20 grados hacia donde estaba el jinete al empezar
// el paso (`riderStart`, 0x444080..0x44408b) si está a 150 o más, se mueve con su velocidad girada por sus ángulos
// y (rand() & 15) − 8 en z, con rand() % 32 en 0 toma otra velocidad (y otro rumbo si está cerca), y sube o baja
// 1,5 su velocidad en z para quedar entre 100 y 200 por encima del jinete.
void FlyTheGuardianAngel(HelperState& helper, const ActorState& rider, glm::vec2 riderStart,
                         Render::Models::CrtRandom& random);
} // namespace Client::Scenes
