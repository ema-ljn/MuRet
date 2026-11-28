#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <optional>
#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::MapScripts::LandOfTrials
{

// 0x4401e0 (0x602ed0), with map 31 (0x43ff10), after the common rand() of 0x602ef4 (which it does not use): the 3
// and the 0x35, with rand() % 3 at 0, release the splash 0x6f2/3 and the smoke 0x5f2/22 on the object, with light
// (1, 1, 1), its scale and no owner; the 0x31 leaves on bone 3 of the global pose, 10 lower in y, two red 0x5ac
// sprites (the first of scale ((sin(clock · 0.002f) · 0.35f + 0.65f) · 1.2f + 0.3f), the second of 0.5) and a 0x5fd
// of 1.2 in grey 0.3, turned rand() % 360. A missing pool, rand() or pose cuts where the ASM does, keeping what is
// already created.
[[nodiscard]] Core::Expected<void, std::string> StepLandOfTrialsObjectFrame(const Kernel::WorldObject& object,
                                                                            const Kernel::GameObjectPass& pass);

// What 0x4403b0 (the chain of 0x6002e0 on the 31) does with an object's body.
struct TrialsBodyDraw
{
    // The model's color (+0x48..+0x50) the body is drawn with.
    glm::vec3 color{1.0f};
    // The 0x34 draws a RenderBody of its own and returns 0: 0x6002e0 goes on and draws it again with the color it was
    // left with (Q-C317).
    bool twice = false;
};

// 0x4403b0: the 0x1b and the 0x36 multiply the model's color (`color`, the one from 0x5ffce0) by
// sin(clock · 0.0012f + phase) · 0.5f + 0.9f, with the phase at +0x84; the 0x34, by the same pulse with 0.0009f;
// the 0xa sets it to (0.56; 0.8; 0.81). The other types still have no case (nullopt). A pulse outside the ported
// domain cuts.
[[nodiscard]] Core::Expected<std::optional<TrialsBodyDraw>, std::string>
LandOfTrialsBody(const Kernel::WorldObject& object, const glm::vec3& color, float worldClock);
} // namespace Client::MapScripts::LandOfTrials
