#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <functional>
#include <string>

// The Crywolf camera tour (CCameraMove, 0x403300..0x404c90; CL9 milestone 11, phases 3 and 5; 06 § El recorrido de la
// cámara). Only the BD 00 with stage 2 starts it (0x4e597f → 0x4033b0), 5000 ms later; no key, command or map entry
// starts it. Then the script (0x4035f0): the camera goes from cell to cell (0x403d90, 0x4034e0) and, in each one, the
// stage creates characters with keys 10000 onwards (0x4039f0, the monster spawn of milestone 2), makes them walk
// (0x403760) and changes their action (0x403900). *Static*.
namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Events
{
struct CameraTour;
} // namespace Client::Events

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Events::Crywolf
{

// What the tour's step (0x403590) asks for outside the object.
struct CameraTourContext
{
    Kernel::WorldState& world;
    Crt::CrtRandom& random;
    // timeGetTime, of 0x4033f0 and 0x404c90.
    std::uint32_t now = 0;
    // 0x5141e0 and 0x4ea030: the spawn of the script's characters and their actions. nullptr stops at the first spawn.
    const Scenes::WorldEntryResources* resources = nullptr;
    // 0x508a80 from 0x403760: the terrain height (0x5f93c0). Empty, the first character that walks stops.
    std::function<double(float, float)> height{};
    // 0x403390, on each step with the tour running: 0x5be150 (the cursor item goes back to its slot) and 0x657880(0x1b)
    // (the panels, as with Esc). Empty stops.
    std::function<Core::Expected<void, std::string>()> releaseTheUi{};
};

// 0x4033d0: +0x158 and +0x6a at 0 and the stage at 2.
[[nodiscard]] bool CameraTourRunning(const CameraTour& tour);

// 0x403300: the constructor's values.
void ResetTheCameraTour(CameraTour& tour);

// 0x403d60: removes the script's characters, from 10000 to +0x14c − 1, with 0x510ff0.
void RemoveTheCameraTourActors(Kernel::WorldState& world, const CameraTour& tour);

// 0x403590, from the game step (0x637138):
//   - 0x4033f0: with stage 2 and the wait, 0x404c90(5000) turns it off; with stage 3, 0x403d60 and 0x403300;
//   - with 0x4033d0 true, 0x403390 and the script of stage 2 (0x4035f0).
// 0x4033d0 is also read by the click (0x59ae24), the UI (0x59ff3f), Esc (0x623ec8), the camera (0x635ead) and the
// world center (0x637324): the last two, with 0x4034b0, in Camera::GameCameraInput::tour (GameScene::CameraOfTheFrame).
[[nodiscard]] Core::Expected<void, std::string> StepTheCameraTour(const CameraTourContext& context);
} // namespace Client::Events::Crywolf
