#pragma once

#include "Core/IClock.h"
#include "Scenes/GameDrawTail.h"

#include <cstdint>
#include <span>

namespace Client::Render::Models
{
class CrtRandom;
}

namespace Client::World
{
class FrameSprites;
class ModelEffects;
struct ObjectModelStepState;
} // namespace Client::World

// Lo que 0x60b3d0 y 0x609640 leen de los globales para dibujar un ítem, en cualquier escena que dibuje un
// jugador con 0x50ba30: el reloj, el pulso del brillo (0x7d36ba8, que escribe 0x639482 antes de cada frame),
// el de 0x6c9de4/0x6c9de8, la elección del brillo ancient de cada BMD, las mallas sin luz, el rand() global,
// los sprites del frame, el buffer global de la pose, los joints y los efectos con modelo. Contrato: 06 § El
// dibujo del juego, "Lo que cuelga en el juego".
namespace Client::Scenes
{
struct FaunaDrawCarry;
struct GameItemRenderState;
struct ItemDrawContext;
struct ItemSpecialDrawContext;

// Las tablas del frame del dueño del dibujo: las del mundo en un frame del original y las proyectadas entre
// frames. Tienen que vivir mientras se use el contexto.
struct ItemDrawResources
{
    Core::Duration frameClock{};
    Render::Models::CrtRandom& random;
    GameItemRenderState& itemRender;
    World::FrameSprites& sprites;
    FaunaDrawCarry& poseCarry;
    World::ObjectModelStepState& modelState;
    std::span<JointState> joints;
    World::ModelEffects& effects;
    std::int32_t map = 0;
    bool originalFrame = true;
};

// Arma `special` y `item` sobre `resources`: los joints de los ítems sólo los 613/4 y 614/14 de Helper07 con su
// dueño opaco, y los efectos con modelo sólo el rayo c2/3 y el dragón, la llegada y el teletransporte con
// subtipo 0. Lo demás corta con 0x530860 o 0x518cf0. `item.special` apunta a `special`.
void FillItemDrawContexts(ItemSpecialDrawContext& special, ItemDrawContext& item, const ItemDrawResources& resources);
} // namespace Client::Scenes
