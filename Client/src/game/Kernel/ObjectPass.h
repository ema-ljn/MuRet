#pragma once

#include "Audio/SoundId.h"
#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Actors::Drawing
{
struct DrawnObject;
} // namespace Client::Actors::Drawing

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Kernel
{
class KalimaWater;
} // namespace Client::Kernel

namespace Client::Particles
{
class FrameSprites;
class Particles;
} // namespace Client::Particles

// What the draw of a world's objects in the game (0x6046a0) reads outside them: what game/WorldObjects hands the map
// scripts and the events with each object.
namespace Client::Kernel
{
struct ActorState;
struct CrywolfGlobals;
struct JointRequest;
struct ObjectModelStepState;
struct PoseBuffer;

struct GameObjectPass
{
    // DAT_006c9dd0.
    std::uint8_t map = 0;
    // Whether the draw is from an original frame: 0x602ed0 takes its rand() (0x602ef4) per object that arrives and
    // Devias' 0x64 opens the own record. An intermediate MuRet draw only draws.
    bool originalFrame = true;
    Crt::CrtRandom* random = nullptr;
    // What Devias' 0x64 looks at: the hero's class (+0x1d4), the own record's level (+0xe) and the count of that
    // record's open accesses (WorldRuntime::ownRecordDepth).
    std::uint8_t heroClass = 0;
    std::uint16_t ownLevel = 0;
    std::uint8_t* ownRecordDepth = nullptr;
    // 0x5767b88: the world clock in ms, from which Lorencia's 0x69 takes its V (0x603018).
    float worldClock = 0.0f;
    // The shared global table of 0x743a198. Without it, the historical cut at the first drop remains.
    Particles::Particles* particles = nullptr;
    // 0x5ffdf0 publishes the pose scratch before 0x602ed0 and before the birds.
    std::function<Core::Expected<void, std::string>(const Actors::Drawing::DrawnObject&)> onPose{};
    Particles::FrameSprites* sprites = nullptr;
    const PoseBuffer* retainedPose = nullptr;
    std::function<Core::Expected<void, std::string>(const JointRequest&)> createJoint{};
    ObjectModelStepState* modelState = nullptr;
    // Publication on the caller's presentation scratch, without running 0x602ed0 or consuming RNG.
    bool publishProjectedPose = false;
    // DAT_07d2d360, lent by the global pulse 0x637700: without a producer no frame 0 is invented.
    std::optional<std::uint32_t> waterTextureFrame{};
    const ActorState* hero = nullptr;
    const Kernel::KalimaWater* kalima = nullptr;
    std::function<void(Audio::SoundId)> requestSound{};
    // 0x518cf0: the model effects, for butterfly 0xae of the 54's 0x3c (0x4257d5).
    Effects::ModelEffects* effects = nullptr;
    // 0x6b8544 (timeGetTime), which the 54's 0x3c and the 33's 0x46 read in the draw (Q-C304).
    std::function<std::uint32_t()> systemTime{};
    // DAT_006d5dc8: with 4 (the selection), the butterfly comes out hidden and the 0x6f1/2 with light 0.25.
    std::int32_t scene = 0;
    // 0x5f93c0(x, y): the terrain height, onto which the 0x1c and the 0x1d of 11 to 17 flatten their shadow (0x600d80).
    std::function<float(float, float)> terrainHeight{};
    // DAT_006c634c, the Chaos Castle area: 0xff until a packet moves it (CL9.5).
    std::uint8_t chaosCastleArea = 0xff;
    // 0x4294b0, the siege byte (0x7239ec): off until a packet turns it on (CL9.5).
    bool siegeActive = false;
    // 0x5763538 (WorldRuntime::objectDrawCounter): with an original frame, 0x6046a0 sets it to 1 after 41 and adds 1
    // before looking at an object (0x6046be..0x6046ce). Without it nothing is counted.
    std::int32_t* objectDrawCounter = nullptr;
    // Crywolf's globals (WorldRuntime::crywolf), which 0x430510 and 0x4306a0 read and write on the 34.
    CrywolfGlobals* crywolf = nullptr;
};

// 0x4ec110(0x68b181c + bone · 0x30, offset, output, 0): the bone's point in the pose's global buffer, the last posed
// model's (Translate at 0: the bone is already scaled and translated to the world). A bone not yet written keeps the
// .bss zero matrix. Without the borrowed pose, it stops.
[[nodiscard]] Core::Expected<glm::vec3, std::string> ObjectBonePoint(const GameObjectPass& pass, std::size_t bone,
                                                                     const glm::vec3& offset);
} // namespace Client::Kernel
