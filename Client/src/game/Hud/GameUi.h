#pragma once

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/SkillTable.h"
#include "Actors/ActorPickingBox.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Ui/GameUiContext.h"
#include "Ui/OldWindows.h"
#include "Ui/Widget.h"
#include "Terrain/TerrainPickingSegment.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <glm/ext/vector_float3.hpp>

namespace Client::Tables
{
struct ItemSetTables;
}

namespace Client::Input
{
class InputState;
}

namespace Client::Ui
{
struct GameUiState;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Render::Text
{
class ITextMeasure;
}

namespace Client::Tables
{
class TextTable;
}

namespace Client::Ui
{
struct GameUiState;
}

// The game's UI in the step and in the frame, CL8 milestone 1 (06 § HUD y ventanas, "El orden" and "El clic es
// primero de la UI"): the dialogs before the step (0x623e70), the step's UI block (0x636ded..0x63705b) with 0x5ee140
// and 0x59ff10, and the frame's picking (0x59ec80). What belongs to another milestone or wave stops with its address;
// what the original does with no effect in MuRet (empty lists, objects nobody builds) goes as a comment in its place.
namespace Client::Terrain
{
struct TerrainPickingSegment;
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Kernel
{
class WorldState;
struct FloorItemState;
struct WorldRuntime;
} // namespace Client::Kernel

namespace Client::Chat
{
struct GameChatContext;
} // namespace Client::Chat

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Movement
{
struct GameMovementState;
} // namespace Client::Movement

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Hud
{
// 0x553520 for the old windows' keys: the same latch in the step and when handling messages during the draw.
[[nodiscard]] bool OldWindowKeyEdge(Ui::GameUiState& ui, const Input::InputState& input, std::uint8_t virtualKey);

// 0x6578e0: chat/guild/panel fields and GetFocus other than the game window.
[[nodiscard]] bool GameTextInputBusy(const Kernel::WorldRuntime& runtime);


// The old windows' globals over the game's: the mouse's, the UI's, the keyboard focus (the chat input's) and the last
// sound.
[[nodiscard]] Ui::OldWindowGlobals OldWindowGlobalsOf(Kernel::WorldRuntime& runtime);

// What the old windows' step reads from the game, besides the mouse, the keys, the texts and the wire: the arrows
// (GetAsyncKeyState, 0x47b0f0), the whisper (0x4694c0), the bad words (0x622b40 with 0), the hero's name, the saved
// letter (0x4dcb70 from 0x4a11b0), the portrait character (Messenger/LetterPortrait) and 0x78abaac. Used by the UI
// step (0x490490) and the one that runs inside the draw (0x45aa50 from the old windows' draws,
// Ui::OldWindowDrawView::stepView), which must see the same. With `effects` false (MuRet's intermediate draw, on the
// windows' copy), the whisper, the saved letter and the portrait do nothing. The hooks keep what of `context` they
// use, not `context`.
void LendTheGameHooks(Ui::OldWindowStepView& view, const Ui::GameUiContext& context, bool effects);

// 0x637c4a..0x637c5c → 0x623e70: Esc and the open dialog, on every step of scene 5 before its logic.
[[nodiscard]] Core::Expected<void, std::string> StepTheGameDialogs(const Ui::GameUiContext& context);

// 0x636ded..0x63705b: the step's UI block, after the water waves (0x636de8) and before the objects (0x63706a).
[[nodiscard]] Core::Expected<void, std::string> StepTheGameUi(const Ui::GameUiContext& context);

struct FramePickingContext
{
    Kernel::WorldState& world;
    // The frame's rand(): the real one in a frame of the original, the copy in a projected one.
    Crt::CrtRandom& random;
    std::uint8_t& ownRecordDepth;
    // 0x6c8e28, 0x6c8e2c, 0x6c8e30, 0x6c8e34 and 0x6c8e38.
    std::array<std::int32_t, 5>& targets;
    // GetAsyncKeyState(0x12) at 0x59ef37.
    bool altDown = false;
    // The draw's mouse segment (GameScene::MouseRay): the camera (0x7d36834) and the far point (0x7d366c0).
    std::optional<Terrain::TerrainPickingSegment> ray{};
    // 0x7d3682c and 0x7d36828, in 640 × 480, which the party list reads (0x5e4140).
    Ui::Point mouse{};
    // The draw's camera position (0x7d36884), different from the ray's origin.
    glm::vec3 cameraPosition{};
    std::span<Actors::ActorPickingBox> pickingBoxes{};
    // CL9 milestone 5: the draw's floor items (0x77f46c8, or its copy in an intermediate draw) for 0x59e2e0, with the
    // same segment; without it (before the first draw) it does not search.
    std::span<Kernel::FloorItemState> floorItems{};
    // The current map's terrain, whose .map layers the pick reads in Lorencia and Devias. nullptr:
    // Terrain::EmptyTerrain().
    const Terrain::WorldTerrain* terrain = nullptr;
};

// 0x63717f → 0x5ac8d0: the function keys, at the end of the step's logic with the world loaded. The space bar picks up
// the nearest floor item (Items/FloorItemActions, CL9 milestone 5); F1 (0x77f3b08's helps and the set label), F2 (the
// normal chat in the log), F3 (the whisper mode) and F4 (the log's size and place) change their thing and sound. Print
// Screen (0x637cff, the capture) is not from here: it goes after any scene's step (Scenes::Screenshots).
[[nodiscard]] Core::Expected<void, std::string> StepTheFunctionKeys(const Ui::GameUiContext& context);

// 0x637641 → 0x59ec80: the frame's picking, 2D first. The previous chosen (0x6c8e30) stays if it is a monster (+0x88 at
// 2) with auto attack and no buttons; the other places go back to −1. Off the UI, without Alt: the characters of masks
// 0x22 and 1 (reversed with skill 0x1a to 0x1c of the Dark Wizard or the Elf, which opens the own record) into
// 0x6c8e30, with none those of mask 4 (the NPCs) into 0x6c8e2c (PickTheCharacter, 0x59e700) and, with no NPC, the floor
// item (0x59e2e0, CL9 milestone 5); with Alt, the floor item first and then 4, 0x22 and 1. The objects (0x59eb90)
// leave −1. The party (CL9 milestone 7, Party/PartyActors.h): without Alt, before searching, the list on the right
// (0x5e4140), which can choose a member; with the mouse on the UI, with what the list chose or with a chosen one and
// 0x6c8e38 set, 0x59e3b0 searches the members in the viewport. At the end, with no chosen one, 0x6c8e38 at −1 (except
// when the second character attempt hits: it returns earlier).
[[nodiscard]] Core::Expected<void, std::string> PickInTheFrame(const FramePickingContext& context);
} // namespace Client::Hud
