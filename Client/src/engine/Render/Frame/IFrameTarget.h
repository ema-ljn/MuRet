#pragma once

#include "Core/IClock.h"
#include "Render/Frame/SceneCanvas.h"

namespace Client::Render::Frame
{
class IFrameTarget
{
public:
    virtual ~IFrameTarget() = default;
    // Clears the screen; the canvas, with an empty SpriteBatch, is valid until Present.
    virtual SceneCanvas& BeginFrame(Core::Duration frameClock) = 0;
    // Draws what the SpriteBatch collected and presents: the swap happens inside Render (ILoopClient.h).
    virtual void Present() = 0;
};
} // namespace Client::Render::Frame
