#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include <cstdint>
#include <string>

namespace Client::Render
{
class ModelBatch;
}
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render::Models
{
class IModelLibrary;
} // namespace Client::Render::Models
namespace Client::World
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::World

// Las cáscaras 0x137 (World/SkinShellEffects.h) del lado de las escenas: las que suelta el joint 0x632/0 o /1 con
// dueño al llegar al suelo (StormJoints.cpp) y su dibujo en 0x6002e0.
namespace Client::Scenes
{
class WorldState;
struct ActorState;
struct DrawnObject;

// 0x53c1c6..0x53c246, con el personaje dueño del joint. 0x4eb860(0x68b181c, 0, 0, 0, +0x1c, +0x28, 0, 1) posa el
// 0x20b (Player\Shadow01, 0x5769ad8 + 0x1c9a0) con su acción, su escala y su origen compartidos (+0xa0, +0x68 y +0x6c,
// los del último dibujo de una sombra: 0x60cf70 desde 0x50bcec; WorldState::ModelPlacement) y los ángulos y los de la
// cabeza del dueño, y 0x4ebc80(0x68b181c, +0x124, +0x130, +0x13c, 0) pasa sus vértices sin escala ni origen (0x5fccc0)
// y le escribe al dueño la caja de +0x13c con su caja de +0x124..+0x138 y ese origen. Después, 0x4ee770(0, 0x137,
// subtipo, 0, 0) (Scenes::EmitVertexEffects): por cada esquina de la malla 0, con rand() % 8 en 0, una 0x137 del
// subtipo en el vértice. Sin el 0x20b o su esqueleto, o si la pose falla, corta en 0x53c1eb.
[[nodiscard]] Core::Expected<void, std::string> ShedTheSkinShells(ActorState& owner, std::int32_t subtype,
                                                                  const Render::Models::IModelLibrary& models,
                                                                  WorldState& world, World::ModelEffects& effects,
                                                                  Crt::CrtRandom& random);

// 0x600557..0x6005e0, el caso del 0x137 en 0x6002e0 (después de posar): el /0 con el color del modelo en su luz (+0xf4)
// y RenderBody(2) con Chrome01 (0x5c0); el /1 con el color en (0,1f; 0,5; 1) y RenderBody(0x42) con Chrome01; con otro
// subtipo, nada (0x602e7d). Siempre true: el cuerpo común no se dibuja.
[[nodiscard]] Core::Expected<bool, std::string> DrawTheSkinShellBody(Render::ModelBatch& batch,
                                                                     const Render::Models::IModelLibrary& models,
                                                                     const World::ModelEffect& effect,
                                                                     DrawnObject& object, Core::Duration frameClock);
} // namespace Client::Scenes
