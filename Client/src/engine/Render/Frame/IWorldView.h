#pragma once

#include "Render/Frame/SceneWorld.h"
#include "Render/Pass3D.h"

// A scene's 3D pass (terrain, objects, characters and world effects) in the original's viewport (in the login,
// 0x620db0(0, 25, 640, 430), 06 § El login, el dibujo).
namespace Client::Render::Frame
{
class IWorldView
{
public:
    virtual ~IWorldView() = default;
    // `alpha` in [0, 1) is ILoopClient::Render's. Runs after the end of the 2D (flushSprites); what the scene draws
    // afterwards goes on top.
    virtual void Draw(Render::Frame::SceneWorld world, const Render::Pass3D& pass, float alpha) = 0;
};

class EmptyWorldView final : public IWorldView
{
public:
    void Draw(Render::Frame::SceneWorld /*world*/, const Render::Pass3D& /*pass*/, float /*alpha*/) override {}
};
} // namespace Client::Render::Frame
