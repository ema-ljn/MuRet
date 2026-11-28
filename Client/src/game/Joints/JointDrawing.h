#pragma once

#include "Core/Expected.h"
#include "Kernel/DrawTailState.h"

#include <cstdint>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Particles
{
class FrameSprites;
} // namespace Client::Particles

namespace Client::Render
{
class ModelBatch;
} // namespace Client::Render

// The joints' part of the draw tail (0x6374fe..0x637572).
namespace Client::Joints
{
struct JointDrawResources
{
    Crt::CrtRandom& random;
    Particles::FrameSprites& sprites;
    bool originalFrame = true;
    float worldClock = 0; // 0x53c8ee: signed ftol, remainder 1000 and the UV scroll.
};

[[nodiscard]] Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch,
                                                           std::span<Kernel::JointState> joints, std::uint8_t mode);
[[nodiscard]] Core::Expected<void, std::string> DrawJoints(Render::ModelBatch& batch,
                                                           std::span<Kernel::JointState> joints, std::uint8_t mode,
                                                           const JointDrawResources& resources);
} // namespace Client::Joints
