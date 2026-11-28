#pragma once

#include "Core/IClock.h"
#include "Models/ItemGlow.h"
#include "Kernel/DrawTailState.h"

#include <cstdint>
#include <span>

namespace Client::Kernel
{
struct ObjectModelStepState;
struct PoseBuffer;
} // namespace Client::Kernel

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

// What 0x60b3d0 and 0x609640 read from the globals to draw an item, in any scene that draws a player with 0x50ba30: the
// clock, the glow's pulse (0x7d36ba8, which 0x639482 writes before each frame), that of 0x6c9de4/0x6c9de8, each BMD's
// ancient glow choice, the unlit meshes, the global rand(), the frame's sprites, the global pose buffer, the joints and
// the model effects. Contract: 06 § El dibujo del juego, "Lo que cuelga en el juego".
namespace Client::Actors::Drawing
{
struct GameItemRenderState;
struct ItemDrawContext;
struct ItemSpecialDrawContext;

// The tables of the draw's frame owner: the world's in an original frame and the projected ones between frames. They
// must live as long as the context is used.
struct ItemDrawResources
{
    Core::Duration frameClock{};
    Crt::CrtRandom& random;
    GameItemRenderState& itemRender;
    Particles::FrameSprites& sprites;
    Kernel::PoseBuffer& poseCarry;
    Kernel::ObjectModelStepState& modelState;
    std::span<Kernel::JointState> joints;
    Effects::ModelEffects& effects;
    std::int32_t map = 0;
    bool originalFrame = true;
    std::uint8_t glowDetail = Models::DefaultGlowDetail;
};

// Sets up `special` and `item` on `resources`, except what the items create in other modules (Session/ItemCreators.h).
// `item.special` points to `special`.
void FillItemDrawContexts(ItemSpecialDrawContext& special, ItemDrawContext& item, const ItemDrawResources& resources);
} // namespace Client::Actors::Drawing
