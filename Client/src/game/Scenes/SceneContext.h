#pragma once

#include "Kernel/PacingMode.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/ITextureStore.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <filesystem>

namespace Client::Camera
{
struct GameCamera;
struct GameCameraInput;
} // namespace Client::Camera

namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Session
{
struct ModuleStates;
} // namespace Client::Session

namespace Client::FrontEnd
{
class SelectionState;
} // namespace Client::FrontEnd

// What the scenes share. main.cpp builds it and it outlives the SceneManager; the tests, with a FakeTextureStore
// (support/). What was read before the first scene goes apart, in GameData.h.
namespace Client::Scenes
{
struct WorldEntryResources;

// Where they read from (the client's Data\), where they upload their textures (Render::ITextureStore), the log and
// the loop's clock.
struct SceneContext
{
    std::filesystem::path dataRoot;
    Render::ITextureStore& textures;
    Core::Log& log;
    // SceneManager's: what the original reads from DAT_05767b88 in the logic, like the login character's action
    // (0x634c60). nullptr without a loop (the tests that do not look at it): the scene acts as if there were none.
    Core::IClock* clock = nullptr;
    // The cloth's globals (Models/Cloth.h): the base wind, the logic step counter and the CRT's rand(), which scenes 2,
    // 4 and 5 advance on each step (0x637700). nullptr without a loop: the characters go without cloth.
    Models::ClothWorld* cloth = nullptr;
    // The loop's keyboard and mouse, those of each step: what the original reads in the draw from the WndProc's
    // globals, like the cursor (0x5ac2d0, Ui/GameCursor.h). nullptr without a loop: no cursor.
    const Input::InputState* input = nullptr;
    // The loop's pacing (Kernel/PacingMode.h): in Faithful, what the original runs once per drawn frame (the cloth and
    // the scepter's glow) runs once per draw (OriginalFramePaceOf).
    Kernel::PacingMode pacing = Kernel::PacingMode::Decoupled;
    // 0x635ae0's global state: it lives in main and survives the scene changes.
    Camera::GameCamera* camera = nullptr;
    // 0x635ae0's inputs, not a full copy of the Hero actor. SelectionState publishes the position each reception
    // builds; the selection draw writes Hero Z = 163 after the camera. L1 will bring the reply's XYZ for the game.
    Camera::GameCameraInput* cameraInput = nullptr;
    // The selection on the durable pool's first five records (WorldState) and the current world tables. main owns both
    // and they outlive all the scenes; it keeps the login's tables after its Leave.
    FrontEnd::SelectionState* selection = nullptr;
    // What WinMain zeroes and lasts until closing (WorldState.h): the own record and the globals of the world entry
    // wait (WorldRuntime). It is the same one `selection` lends. nullptr in the tests that do not look at it: the
    // selection enters without preparing the own record, and the world loading and the game do not enter (their Enter
    // gives the error), because the wait lives there.
    Kernel::WorldState* world = nullptr;
    // The GetTextExtentPointA of the texts' DC (0x57634f4) outside the draw: the mouse over the chat record's names
    // (0x45dec0) and the messages' cuts. nullptr without a loop: it does not measure.
    const Render::Text::ITextMeasure* textMeasure = nullptr;
    // Those of the world replies (SessionEventApplier), which main builds once: the game uses them to pick up, drop and
    // equip (InventoryServicesOf). nullptr in the tests that do not look at it: what needs to dress the hero, the raven
    // or the messages stops.
    const WorldEntryResources* worldEntry = nullptr;
    // The main thread's clipboard for the EDITs; nullptr in tests that do not edit text.
    Platform::IClipboard* clipboard = nullptr;
    // What each module owns of the original's globals, beside `world` (ADR 0029). nullptr in the tests that do not
    // look at it: the selection and the game do not enter, as without `world`.
    Session::ModuleStates* states = nullptr;
};

// How the cloth and the scepter count the original's frames with the pace `pacing`.
[[nodiscard]] constexpr Models::OriginalFramePace OriginalFramePaceOf(Kernel::PacingMode pacing)
{
    return pacing == Kernel::PacingMode::Faithful ? Models::OriginalFramePace::OncePerDraw
                                                  : Models::OriginalFramePace::ByTheClock;
}
} // namespace Client::Scenes
