#pragma once

#include "Audio/MusicPlayer.h"
#include "Audio/Playback.h"
#include "Audio/SoundId.h"
#include "Kernel/ISoundRequests.h"
#include "Tables/MonsterSkillTable.h"
#include "Tables/DialogTable.h"
#include "Tables/MoveRequirements.h"
#include "Tables/NpcNames.h"
#include "Actors/MovementSpeed.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Chat/ChatBubbles.h"
#include "Events/EventGlobals.h"
#include "Events/EventMapObject.h"
#include "Actors/FullSetScan.h"
#include "Movement/GameMovementState.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Guild/GuildState.h"
#include "Items/HudItems.h"
#include "Tooltips/ItemHelpLevels.h"
#include "Hud/MapBanner.h"
#include "Sound/MonsterSounds.h"
#include "Monsters/NpcNames.h"
#include "Npc/NpcWindowState.h"
#include "Quests/QuestState.h"
#include "Kernel/SceneId.h"
#include "Social/SocialState.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldTextQueue.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Ui/SlideBanner.h"
#include "Chat/WhisperSenders.h"
#include "Kernel/ItemState.h"
#include "Kernel/ChosenNpc.h"
#include "Kernel/MouseState.h"
#include "Weather/LeafWeather.h"
#include "WorldObjects/ObjectModelStepState.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

// The plain globals that survive scene changes: the current world, the shared speed state and what the wait of the
// world entry needs (06, "La entrada al mundo", steps 1 to 4 and 6). They start at 0, like the original's .bss; the
// access counts of the protection table, which the original keeps beside each entry, MuRet sets to 0 without having
// read them (CL7.P, P-C22 c).
namespace Client::Kernel
{
// An entry of the health bars' queue (0x7239a8, 0x10 bytes): the character's position with z over the top of its box
// (+0x18 + +0x138 + 100) and its +0x93, the low byte of the fourth DWORD (the other three are what was on the stack,
// which 0x42a53d discards).
struct QueuedHealthBar
{
    glm::vec3 position{};
    std::uint8_t fill = 0;
};

// .data[0x6c8c5c] = 1e 00 00 00; the weather's other four fields are in the .bss.
inline constexpr std::int32_t InitialLeafCount = 30;

// The Crywolf interface's globals that the BD 00 (0x42fe80), the BD 02 to 09 and the drawing (0x4365c0) write, and that
// 0x42fdc0 puts back to their initial value (not all: the .data ones at 0x6c73cc.., the rest in the .bss).
struct CrywolfInterface
{
    // 0x723a40: a float (FLD, FADD and FSTP at 0x4370d6 and 0x4372dc), how far the state's ornament 0x6b8 drops: it
    // rises by 1 up to 21 with 0x723a3d at 1 and goes back by 1 to 0 with 0x723a3d at 0. 0x42fdc0 writes the dword 0.
    float float723a40 = 0.0f;
    // 0x723a3d: 0x6b7's bar (with 0x723a40 at 21); the BD 05 shows and hides it by its dword's sign.
    std::uint8_t byte723a3d = 0;
    // 0x723a44: the altars' contract window (06 § La interfaz de Crywolf), 1 with Yes and No or 2 with OK. 0x435b00
    // opens it; the drawing (0x436240, with the button pressed under the mouse), the results window (0x436b15) and
    // 0x42fdc0 close it.
    std::uint8_t byte723a44 = 0;
    // 0x723a48 and 0x723b10: that window's two 200-byte lines, which 0x435b00 fills (strcpy from Text.bmd or
    // wsprintfA) and 0x436240 draws. 0x42fdc0 does not touch them.
    std::array<std::array<char, 200>, 2> contractLines{};
    std::uint8_t byte723ca0 = 0;
    std::uint8_t byte723bec = 0;
    // 0x723bd8: the BD 05's pkt[8] (0x435a8e): text number 1948 and icon 0x6bb (or 0x6bc with 0).
    std::int32_t dword723bd8 = 0;
    // 0x723bdc, 0x723be0, 0x723be4 and 0x723be8, at 0 in 0x42fdc0. The first is the pressed button: 5 the results
    // one, which the drawing closes with the mouse over it (0x436ad8), and 1 (No), 2 (Yes) or 3 (OK) the contract's
    // (0x435c5c, 0x436274); the second, 0x435b00's value (the altar's key, which Yes sends); the third and the fourth
    // are the BD 04's pkt[4] and pkt[5] (0x435a46, with stage 4 on map 34), the fourth the state clock's minutes.
    std::array<std::int32_t, 4> dwords723bdc{};
    // 0x7239f4: the BD 04's GetTickCount (0x435a51), which the state clock counts from.
    std::int32_t dword7239f4 = 0;
    // 0x6c73d1 (.data 0xff): the notice; with −1 (signed) 0x4365c0 does not draw the banner (0x436bc7).
    std::int8_t message = -1;
    // 0x6c73d4 (.data 0x64): the BD 05's dword (0x436590), the length of bar 0x6b7.
    std::int32_t dword6c73d4 = 0x64;
    // 0x6c73d8 (.data 1): the banner's frame count.
    std::int32_t dword6c73d8 = 1;
    // 0x6c73dc (.data 0xa): the banner is 0x6b6 + this (0x6c0, icon_failure; 0x6c1, icon_success with state 1).
    std::int32_t dword6c73dc = 0x0a;
    // 0x6c73e0..0x6c73f4, six dwords at −1: the first, 0x430180's tip (0 to 3); the other five, the results' points
    // (0x6c73e4 + 4 · place, from the BD 08), with −1 an empty row.
    std::array<std::int32_t, 6> dwords6c73e0{-1, -1, -1, -1, -1, -1};
    // 0x6c73f8 (.data 0x186): how far the rank (0x6d1) still has to go; it drops by 15 per frame.
    std::int32_t dword6c73f8 = 0x186;
    // 0x723c08: the light of state 2's two 0x4ecc30 (0x430a33) and the length of bar 0x6bf; the BD 02 writes it.
    std::int32_t light = 0;
    // 0x6c73cc..0x6c73d0 (.data 02 × 5): the BD 02's five altars (0x430000): with 0x1? in the high nibble, 0x6cd,
    // 0x6ce or 0x6cf by the low one (1, 2 or other); otherwise 0x6bd or 0x6be with 1 or 2, and nothing with other.
    std::array<std::uint8_t, 5> altars{2, 2, 2, 2, 2};
    // 0x723bed and 0x723bf0: the BD 07's rank (0x6d2 + this) and experience (nine digits), 0x437600.
    std::uint8_t byte723bed = 0;
    std::int32_t dword723bf0 = 0;
    // 0x723a30 + place and 0x7239f8 + 11 · place: the results' class and name (10 bytes and the 0), 0x437620.
    std::array<std::uint8_t, 5> rankingClasses{};
    std::array<std::array<char, 11>, 5> rankingNames{};
    // 0x723bf9: the results window, which the BD 08 opens and the button closes (0x436b21).
    std::uint8_t byte723bf9 = 0;
    // 0x723bf8: at 1 with the BD 04 and with each drawing of the state (0x4373fb).
    std::uint8_t byte723bf8 = 0;
    // 0x723bf4, 0x723bfc and 0x723c00 (.bss 0): the minutes left, the seconds gone (0 to 59) and the last second
    // counted from 0x7239f4 (0x437412..0x437490).
    std::int32_t dword723bf4 = 0;
    std::int32_t dword723bfc = 0;
    std::int32_t dword723c00 = 0;
    // 0x7d3a0e4 and 0x7d3a0e8 (.bss 0): 0x63b0e3's timer, which only 0x430180 asks for (every 10,000 ms of
    // timeGetTime).
    std::uint8_t tipTimerRestart = 0;
    std::uint32_t tipTimerStart = 0;
    // 0x7d2dc9d: the BD 09's pkt[4] (and 0x4e1629's); the chaos box reads it (0x5ff2f2, CL9.3) and 0x65a94f clears
    // it.
    std::uint8_t byte7d2dc9d = 0;
};

// Crywolf's globals (map 34), all in the .bss.
struct CrywolfGlobals
{
    // 0x723c05: the stage the chat's drawings read (with 3 or 4 their lines drop). Only the BD 00 (0x42fe80, CL9
    // milestone 11) and 0x42fdc0 (the return to the selection and stage 6) write it.
    std::uint8_t stage = 0;
    // 0x723c04: the event's state, which 0x42fe50 reads. Only the BD 00 changes it (0x42fe80, CL9 milestone 11) and
    // 0x42fdc0 resets it to 0. With 0, map 34's load takes EncTerrain35.att and TerrainLight (0x61a560), and the
    // drawing of its objects takes state 0's cases (Q-C340).
    std::uint8_t state = 0;
    CrywolfInterface ui{};
    // 0x5763358: the weather, rand() % 3 every 70,001 frames of its objects (0x430510). The leaves (0x530490 and
    // 0x5306a0) and the drawing's 0x49 (0x430942) read it.
    std::int32_t weather = 0;
    // 0x723c0c: the objects' frames since the last weather draw (0x430523).
    std::int32_t frameCount = 0;
    // 0x723c10: the spin of 0x29's second sprite 0x60f, 0 to 359 (0x4305db).
    float glowRotation = 0.0f;
    // 0x576353c: the 0x49 sets it to 1 with the weather at 0 (0x430950); the HUD spends it (0x5a7520 → 0x435930,
    // CL8), which MuRet does not have.
    std::uint8_t sandOverlay = 0;
};

// The chat (CL8 milestone 3; 06 § El chat), which lives from the login to the game: the new log (0x576350c's object)
// and the old area and the notices (0x77d9868 and 0x77f3b88 with their clocks). What its drawings write goes in
// HudState.
struct ChatRuntime
{
    Chat::ChatLogState log{};
    Ui::MessageAreaState area{};
    // 0x77f3b78 (.bss 0): the sound of the filter's notices. The F3 30 (0x4dd570) and the Esc menu (0x62567b) change
    // it.
    bool noticeSound = false;
    // 0x77f3b98 (.bss 0): with 1, the 02 does nothing (0x4e34f1). /whisper off and on and the Esc menu write it.
    bool whisperBlocked = false;
    // 0x57678c4: the 11 bytes of the last outgoing whisper's target (0x4b87e9, on sending). The 0C with 0 names the
    // error with them (0x4e3684); with none, empty like the .bss.
    std::string lastWhisperTarget{};
    // 0x77997f8 and 0x77f3b9c: those who whispered while the own level was below 10 (0x554d20).
    Chat::WhisperSenders whisperSenders{};
    // 0x77e1be0: the bubbles.
    Chat::ChatBubbleTable bubbles{};
    // +0x510 and +0x511 of the selection's bar (the new UI's window +0x7f50, 0x7d53728): the first 0D with 1 that
    // arrives in the selection (0x63eda0), never written again because the bar is built once (0x63e6e0). Empty, +0x510
    // at 0.
    std::optional<std::string> selectionNotice{};
    // 0x6c7a8c (.data 1): Slide.bmd's banners on. The F3 30's bit 0x10 turns it off (0x4dd7fa).
    bool slidesOn = true;
    // The chat input (the EDITs of [0x57634f8], their buffers, the throttle and the macros).
    Chat::ChatInputState input{};
};

// The game HUD's globals (0x5a7320 and its children, CL8 milestone 2), in the .bss unless they say otherwise.
struct HudState
{
    // 0x78aba48: how many are in the party. Opening the window zeroes it (0x659aa0) and the list writes it (C1 42,
    // 0x4d67e0, CL9.4).
    std::int32_t partyCount = 0;
    // 0x78a7068 and 0x78a706c: the party window's corner, which its drawing (0x5f0be0) sets with the window open, at
    // (0x1c2, 0). Its step (0x5e3c80) reads it and sees the .bss zeros before the first drawing (Q-C650).
    std::int32_t partyX = 0;
    std::int32_t partyY = 0;
    // 0x78a5a1c and 0x78a5a18: the trade window's corner, which its drawing (0x5f3b40) sets at (0x104, 0). Its step
    // (0x5ecde0) reads it and sees the .bss zeros before the first drawing (Q-C650).
    std::int32_t tradeX = 0;
    std::int32_t tradeY = 0;
    // +0xc and +0x10 of [0x6e592c]: the commands window's corner, which its drawing (0x40a650) sets with the window
    // open, at (0x1c2, 0). Its step (0x40a450) reads it and sees the constructor's zeros before the first drawing
    // (*inferred*, Q-C650).
    std::int32_t commandsX = 0;
    std::int32_t commandsY = 0;
    // 0x5767960: the summon's life, 0 to 100. Its packet writes it (CL9.2).
    std::int32_t summonLife = 0;
    // 0x77f3b74: the macros' throttle. One that reaches it sets it to 0x64 (0x5a1ea1) and 0x637d44 lowers it by one
    // per step; above 0 none goes out (0x5a116f) and the HUD draws "Macro Time" (0x5a7369).
    std::int32_t macroTime = 0;
    Items::QuickSlots quickSlots{};
    // 0x6c8e5c (.data −1) and 0x6c8e64 (.data 0x80000000): the text color and the background color 0x465950 reads.
    // Each HUD text leaves its own, and the next one that sets none comes out with them, also in the next frame.
    Render::Text::TextColors textColors{.text = 0xffffffffu, .background = 0x80000000u};
    // 0x77f3b66: a durability warning (0x5aac20) and a bubble (0x555b00) set it, and the bar (0x5a9290) clears it.
    // 0x554000 reads it to shift the texts' y (MuRet, only in the bubbles: 08, P-C83), and so does the inventory
    // (milestone 4).
    std::uint8_t durabilityWarning = 0;
    // 0x77f31b8 and 0x77f31bc: the 640 × 480 size of the last text that stored it: the arrows or the bolts
    // (0x5ab810), the old area (0x5554e0) and the notices (0x554810); a bubble's (0x555b00) is in pixels.
    std::int32_t arrowsTextWidth = 0;
    std::int32_t arrowsTextHeight = 0;
    // 0x77f3b5c: with the left button down, cursor 0xa (CursorDontMove) instead of 3 (0x5ac7a2). Who writes it,
    // unread: it stays at 0.
    std::uint8_t pushCursor = 0;
    // What the chat's drawings write (milestone 3): the log's frame (the step sets its corner), the widths and the
    // blinks of the old area and the notices.
    Chat::ChatLogFrame chatFrame{};
    Ui::MessageFrameState messageFrame{};
    // The font of the last SelectObject on the texts' DC (0x57634f4), which 0x45bb20, 0x555190 and 0x554610 measure.
    // 0x61bcd0 creates the DC with none (the system's) and the login draws with one before the first message: MuRet
    // starts with the normal one.
    Render::Text::FontKind dcFont = Render::Text::FontKind::Normal;
    // 0x78a5a14 and 0x78a5a10: the inventory's corner, which its drawing (0x5f2340) sets each frame: x at 0x104 with
    // the character or the party open and at 0x1c2 otherwise, and y always at 0. The 3D items of its grids (0x5f7610)
    // and the figures (0x5f7840) of the same frame read them (CL8 milestone 4).
    std::int32_t inventoryX = 0;
    std::int32_t inventoryY = 0;
    // 0x78aba14 and 0x78aba18: the character window's corner, which its drawing (0x5ee2e0) sets each frame, at (0x1c2,
    // 0). Its step (0x5e4510) and its close button (0x5f74b4) read them, CL8 milestone 5.
    std::int32_t characterX = 0;
    std::int32_t characterY = 0;
    // 0x78aadcc..0x78aadd4 (.bss, at 0): the angles 0x5e0720 poses each UI item with. It writes them on each item and
    // they last to the next item and the next frame: the 0x1e1e with a step other than 0, 1 or 2 poses with the
    // previous one's (Q-C478).
    glm::vec3 uiItemAngles{};
    // [0x5763528]: Slide.bmd's banner (0x46aea0), which the 15 draws (0x46b090) and writes while drawing (the opacity,
    // the shift, the scrolling text and 0x5762ee4). The 0x3eb timer and the 0D fill it, and the UI step advances it.
    Ui::SlideBanner slides{};
    // [0x5763534]: the banner with the map's name (Scenes/MapBanner.h), which the 19 draws (0x65dd70).
    Hud::MapBanner mapBanner = Hud::MakeTheMapBanner();
    // 0x78793c8, 0x787a560 and 0x78a9230: the tooltips' lines, their colors and their bold flags (CL8 milestone 5).
    Tooltips::TooltipLines tooltip{};
    // 0x787a15c and 0x6c9334: the item help's per-level table and its item (−1 in the .data), which 0x5afc00 fills.
    Tooltips::ItemHelpLevels itemHelp{};
};

// 0x78ab9b8, the cursor item's grid: a pointer in the .bss (0 at start). Picking up from a grid (0x5c52e2, with that
// grid) and from the equipment, and MoveHero (0x5c2d37 and 0x59bf32, with 0x78a9bb0) write it. MuRet picks up from the
// inventory, the trade and personal shop grids (CL9 milestone 7, 0x5c4110) and the NPC shop, warehouse, chaos box and
// trainer grids (CL9 milestone 6). 0x5be150 returns the item to the own trade grid and to the shop's (0x5be191 and
// 0x5be3eb); the other's (0x78a5a20) has no branch and goes on like the inventory's.
enum class CursorGrid : std::uint8_t
{
    None,         // 0
    Inventory,    // 0x78a9bb0
    OwnTrade,     // 0x78a92a8
    OtherTrade,   // 0x78a5a20
    PersonalShop, // 0x78ab0b8
    NpcGrid,      // 0x78a7070
    ChaosBox,     // 0x7d2d380
    TrainerBox,   // 0x723c80
};

// The pets of 0x723960 (CL9 milestone 4): 0x14 bytes per type, three (0x420cd0 zeroes fifteen dwords).
inline constexpr std::size_t PetStatsCount = 3;
inline constexpr std::size_t RavenPetStats = 0;
inline constexpr std::size_t DarkHorsePetStats = 1;

struct PetStats
{
    std::uint32_t experience = 0;     // +0: the A9's DWORD (pkt[8])
    std::uint32_t nextExperience = 0; // +4: (level + 11) · (level + 1)³ · 100, in 32 bits (0x4221d0)
    std::uint16_t level = 0;          // +8: pkt[6]
    std::uint16_t durability = 0;     // +0xa: the durability of the own record's horse (+0xdc6) or of the label
    // +0xc to +0x12: what 0x4221d0 computes from the level, the leadership (+0x36 + +0x20 of the own record) and, for
    // the horse, the strength and +0x2a. The pet window (0x4249a0) and its tooltip (0x422990) read them.
    std::uint16_t word0c = 0;
    std::uint16_t word0e = 0;
    std::uint16_t word10 = 0;
    std::uint16_t word12 = 0;
};

// 0x723914, 0x723918 and 0x72391c: the last item the pets' tooltip (0x422990) asked the A9 for. If the item it shows
// is not that one (another place in the box, another type or another D+4), it notes it, asks for the A9 and draws
// nothing that frame. They start at 0 (the .bss). 0x421d90, from the trade (0x44b910, CL9.3), zeroes eighteen dwords
// from 0x723918: the type and D+4, not the key.
struct PetTooltipQuery
{
    // ((y & 0xffff) << 16) | (x & 0xffff), with 0x422990's x and y: the box's top center.
    std::uint32_t key = 0;
    std::uint16_t type = 0;
    // The item's D+4 (Kernel::ItemState::scalar1Low).
    std::uint32_t scalar = 0;
};

// A node of [0x6e5640]'s named bone list (0x401000 → 0x4011c0): the character (its pool place), the name the
// monster's creator gives it and its model's bone. The effects read it (CL11).
struct NamedBone
{
    std::size_t actor = 0;
    std::string name{};
    std::uint32_t bone = 0;
};

// The floor items' globals (CL9 milestone 5; 06 § Los ítems del piso).
struct FloorItemGlobals
{
    // 0x77f27b0 (.bss 0): the floor item about to be picked up. The bar (0x5ac9a1) and MoveHero (0x59dc8e) write it;
    // 0x5650c0 (its case 1) and the 22's notice (0x4d0f5f) read it.
    std::int32_t chosen = 0;
    // 0x77f3b40 (.bss 0): at 1 only while the bar calls 0x5650c0 (0x5ac9a7..0x5ac9bc), which then skips the distance
    // to the target.
    std::uint8_t fromSpace = 0;
    // 0x6c82e8 (.data −1): the item asked for with the C1 22. While it is not −1, 0x5650c0 asks for no other; the 22
    // (0x4d10b9) and 0x4c2480 (0x4c24cf) set it back to −1.
    std::int32_t pendingPickUp = -1;
    // 0x6c82ec (.data −1): the place of the item dropped with the C1 23 (0x5e068d). The 23 sets it back to −1
    // (0x4d13d1, 0x4d13e5) and nobody reads it.
    std::int32_t pendingDrop = -1;
};

struct WorldRuntime
{
    // 0x41ed10: the request goes to Sound through the port, and lastSoundId keeps it (CL10).
    void RequestSound(Audio::SoundId id, std::optional<glm::vec3> origin = std::nullopt, bool loop = false)
    {
        lastSoundId = id;
        if (sounds != nullptr)
            sounds->Request(id, origin, loop);
    }
    void StopSound(Audio::SoundId id, bool rewind)
    {
        if (sounds != nullptr)
            sounds->Stop(id, rewind);
    }
    void SetSoundVolume(std::uint8_t level)
    {
        if (sounds != nullptr)
            sounds->SetEffectsVolume(level);
    }
    // Q-C180/Q-C27: the first data notice that asked for WM_DESTROY. It lasts even if Poll fails; main shows the modal
    // before closing. Unsupported-map and errors without a notice do not write it.
    std::optional<std::string> fatalDataMessage{};
    Actors::Drawing::GameItemRenderState itemRender{};
    // The panels' bytes (0x7d53708..0x7d53718 and the globals their functions write), the dialogs (0x7d3a10c,
    // 0x7d3a110) and the rest of the game UI (CL8 milestone 1): the .bss, which nobody clears on a scene change.
    Ui::PanelState panels{};
    Ui::DialogState dialogs{};
    // What the castle packets keep outside the battle (CL9 milestone 12): the .bss 0x7d5c504..0x7d5c658, the fields of
    // [0x7d536f8] and [0x7d53700] they write, and 0x78aba30/0x78aba34.
    CastleSiege::CastleSiegeState castleSiege{};
    // The guild, the alliance and the war (CL9 milestone 8): the guild table (0x787aa48), the members (0x787a5e8), the
    // war's globals and what the packets keep in the two guild windows.
    Guild::GuildState guild{};
    // 0x7d36a98, 0x7d368e4, 0x7d368b8 and 0x7d3a0f4..0x7d3a0f9: the old boxes' text and buttons (milestone 6).
    Ui::DialogBoxState dialogBox{};
    Ui::GameUiState ui{};
    // The NPC windows' grids and globals (CL9 milestone 6).
    Npc::NpcWindowState npcWindows{};
    // *(0x5763514), the old windows' manager, and [0x5763524]'s window list (milestone 7): WinMain builds them and
    // they last the whole game.
    Ui::OldWindowManager oldWindows{};
    HudState hud{};
    ChatRuntime chat{};
    MouseState mouse{};
    Movement::GameMovementState movement{};
    // The five mouse selectors start at -1 in the .data.
    std::array<std::int32_t, 5> selectedTargets{-1, -1, -1, -1, -1};
    ChosenNpc chosenNpc{};
    // 0x723c18: the 3D texts' queue (WorldTexts.h). 0x4381f0 fills it from the characters' body (0x4fd603, the 0xc5)
    // and its drawing (0x4383f0) empties it each game frame.
    WorldTextQueue worldTexts{};
    // 0x723c68 (+8, the list; +0xc, the count): movereq.bmd's destinations, which 0x448440 reads at load (0x61ccb0)
    // and the /move window walks in its step (0x448580) and drawing (0x448fa0). main.cpp copies them from GameData.
    std::vector<Tables::MoveRequirement> moves{};
    // [0x710148]: the quests' object (CL9 milestone 2). main.cpp copies quest.bmd's records (GameData::quests).
    Quests::QuestState quest{};
    // 0x7688ac0: Dialog.bmd's 200 dialogs, which main.cpp copies from GameData::dialogs. Empty, showing one aborts.
    Tables::DialogTable dialogTable{};
    // 0x7681eb8 (CL9 milestone 2): NpcName(%s).txt's 512 names, which 0x54ebb0 looks up (the quest window's title,
    // 0x41be2c) and the creation's tail (0x5140c0) copies to the name. main.cpp copies GameData::npcNames into them
    // (0x61ed05); without the file they stay at 0.
    Monsters::NpcNameTable npcNames = Monsters::NpcNameTable(Tables::NpcNameCount);
    // 0x6eeda8: the A0 sets it to 0 and reaching an NPC (0x569593) to 1 if it is 0xe5. The quests' dialog drawing
    // (0x41ba20) reads it with class 3 or 4.
    std::uint8_t byte6eeda8 = 0;
    // 0x7879f80: the cursor item, a 0x48-byte ItemState in the .bss. 0x5f7b10 draws it (CL8 milestone 4).
    Kernel::ItemState cursorItem{};
    // 0x7879fbc, cursorItem's +0x3c (occupied): above 0 with an item on the cursor. MuRet keeps it apart, and whoever
    // picks up or drops the item writes both.
    std::int32_t cursorItemCount = 0;
    FloorItemGlobals floor{};
    // 0x78ab9b8: the grid the cursor item came from (CL8 milestone 4).
    CursorGrid cursorGrid = CursorGrid::None;
    // 0x78a59c8: the dialogs' item (0x633110 draws it, 0x63330f names it, 0x623e70 uses it), with the grid cell in
    // byte +9 (0x78a59d1), padding in the ItemState. The fruit writes it (0x5c8083, CL9 milestone 5).
    Kernel::ItemState dialogItem{};
    std::uint8_t dialogItemCell = 0;
    // 0x77f3cc8: the place the outgoing C1 .. 24 goes to (0x5ca4b0, 0x5bf209, 0x59bf46).
    std::int32_t itemMoveTarget = 0;
    // 0x787a518: the copy of the item under the mouse that the label reads (0x5c42a7 and 0x5c3aba). Picking up an
    // item sets its type to 0xffff (0x5c5358, 0x5c2d74).
    Kernel::ItemState hoveredItemCopy{};
    std::int32_t storm = 0;
    std::int32_t field78abb10 = 0;
    std::uint8_t byte6c8e4d = 0xff;
    std::optional<Audio::SoundId> lastSoundId{};
    WorldObjects::ObjectModelStepState objectModelState{};
    // 0x7d365a0, .bss 0. 0x6061c0 reads the retained count before 0x60657a; 0x6063d0 produces it only for Icarus,
    // Chaos Castle and the siege. Kalima keeps 0 after the successful pass's reset.
    std::int32_t objectSelectionCount = 0;
    // 0x723c4c, .bss 0: the system time (timeGetTime) of Land of Trials' last 0x1ef ambience (0x44016b).
    std::uint32_t trialsAmbientTime = 0;
    // DAT_00723c60, the event object 0x447d70 builds. Empty like the .bss; only 0x447d70 frees it (0x447eb0), and only
    // to build another: leaving the event for a common map, it stays alive (Q-C320).
    std::optional<Events::EventMapObject> eventObject{};
    // 0x6c634c (.data 0xff): the Chaos Castle area that 0x404e20 and 0x405e00 read (Q-C229); 0x404d60 sets it back to
    // 0xff.
    std::uint8_t chaosCastleArea = 0xff;
    // 0x6c634d (.data 1): the Chaos Castle trap armed. 0x4055b0 spends it on the first 3 seen; only Chaos Castle's 9B
    // (0x404e45, CL9 milestone 11) arms it again.
    std::uint8_t chaosCastleTrapArmed = 1;
    // The rest the events' packets write (CL9 milestone 11).
    Events::EventGlobals events{};
    // Crywolf's part that the load, its objects and its leaves read and write.
    CrywolfGlobals crywolf{};
    // 0x5763538 (.bss 0): 0x6046a0's counter, which wraps from 1 to 41 at the start of each objects' drawing, in the
    // login, the selection and the game (0x6046be..0x6046ce). Only Crywolf's 0x29 reads it.
    std::int32_t objectDrawCounter = 0;
    // DAT_006c9dd0, the current world. Whoever calls 0x61a560 writes it just before (05, "La carpeta de cada mapa"):
    // the login with 55 (06, "El login, al entrar") and the selection with 54 in 0x633f20 (06, "Al entrar"). That 54
    // stays in the world's load and in scene 5's wait, which runs with what the selection left until the F3 03 reply
    // writes the game's (06, "La entrada al mundo", step 1 and row 1).
    // The −1 before the first write is the one MuRet already used; the original's initial value is unread. With −1,
    // 0x511290 does not add the pet's shift (06, CL7.3 B).
    std::int32_t map = -1;
    // 0x7d3a0d0 and 0x7d3a0d4, Fenrir's: one for all characters (06, "Caminar", the speed). 0x4f2250 zeroes the first
    // (0x4f22a4). The DTO's zeros do not claim the original's initial value.
    Actors::MovementSpeedSharedState movementSpeed;

    // DAT_07d3a101, the world loaded: at 0, the game step leaves at 0x636db5 and its drawing returns 0 at 0x6371a6,
    // without reading the counter. The selection's load of 54 (0x633f30, inside 0x633f20) and the game step with state
    // 0x3d (0x636dac) set it to 1, and only the F1 02 to 0 (0x4c2f39 with 2 and 0x4c38f9 with 1, which MuRet follows:
    // ApplyReturnToSelection). The login also sets it to 1 when it loads 55 (0x6349ab); MuRet does not write it there
    // because the game, its only ported reader, always comes after 0x633f20.
    bool drawingEnabled = false;
    // DAT_006d5dc8 written by a packet: the F1 02 with 1 sets 4 (0x4c363f) while the network is read, and the
    // original's next step is already the selection's. MuRet leaves it here and the SceneManager takes it after the
    // Poll (main.cpp) and enters it on its next step. Empty, nobody asked for it.
    std::optional<Kernel::SceneId> orderedScene{};
    // DAT_07d3a13e: whether the world's load (0x6356f0) already set up the counter and uploaded its textures. The
    // selection clears it just before going to scene 3 (0x633efe, 0x634469, 0x634631) and 0x6356f0 sets it
    // (0x635980). Nothing else writes it.
    std::uint8_t loadingOnceFlag = 0;
    // DAT_077f3b04, the counter of the wait and of the gates, which lives in the protection table (06, "La tabla de
    // protección"): 9,999,999 in scene 3 and in a gate request, 30 in the F3 03 reply, and it drops by 1 in MoveHero
    // (0x59af01). Above 30 the game's drawing does not present (0x63722e).
    std::int32_t warpCounter = 0;
    // How many accesses hold the counter open: the byte after its encrypted copy.
    std::uint8_t warpDepth = 0;
    // How many accesses hold the own record open ([0x7681eb4], 0xf08 bytes).
    std::uint8_t ownRecordDepth = 0;
    // 0x723960: what the A9 writes for each pet (0x421db0 and 0x4221d0, Protocol/Pets/PetInfo.h), by type: 0 the
    // raven and 1 the horse. 0x420cd0 zeroes the three. The horse's level ([0x72397c]) is read by 0x4225d0 (the low
    // byte) and 0x422930; the raven's ([0x723968]) by the leadership requirement of 0x1a5 (0x5bf027) and 0x422930.
    std::array<PetStats, PetStatsCount> pets{};
    // 0x723910: the pet window's tab (0 the horse, which 0x424e40 draws, and 1 the raven, 0x416670), which
    // 0x424759's two buttons choose. The 0x723911 next to it is Ui::DelegatedPanels::pet.
    std::uint8_t petTab = 0;
    // 0x723914, 0x723918 and 0x72391c: which item the pets' tooltip (0x422990) asked the A9 for.
    PetTooltipQuery petTooltipQuery{};
    // DAT_057678b0: the pool place the F3 03 reply picks for the hero, rand() % 400 (0x4c44d4, the only write; 06,
    // "La entrada al mundo", row 3). Each reply draws it again.
    std::int32_t heroSlot = 0;
    // DAT_0743a170, the published hero, as a pool place: WinMain leaves it at 0 (0x4bafdf) and the F3 03 reply moves
    // it to heroSlot's only after dressing it with 0x512a30 (0x4c4725). Nothing else writes it: on a second reply,
    // until then it is the previous hero, and after the return to the selection it stays on the game's place (Q-C213).
    std::size_t hero = 0;
    // [0x743a178] and [0x743a17c], what the last 0x505820 left (06, "El conjunto completo de 0x505820"): in the .bss,
    // 0 at start. 0x512a30 writes it with the published hero, and so do the characters' drawing (0x50ba30 at
    // 0x50c91e), the login's and the selection's (ScanTheDrawnCharacter). nullopt after a game frame that reaches the
    // characters (0x6374dd → 0x510d00), which MuRet does not draw: what it left is unknown, and 0x550990 stops with an
    // error if it reads it.
    std::optional<Actors::Scan505820> fullSetScan = Actors::Scan505820{};
    // The trade, the party and the duel (CL9 milestone 7).
    Social::SocialState social{};
    // [0x78a6758], in the .bss: 0 at start. 0x5bc740, after putting an item in one of the inventory grids, compares it
    // signed with 12 (0x5bcd0b and 0x5bd08b) and, if lower, opens the own record for the speeds (0x54fe10). Fifteen
    // places of the inventory and game interface write it (0x44add8, 0x561b6d, 0x561bf7, 0x59bf3c, 0x5c2d5c, 0x5c5131,
    // 0x5c5154, 0x5c51c3, 0x5c51f8, 0x5c5277, 0x5c5301, 0x5c5330, 0x5c6c15, 0x5c8edd and 0x5c8f53; *static*). MuRet has
    // the inventory's: picking up from the grid (0x5c5301) and from the equipment (0x5c2d5c) and MoveHero (0x59bf3c),
    // the place the cursor item came from.
    std::int32_t slot78a6758 = 0;
    // 0x77f36d0, layer 1 of the hero's cell, in the .bss: 0 at start. MoveHero writes it (0x59e2ba), after the
    // counter. The objects' step (the roofs of 0x604ab0, 0x604bc4 and 0x604c06) and the leaves' (0x637082..0x6370e5)
    // read it, among others.
    std::int32_t heroCellLayer = 0;
    // 0x75fdc38: the 0F's target. 0x75fdc3c is the current count, which that packet does not change. 0x530490's
    // durable state for the future leaves binder; receiving a 0F does not create rain.
    Weather::LeafWeather leafWeather{.firstWave = InitialLeafCount};
    // [0x5767964] (.bss, 0): the place of the last one that hit. The 18 with actions 0x78 and 0x79 (0x4cb849), the
    // 0x19 (0x4cce7d) and the 0x69 (0x4cc2e1) write it with the source, but only if they find the target (0x4cce68,
    // 0x4cc2c0), and the 0x1A (0x4cedf7) always; the place they write is 400 if they do not find whom they look for.
    // Only the D7 that hits the hero with the shield reads it (0x4cb32f, Actors::RaiseTheShield).
    std::int32_t lastAttacker = 0;
    // [0x6c88ac] (.data, 15): after how many steps of +0x381 0x4fd5f0's skill runs (0x4fdbe5, signed). Only 0x4f97b0
    // writes it: 15 at 0x4f987d, before its switch, and 5 at 0x4f9ee2, a skill case (milestone 4).
    std::int32_t skillThreshold = 15;
    // [0x5767948]: one state for H3's shot and H4's skills.
    std::uint32_t lastShotSkill = 0;
    // +0xaa..+0xbc of [0x5769ad8]'s models: what 0x612c80 wrote from each monster's creation (0x612d90,
    // MonsterSounds.h), which 0x4f4a10 and 0x4f5660 read (CL9 milestone 3, phase 2c). Never cleared: nothing else
    // writes those WORDs, and a model that is freed keeps its own.
    Sound::ModelSoundTable modelSounds{};
    // [0x7437670]: the monsters' skills from MonsterSkill.bmd (0x54a440), which main.cpp copies from
    // GameData::monsterSkills and 0x4f92f0 reads (CL9 milestone 3, phase 2c). Without the file, the .bss.
    Tables::MonsterSkillTable monsterSkills{};
    // 0x7239a8: the health bars' queue, a std::deque of 0x1000-byte blocks that 0x42a2b0 fills from the characters'
    // step (the 0x1a8 with +0x93 below 5) and 0x42a4d0 empties half at a time in the HUD (Scenes/HealthBars.h). Only
    // those two and its static constructor (0x4293d0) touch it: a map change does not clear it.
    std::deque<QueuedHealthBar> healthBars{};
    // [0x6e5640]: the characters' named bones (0x401000). 0x4011c0 removes the one of the same character and name
    // before adding the new one; 0x401410 (from 0x401040, at 0x512870, 0x510ff0 and 0x510f70) removes all of a
    // character's. In what order their reader walks them, unread (CL11).
    std::vector<NamedBone> namedBones{};
    // The music and the WAVs a map or a model registers; the requests go through `sounds`.
    Audio::IPlayback* audio = nullptr;
    Kernel::ISoundRequests* sounds = nullptr;
    Audio::Music music{};
};

// 0x63722e and 0x59afd6: with the counter above this, the game neither draws nor moves the hero.
inline constexpr std::int32_t WaitDrawThreshold = 30;
// 0x6357eb: what the world's load leaves in the counter, 0x98967f.
inline constexpr std::int32_t WorldLoadingWait = 9'999'999;
} // namespace Client::Kernel
