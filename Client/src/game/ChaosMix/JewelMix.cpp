#include "ChaosMix/JewelMix.h"

#include "Core/Expected.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/ChaosMix/JewelMix.h"
#include "Protocol/Outgoing.h"
#include "Chat/GameMessages.h"
#include "Hud/GameUi.h"
#include "ChaosMix/JewelBundleList.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnInventory.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "Kernel/ItemState.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::ChaosMix
{
namespace
{
// 0x41ed10(0x19, 0, 0) and 0x6c8dcc: the dialogs' button sound and the maximum of MouseUpdateTime.
constexpr Audio::SoundId DialogSound = Sound::ButtonClickSound;
constexpr std::int32_t DialogMouseUpdateTimeMax = 6;
// 0x6e5794: mix (the 0xa2) or unmix (the 0xa4).
constexpr std::int32_t MixMode = 0;
constexpr std::int32_t UnmixMode = 1;
constexpr std::int32_t ChoiceDialog = 0xa2;
constexpr std::int32_t UnmixDialog = 0xa4;
// 0x6284e5: the one that waits for the BC after the 0xa3's yes.
constexpr std::int32_t WaitingDialog = 0xa5;
// 0x6c62e0 and 0x6c62e1 unchosen.
constexpr std::uint8_t Unchosen = 0xff;
// 0x6c932e: the 0xa3's answers (0x6284b6 and 0x628490).
constexpr std::uint8_t YesAnswer = 1;
constexpr std::uint8_t NoAnswer = 2;

// 0x402bc0: the jewels and the bundles.
constexpr std::uint16_t BlessJewel = 0x1c0d;
constexpr std::uint16_t SoulJewel = 0x1c0e;
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint8_t BlessKind = 0;
constexpr std::uint8_t SoulKind = 1;
// 0x402c61, 0x402cc9 and 0x402cdf: the reasons for a no.
constexpr std::uint8_t MissingJewels = 2;
constexpr std::uint8_t NoBundle = 4;
constexpr std::uint8_t OtherRefusal = 5;
// 0x402cb2: what it leaves when the mix is possible.
constexpr std::uint8_t Possible = 100;
// 0x402c7e: the bundle's cell, 0 to 0x3f.
constexpr std::int32_t BundleCells = 0x40;
// 0x402cf9 and 0x402d04..0x402d1a: the price, 1000000 to unmix and jewels · 5⁵ · 16 to mix.
constexpr std::int32_t UnmixPrice = 1000000;
constexpr std::int32_t PricePerJewel = 50000;
// 0x402b8c and 0x402b95: 0x402b70's error warnings (Text.bmd).
constexpr std::size_t MissingJewelsText = 1817;
constexpr std::size_t NoBundleText = 1818;
// 0x401f55: the bundle's cell becomes an inventory slot.
constexpr std::int32_t InventorySlotBase = 12;
// 0x402500: the BC 00's bundle, in tens.
constexpr std::int32_t JewelsPerBundle = 10;

// The buttons, from 0x6259df..0x625b07 (0xa1), 0x625b3c..0x625c7e (0xa2) and 0x625fad..0x626002 (0xa6), edges
// included.
constexpr std::int32_t ButtonLeft = 245;
constexpr std::int32_t ButtonRight = 395;
constexpr std::int32_t MenuFirstTop = 190;
constexpr std::int32_t MenuStep = 65;
constexpr std::int32_t ButtonHeight = 35;
constexpr std::int32_t KindTop = 185;
constexpr std::int32_t KindHeight = 25;
constexpr std::int32_t KindWidth = 80;
constexpr std::int32_t KindRowStep = 40;
constexpr std::int32_t CountStep = 45;
constexpr std::array<std::uint8_t, 3> Counts{10, 20, 30};
constexpr std::int32_t CloseLeft = 279;
constexpr std::int32_t CloseRight = 360;
constexpr std::int32_t MenuCloseTop = 320;
constexpr std::int32_t MenuCloseBottom = 345;
constexpr std::int32_t ChoiceCloseTop = 365;
constexpr std::int32_t ChoiceCloseBottom = 390;
constexpr std::int32_t ResultLeft = 287;
constexpr std::int32_t ResultRight = 352;
// 0x625d00..0x625d6f: the 0xa4's two jewels, 80 wide from x = 235, with y from 160 to 185.
constexpr float UnmixKindLeft = 235.0f;
constexpr float UnmixKindWidth = 80.0f;
constexpr float UnmixKindTop = 160.0f;
constexpr float UnmixKindBottom = 185.0f;
// 0x625f4d..0x625f96: without a jewel, the 0xa4's cancel.
constexpr float UnmixCloseLeft = 279.5f;
constexpr float UnmixCloseRight = 359.5f;
constexpr float UnmixCloseTop = 200.0f;
constexpr float UnmixCloseBottom = 225.0f;
// 0x625dc3..0x625f37: with the jewel, the OK and the cancel of 80 × 25, from 10 below the list (its bottom edge at
// height + 0xf0).
constexpr std::int32_t UnmixListBottom = 0xf0;
constexpr float UnmixButtonsDrop = 10.0f;
constexpr float UnmixButtonHeight = 25.0f;
constexpr float UnmixOkLeft = 234.5f;
constexpr float UnmixOkRight = 314.5f;
constexpr float UnmixCancelLeft = 324.5f;
constexpr float UnmixCancelRight = 404.5f;
// 0x402a20: the bundle's level, (+4 >> 3) & 0xf.
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t ResultTop = 170;
constexpr std::int32_t ResultBottom = 195;

// x in [left, right] and y in [top, bottom], edges included: the FCOMs of the floats with the integer mouse.
bool Within(const Hud::GameUiContext& context, std::int32_t left, std::int32_t right, std::int32_t top,
            std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX <= right && context.mouseY >= top && context.mouseY <= bottom;
}

// The same with the edges in float (0x625d17..0x625d59 and 0x625f4d..0x625f96).
bool WithinFloat(const Hud::GameUiContext& context, float left, float right, float top, float bottom)
{
    const auto x = static_cast<float>(context.mouseX);
    const auto y = static_cast<float>(context.mouseY);
    return x >= left && x <= right && y >= top && y <= bottom;
}

void LeaveTheMouseTime(Kernel::WorldRuntime& runtime)
{
    runtime.mouse.updateTime = 0;
    runtime.mouse.updateTimeMax = DialogMouseUpdateTimeMax;
}

// The waiting dialog becomes the open one (0x6236f0(0) and the MOVs of 0x625a49, 0x625cbc and 0x6284ce).
void CloseTheDialog(Ui::DialogState& dialogs)
{
    dialogs.current = dialogs.pending;
    dialogs.pending = 0;
}

// Opens `id` as 0x625a5f..0x625a97 and 0x6284e3 do: waiting if one is open.
void OpenAfter(Ui::DialogState& dialogs, std::int32_t id)
{
    if (dialogs.current != 0)
        dialogs.pending = id;
    else
        dialogs.current = id;
}

Core::Expected<void, std::string> Send(const JewelMixSender& send, Protocol::Outgoing packet, std::string_view what)
{
    if (!send)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return send(std::move(packet));
}

Core::Expected<void, std::string> Send(const Hud::GameUiContext& context, Protocol::Outgoing packet,
                                       std::string_view what)
{
    return Send(context.sendNpcWindow, std::move(packet), what);
}

// 0x402eb0: 0x402e10, the dialog closed and the C1 03 31 in the clear.
Core::Expected<void, std::string> CloseTheJewelMix(Kernel::WorldState& world, const JewelMixSender& send)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    ResetTheJewelMix(runtime.npcWindows);
    Ui::PushDialog(runtime.dialogs, 0);
    return Send(send, Protocol::Session::WriteWindowClose(), "0x402f91: el C1 03 31");
}

Core::Expected<void, std::string> CloseTheJewelMix(const Hud::GameUiContext& context)
{
    return CloseTheJewelMix(context.world, context.sendNpcWindow);
}

Core::Expected<void, std::string> ReturnToTheChoice(const Hud::GameUiContext& context)
{
    return ReturnToTheJewelChoice(context.world, context.sendNpcWindow);
}

// 0x402cf0
void PriceTheMix(Npc::JewelMixState& jewels)
{
    if (jewels.mode != MixMode)
        jewels.price = UnmixPrice;
    else
        jewels.price = static_cast<std::int8_t>(jewels.count) * PricePerJewel;
}

// 0x402bc0: whether there is enough to mix. To mix it counts the jewels of the kind in the 64 cells, row by row, and
// it is enough once the count (a byte) reaches 0x6c62e1, signed; to unmix it checks the bundle in cell 0x6c62d8.
Core::Expected<bool, std::string> CheckTheJewels(Kernel::WorldState& world)
{
    Npc::JewelMixState& jewels = world.Runtime().npcWindows.jewels;
    const auto& cells = world.Inventory().cells;
    jewels.field6e57a0 = 0;
    jewels.counted = 0;
    const auto possible = [&]
    {
        jewels.field6e57a0 = Possible;
        PriceTheMix(jewels);
        return true;
    };
    if (jewels.mode == MixMode)
    {
        const std::int32_t needed = static_cast<std::int8_t>(jewels.count);
        for (const Kernel::ItemState& cell : cells)
        {
            if ((cell.id == SoulJewel && jewels.kind == SoulKind) ||
                (cell.id == BlessJewel && jewels.kind == BlessKind))
                ++jewels.counted;
            if (jewels.counted == needed)
                return possible();
        }
        if (jewels.counted >= needed && jewels.count != Unchosen)
        {
            jewels.refusal = OtherRefusal;
            return false;
        }
        jewels.field6e57a0 = 0;
        jewels.refusal = MissingJewels;
        return false;
    }
    if (jewels.mode != UnmixMode)
    {
        jewels.refusal = OtherRefusal;
        return false;
    }
    // 0x402c74..0x402c81: −1 and from 0x40 on, no; another negative would read before the grid.
    if (jewels.cell < -1)
        return Core::Unexpected(std::format("0x402c83: la casilla {} lee antes del inventario", jewels.cell));
    if (jewels.cell != -1 && jewels.cell < BundleCells)
    {
        const std::uint16_t id = cells[static_cast<std::size_t>(jewels.cell)].id;
        if ((id == BlessBundle && jewels.kind == BlessKind) || (id == SoulBundle && jewels.kind == SoulKind))
        {
            jewels.counted = 1;
            return possible();
        }
    }
    jewels.refusal = NoBundle;
    jewels.field6e57a0 = 0;
    return false;
}

// 0x402b70: whether there is enough; if not, the warning (2 and 4) and 0x402e50's return.
Core::Expected<bool, std::string> AskForTheJewels(const Hud::GameUiContext& context)
{
    auto enough = CheckTheJewels(context.world);
    if (!enough.HasValue() || enough.Value())
        return enough;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::uint8_t refusal = runtime.npcWindows.jewels.refusal;
    if (refusal == MissingJewels || refusal == NoBundle)
    {
        const std::size_t index = refusal == MissingJewels ? MissingJewelsText : NoBundleText;
        const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
        if (!text)
            return Core::Unexpected(std::format("0x402b9f: sin el texto {} de Text.bmd", index));
        if (context.measure == nullptr)
            return Core::Unexpected(std::format("0x402b9f: el mensaje del texto {} sin medidor", index));
        // 0x555190(0x6e57a4, text, 2): the name is an empty .bss string.
        if (auto added = Chat::AddGameMessage(runtime, *context.measure, context.screen, {}, *text, Ui::ErrorMessage);
            !added.HasValue())
            return Core::Unexpected(std::move(added).Error());
    }
    if (auto back = ReturnToTheChoice(context); !back.HasValue())
        return Core::Unexpected(std::move(back).Error());
    return false;
}

// 0x4029e0: the BC, once and without a pending "no".
Core::Expected<void, std::string> SendTheJewelMix(const Hud::GameUiContext& context)
{
    Npc::NpcWindowState& windows = context.world.Runtime().npcWindows;
    Npc::JewelMixState& jewels = windows.jewels;
    if (windows.field6e5798 == 1 || jewels.refusal != 0)
        return {};
    windows.field6e5798 = 1;
    if (jewels.mode == MixMode)
    {
        // 0x40276b..0x402794: the signed bundle / 10, toward zero, minus 1, in a byte.
        const auto bundle = static_cast<std::uint8_t>(static_cast<std::int8_t>(jewels.count) / JewelsPerBundle - 1);
        return Send(context, Protocol::ChaosMix::WriteJewelMix(jewels.kind, bundle), "0x402500: el BC 00");
    }
    // 0x401f4e..0x401f5a: the cell stays as a slot in the global.
    jewels.cell += InventorySlotBase;
    return Send(context,
                Protocol::ChaosMix::WriteJewelUnmix(jewels.kind, static_cast<std::uint8_t>(jewels.level),
                                                    static_cast<std::uint8_t>(jewels.cell)),
                "0x401f30: el BC 01");
}

// 0x625cbc..0x625ce5: closes and opens the 0xa3 with its text.
Core::Expected<void, std::string> AskTheQuestion(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const Npc::JewelMixState& jewels = runtime.npcWindows.jewels;
    CloseTheDialog(runtime.dialogs);
    const Ui::DialogTextArguments arguments{.jewelMode = jewels.mode,
                                            .jewelKind = jewels.kind,
                                            .jewelCount = jewels.counted,
                                            .jewelLevel = jewels.level,
                                            .jewelPrice = jewels.price};
    if (auto opened = Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.texts, 0, 0, Ui::ServerTimeDialog,
                                         nullptr, &arguments);
        !opened.HasValue())
        return opened;
    LeaveTheMouseTime(runtime);
    return {};
}

// 0x402a10 and 0x402a20: 0x6e5660's list emptied (0x482f80) and filled, cell by cell, with the chosen jewel's
// bundles (0x181e with 0, 0x181f with 1) and their level (0x46e940).
void FillTheBundles(Kernel::WorldState& world)
{
    Npc::JewelMixState& jewels = world.Runtime().npcWindows.jewels;
    ClearTheBundles(jewels.bundles);
    const auto& cells = world.Inventory().cells;
    for (std::size_t cell = 0; cell < cells.size(); ++cell)
    {
        const std::uint16_t id = cells[cell].id;
        if ((id != BlessBundle || jewels.kind != BlessKind) && (id != SoulBundle || jewels.kind != SoulKind))
            continue;
        AddTheBundle(jewels.bundles, static_cast<std::int32_t>(cell),
                     static_cast<std::uint8_t>((cells[cell].scalar1Low >> LevelShift) & LevelMask));
    }
}

// 0x402a90(cell, level): stores them in 0x6c62d8 and 0x6c62dc and asks with 0x402b70; if enough, closes and opens the
// 0xa3 with its text, like the 0xa2 (0x402aad..0x402acf).
Core::Expected<void, std::string> AskForTheBundle(const Hud::GameUiContext& context, std::int32_t cell,
                                                  std::int32_t level)
{
    Npc::JewelMixState& jewels = context.world.Runtime().npcWindows.jewels;
    jewels.cell = cell;
    jewels.level = level;
    auto enough = AskForTheJewels(context);
    if (!enough.HasValue())
        return Core::Unexpected(std::move(enough).Error());
    if (!enough.Value())
        return {};
    return AskTheQuestion(context);
}
} // namespace

// 0x402e10: everything to its startup value.
void ResetTheJewelMix(Npc::NpcWindowState& windows)
{
    Npc::JewelMixState& jewels = windows.jewels;
    jewels.mode = MixMode;
    windows.field6e5798 = 0;
    jewels.refusal = 0;
    jewels.kind = Unchosen;
    jewels.count = Unchosen;
    jewels.counted = 0;
    jewels.price = -1;
    jewels.field6e57a0 = 0;
    jewels.cell = -1;
    jewels.level = -1;
}

Core::Expected<void, std::string> ReturnToTheJewelChoice(Kernel::WorldState& world, const JewelMixSender& send)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Npc::NpcWindowState& windows = runtime.npcWindows;
    if (windows.field6e5798 == 1)
        windows.field6e5798 = 0;
    Ui::PushDialog(runtime.dialogs, 0);
    windows.jewels.refusal = 0;
    windows.jewels.kind = Unchosen;
    if (windows.jewels.mode == MixMode)
        Ui::PushDialog(runtime.dialogs, ChoiceDialog);
    else if (windows.jewels.mode == UnmixMode)
        Ui::PushDialog(runtime.dialogs, UnmixDialog);
    else
        return CloseTheJewelMix(world, send);
    return {};
}

Core::Expected<void, std::string> StepTheJewelMenu(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.mouse.pendingClick;
    if (!click)
        return {};
    click = false;
    for (std::int32_t button = 0; button < 2; ++button)
    {
        const std::int32_t top = MenuFirstTop + button * MenuStep;
        if (!Within(context, ButtonLeft, ButtonRight, top, top + ButtonHeight))
            continue;
        // 0x625a49..0x625ab1.
        CloseTheDialog(runtime.dialogs);
        OpenAfter(runtime.dialogs, button == 0 ? ChoiceDialog : UnmixDialog);
        runtime.npcWindows.jewels.mode = button == 0 ? MixMode : UnmixMode;
        LeaveTheMouseTime(runtime);
        break;
    }
    // 0x625abb..0x625b0d.
    if (Within(context, CloseLeft, CloseRight, MenuCloseTop, MenuCloseBottom))
        return CloseTheJewelMix(context);
    return {};
}

Core::Expected<void, std::string> StepTheJewelChoice(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Npc::JewelMixState& jewels = runtime.npcWindows.jewels;
    bool& click = runtime.mouse.pendingClick;
    if (!click)
        return {};
    click = false;
    // 0x625b42: BL, the jewel, which is stored at the end.
    std::uint8_t kind = jewels.kind;
    for (std::int32_t button = 0; button < 2; ++button)
    {
        // 0x625b59..0x625bbf: x from 245 + 80 · k, while the drawing places them from 235 + 90 · k (07 Q-C951).
        const std::int32_t left = ButtonLeft + button * KindWidth;
        if (!Within(context, left, left + KindWidth, KindTop, KindTop + KindHeight))
            continue;
        kind = button != 0 ? SoulKind : BlessKind;
        LeaveTheMouseTime(runtime);
        break;
    }
    // 0x625bf9: with BL negative (no jewel), the bundles are not checked.
    std::int32_t top = KindTop + KindRowStep;
    for (std::size_t count = 0; count < Counts.size() && static_cast<std::int8_t>(kind) >= 0; ++count)
    {
        if (!Within(context, ButtonLeft, ButtonRight, top, top + ButtonHeight))
        {
            top += CountStep;
            continue;
        }
        // 0x625c83..0x625cef.
        jewels.kind = kind;
        jewels.count = Counts[count];
        auto enough = AskForTheJewels(context);
        if (!enough.HasValue())
            return Core::Unexpected(std::move(enough).Error());
        if (enough.Value())
        {
            if (auto asked = AskTheQuestion(context); !asked.HasValue())
                return asked;
        }
        // 0x625c5f: the close, with the jewel already stored.
        if (Within(context, CloseLeft, CloseRight, ChoiceCloseTop, ChoiceCloseBottom))
            return CloseTheJewelMix(context);
        return {};
    }
    // 0x625c57.
    jewels.kind = kind;
    if (Within(context, CloseLeft, CloseRight, ChoiceCloseTop, ChoiceCloseBottom))
        return CloseTheJewelMix(context);
    return {};
}

Core::Expected<void, std::string> StepTheJewelUnmix(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Npc::JewelMixState& jewels = runtime.npcWindows.jewels;
    bool& click = runtime.mouse.pendingClick;
    // 0x625d0c: the click, read before the jewels (DL).
    const bool clicked = click;
    float left = UnmixKindLeft;
    for (std::uint8_t kind = 0; kind < 2; ++kind, left += UnmixKindWidth)
    {
        if (!WithinFloat(context, left, left + UnmixKindWidth, UnmixKindTop, UnmixKindBottom) || !clicked)
            continue;
        // 0x625d73..0x625dac.
        click = false;
        jewels.kind = kind;
        FillTheBundles(context.world);
        if (jewels.bundles.rows.empty())
        {
            const std::optional<std::string_view> text = Ui::TextAt(context.texts, NoBundleText);
            if (!text)
                return Core::Unexpected(std::format("0x625d9a: sin el texto {} de Text.bmd", NoBundleText));
            if (context.measure == nullptr)
                return Core::Unexpected(std::format("0x625da4: el mensaje del texto {} sin medidor", NoBundleText));
            // 0x555190(0x7d3a158, text, 2): the name is a .bss string only this PUSH reads (*inferred*: empty).
            if (auto added =
                    Chat::AddGameMessage(runtime, *context.measure, context.screen, {}, *text, Ui::ErrorMessage);
                !added.HasValue())
                return Core::Unexpected(std::move(added).Error());
            if (auto back = ReturnToTheChoice(context); !back.HasValue())
                return back;
        }
        break;
    }
    if (jewels.kind == Unchosen)
    {
        // 0x625f4d..0x625f96 and 0x625f1f..0x625f37: the cancel, without clearing the click (07 Q-C952).
        if (WithinFloat(context, UnmixCloseLeft, UnmixCloseRight, UnmixCloseTop, UnmixCloseBottom) && click)
            return CloseTheJewelMix(context);
        return {};
    }
    // 0x625dbe: the list's step. The left double click on a row asks for its bundle from inside (0x46f297).
    if (const BundleListStep listed = StepTheBundleList(context); listed.asked)
    {
        if (auto asked = AskForTheBundle(context, listed.asked->cell, static_cast<std::int8_t>(listed.asked->level));
            !asked.HasValue())
            return asked;
    }
    // 0x625dc3..0x625eb8: the OK, x in [234.5, 314.5] and y in [height + 0xf0 + 10, + 25]; the click (which it clears)
    // asks for the chosen one (vtable + 0x64) with its signed level (0x625ea7); without a chosen one, nothing.
    const float top = static_cast<float>(jewels.bundles.height + UnmixListBottom) + UnmixButtonsDrop;
    if (WithinFloat(context, UnmixOkLeft, UnmixOkRight, top, top + UnmixButtonHeight) && click)
    {
        click = false;
        if (const std::optional<Npc::JewelBundleList::Row> chosen = ChosenBundle(jewels.bundles))
        {
            if (auto asked = AskForTheBundle(context, chosen->cell, static_cast<std::int8_t>(chosen->level));
                !asked.HasValue())
                return asked;
        }
    }
    // 0x625ebe..0x625f37: the cancel, x in [324.5, 404.5]; the click (which it does not clear) closes everything.
    if (WithinFloat(context, UnmixCancelLeft, UnmixCancelRight, top, top + UnmixButtonHeight) && click)
        return CloseTheJewelMix(context);
    return {};
}

Core::Expected<void, std::string> StepTheJewelResult(const Hud::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    bool& click = runtime.mouse.pendingClick;
    if (!click)
        return {};
    click = false;
    if (!Within(context, ResultLeft, ResultRight, ResultTop, ResultBottom))
        return {};
    return ReturnToTheChoice(context);
}

Core::Expected<void, std::string> AnswerTheJewelMix(const Hud::GameUiContext& context, bool yes)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.social.trade.warningByte = yes ? YesAnswer : NoAnswer;
    LeaveTheMouseTime(runtime);
    runtime.RequestSound(DialogSound); // (CL10)
    if (!yes)
        return ReturnToTheChoice(context);
    CloseTheDialog(runtime.dialogs);
    OpenAfter(runtime.dialogs, WaitingDialog);
    return SendTheJewelMix(context);
}
} // namespace Client::ChaosMix
