#pragma once

#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Render/ModelBatch.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Render/Frame/IWorldView.h"

#include <functional>

// Given by IFrameTarget::BeginFrame, valid until its Present.
namespace Client::Render::Frame
{
struct SceneCanvas
{
    // Empty (SpriteBatch::Begin), with the state the previous frame left (Q-C75).
    Render::SpriteBatch& sprites;
    // In the same batch, with the current color and blend.
    ITextDrawer& text;
    IWorldView& world;
    Render::ModelBatch& models;
    // 0x621a90, the end of the 2D: draws and empties `sprites` without touching its state. Goes before the 3D pass,
    // which draws separately: without it, the batch would come out on top of the world.
    std::function<void()> flushSprites;
    // The end of the 3D pass: draws and empties `models` with 0x620db0's state. Goes after the models and before the 2D
    // on top; the 2D turns depth off.
    std::function<void()> flushModels;
    // The drawing area in pixels: the original's W and H.
    Core::PixelSize screen;
    // The loop IClock's Now() at frame start. In Main it is SDL's, counting from MuRet's start: it moves the login sand
    // (Q-C65).
    Core::Duration frameClock{};
};
} // namespace Client::Render::Frame
