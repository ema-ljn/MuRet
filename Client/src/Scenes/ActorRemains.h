#pragma once

#include "Core/Expected.h"
#include "World/GroundMarks.h"

#include <glm/ext/vector_float3.hpp>

#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::World
{
class ModelEffects;
}

// Los restos del que termina de morir (CL9 hito 3, fase 7): 0x52e810, que llaman 0x4fcb10 (el 0x20a con la acción 0xc3
// o 0xc4, 0x4fcb9c) y 0x4fcbb0 (los monstruos con la 6, 0x4fcc3c) con +0x38a en 0, que ponen en 1 antes. *Estático*.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Las marcas del suelo" y "Los restos (0x52e810)".
namespace Client::Scenes
{
struct ActorState;

// Lo que 0x52e810 lee y escribe además del personaje: el modelo de su tipo (0x5769ad8 + tipo · 0xe0, su +0x54) con la
// escala (+0x68) y la posición (+0x6c) del último que las copió en él (Q-C1178, como Q-C412:
// WorldState::ModelPlacement), la tabla de los efectos con modelo (0x518cf0), la de las marcas del suelo (0x548fe0) y
// el rand().
struct ActorRemainsContext
{
    const Render::Models::Skeleton* skeleton = nullptr;
    float modelScale = 0.0f;
    glm::vec3 modelOrigin{};
    World::ModelEffects& effects;
    World::GroundMarkTable& marks;
    Render::Models::CrtRandom& random;
};

// 0x52e810(c): con el hueso +0x54 del modelo en −1, nada. El 0x161 (el monstruo de hielo de Devias) apaga su byte vivo
// y crea diez pedazos de hielo 0xca (0x518cf0, ModelEffects::CreateIceShard, cinco rand() cada uno); el 0x159, el 0x160
// y el 0x164 no dejan nada. Los demás, dos veces: rand() % 100 − 50 para la x y otro para la y del corrimiento (z en
// 0), el punto del hueso +0x54 de los huesos propios (+0x120, 0x4ec110 con Translate en 1; sin ellos, la matriz en
// cero, Q-C318), el lado ((rand() & 3) + 8) · 0,1f, el giro rand() % 360 y la marca de sangre 0x5e3 con la luz +0xf4
// (0x548fe0, World::AddGroundMark, con el rand() de la vida si hay lugar). Sin el modelo cargado corta en 0x52e834; si
// AddGroundMark corta, también.
[[nodiscard]] Core::Expected<void, std::string> LeaveTheRemains(ActorState& actor, const ActorRemainsContext& context);
} // namespace Client::Scenes
