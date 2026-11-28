#pragma once

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Ui/Dialogs.h"
#include "Ui/PanelDialog.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

// The game's panels (06 § HUD y ventanas, "Los paneles"): bits of [0x576352c] in immediate mode, not classes. The
// step handles them in 0x5ee140 and the HUD draws them with free functions. The registry reproduces their functions
// with the original's order of checks, because the trade (0x20) and NPC shop (0x10000) ones are bytes of the
// protection table and each check draws a rand(). Each id's state is a byte of its own or one that lives in another
// object (DelegatedPanels): an id whose owner is not ported stays closed and its check draws what it draws. What a
// panel does on opening or closing that belongs to another wave bails out with its address (decision 4 of the CL8
// plan).
namespace Client::Ui
{
using PanelId = std::uint32_t;

inline constexpr PanelId FriendsPanel = 0x1; // the old UI: 0x490a00(old+0x48)
inline constexpr PanelId MovePanel = 0x2;    // M, the /move window: mode 4 of 0x77f3b08
inline constexpr PanelId PartyPanel = 0x4;   // P
inline constexpr PanelId QuestPanel = 0x8;   // T
inline constexpr PanelId GuildPanel = 0x10;  // G
inline constexpr PanelId TradePanel = 0x20;  // byte protegido 0x7d53711
inline constexpr PanelId WarehousePanel = 0x40;
inline constexpr PanelId ClosedPanel = 0x80; // 0x659429: its branch always returns 0
inline constexpr PanelId ChaosBoxPanel = 0x100;
inline constexpr PanelId CommandsPanel = 0x200; // D
inline constexpr PanelId PetPanel = 0x400;
inline constexpr PanelId PersonalShopPanel = 0x800;    // the hero's own
inline constexpr PanelId EventNpcPanel = 0x1000;       // 0x7d53712 with 0x7d53714 at 0
inline constexpr PanelId Panel2000 = 0x2000;           // 0x7d53718, unidentified
inline constexpr PanelId SecondEventNpcPanel = 0x4000; // 0x7d53712 with 0x7d53714 at 1
inline constexpr PanelId TrainerPanel = 0x8000;        // the NPC of the 0x723c80 box
inline constexpr PanelId NpcShopPanel = 0x10000;       // byte protegido 0x7d53710
inline constexpr PanelId OtherShopPanel = 0x20000;     // someone else's personal shop
inline constexpr PanelId SecondGuildPanel = 0x40000;
inline constexpr PanelId CastleSiegePanel = 0x80000;
inline constexpr PanelId CastleSiegeListPanel = 0x100000;
inline constexpr PanelId SecondCastleSiegePanel = 0x200000;
inline constexpr PanelId SiegeWeaponPanel = 0x400000;       // 0x724598 at 1 (*inferred*: the siege weapon)
inline constexpr PanelId SecondSiegeWeaponPanel = 0x800000; // 0x724598 at 2
inline constexpr PanelId SiegeGatePanel = 0x1000000;        // 0x7245a0 (*inferred*: the gate)
inline constexpr PanelId CharacterPanel = 0x2000000;        // C
inline constexpr PanelId InventoryPanel = 0x4000000;        // I and V
// IsOpen(0): any of the bits 1..0x40000000, in that order, up to the first open one.
inline constexpr PanelId AnyPanel = 0;
// 0x657a90: CloseByMask(−1, 0).
inline constexpr PanelId AllPanels = 0xffffffffu;

// 0x659290 with 2: the /move window is overlay mode 4.
inline constexpr std::int32_t MoveOverlayMode = 4;
// 0x5b0550: the F1 helps (1 and 2) and the chat item's (3).
inline constexpr std::int32_t FirstHelpMode = 1;
inline constexpr std::int32_t SecondHelpMode = 2;
inline constexpr std::int32_t ItemHelpMode = 3;

// 0x40a450 and 0x40a650: the twelve D commands, of which 0 is neither chosen nor drawn.
inline constexpr std::size_t CommandCount = 12;
// [0x6e592c]'s +0x14..+0x1f: a command's state that 0x406050 sets on each step and that entering the login and the
// selection leave so.
inline constexpr std::uint8_t CommandReady = 1;

// [0x6e592c] (0x40acd0 builds it in .data with +8 and +0x20 at −1): what the commands window keeps besides +4
// (DelegatedPanels::commands). The corner (+0xc and +0x10) goes in Kernel::HudState, like the other windows'.
struct CommandsWindowState
{
    // +8: whom the command goes to: 0x406050 sets it to −1 and, with a player two cells away or less, to the chosen one
    // (0x4060d0 and 0x4061a7); the commands that close the window set it back to −1 (0x406af7 and 0x4072b8).
    std::int32_t target = -1;
    // +0x14..+0x1f: 0x406050 sets them to 1 on each step, before looking at the chosen command. The drawing (0x40abe5)
    // gives a notice with 0, 0xff or 0xfe. *Inferred*: they start at 1, because entering the login (0x634bf0) and the
    // selection (0x634271) set them so before the game's first step; the constructor does not touch them.
    std::array<std::uint8_t, CommandCount> states{CommandReady, CommandReady, CommandReady, CommandReady,
                                                  CommandReady, CommandReady, CommandReady, CommandReady,
                                                  CommandReady, CommandReady, CommandReady, CommandReady};
    // +0x20: the command chosen with the click (0x40a50a), −1 with none.
    std::int32_t chosen = -1;
};

// The ids whose state lives in another object. MuRet does not have their owners: they stay at 0, closed, until their
// wave.
struct DelegatedPanels
{
    // 1: 0x490a00(*(0x5763514)+0x48), the old manager's friends window. In the game PanelContext::friendsShown checks
    // it (CL9 milestone 9); without it, this byte.
    std::uint8_t friends = 0;
    // 8: *(0x710148)+0x21383. IsOpen checks it non-zero and CanOpen opens nothing with 1.
    std::uint8_t quest = 0;
    // 0x10: 0x6522e0, the guild's +0x7c ([0x7d536f0], CL9 milestone 8). 0x6522d0 opens it and 0x6522f0 closes it.
    std::uint8_t guild = 0;
    // 0x200: [0x6e592c]'s +4, the commands window (D, CL8 milestone 6). Open opens it and CloseByMask and its close
    // button (0x40a58a) close it.
    std::uint8_t commands = 0;
    // The rest of [0x6e592c] that the commands' step (0x40a450) and drawing (0x40a650) read.
    CommandsWindowState commandsWindow{};
    // 0x400: 0x420cc0, 0x723911 at 2 (the pet). 0x424230 sets it back to 0 on each step with the character closed.
    std::uint8_t pet = 0;
    // 0x8000: 0x449bf0, 0x724580 > 0. With 3 (0x449c00), opening the inventory does not close it.
    std::int32_t trainer = 0;
    // 0x724584 (byte): the mix 0x44ae10 found in the trainer's box (1 or 2), which its mode 3 (0x44d010) and its
    // drawing (0x44d680) read; closing the trainer (0x4499b0) sets it to 0.
    std::uint8_t field724584 = 0;
    // 0x40000: 0x6570a0, guild 2's +0x7c ([0x7d536f4], CL9 milestone 8). 0x657090 opens it and 0x6570b0 closes it.
    std::uint8_t secondGuild = 0;
    // 0x80000, 0x100000 and 0x200000: [0x7d536f8]+4, [0x7d536fc]+4 and [0x7d53700]+0x14 (Castle Siege, CL9.5).
    std::uint8_t castleSiege = 0;
    std::uint8_t castleSiegeList = 0;
    std::uint8_t secondCastleSiege = 0;
    // [0x7d536fc]+0x18: with 3 it goes along with the inventory.
    std::int32_t castleSiegeListMode = 0;
    // 0x400000 and 0x800000: 0x44dc10, 0x724598 at 1 or 2; non-zero, the world width is 450 (0x44dc20).
    std::uint8_t siegeWeapon = 0;
    // 0x1000000: 0x44f0f0, 0x7245a0; non-zero, the world width is 450 (0x44f100).
    std::uint8_t siegeGate = 0;
    // 0x661b90 without an item on the cursor: some slot of the chaos box grid (0x7d2d380) taken (CL9.3).
    bool chaosBoxHasItem = false;
    // [0x7d53704] (0x65feb0; 0x6600d0 opens it from the packets, CL9 milestone 8). 0x657880 gives it the key first.
    PanelDialogState panelDialog{};
};

// The registry's bytes and the globals its functions write, all in .bss.
struct PanelState
{
    std::uint8_t inventory = 0;    // 0x7d53708
    std::uint8_t character = 0;    // 0x7d53709
    std::uint8_t party = 0;        // 0x7d5370a
    std::uint8_t warehouse = 0;    // 0x7d5370d
    std::uint8_t chaosBox = 0;     // 0x7d5370e
    std::uint8_t personalShop = 0; // 0x7d5370f, the one of 0x800 and 0x20000
    // 0x7d53710 and 0x7d53711 live in the protection table: each check opens and closes them, and the close that leaves
    // the count at 0 draws a rand() (06 § La tabla de protección).
    std::uint8_t npcShop = 0;
    std::uint8_t npcShopDepth = 0;
    std::uint8_t trade = 0;
    std::uint8_t tradeDepth = 0;
    std::uint8_t eventNpc = 0;     // 0x7d53712
    std::uint8_t eventNpcKind = 0; // 0x7d53714
    std::uint8_t panel2000 = 0;    // 0x7d53718
    // 0x77f3b08, 0x5b0550's overlay mode.
    std::int32_t overlayMode = 0;
    // 0x77f3b0c: the mode 3 help's item, as an Item.bmd slot. The chat item help writes it (0x576939).
    std::int32_t helpItem = 0;
    // 0x78abb00: CanOpen opens nothing with a value other than 0, the world width counts it as a panel and SendMove
    // sends C1 03 97 (CL9.5). Closing the inventory sets the others to 0 (0x659250..0x65926a).
    std::int32_t field78abb00 = 0;
    std::int32_t field78abb04 = 0;
    // 0x78abb08: a byte of panel 0x2000, which its step (0x5e74f0, CL9) flips and which opening it (0x65b259), closing
    // it (0x658bea) and the no of the dialog of 0x78abb14's 4 (0x5d9c56) set to 0.
    std::uint8_t field78abb08 = 0;
    std::uint8_t field78abb09 = 0;
    // 0x78abb0c: the repair mode (1 or 2) the cursor draws (06 § El cursor). Entering the world and map changes also
    // set it to 0.
    std::int32_t repairMode = 0;
    // 0x78abb27: what goes with the repair mode, 2 with the mode on and 0 off (B and R, 0x5a4f1c and 0x5a54e1).
    std::uint8_t repairCursor = 0;
    DelegatedPanels delegated{};
};

// What the registry's functions read from outside.
struct PanelContext
{
    Crt::CrtRandom& random;
    DialogState& dialogs;
    // 0x6c9dd0: CanOpen opens neither the friends nor the commands in Chaos Castle (18 to 23).
    std::int32_t map = -1;
    // 0x7879fbc: with an item on the cursor the inventory does not close (0x661b90).
    std::int32_t cursorItemCount = 0;
    // The hero's +0x1d4 & 7: with 4 (Dark Lord), opening the inventory requests the raven and the horse (petRequests,
    // 0x65c039).
    Kernel::CharacterClass heroClass = Kernel::CharacterClass::DarkWizard;
    // 0x576793c: the exit asked for from the Esc menu (milestone 6). With 1, Open does not open.
    bool logoutRequested = false;
    // 0x6689d0, the hero with +0x1f2 ≥ 0 and +0x368 at 0x80: only the world width reads it with the CS list open.
    bool heroSiegeMark = false;
    // 0x661b90, when the world is lent: a live check of the 32 WORD types and the cursor.
    std::function<bool()> inventoryBusy{};
    // 0x659aa0..0x659c3d: what opening the party does if it closed the rest, before turning it on: 0x78aba48 (the party
    // count) to 0 and the list request, C1 03 42. Without it, opening the party bails out.
    std::function<void()> partyOpened{};
    // 0x658a18..0x658bc7 and 0x658c0e..0x658dbe: closing an event NPC (0x1000 or 0x4000) sends the C1 03 31 in the
    // clear before setting 0x7d53712 to 0. Without it, closing them bails out.
    std::function<void()> eventNpcClosed{};
    // 0x41ed10(id, 0, 0): the sound, CL10's hook.
    std::function<void(Audio::SoundId)> sound{};
    // 0x659c8c..0x659e3c: what opening the guild does if it closed the rest, before opening it: the list request, C1 03
    // 52 (Protocol::Guild::WriteMemberListRequest). Without it, opening the guild bails out.
    std::function<void()> guildOpened{};
    // 0x6522f0 and 0x6570b0, vtable + 0x2c of the two guild windows, which CloseByMask calls with their panel open
    // (Guild/GuildPanels): the sound, their dialogs and, window 2, the C1 04 54 00. Without them, closing them bails
    // out.
    std::function<Core::Expected<void, std::string>()> guildClosed{};
    std::function<Core::Expected<void, std::string>()> secondGuildClosed{};
    // The function of whoever opened the panel dialog (0x4d6b60 or 0x653c00), which 0x6603c0, 0x660410 and 0x6605b0
    // call. Without it, answering bails out.
    PanelDialogAnswer panelDialogAnswer{};
    // 0x6605b0 with the mouse, the old UI's globals and the step's clock (Guild/GuildPanels). Without it, the step with
    // the dialog open bails out.
    std::function<Core::Expected<void, std::string>()> panelDialogStep{};
    // 0x657930 with [0x7d536f4] open: its vtable + 0x18 (0x45aa80(0), which calls 0x656df0) with the mouse, the clock
    // and the step's filters (Guild/SecondGuildWindow). Without it, the step with guild window 2 open bails out.
    std::function<Core::Expected<void, std::string>()> secondGuildStep{};
    // 0x657977: the guild window's step (0x45aa80(0) and 0x651840, Guild::StepTheGuildWindow), with the mouse and the
    // step's wiring. Without it, with the panel open, the step bails out.
    std::function<Core::Expected<void, std::string>()> guildStep{};
    // 0x65bef0..0x65bf0d: with the other panels closed, 0x64ae10 requests B2 03 and opens the castle registration.
    std::function<Core::Expected<void, std::string>()> castleRegistrationOpened{};
    // 0x44dcf0/0x44f770: the weapon and gate controls; aux is the gate's key.
    std::function<Core::Expected<void, std::string>(PanelId, std::uint32_t)> siegeControlOpened{};
    std::function<Core::Expected<void, std::string>(PanelId)> siegeControlClosed{};
    // 0x657b94: 0x64b200 sends C1 03 31 and then closes the registration and goes back to its first tab.
    std::function<Core::Expected<void, std::string>()> castleRegistrationClosed{};
    // 0x661310/0x6487c0 and 0x661bc0/0x6487d0. false reports Senior's lock; CloseByMask does not look at that return (a
    // native void call) nor clears its success flag for this panel.
    std::function<Core::Expected<void, std::string>(PanelId)> castleWindowOpened{};
    std::function<Core::Expected<bool, std::string>(PanelId)> castleWindowClosing{};
    std::function<Core::Expected<void, std::string>()> castleSeniorBlocked{};
    // 0x657930's order: registration, Senior and hunting zone, after the two guild windows.
    std::function<Core::Expected<void, std::string>(PanelId)> castleWindowStep{};
    // 0x65c966..0x65ca6d: what opening the character does outside the panels, whether or not it closes the rest:
    // [0x6eed3c] from +0x9304 to +0x9327 to 0, and with the own record open, 0x550d10 if an equipment slot has
    // durability 0 (0x550cf0). Without it, opening the character bails out.
    std::function<Core::Expected<void, std::string>()> characterOpened{};
    // 0x657fb5..0x6584a4: what closing the trade does between the check and the write of the protected byte: with aux
    // at 0 the C1 03 3D in C3 (two rand()), and the two grids (0x78a92a8 and 0x78a5a20) emptied, with 0x5bbdf0 on each
    // pass if 0x78abaa5 is 1 (Social::CloseTheTradeGrids). Without it, closing the trade bails out.
    std::function<Core::Expected<void, std::string>(bool aux)> tradeClosed{};
    // 0x65ac65..0x65b21d and 0x65b33a..0x65ba37: what opening a personal shop (0x800 or 0x20000) does if it closed the
    // rest, after the inventory and before setting 0x7d5370f: with 0x6c931c other than −1 the C1 04 F3 10 in C3 (two
    // rand()), the two price maps (0x7245e4 and 0x7245e0) if they were missing and 0x6c931c to 1 or 2
    // (PersonalShop::OpenThePersonalShop). Without it, opening a shop bails out.
    std::function<Core::Expected<void, std::string>(PanelId id)> personalShopOpened{};
    // 0x6589cb and 0x6589ef: 0x5eb020 on closing either one, before clearing 0x7d5370f
    // (PersonalShop::CloseThePersonalShop). Without it, closing a shop bails out.
    std::function<Core::Expected<void, std::string>()> personalShopClosed{};
    // The raven and horse requests, C1 06 A9 00 00 01 and A9 01 00 08, with the XOR32 and through send() (CL9 milestone
    // 4): what opening the pet does if it closed the rest, after 0x420cb0 (0x659ff6..0x65a8a3), and opening a Dark
    // Lord's inventory after turning it on (0x65c039..0x65c946, the same code repeated). Without it, both bail out.
    std::function<void()> petRequests{};
    // What opening and closing the warehouse (0x40), the chaos box (0x100), the trainer (0x8000) and the NPC shop
    // (0x10000) do outside the panels (CL9 milestone 6): the grids, the cursor item, the sends and the messages
    // (Npc/NpcWindows.h). Without them, opening and closing them bails out.
    // Opening, with the rest closed and before opening the inventory: 0x40's part (0x659f8d..0x659f94) and 0x100's
    // (0x65a94f..0x65ac07 and 0x65ac28), which 0x6599a0(0x4000000) does not read; 0x8000 does not open the inventory
    // (0x449940(1), 0x65be8b).
    std::function<Core::Expected<void, std::string>(PanelId)> npcWindowOpened{};
    // Closing, from 0x657ab0 with the panel open: false if it does not close (the 24 lock in 0x40, the chaos box with
    // something in 0x100, the trainer's with something in 0x8000), and the registry leaves the byte as it was.
    std::function<Core::Expected<bool, std::string>(PanelId)> npcWindowClosing{};
    // 0x659c68..0x659c70: opening the quests, if it closed the rest, is 0x419840(−1) on [0x710148] (the list, with its
    // dialog). Without it, opening the quests bails out.
    std::function<Core::Expected<void, std::string>()> questOpened{};
    // 0x657b2b..0x657b31: closing the quests is 0x419900, which sets the window to 0 (the registry does that) and sends
    // the C1 03 31 in the clear. Without it, closing the quests bails out.
    std::function<void()> questClosed{};
    // Panel 1, the old manager's friends window: 0x490a00(+0x48) to know whether it is there (without it,
    // DelegatedPanels::friends), 0x491080(0x186, 0x106) on opening it (0x659f3d; without it, opening bails out) and
    // 0x4914e0 on closing it (0x6584e5; without it, closing bails out).
    std::function<bool()> friendsShown{};
    std::function<Core::Expected<void, std::string>()> friendsOpened{};
    std::function<Core::Expected<void, std::string>()> friendsClosed{};
};

class PanelRegistry
{
public:
    PanelRegistry(PanelState& state, PanelContext& context) : m_state(state), m_context(context) {}

    // 0x659290. Only 0x20 and 0x10000 draw a rand(); with 0 it walks the bits.
    [[nodiscard]] bool IsOpen(PanelId id);
    // 0x659860.
    [[nodiscard]] bool CanOpen(PanelId id);
    // 0x6599a0(id, aux): false if it did not try; true with the two sounds even if the case failed (Q-C445).
    [[nodiscard]] Core::Expected<bool, std::string> Open(PanelId id, std::uint32_t auxiliary = 0);
    // 0x65cae0(id, 0): false if it was closed, without sounds.
    [[nodiscard]] Core::Expected<bool, std::string> Close(PanelId id);
    // 0x65cae0(id, aux): `aux` reaches 0x657ab0; only the trade reads it (with 1, without the 3D: 0x4d5906).
    [[nodiscard]] Core::Expected<bool, std::string> Close(PanelId id, bool aux);
    // 0x657ab0(mask, 0): the 26 checks in their fixed order, each only if it is in the mask.
    [[nodiscard]] Core::Expected<bool, std::string> CloseByMask(PanelId mask);
    [[nodiscard]] Core::Expected<bool, std::string> CloseByMask(PanelId mask, bool aux);
    // 0x657880(vk): Esc closes the open one with the lowest bit.
    [[nodiscard]] Core::Expected<bool, std::string> PanelKey(std::int32_t virtualKey);
    // 0x657a90: all of them, and the panel dialog (0x6603c0, which does nothing when closed).
    [[nodiscard]] Core::Expected<void, std::string> CloseAll();
    // 0x657930: the panel dialog, the two guilds and Castle Siege.
    [[nodiscard]] Core::Expected<void, std::string> StepTheObjectPanels();
    // 0x5bb8d0: in 640-wide pixels: 640 without panels, 450 with one on the right and 260 with two.
    [[nodiscard]] std::int32_t WorldWidth();

private:
    void PlayTheSounds() const;
    // 0x6599a0 with 0x40, 0x100 and 0x10000: with the rest closed, the outside part, the inventory and the panel's
    // byte.
    [[nodiscard]] Core::Expected<void, std::string> OpenTheNpcWindow(PanelId id, PanelId mask);
    // 0x657ab0 with 0x40, 0x100, 0x8000 and 0x10000 open: false if it did not close.
    [[nodiscard]] Core::Expected<bool, std::string> CloseTheNpcWindow(PanelId id);

    PanelState& m_state;
    PanelContext& m_context;
};

// What the original repeats in five places on [0x6e592c]: +4 to 0, +8 and +0x20 to −1 and the twelve states to 1.
// CloseByMask (0x65897a..0x658999), the close button (0x40a58a..0x40a5a1) and entering the selection (0x634271) and
// the login (0x634bf0); the fifth, the Chaos Castle map change (0x4c7b34), bails out earlier in MuRet (0x4906a0).
void ResetTheCommands(DelegatedPanels& delegated);

// 0x5bb8d0
inline constexpr std::int32_t FullWorldWidth = 0x280;
inline constexpr std::int32_t OnePanelWorldWidth = 0x1c2;
inline constexpr std::int32_t TwoPanelWorldWidth = 0x104;
} // namespace Client::Ui
