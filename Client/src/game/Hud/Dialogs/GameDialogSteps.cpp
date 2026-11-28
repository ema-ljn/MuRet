#include "Hud/Dialogs/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Tables/DialogTable.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Npc/DialogAnswers.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party/Party.h"
#include "Protocol/Social/Trade.h"
#include "Models/LogicalAccess.h"
#include "Math/RoundedMath.h"
#include "Chat/GameMessages.h"
#include "Events/EventGlobals.h"
#include "Chat/GameChat.h"
#include "Hud/GamePanels.h"
#include "Ui/GameUiContext.h"
#include "Guild/GuildReceive.h"
#include "Guild/GuildState.h"
#include "Items/InventoryMoves.h"
#include "ChaosMix/JewelMix.h"
#include "Npc/NpcWindowState.h"
#include "Npc/NpcWindowSteps.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Items/OwnItemPlacement.h"
#include "PersonalShop/PersonalShopWindow.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Chat/MacroFile.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"
#include "Items/HudItems.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Hud::Dialogs
{
namespace
{
// 0x41ed10(0x19, 0, 0)
constexpr Audio::SoundId DialogSound = Sound::ButtonClickSound;
// 0x6c8dcc
constexpr std::int32_t DialogMouseUpdateTimeMax = 6;

// 0x62dac4: the table of the Esc menu's first four buttons; 4 goes straight to 0x6240a1.
constexpr std::int32_t ExitGameButton = 0;
constexpr std::int32_t AnotherServerButton = 1;
constexpr std::int32_t AnotherCharacterButton = 2;
constexpr std::int32_t OptionsButton = 3;
// 0x623f9c..0x623fb3, in scene 5.
constexpr std::int32_t EscapeMenuButtons = 5;
// 0x623fe8..0x62400d: x in [0x104, 0x17c) and y in [(6 + 3i) · 10, + 0x16).
constexpr std::int32_t EscapeMenuLeft = 0x104;
constexpr std::int32_t EscapeMenuRight = 0x17c;
constexpr std::int32_t EscapeMenuButtonHeight = 0x16;
// 0x624087: Text.bmd's 592 (0x76e879c), as an error (type 2).
constexpr std::size_t ChaosBoxOpenText = 592;

// 0x459f00(0x104, y, 0x78, 0x16, 1): the option rows.
constexpr std::int32_t OptionsLeft = 0x104;
constexpr std::int32_t OptionsWidth = 0x78;
constexpr std::int32_t OptionsRowHeight = 0x16;
constexpr std::int32_t BackRowY = 0x1e;
constexpr std::int32_t AutoAttackRowY = 0x3c;
constexpr std::int32_t NoticeSoundRowY = 0x5a;
constexpr std::int32_t VolumeRowY = 0x78;
constexpr std::int32_t SlidesRowY = 0x96;
constexpr std::int32_t CloseRowY = 0xe8;
// 0x625715..0x625730: the volume bar, 0x32 × 8 from its corner; one point every 5 pixels.
constexpr std::int32_t VolumeBarWidth = 0x32;
constexpr std::int32_t VolumeBarHeight = 8;
constexpr std::int32_t VolumeStepPixels = 5;
// 0x625794..0x625799: 0 to 9.
constexpr std::int32_t MaxVolume = 9;
// 0x6257f5: 0x41eed0(−10000), DirectSound's silence.
constexpr std::int32_t SilentVolume = -10000;
// 0x6b87a8 (10.0f) and 0x6ba7a0 (−2000.0): −2000 · log10(10 / v).
constexpr double VolumeNumerator = 10.0;
constexpr double VolumeScale = -2000.0;
// 0x625864..0x625897: the five glow boxes, from x = 0x10e every 0x14 (up to 0x172), 0x12 wide and y from 0xc8 to 0xda,
// edges included.
constexpr std::int32_t GlowBoxFirstX = 0x10e;
constexpr std::int32_t GlowBoxStep = 0x14;
constexpr std::int32_t GlowBoxEndX = 0x172;
constexpr std::int32_t GlowBoxWidth = 0x12;
constexpr std::int32_t GlowBoxTop = 0xc8;
constexpr std::int32_t GlowBoxBottom = 0xda;

// 0x6263f7..0x626417
constexpr std::int32_t OkLeft = 0x11c;
constexpr std::int32_t OkRight = 0x162;
constexpr std::int32_t OkTop = 0x62;
constexpr std::int32_t OkBottom = 0x77;
// 0x6263f2: 0x73 does not look at the button, only Enter.
constexpr std::int32_t EnterOnlyDialog = 0x73;
// 0x626433..0x626451: the ones that close the game (SendMessageA with WM_DESTROY, 0x62828e).
constexpr std::array<std::int32_t, 4> QuittingDialogs{0x70, 0x6f, 0x1a, 0x1c};
// 0x62825c: the one that closes and calls 0x5e7aa0(6).
constexpr std::int32_t ReopeningDialog = 0x8f;
constexpr std::int32_t ReopenedKeypadMode = 6;
// 0x62843f..0x62845d: 0x98, another of those that only leave the answer in 0x6c932e (0x62a779).
constexpr std::int32_t ConfirmDialog = 0x98;
// 0x629c0e: the jewel of bless on 0x1a25 (CL9 milestone 5, 0x5d8324).
constexpr std::int32_t BlessDialog = 0xa9;
// 0x625910: 0x65aaf2 opens it when opening the chaos box.
constexpr std::int32_t ChaosMixModeDialog = 0x8e;
// 0x6259c6, 0x625b23 and 0x625f98
constexpr std::int32_t JewelMenuDialog = 0xa1;
constexpr std::int32_t JewelChoiceDialog = 0xa2;
constexpr std::int32_t JewelUnmixDialog = 0xa4;
constexpr std::int32_t JewelResultDialog = 0xa6;
// 0x625929..0x625986: 0x8e's two buttons, x in [245, 395] and y from 180 every 85, 35 high, edges included.
constexpr std::int32_t MixModeLeft = 245;
constexpr std::int32_t MixModeRight = 395;
constexpr std::int32_t MixModeFirstTop = 180;
constexpr std::int32_t MixModeStep = 85;
constexpr std::int32_t MixModeHeight = 35;
constexpr std::int32_t MixModes = 2;
// 0x628b32..0x628b49: 0x96's yes opens 0xa8 with 0x633300(1, 0x76e, 0xa8).
constexpr std::int32_t LuckDialogLines = 1;
constexpr std::int32_t LuckDialogText = 0x76e;
// 0x628582..0x62858e: 0x9a's yes without the item (the dword 0x78a59cc at 0xff) does nothing.
constexpr std::uint32_t NoDialogItem = 0xff;
// 0x628702..0x628710: the item's level, (0x78a59cc >> 3) & 0xf.
constexpr unsigned ItemLevelShift = 3;
constexpr std::uint32_t ItemLevelMask = 0xf;
// 0x628bb2..0x628bd7: 0xa8's third button (or its Esc) chooses 2, which closes without the sound.
constexpr std::int32_t ThirdChoice = 2;
// 0x62a7f7..0x62c560: yes and no. Both buttons are 0x46 × 0x15 at y 0x62; with 0x7d and 0x95 (the yes) or 0x71, 0x7d
// and 0x95 (the no), 0x28 wide at another x.
constexpr std::int32_t PartyRequestDialog = 0x77;
// 0x62a878 and 0x62c567: dialog 0x7d (CL9 milestone 8), the master's number to kick a member, disband the guild or
// leave it (C1 53).
constexpr std::int32_t LeaveNumberDialog = 0x7d;
constexpr std::int32_t YesLeft = 0xea;
constexpr std::int32_t NoLeft = 0x14e;
constexpr std::int32_t NarrowYesLeft = 0x143;
constexpr std::int32_t NarrowNoLeft = 0x175;
constexpr std::int32_t YesNoWidth = 0x46;
constexpr std::int32_t NarrowWidth = 0x28;
constexpr std::int32_t YesNoTop = 0x62;
constexpr std::int32_t YesNoBottom = 0x77;
// 0x62c54c..0x62c560: the no table's cases (0x62db1c) that are not 0x77, 0x78 or 0x7d, with their range and whose
// they are (08 P-C363); the other ids go to the close (0x62d745). They arrive with the dialog that waits behind the yes
// of 0x77 or 0x78. 0x95 shares 0x7d's (0x62c567), but its step stops earlier.
struct OtherNoCase
{
    std::int32_t id = 0;
    std::string_view address;
    std::string_view owner;
};
constexpr auto OtherNoCases = std::to_array<OtherNoCase>({
    {0x71, "0x62c58f", "del borrado de la selección (0x622fb0, el F3 02), que el juego no abre"},
    {0x76, "0x62c880", "del gremio (el C1 51), que el juego no abre"},
});
// 0x6282dc..0x62839f: the yes/no dialogs 0x94, 0x96, 0x98, 0x99, 0x9a, 0xa3, 0xa8, 0xa9 and 0xaa run their two
// buttons (0xd5, 0x64; 0xd2 in 0xa3), like 0x62d756 with another y, and leave their state at 1 and 3 (2 and 4
// hovered). In 0x94, 0x98 and 0x99 (the low price, which 0x62845d sends to the same end) the first answers 1 and the
// second, or Esc, 2 (0x62a779..0x62a7e6).
constexpr std::int32_t WarningDialog = 0x94;
constexpr std::int32_t WarningButtonsX = 0xd5;
constexpr std::int32_t WarningButtonsY = 0x64;
constexpr std::int32_t ServerTimeButtonsY = 0xd2;
constexpr std::uint8_t WarningYes = 1;
constexpr std::uint8_t WarningNo = 2;
// The padding and the serial of the C1 05 41 in C3.
constexpr std::uint32_t C3Randoms = 2;
// 0x626460: the typed zen, signed, up to 100000000 (0x5f5e100); with more, dialog 0x75. Without the zen (the own plus
// what is already in the trade, unsigned), 0x74.
constexpr std::int32_t MaxTypedMoney = 100'000'000;
constexpr std::int32_t TooMuchMoneyDialog = 0x75;
constexpr std::int32_t NotEnoughMoneyDialog = 0x74;
// 0x62821f..0x62823a: what 0x73's close leaves: the field length, the two Tab fields and the zen mode.
constexpr std::int32_t ClosedDialogFieldLimit = 0x2a;

// 0x61c2b6..0x61c2c3: the key places it walks (0 to 0x71).
constexpr std::size_t OptionKeyPlaces = 0x72;
constexpr std::uint8_t NoKey = 0xff;
// 0x61c210's stack bytes: [10] the flags, [11..13] Q, W and E and [14] the chat log.
constexpr std::size_t FlagsByte = 10;
constexpr std::size_t FirstQuickSlotByte = 11;
constexpr std::size_t ChatWindowByte = 14;
constexpr std::uint8_t AutoAttackOn = 0x01;
constexpr std::uint8_t AutoAttackOff = 0x02;
constexpr std::uint8_t NoticeSoundOn = 0x04;
constexpr std::uint8_t NoticeSoundOff = 0x08;
constexpr std::uint8_t SlidesOff = 0x10;
// 0x61c3b2..0x61c3c5: the rows / 3 (IMUL by 0x55555556); 0x61c3d9: the transparency · 10.0f.
constexpr std::int32_t RowsPerStep = 3;
constexpr double AlphaSteps = 10.0;
constexpr unsigned NibbleShift = 4;

// The dispatch table, in the order of 0x623f78..0x6263d9. The generic OK rows are the ones the original compares
// before reaching it (0x73 and 0x8f), and its range handles them apart.
constexpr auto Routes = std::to_array<DialogRoute>({
    {Ui::EscapeMenuDialog, DialogStep::EscapeMenu, "0x623f90", ""},
    {Ui::OptionsDialog, DialogStep::Options, "0x62556f", ""},
    {ChaosMixModeDialog, DialogStep::ChaosMixMode, "0x625910", ""},
    {JewelMenuDialog, DialogStep::JewelMenu, "0x6259c6", ""},
    {JewelChoiceDialog, DialogStep::JewelChoice, "0x625b23", ""},
    {JewelUnmixDialog, DialogStep::JewelUnmix, "0x625cf4", ""},
    {JewelResultDialog, DialogStep::JewelResult, "0x625f98", ""},
    {0xa7, DialogStep::WarOutcome, "0x62601e", ""},
    {0x9c, DialogStep::ConfirmBox, "0x6260d1", ""},
    {0x91, DialogStep::ShopDialog, "0x6260f6", ""},
    {Ui::MessageBoxDialog, DialogStep::Buttons, "0x62d756", ""},
    {Ui::AnswersDialog, DialogStep::Buttons, "0x62d756", ""},
    {Ui::PagedDialog, DialogStep::Buttons, "0x62d756", ""},
    {0x8b, DialogStep::Buttons, "0x62d756", ""},
    {0x97, DialogStep::Buttons, "0x62d756", ""},
    {0x9b, DialogStep::Buttons, "0x62d756", ""},
    {0x71, DialogStep::Cut, "0x62a810", "CL9.4"},
    {LeaveNumberDialog, DialogStep::YesNo, "0x62a810", ""},
    {0x95, DialogStep::YesNo, "0x62a7f7", ""},
    {0x76, DialogStep::Cut, "0x62a7f7", "CL9.4"},
    {Ui::IgnoredOverChaosCastle, DialogStep::YesNo, "0x62a7f7", ""},
    {PartyRequestDialog, DialogStep::YesNo, "0x62a7f7", ""},
    {Ui::ForcedDialog, DialogStep::YesNo, "0x62a7f7", ""},
    {WarningDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::ItemNameDialog, DialogStep::Warning, "0x6282dc", ""},
    {ConfirmDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::MoneyTextDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::ChaosCastleDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::ServerTimeDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::ItemNameWithLuckDialog, DialogStep::Warning, "0x6282dc", ""},
    {BlessDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::NoticeQuestionDialog, DialogStep::Warning, "0x6282dc", ""},
    {Ui::DisconnectDialog, DialogStep::GenericOk, "0x6263df", ""},
    {0x6f, DialogStep::GenericOk, "0x6263df", ""},
    {0x1a, DialogStep::GenericOk, "0x6263df", ""},
    {0x1c, DialogStep::GenericOk, "0x6263df", ""},
    {EnterOnlyDialog, DialogStep::GenericOk, "0x6263df", ""},
    {ReopeningDialog, DialogStep::GenericOk, "0x6263df", ""},
});

bool InBox(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t width, std::int32_t height)
{
    return context.mouseX >= left && context.mouseX < left + width && context.mouseY >= top &&
           context.mouseY < top + height;
}

// What the buttons leave: MouseUpdateTime (0x77f3b10) at 0, its max at 6 and the sound.
void PressTheButton(Kernel::WorldRuntime& runtime, bool sound = true)
{
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = DialogMouseUpdateTimeMax;
    if (sound)
        runtime.RequestSound(DialogSound);
}

// 0x62da70 and 0x628280: the waiting one becomes open.
void CloseTheDialog(Kernel::WorldRuntime& runtime, bool sound = true)
{
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    PressTheButton(runtime, sound);
}

Core::Expected<Ui::IGameUiRequests*, std::string> RequestsOf(const Ui::GameUiContext& context, std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: sin el cable ni Macro.txt", where));
    return context.requests;
}

// 0x61c210 and 0x61c170(Macro.txt): the options to the GS and the macros to disk.
Core::Expected<void, std::string> SaveTheOptions(const Ui::GameUiContext& context, std::string_view where)
{
    auto requests = RequestsOf(context, where);
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    auto body = OptionBodyOf(context.world, context.random, context.chosenSlot);
    if (!body.HasValue())
        return Core::Unexpected(std::move(body).Error());
    requests.Value()->SendOptionData(body.Value());
    return requests.Value()->SaveMacros(Chat::WrittenMacros(context.world.Runtime().chat.input.macros));
}

// 0x659290(0x100), without rand().
bool ChaosBoxOpen(const Ui::GameUiContext& context)
{
    GamePanels panels(context.world, context.random);
    return panels.Registry().IsOpen(Ui::ChaosBoxPanel);
}

// 0x555190(0x7d3a14c, Text[592], 2): the name is an empty .bss string.
Core::Expected<void, std::string> SayTheChaosBoxIsOpen(const Ui::GameUiContext& context, std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, ChaosBoxOpenText);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, ChaosBoxOpenText));
    if (context.measure == nullptr)
        return Core::Unexpected(std::format("{}: el mensaje del texto {} sin medidor", where, ChaosBoxOpenText));
    return Chat::AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {}, *text, Ui::ErrorMessage);
}

// 0x62411c..: 0x576793c at 1 and the C3 F1 02 with `kind`: one rand() for the padding (0x62449a) and the serial
// (0x5767913) through the protection table (0x6244d0 and 0x6244f2), a byte encrypted again with another.
Core::Expected<void, std::string> RequestTheLogout(const Ui::GameUiContext& context, std::uint8_t kind,
                                                   std::string_view where)
{
    auto requests = RequestsOf(context, where);
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    context.world.Runtime().ui.logoutRequested = true;
    static_cast<void>(context.random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Models::LogicalAccess serial(serialDepth, context.random, Models::SerialCloseRandoms);
    }
    requests.Value()->RequestLogout(kind);
    return {};
}

Core::Expected<void, std::string> PressTheEscapeMenuButton(const Ui::GameUiContext& context, std::int32_t button)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::DialogState& dialogs = runtime.dialogs;
    switch (button)
    {
    case ExitGameButton:
    {
        // 0x624036: the log (0x41fa40 and 0x41fc10(1)); in scene 5, the F3 30, Macro.txt (0x6d6220) and the chaos box.
        if (auto requests = RequestsOf(context, "0x624036"); requests.HasValue())
            requests.Value()->Log("> Menu - Exit game.");
        if (auto saved = SaveTheOptions(context, "0x62405d"); !saved.HasValue())
            return saved;
        if (ChaosBoxOpen(context))
            return SayTheChaosBoxIsOpen(context, "0x624093");
        return RequestTheLogout(context, Protocol::Session::LogoutQuitsTheGame, "0x62411c");
    }
    case AnotherServerButton:
    {
        // 0x6247ee: the log; the chaos box or the F1 02 with 2, and then, in scene 5, the F3 30 and Macro.txt
        // (0x6d6250, 0x624ec0).
        if (auto requests = RequestsOf(context, "0x6247ee"); requests.HasValue())
            requests.Value()->Log("> Menu - Join another server.");
        if (ChaosBoxOpen(context))
        {
            if (auto said = SayTheChaosBoxIsOpen(context, "0x624844"); !said.HasValue())
                return said;
        }
        else if (auto logout = RequestTheLogout(context, Protocol::Session::LogoutReturnsToServerList, "0x624851");
                 !logout.HasValue())
            return logout;
        return SaveTheOptions(context, "0x624ed5");
    }
    case AnotherCharacterButton:
    {
        // 0x624eec: the log; in scene 5, the F3 30 and Macro.txt (0x6d6288); the chaos box or the F1 02 with 1.
        if (auto requests = RequestsOf(context, "0x624eec"); requests.HasValue())
            requests.Value()->Log("> Menu - Join with another character.");
        if (auto saved = SaveTheOptions(context, "0x624f13"); !saved.HasValue())
            return saved;
        if (ChaosBoxOpen(context))
            return SayTheChaosBoxIsOpen(context, "0x624f45");
        return RequestTheLogout(context, Protocol::Session::LogoutReturnsToSelection, "0x624f4a");
    }
    case OptionsButton:
        // 0x625538: with a dialog open (the menu), the options wait and 0x6240a1 opens them; with none, they open
        // without going through the close.
        if (dialogs.current != 0)
            dialogs.pending = Ui::OptionsDialog;
        else
            dialogs.current = Ui::OptionsDialog;
        return {};
    default:
        return {};
    }
}

// 0x623f90..0x62410b: the Esc menu (dialog 0x6e) in scene 5, with five buttons.
Core::Expected<void, std::string> StepTheEscapeMenu(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::DialogState& dialogs = runtime.dialogs;
    if (runtime.cursorItemCount > 0)
        return {};
    for (std::int32_t button = 0; button < EscapeMenuButtons; ++button)
    {
        // 0x623fdf..0x623fe6: (6 + 3i) · 10.
        const std::int32_t top = (6 + 3 * button) * 10;
        if (!InBox(context, EscapeMenuLeft, top, EscapeMenuRight - EscapeMenuLeft, EscapeMenuButtonHeight) ||
            !runtime.mouse.pendingClick)
            continue;
        runtime.mouse.pendingClick = false;
        // 0x62402f: the table 0x62dac4 for the first four; the three that request something come back to 0x62409b.
        const bool openedTheOptions = button == OptionsButton && dialogs.current == 0;
        if (auto pressed = PressTheEscapeMenuButton(context, button); !pressed.HasValue())
            return pressed;
        // 0x6240a1: with another than the options, the waiting one becomes open. 0x625554 jumps to 0x6240bf.
        if (!openedTheOptions && dialogs.current != Ui::OptionsDialog)
        {
            dialogs.current = dialogs.pending;
            dialogs.pending = 0;
        }
        PressTheButton(runtime);
    }
    return {};
}

// 0x6257ca..0x6257fa: 0x41eed0, DirectSound's volume (the CL10 hook).
std::int32_t DirectSoundVolume(std::int32_t volume)
{
    if (volume <= 0)
        return SilentVolume;
    const double ratio = VolumeNumerator / static_cast<double>(std::min(volume, MaxVolume));
    return static_cast<std::int32_t>(VolumeScale * Math::RoundedLog10(ratio));
}

// 0x62568a..0x625804
void StepTheVolume(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    bool& click = runtime.mouse.pendingClick;
    if (!InBox(context, OptionsLeft, VolumeRowY, OptionsWidth, OptionsRowHeight))
        return;
    std::int32_t volume = ui.volume;
    const bool wasNotDragging = ui.volumeDragging == 0;
    const auto onTheBar = [&](bool lookAtY)
    {
        return context.mouseX >= ui.volumeBarX && context.mouseX < ui.volumeBarX + VolumeBarWidth &&
               (!lookAtY || (context.mouseY >= ui.volumeBarY && context.mouseY < ui.volumeBarY + VolumeBarHeight));
    };
    // 0x625737..0x625759: the click goes back to 1 and the bar stays grabbed.
    const auto grab = [&]
    {
        click = true;
        ui.volumeDragging = 1;
        volume = (context.mouseX - ui.volumeBarX) / VolumeStepPixels;
    };
    if (click)
    {
        click = false;
        runtime.mouse.updateTime = 0;
        runtime.mouse.updateTimeMax = DialogMouseUpdateTimeMax;
        if (wasNotDragging)
            runtime.RequestSound(DialogSound);
        if (onTheBar(true))
            grab();
        else if (ui.volumeDragging != 0)
        {
            ui.volumeDragging = 0;
            if (onTheBar(false))
                grab();
        }
    }
    else
        ui.volumeDragging = 0;
    // 0x625782..0x6257ab: the wheel, which is spent.
    if (ui.wheel > 0)
    {
        ui.wheel = 0;
        volume = std::min(volume + 1, MaxVolume);
    }
    else if (ui.wheel < 0)
    {
        ui.wheel = 0;
        volume = std::max(volume - 1, 0);
    }
    if (volume == ui.volume)
        return;
    ui.volume = volume;
    ui.volumeRequest = DirectSoundVolume(volume);
    runtime.SetSoundVolume(static_cast<std::uint8_t>(volume));
}

// 0x625563..0x62590f (dialog 0x93).
Core::Expected<void, std::string> StepTheOptions(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    Ui::DialogState& dialogs = runtime.dialogs;
    bool& click = runtime.mouse.pendingClick;
    if (runtime.cursorItemCount > 0)
        return {};
    const auto pressed = [&](std::int32_t y)
    {
        if (!InBox(context, OptionsLeft, y, OptionsWidth, OptionsRowHeight) || !click)
            return false;
        click = false;
        PressTheButton(runtime);
        return true;
    };
    // 0x6255ca..0x6255de: going back leaves the menu waiting and the options open (Q-C550).
    if (pressed(BackRowY))
        (dialogs.current != 0 ? dialogs.pending : dialogs.current) = Ui::EscapeMenuDialog;
    if (pressed(AutoAttackRowY))
        ui.autoAttack = !ui.autoAttack;
    if (pressed(NoticeSoundRowY))
        runtime.chat.noticeSound = !runtime.chat.noticeSound;
    StepTheVolume(context);
    if (pressed(SlidesRowY))
        runtime.chat.slidesOn = !runtime.chat.slidesOn;
    // 0x625856..0x62589b: the first glow box with the mouse and the click, without spending it or sounding.
    std::uint8_t box = 0;
    for (std::int32_t x = GlowBoxFirstX; x < GlowBoxEndX; x += GlowBoxStep, ++box)
    {
        if (context.mouseX >= x && context.mouseX <= x + GlowBoxWidth && context.mouseY >= GlowBoxTop &&
            context.mouseY <= GlowBoxBottom && click)
        {
            ui.glowDetail = box;
            break;
        }
    }
    // 0x6258a1..0x6258fa: the close row.
    if (InBox(context, OptionsLeft, CloseRowY, OptionsWidth, OptionsRowHeight) && click)
    {
        click = false;
        // 0x6258e7: the sound comes before reading the pending dialog (0x6258ec).
        PressTheButton(runtime);
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
    }
    return {};
}

// 0x62d91d..0x62da57: 0x8d's answers. With the mouse over one (no more than 0x6a from x = 0x13f) and the click, the
// click goes off, the buttons leave their part and the waiting dialog becomes open; with the answer's +0x130 in
// Dialog.bmd at 0 or more, 0x632f70 shows it.
Core::Expected<void, std::string> StepTheAnswers(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Ui::DialogBoxState& box = runtime.dialogBox;
    const std::int32_t answersTop =
        Ui::DialogTextTop(box, Ui::AnswersDialog) + box.lineCount * Ui::DialogLineStep; // 0x62d943..0x62d94a
    // 0x62d948: a negative mouse y returns.
    if (context.mouseY < 0)
        return {};
    const std::int32_t offset = context.mouseY - answersTop;
    if (offset >= box.answerCount * Ui::DialogLineStep)
        return {};
    if (std::abs(Ui::AnswersCenterX - context.mouseX) > Ui::AnswersHalfWidth)
        return {};
    bool& click = runtime.mouse.pendingClick;
    if (!click)
        return {};
    // 0x62d98c..0x62d9a9: signed division, toward zero; a negative offset returns.
    const std::int32_t answer = offset / Ui::DialogLineStep;
    if (offset < 0 || answer < 0)
        return {};
    // 0x62d9af..0x62d9ca: the count of 0x6d5e28's record in Dialog.bmd (+0x12c), with 1 as the minimum.
    const Tables::DialogTable& table = runtime.dialogTable;
    if (box.dialogIndex < 0 || static_cast<std::size_t>(box.dialogIndex) >= table.size())
        return Core::Unexpected(std::format("0x62d9b8: el diálogo {} no está en Dialog.bmd", box.dialogIndex));
    const Tables::DialogRecord& dialog = table[static_cast<std::size_t>(box.dialogIndex)];
    if (answer >= std::max(dialog.answerCount, 1))
        return {};
    if (static_cast<std::size_t>(answer) >= dialog.next.size())
        return Core::Unexpected(std::format("0x62d9fe: la respuesta {} pasa las 10 de Dialog.bmd", answer));
    click = false;
    PressTheButton(runtime);
    const std::int32_t next = dialog.next[static_cast<std::size_t>(answer)];
    runtime.dialogs.current = runtime.dialogs.pending;
    runtime.dialogs.pending = 0;
    if (next < 0)
        return {};
    return Ui::OpenTheAnswersDialog(runtime.dialogBox, runtime.dialogs, &table, context.texts, next);
}

// 0x62d756..0x62da57: the boxes with buttons; Enter or a button closes them, except 0x8d, which goes on to its answers.
Core::Expected<void, std::string> StepTheButtons(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::DialogBoxState& box = runtime.dialogBox;
    const Ui::Point mouse{context.mouseX, context.mouseY};
    const std::int32_t selected =
        Ui::StepTheDialogButtons(box, mouse, runtime.mouse.pendingClick, runtime.mouse.leftHeld);
    // 0x62d7eb: the arrows read the click after the buttons.
    if (runtime.dialogs.current == Ui::PagedDialog)
    {
        if (auto paged = Ui::StepTheDialogPages(box, context.texts, mouse, runtime.mouse.pendingClick);
            !paged.HasValue())
            return paged;
    }
    // 0x62d911: 0x8d does not reach 0x62da58's close (neither Enter nor its buttons close it).
    if (runtime.dialogs.current == Ui::AnswersDialog)
        return StepTheAnswers(context);
    // 0x62da58..0x62da94.
    if (runtime.ui.enterPressed)
        runtime.ui.enterPressed = false;
    else if (selected == -1)
        return {};
    CloseTheDialog(runtime);
    return {};
}

// 0x625910..0x6259c5: the chaos box mode (0x8e). With the click, which it does not turn off, the button under the
// mouse leaves its number in 0x7d3a114 (the mode 0x5fd4b0 and the box's drawing read) and closes, without sound or
// MouseUpdateTime. The drawing puts the buttons 10 pixels lower (Q-C945).
Core::Expected<void, std::string> StepTheChaosMixMode(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (!runtime.mouse.pendingClick)
        return {};
    for (std::int32_t mode = 0; mode < MixModes; ++mode)
    {
        const std::int32_t top = MixModeFirstTop + mode * MixModeStep;
        if (context.mouseY < top || context.mouseY > top + MixModeHeight || context.mouseX < MixModeLeft ||
            context.mouseX > MixModeRight)
            continue;
        runtime.npcWindows.chaosMixMode = mode;
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
        return {};
    }
    return {};
}

// 0x62649e..0x626f06, with the trade open (0x659290(0x20), one rand()): the typed zen against the own (+0xecc of the
// own record, opened at 0x6264b3..0x626784 and closed at 0x626f06..0x62820a) plus what is already in the trade
// (0x78abab8), unsigned. Without it, 0x74 instead of 0x73. With it: the own OK, if it was on, goes off and the C1 04 3C
// goes with 0 in C3 (0x626598..0x626b07, the padding and the serial); with zen in the trade (signed), 0x78abac8 at
// 0x96; the zen goes to 0x57678ac and in the C1 08 3A with the XOR32, without C3 (0x626b2a..0x626eaf); and 0x73 closes.
Core::Expected<void, std::string> OfferTheTypedMoney(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::TradeState& trade = runtime.social.trade;
    const auto typed = static_cast<std::uint32_t>(runtime.social.typedMoney);
    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    if (typed > context.world.OwnCharacter().zen + trade.ownMoney)
    {
        Ui::PushDialog(runtime.dialogs, 0);
        Ui::PushDialog(runtime.dialogs, NotEnoughMoneyDialog);
        return {};
    }
    auto requests = RequestsOf(context, "0x626b07");
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    if (trade.ownOk != 0)
    {
        trade.ownOk = 0;
        context.random.Discard(C3Randoms);
        requests.Value()->SendGamePacket(Protocol::Social::WriteTradeOkButton(0));
    }
    if (static_cast<std::int32_t>(trade.ownMoney) > 0)
        trade.okWait = Social::TradeOkWait;
    trade.sentMoney = typed;
    requests.Value()->SendGamePacket(Protocol::Social::WriteTradeMoney(typed));
    Ui::PushDialog(runtime.dialogs, 0);
    return {};
}

// 0x626460..0x6270ed and 0x628215..0x6282c8: 0x73's OK (the trade zen), only with Enter. With more than 100000000,
// 0x75 in its place (0x6236f0(0) and 0x6236f0(0x75) inline); otherwise, with the trade open, the zen; without the
// trade, the warehouse's (0x6270ed, the 81 by 0x78abacc, Npc::MoveTheWarehouseMoney). At the end, the text fields
// empty (0x553570(0)), length 0x2a, two Tab fields, the zen mode and the chat field at 0, and the button.
Core::Expected<void, std::string> StepTheMoneyOk(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Social::SocialState& social = runtime.social;
    if (social.typedMoney > MaxTypedMoney)
    {
        Ui::PushDialog(runtime.dialogs, 0);
        Ui::PushDialog(runtime.dialogs, TooMuchMoneyDialog);
    }
    else
    {
        GamePanels panels(context.world, context.random);
        if (panels.Registry().IsOpen(Ui::TradePanel))
        {
            if (auto offered = OfferTheTypedMoney(context); !offered.HasValue())
                return offered;
        }
        else if (auto moved = Npc::MoveTheWarehouseMoney(context); !moved.HasValue())
            return moved;
    }
    Chat::ResetTheTextBuffers(runtime.chat.input, false);
    social.dialogFieldLimit = ClosedDialogFieldLimit;
    runtime.chat.input.fieldCount = Chat::ChatFieldCount;
    social.moneyField = 0;
    runtime.ui.chatOpen = false;
    PressTheButton(runtime);
    return {};
}

// 0x6263df..0x62645a and 0x62825c..0x6282c8
Core::Expected<void, std::string> StepTheGenericOk(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t id = runtime.dialogs.current;
    const bool enter = runtime.ui.enterPressed;
    runtime.ui.enterPressed = false;
    bool& click = runtime.mouse.pendingClick;
    if (id != EnterOnlyDialog && InBox(context, OkLeft, OkTop, OkRight - OkLeft, OkBottom - OkTop) && click)
        click = false;
    else if (!enter)
        return {};
    if (std::ranges::find(QuittingDialogs, id) != QuittingDialogs.end())
    {
        // 0x628299: WM_DESTROY frees the audio (0x4b32fd..0x4b3307) before 0x6282c3's sound.
        runtime.ui.destroyRequested = true;
        CloseTheDialog(runtime, false);
        return {};
    }
    // 0x626460..0x6282c8: the trade or warehouse zen, with its tail.
    if (id == EnterOnlyDialog)
        return StepTheMoneyOk(context);
    if (id == ReopeningDialog)
    {
        // 0x628264..0x62827b: it closes and opens the warehouse keypad at 6.
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
        Npc::OpenTheKeypad(runtime, context.random, ReopenedKeypadMode);
        PressTheButton(runtime);
        return {};
    }
    CloseTheDialog(runtime);
    return {};
}

// [x + 0xd5, x + w + 0xd5] × [y + top, y + h + top], signed and edges included.
bool OverTheWarningButton(const Ui::GameUiContext& context, const Ui::DialogButton& button, std::int32_t top)
{
    return context.mouseX >= button.x + WarningButtonsX && context.mouseY >= button.y + top &&
           context.mouseX <= button.x + button.width + WarningButtonsX &&
           context.mouseY <= button.y + button.height + top;
}

// 0x6282dc..0x62843f: the two yes/no buttons. Each starts at its state (1 and 3) and, hovered, adds 1 and, with the
// click, stays chosen. In 0xa8, the third (0x7d36908, stateless) with the mouse and the click sets the first to 2
// (0x6283a1..0x628409). Then the click and the button down at 0 and, with Esc, the second at 1 (at 2 in 0xa8,
// 0x62841e). It returns each one's choice: 0 the yes, 1 the no, 2 0xa8's third and −1 nothing.
std::array<std::int32_t, 2> StepTheWarningButtons(const Ui::GameUiContext& context, bool escape)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::DialogBoxState& box = runtime.dialogBox;
    const std::int32_t id = runtime.dialogs.current;
    const std::int32_t top = id == Ui::ServerTimeDialog ? ServerTimeButtonsY : WarningButtonsY;
    std::array<std::int32_t, 2> chosen{-1, -1};
    for (std::size_t index = 0; index < chosen.size(); ++index)
    {
        Ui::DialogButton& button = box.buttons[index];
        button.state = static_cast<std::int32_t>(1 + 2 * index);
        if (!OverTheWarningButton(context, button, top))
            continue;
        if (runtime.mouse.pendingClick)
            chosen[index] = static_cast<std::int32_t>(index);
        ++button.state;
    }
    if (id == Ui::ItemNameWithLuckDialog && OverTheWarningButton(context, box.thirdButton, top) &&
        runtime.mouse.pendingClick)
        chosen[0] = ThirdChoice;
    runtime.mouse.pendingClick = false;
    runtime.mouse.leftHeld = false;
    if (escape)
        chosen[1] = id == Ui::ItemNameWithLuckDialog ? ThirdChoice : 1;
    return chosen;
}

// 0x628c11, 0x6293ea and 0x629c85: the dialogs' 26 uses the inventory's same checks, wait and C3.
Core::Expected<void, std::string> UseTheDialogItem(const Ui::GameUiContext& context, const Items::InventoryUse& use)
{
    Items::InventoryServices services = context.inventory != nullptr ? *context.inventory : Items::InventoryServices{};
    services.sendUseItem = [&context, where = use.where](const Protocol::Items::UseItem& item)
    {
        if (!context.sendNpcWindow)
            return Core::Expected<void, std::string>(Core::Unexpected(std::format("{}: el 26 sin la sesión", where)));
        return context.sendNpcWindow(Protocol::Items::WriteUseItem(item));
    };
    GamePanels panels(context.world, context.random, GamePanelTables{.texts = context.texts});
    const auto used = Items::UseInventoryItemOn(
        Items::InventoryMoveContext{.world = context.world, .random = context.random, .services = services},
        panels.Registry(), use);
    if (!used.HasValue())
        return used;
    CloseTheDialog(context.world.Runtime());
    return {};
}

Core::Expected<void, std::string> SendTheAnswer(const Ui::GameUiContext& context, Protocol::Outgoing packet,
                                                std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// 0x628521..0x628ac7: 0x9a. The no closes; the yes, with the dialogs' item, sends the AF 01 and closes.
Core::Expected<void, std::string> AnswerTheChaosCastle(const Ui::GameUiContext& context, std::int32_t entry)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (entry == 1)
    {
        CloseTheDialog(runtime);
        return {};
    }
    const std::uint32_t item = runtime.dialogItem.scalar1Low;
    if (item == NoDialogItem)
        return {};
    const auto level = static_cast<std::uint8_t>((item >> ItemLevelShift) & ItemLevelMask);
    const auto slot = static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff);
    if (auto sent =
            SendTheAnswer(context, Protocol::Npc::WriteChaosCastleEntry(level, slot), "0x6289b9: el C1 06 AF 01");
        !sent.HasValue())
        return sent;
    CloseTheDialog(runtime);
    return {};
}

// 0x628b8f..0x629bfd: 0xa8. The 2 (the third or Esc) closes without the sound; 0 and 1 stay in 0x78abb26 and use the
// item.
Core::Expected<void, std::string> AnswerTheLuck(const Ui::GameUiContext& context, std::int32_t entry)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (entry == ThirdChoice)
    {
        runtime.dialogs.current = runtime.dialogs.pending;
        runtime.dialogs.pending = 0;
        runtime.mouse.updateTime = 0;
        runtime.mouse.updateTimeMax = DialogMouseUpdateTimeMax;
        return {};
    }
    runtime.ui.field78abb26 = static_cast<std::uint8_t>(entry);
    return UseTheDialogItem(context, {.slot = runtime.dialogItemCell + Items::FirstInventoryGridSlot,
                                      .where = entry == 0 ? "0x6293ea" : "0x628c11"});
}

// 0x62a42b..0x62a771: 0xaa. The no closes; the yes sends the D0 00 and closes.
Core::Expected<void, std::string> AnswerTheNotice(const Ui::GameUiContext& context, std::int32_t entry)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (entry == 0)
    {
        if (auto sent = SendTheAnswer(context, Protocol::Npc::WriteNoticeYes(), "0x62a65a: el C1 04 D0 00");
            !sent.HasValue())
            return sent;
    }
    CloseTheDialog(runtime);
    return {};
}

// 0x6282dc..0x62a7f6: the yes/no dialogs of 0x6282dc, each choice in the buttons' order. 0x94, 0x98 and 0x99 set
// 0x6c932e to 1 with the first and to 2 with the second, each with the close (the waiting one becomes open) and the
// sound; with both, both in that order.
Core::Expected<void, std::string> StepTheWarning(const Ui::GameUiContext& context, bool escape)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t id = runtime.dialogs.current;
    const std::array<std::int32_t, 2> chosen = StepTheWarningButtons(context, escape);
    for (const std::int32_t entry : chosen)
    {
        if (id == Ui::ItemNameWithLuckDialog)
        {
            if (entry < 0 || entry > ThirdChoice)
                continue;
            if (auto answered = AnswerTheLuck(context, entry); !answered.HasValue())
                return answered;
            continue;
        }
        if (entry != 0 && entry != 1)
            continue;
        const bool yes = entry == 0;
        if (id == Ui::ChaosCastleDialog)
        {
            if (auto answered = AnswerTheChaosCastle(context, entry); !answered.HasValue())
                return answered;
        }
        else if (id == Ui::NoticeQuestionDialog)
        {
            if (auto answered = AnswerTheNotice(context, entry); !answered.HasValue())
                return answered;
        }
        else if (id == WarningDialog || id == ConfirmDialog || id == Ui::MoneyTextDialog)
        {
            // 0x62a779..0x62a7e6.
            runtime.social.trade.warningByte = yes ? WarningYes : WarningNo;
            CloseTheDialog(runtime);
        }
        else if (id == Ui::ItemNameDialog)
        {
            // 0x628ae4..0x628b63: the yes closes and opens 0xa8 with the dialogs' item.
            if (!yes)
            {
                CloseTheDialog(runtime);
                continue;
            }
            runtime.dialogs.current = runtime.dialogs.pending;
            runtime.dialogs.pending = 0;
            if (auto opened = Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, LuckDialogLines,
                                                 LuckDialogText, Ui::ItemNameWithLuckDialog, &runtime.dialogItem);
                !opened.HasValue())
                return opened;
            PressTheButton(runtime);
        }
        else if (id == Ui::ServerTimeDialog)
        {
            // 0x62846f..0x628500: the jewel mix question.
            if (auto answered = ChaosMix::AnswerTheJewelMix(context, yes); !answered.HasValue())
                return answered;
        }
        else if (yes)
        {
            // 0x629c57..0x629c85.
            if (auto used =
                    UseTheDialogItem(context, {.slot = runtime.dialogBox.jewelCell + Items::FirstInventoryGridSlot,
                                               .target = runtime.dialogBox.jewelTarget,
                                               .where = "0x629c85"});
                !used.HasValue())
                return used;
        }
        else
        {
            // 0x629c41..0x629c4d: the no drops the jewel and puts it back in its place.
            runtime.dialogBox.jewelCell = 0;
            runtime.dialogBox.jewelTarget = 0;
            const Items::InventoryServices none{};
            const Items::InventoryServices& services = context.inventory != nullptr ? *context.inventory : none;
            if (auto returned = Items::ReturnTheCursorItem(Items::InventoryMoveContext{
                    .world = context.world, .random = context.random, .services = services});
                !returned.HasValue())
                return returned;
            CloseTheDialog(runtime);
        }
    }
    return {};
}

// The yes (1) or no (0) answer to the invitation (0x77, the C1 05 41 with 0x78aba4c's key in C3) or to the trade
// request (0x78, the C1 04 37 with the XOR32, in clear).
Core::Expected<void, std::string> Answer(const Ui::GameUiContext& context, std::int32_t id, std::uint8_t accepted,
                                         std::string_view where)
{
    auto requests = RequestsOf(context, where);
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (id == PartyRequestDialog)
    {
        context.random.Discard(C3Randoms);
        const auto key = static_cast<std::uint16_t>(runtime.social.party.requestKey & 0xffff);
        requests.Value()->SendGamePacket(Protocol::Party::WritePartyRequestResult(accepted, key));
        return {};
    }
    requests.Value()->SendGamePacket(Protocol::Social::WriteTradeResponse(accepted));
    return {};
}

// 0x62b372 and 0x62c567: the ten bytes of 0x7798bf8 (two DWORDs and a WORD) and their length (0x7758590) at 0.
void ClearTheTypedNumber(Chat::ChatInputState& input)
{
    std::fill_n(input.messageBuffer.begin(), Protocol::Guild::LeaveNumberSize, '\0');
    input.messageLength = 0;
}

// 0x62a878..0x62b38d, 0x7d's yes: with 0x6c7b8c at 1, GetText of the number EDIT ([0x5763508]) into 0x7798bf8 (0xff)
// and SetText(0); the C1 17 53 with the ten bytes of slot 0x6d5e2c of 0x787a5e8 (of 0xe) and the ten of 0x7798bf8,
// with the XOR32 and in clear through the socket (no rand()), and those ten bytes and their length at 0. The EDIT
// stays shown and focused (Q-C1295).
Core::Expected<void, std::string> SendTheLeave(const Ui::GameUiContext& context)
{
    auto requests = RequestsOf(context, "0x62a878");
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Chat::ChatInputState& input = runtime.chat.input;
    Chat::ReadTheNumberField(input, input.messageBuffer);
    Chat::ClearTheNumberField(input);
    // 0x62a91c: 0x6d5e2c is −1 in the .data and 0x64faa0 sets it (−1 without the name): outside the 80 slots, the
    // original reads other memory (Q-C1296).
    const std::int32_t member = runtime.guild.kickMember;
    if (member < 0 || static_cast<std::size_t>(member) >= Guild::GuildMemberSlots)
        return Core::Unexpected(std::format("0x62a91c: el lugar {} de 0x6d5e2c lee fuera de los {} miembros de "
                                            "0x787a5e8 (Q-C1296)",
                                            member, Guild::GuildMemberSlots));
    const Guild::GuildMemberSlot& slot = runtime.guild.members[static_cast<std::size_t>(member)];
    Protocol::FrontEnd::NameBytes name{};
    for (std::size_t index = 0; index < name.size(); ++index)
        name[index] = static_cast<std::byte>(slot.name[index]);
    Protocol::Guild::LeaveNumber number{};
    for (std::size_t index = 0; index < number.size(); ++index)
        number[index] = static_cast<std::byte>(input.messageBuffer[index]);
    requests.Value()->SendGuildPacket(Protocol::Guild::WriteLeave(name, number));
    ClearTheTypedNumber(input);
    return {};
}

// 0x62b39c (0x7f's yes) and 0x62c5c2 (the no): the answer to the 61's war request, the C1 04 61 [1 or 0] with the
// XOR32 and in clear through the socket, without rand().
Core::Expected<void, std::string> AnswerTheWar(const Ui::GameUiContext& context, bool accept)
{
    auto requests = RequestsOf(context, accept ? "0x62b39c" : "0x62c5c2");
    if (!requests.HasValue())
        return Core::Unexpected(std::move(requests).Error());
    requests.Value()->SendGuildPacket(Protocol::Guild::WriteWarAnswer(accept));
    return {};
}

// 0x62aca2..0x62b38d: type 1, atoi of 0x78ab9fc and the ten bytes of the chat buffer, with XOR32 and C3.
Core::Expected<void, std::string> SendTheWarehouseConfirmation(const Ui::GameUiContext& context)
{
    auto requests = RequestsOf(context, "0x62aca2");
    if (!requests.HasValue())
        return Core::Unexpected(requests.Error());
    const auto& runtime = context.world.Runtime();
    const auto& typed = runtime.npcWindows.typedNumber;
    const auto password = static_cast<std::uint16_t>(static_cast<std::uint32_t>(Chat::AtoiLikeTheCrt(std::string_view(
                                                         reinterpret_cast<const char*>(typed.data()), typed.size()))) &
                                                     0xffff);
    Protocol::Npc::WarehouseNumber number{};
    for (std::size_t index = 0; index < number.size(); ++index)
        number[index] = static_cast<std::byte>(runtime.chat.input.messageBuffer[index]);
    context.random.Discard(C3Randoms);
    requests.Value()->SendGamePacket(Protocol::Npc::WriteWarehousePassword(1, password, number));
    ClearTheTypedNumber(context.world.Runtime().chat.input);
    return {};
}

// The yes (0x62a86b, the table 0x62daf4): the party invitation (0x62bb1d), the trade request (0x62c20a), 0x7d's
// number (0x62a878) and 0x7f's war request (0x62b39c).
Core::Expected<void, std::string> AnswerYes(const Ui::GameUiContext& context, std::int32_t id)
{
    if (id == LeaveNumberDialog)
        return SendTheLeave(context);
    if (id == 0x95)
        return SendTheWarehouseConfirmation(context);
    if (id == Ui::IgnoredOverChaosCastle)
        return AnswerTheWar(context, true);
    return Answer(context, id, 1, id == PartyRequestDialog ? "0x62bb1d" : "0x62c20a");
}

// 0x62a7f7..0x62d745: the yes, which spends the click and closes (0x62c4ac), and then the no with the dialog left,
// which also enters with Esc. With another case of the no table, its range stops; with no case, only the close.
Core::Expected<void, std::string> StepTheYesNo(const Ui::GameUiContext& context, bool escape)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.mouse.pendingClick;
    std::int32_t id = runtime.dialogs.current;
    const auto inButton = [&context](std::int32_t left, std::int32_t width)
    { return InBox(context, left, YesNoTop, width, YesNoBottom - YesNoTop); };
    const auto narrowYes = id == 0x7d || id == 0x95;
    if (inButton(narrowYes ? NarrowYesLeft : YesLeft, narrowYes ? NarrowWidth : YesNoWidth) && click)
    {
        click = false;
        if (auto said = AnswerYes(context, id); !said.HasValue())
            return said;
        CloseTheDialog(runtime);
        id = runtime.dialogs.current;
    }
    const bool narrowNo = id == 0x71 || id == 0x7d || id == 0x95;
    if (inButton(narrowNo ? NarrowNoLeft : NoLeft, narrowNo ? NarrowWidth : YesNoWidth) && click)
        click = false;
    else if (!escape)
        return {};
    if (const auto other = std::ranges::find(OtherNoCases, id, &OtherNoCase::id); other != OtherNoCases.end())
        return Core::Unexpected(
            std::format("0x62c560: el no del diálogo {:#x} ({}) es {} (08 P-C363)", id, other->address, other->owner));
    // 0x62c567: 0x7d's no clears 0x7798bf8's number, but not the EDIT (Q-C1295).
    if (id == LeaveNumberDialog || id == 0x95)
        ClearTheTypedNumber(runtime.chat.input);
    // 0x62c5c2: 0x7f's no sends the C1 04 61 00 and leaves the guild without war or rival (0x4c1800, which it calls
    // twice at 0x62c854 and 0x62c86a; the second changes nothing).
    if (id == Ui::IgnoredOverChaosCastle)
    {
        if (auto said = AnswerTheWar(context, false); !said.HasValue())
            return said;
        Guild::ClearTheWar(context.world);
    }
    if (id == PartyRequestDialog || id == Ui::ForcedDialog)
    {
        if (auto said = Answer(context, id, 0, id == PartyRequestDialog ? "0x62cd52" : "0x62d4a2"); !said.HasValue())
            return said;
    }
    CloseTheDialog(runtime);
    return {};
}
// 0x62601e..0x6260d0: inclusive edges, without sound, MouseUpdateTime or consuming the click.
void StepTheWarOutcome(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    constexpr std::int32_t Left = 0x11d;
    constexpr std::int32_t Right = 0x163;
    constexpr std::int32_t Top = 0x106;
    constexpr std::int32_t Bottom = 0x11a;
    if (!runtime.mouse.pendingClick || context.mouseX < Left || context.mouseX > Right || context.mouseY < Top ||
        context.mouseY > Bottom)
        return;
    runtime.events.guildWarOutcome = {};
    runtime.events.timeValue = 0;
    Ui::PushDialog(runtime.dialogs, 0);
}
} // namespace

std::span<const DialogRoute> DialogRoutes()
{
    return Routes;
}

DialogRoute DialogRouteOf(std::int32_t id)
{
    const auto found = std::ranges::find(Routes, id, &DialogRoute::id);
    if (found != Routes.end())
        return *found;
    return DialogRoute{.id = id, .step = DialogStep::GenericOk, .address = "0x6263df", .wave = ""};
}

Core::Expected<void, std::string> StepTheOpenDialog(const Ui::GameUiContext& context, bool escape)
{
    const std::int32_t id = context.world.Runtime().dialogs.current;
    if (id == 0)
        return {};
    const DialogRoute route = DialogRouteOf(id);
    switch (route.step)
    {
    case DialogStep::EscapeMenu:
        return StepTheEscapeMenu(context);
    case DialogStep::Options:
        return StepTheOptions(context);
    case DialogStep::Buttons:
        return StepTheButtons(context);
    case DialogStep::GenericOk:
        return StepTheGenericOk(context);
    case DialogStep::YesNo:
        return StepTheYesNo(context, escape);
    case DialogStep::ChaosMixMode:
        return StepTheChaosMixMode(context);
    case DialogStep::JewelMenu:
        return ChaosMix::StepTheJewelMenu(context);
    case DialogStep::JewelChoice:
        return ChaosMix::StepTheJewelChoice(context);
    case DialogStep::JewelUnmix:
        return ChaosMix::StepTheJewelUnmix(context);
    case DialogStep::JewelResult:
        return ChaosMix::StepTheJewelResult(context);
    case DialogStep::ConfirmBox:
        return PersonalShop::StepTheConfirmBox(context);
    case DialogStep::ShopDialog:
        return PersonalShop::StepTheShopDialog(context);
    case DialogStep::Warning:
        return StepTheWarning(context, escape);
    case DialogStep::WarOutcome:
        StepTheWarOutcome(context);
        return {};
    case DialogStep::Cut:
        break;
    }
    return Core::Unexpected(std::format("{}: el diálogo {:#x} es de la {}", route.address, id, route.wave));
}

Core::Expected<Protocol::Player::OptionBody, std::string>
OptionBodyOf(Kernel::WorldState& world, Crt::CrtRandom& random, std::optional<std::uint8_t> chosenSlot)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    if (!chosenSlot || *chosenSlot >= Kernel::OwnSkillKeyPlaces)
        return Core::Unexpected(std::string("0x61c2c6: sin elegido (0x6d5edc), el F3 30 lee fuera de las teclas de "
                                            "las skills (+0x16c del registro propio)"));
    Protocol::Player::OptionBody body{};
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        const Kernel::OwnCharacterRecord& record = world.OwnCharacter();
        const auto& keys = record.skillKeys[*chosenSlot];
        for (std::size_t at = 0; at < OptionKeyPlaces; ++at)
        {
            const std::uint8_t key = keys[at];
            if (key == NoKey)
                continue;
            if (key >= body.size())
                return Core::Unexpected(std::format("0x61c2e8: la tecla {:#x} del lugar {:#x} escribe fuera de los "
                                                    "24 bytes de la pila de 0x61c210",
                                                    key, at));
            body[key] = Kernel::OwnSkillByte(record, at);
        }
    }
    const Ui::GameUiState& ui = runtime.ui;
    std::uint8_t flags = body[FlagsByte];
    flags = static_cast<std::uint8_t>(flags | (ui.autoAttack ? AutoAttackOn : AutoAttackOff));
    flags = static_cast<std::uint8_t>(flags | (runtime.chat.noticeSound ? NoticeSoundOn : NoticeSoundOff));
    if (!runtime.chat.slidesOn)
        flags = static_cast<std::uint8_t>(flags | SlidesOff);
    body[FlagsByte] = flags;
    for (std::size_t slot = 0; slot < runtime.hud.quickSlots.types.size(); ++slot)
        body[FirstQuickSlotByte + slot] = static_cast<std::uint8_t>(runtime.hud.quickSlots.types[slot] & 0xff);
    // 0x61c3ad: the log's vtable + 0x38 (0x472010, +0x104), divided signed.
    std::uint8_t rows = static_cast<std::uint8_t>((runtime.chat.log.rows / RowsPerStep) & 0xff);
    if (ui.chatModes.placement == 0)
        rows = 0;
    // 0x61c3d3..0x61c3df: FLD of the float, FMUL by 10.0f and _ftol: the product is exact and is truncated.
    const auto alpha = static_cast<std::uint8_t>(
        static_cast<std::int32_t>(static_cast<double>(runtime.chat.log.alpha) * AlphaSteps) & 0xff);
    body[ChatWindowByte] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(rows << NibbleShift) | alpha);
    return body;
}
} // namespace Client::Hud::Dialogs
