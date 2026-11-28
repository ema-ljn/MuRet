#include "Events/EventChipWindow.h"

#include "Core/Expected.h"
#include "Math/X87.h"
#include "Protocol/Events/Events.h"
#include "Protocol/Outgoing.h"
#include "Math/RoundedMath.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Events/EventGlobals.h"
#include "Events/EventNpcWindows.h"
#include "Chat/GameChat.h"
#include "Ui/GameHudContext.h"
#include "Hud/GameInventoryHud.h"
#include "Ui/GameUiContext.h"
#include "Hud/GameWindowDrawing.h"
#include "Monsters/NpcNames.h"
#include "Social/SocialState.h"
#include "Items/UiItemDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/PanelRegistry.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Events
{
namespace
{
using Result = Core::Expected<void, std::string>;
using Render::Text::FontKind;

// The window's corner (0x5f658d), the inventory's: the drawing writes it to 0x78a5a14 and 0x78a5a10.
constexpr std::int32_t WindowLeft = 0x1c2;
constexpr std::int32_t WindowTop = 0;
// 0x5e655b..0x5e657d: the mouse in [0x1c2, 0x280) × [0, 0x1b1) belongs to the UI.
constexpr std::int32_t WindowRight = 0x280;
constexpr std::int32_t WindowBottom = 0x1b1;
// The buttons: Interface\... 0xf0, 120 wide (0x42f00000) from x = 485 (0x6ba6a4 in the drawing, 0x1e5 in the step),
// with du = 0.83203125 (0x3f550000).
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr std::int32_t ButtonLeft = 0x1e5;
constexpr std::int32_t ButtonWidth = 0x78;
constexpr float ButtonDu = 0.83203125f;
// Window 4's at y 220 (0x6ba69c), 24 high; the others' at 231 (0x6ba694), 22 high.
constexpr std::int32_t MutoButtonTop = 0xdc;
constexpr std::int32_t MutoButtonHeight = 0x18;
constexpr std::int32_t ChipButtonTop = 0xe7;
constexpr std::int32_t ChipButtonHeight = 0x16;
// The serial number entry: the drawing starts at 450 (0x43e10000) + 35 (0x6b8974) and the corner's y + 250
// (0x6b8920); the confirm button goes 40 (0x6b8bf4) lower, at the y + 290 the step looks at (0x6ba63c).
constexpr float SerialLeft = 450.0f;
constexpr float SerialShift = 35.0f;
constexpr float SerialTop = 250.0f;
constexpr float ConfirmBelow = 40.0f;
constexpr float ConfirmStepTop = 290.0f;
// 0x5f6895: the texts centered at x + 56.5 (0x6ba68c).
constexpr float SerialTextShift = 56.5f;
// 0x5f6918..0x5f695b: frame 0x10f at (x + 3, y + 10), 113 × 18 with du 0.8828125 and dv 0.5625.
constexpr std::uint16_t SerialFrameSlot = 0x10f;
constexpr float SerialFrameLeft = 3.0f;
constexpr float SerialFrameTop = 10.0f;
constexpr float SerialFrameWidth = 113.0f;
constexpr float SerialFrameHeight = 18.0f;
constexpr float SerialFrameDu = 0.8828125f;
constexpr float SerialFrameDv = 0.5625f;
// 0x5f6994..0x5f69c8: the EDIT, 150 × 14 at (ftol(x) + 10, y + 15), with the limit at 12 and filter 2.
constexpr std::size_t SerialLimit = 0xc;
constexpr std::int32_t SerialEditWidth = 0x96;
constexpr std::int32_t SerialEditHeight = 0xe;
constexpr std::int32_t SerialEditLeft = 0xa;
constexpr std::int32_t SerialEditTop = 0xf;
// 0x4dd905: the length of the dialog field (0x6c8e78) with the entry open.
constexpr std::int32_t SerialFieldLimit = 0xc;
// 0x5f6b1f..0x5f6b42: the confirm button's text at (x + 60, y + 5).
constexpr float ConfirmTextLeft = 60.0f;
constexpr float ConfirmTextTop = 5.0f;
// 0x5f6b3c..0x5f6b4a: the top buttons' text, centered at x 0x221 and at y 0xe1 (window 4) or 0xec.
constexpr std::int32_t ButtonTextLeft = 0x221;
constexpr std::int32_t MutoButtonTextTop = 0xe1;
constexpr std::int32_t ChipButtonTextTop = 0xec;
// 0x5f6c6c..0x5f6c93: each type's texts, centered at x + 95 (0x6b87e4) from y + 100 (0x6b86b4), in steps of 15; with
// 3 and 4 from y + 70 (0x6ba1b0). The 9D result, 0x104 lower (and a shadow at x + 1, y + 0x105).
constexpr float TypeTextLeft = 95.0f;
constexpr float TypeTextTop = 100.0f;
constexpr float LowTypeTextTop = 70.0f;
constexpr std::int32_t TypeTextStep = 0xf;
constexpr std::int32_t SerialResultBelow = 0x104;
// 0x5f6ef4 and 0x5f6f72: the labels at x 0x1e5 (y 0x122 and 0x145) and the counts at x 0x230 (y 300 or 335; with 5,
// 290 and 325, 0x6ba63c and 0x6b8978).
constexpr std::int32_t LabelLeft = 0x1e5;
constexpr std::int32_t ItemLabelTop = 0x122;
constexpr std::int32_t CountLabelTop = 0x145;
constexpr std::int32_t CountLeft = 0x230;
constexpr float ItemCountTop = 300.0f;
constexpr float ChipCountTop = 335.0f;
constexpr float FifthItemCountTop = 290.0f;
constexpr float FifthChipCountTop = 325.0f;
constexpr std::int32_t FifthWindow = 5;
// 0x5f6dd4..0x5f6de7: sin(0x5767b88 · 0.01f) against 0.
constexpr float BlinkRate = 0.01f;
// 0x5e72ac and 0x5e74af: the close button (0x19, 0x18b from the inventory's corner, 24 × 24).
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;

// The Rena and the stone (14/21, level 0 and 1) that windows 1 and 2 count and register.
constexpr std::uint16_t ChipItem = 0x1c15;
constexpr std::int32_t StoneLevel = 1;
// 0x5f70c7: the +4 of the models of windows 2 and 4.
constexpr std::uint32_t StoneModelLevel = 0xf;
// 0x5f7131 and 0x5f7150: the two models at (540, 298) and (540, 333).
constexpr float ItemModelLeft = 540.0f;
constexpr float ChipModelTop = 298.0f;
constexpr float StoneModelTop = 333.0f;
// 0x5e6d16: 0x558740(−1, −1), the free slots.
constexpr std::int32_t AnyItem = -1;

// Text.bmd (the address − 0x76bd1dc, over 300).
constexpr std::uint16_t TitleNpc = 0xec;
constexpr std::size_t MutoButtonText = 903;
constexpr std::size_t RenaButtonText = 243;
constexpr std::size_t StoneButtonText = 818;
constexpr std::array<std::size_t, 4> SerialTexts{893, 916, 917, 894};
constexpr std::size_t ConfirmText = 895;
constexpr std::size_t CloseHelpText = 1002;
constexpr std::size_t InventoryFullText = 896;
constexpr std::array<std::size_t, 4> RenaTexts{700, 701, 702, 703};
constexpr std::array<std::size_t, 4> StoneTexts{882, 883, 884, 885};
constexpr std::array<std::size_t, 4> SerialStoryTexts{891, 892, 897, 898};
constexpr std::array<std::size_t, 4> MutoTexts{900, 706, 901, 902};
constexpr std::size_t RenaItemLabel = 245;
constexpr std::size_t StoneItemLabel = 819;
constexpr std::size_t RenaCountLabel = 246;
constexpr std::size_t StoneCountLabel = 820;

constexpr Render::Text::TextColors TitleColors{.text = 0xfff0ff96u, .background = 0};
constexpr Render::Text::TextColors BodyColors{.text = 0xffd2e6ffu, .background = 0};
constexpr Render::Text::TextColors SerialNoteColors{.text = 0xff67bfdfu, .background = 0};
constexpr Render::Text::TextColors HoveredConfirmColors{.text = 0xff697370u, .background = 0};
constexpr Render::Text::TextColors SerialResultColors{.text = 0xff67d2ffu, .background = 0};
constexpr Render::Text::TextColors CountColors{.text = 0xff0096d4u, .background = 0};

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Render::Color Black{0.0f, 0.0f, 0.0f, 1.0f};
constexpr Render::Color Pressed{0.4f, 0.4f, 0.4f, 1.0f};
constexpr Render::Color Dimmed{0.6f, 0.6f, 0.6f, 1.0f};

using Math::F32;
using Math::Truncate;

bool InBox(std::int32_t mouseX, std::int32_t mouseY, std::int32_t left, std::int32_t top, std::int32_t width,
           std::int32_t height)
{
    return mouseX >= left && mouseX < left + width && mouseY >= top && mouseY < top + height;
}

// The drawing's x87 comparisons: left ≤ mouse < left + width and top ≤ mouse < top + height.
bool Hovered(const Ui::GameHudContext& context, float left, float top, float width, float height)
{
    const auto x = static_cast<double>(context.mouse.x);
    const auto y = static_cast<double>(context.mouse.y);
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(width) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(height);
}

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Result Send(const Ui::GameUiContext& context, Protocol::Outgoing packet, std::string_view where)
{
    if (context.chat == nullptr)
        return Core::Unexpected(std::format("{}: el send() sin el contexto de la sesión", where));
    if (context.chat->send)
        context.chat->send(std::move(packet));
    return {};
}

// 0x5e6d34..0x5e6dab with 0x6c7b8c at 1: GetText of the EDIT (vtable + 0x50, up to 0xff bytes) onto the stack and the
// three dwords at [ebp-0x58], [ebp-0x54] and [ebp-0x50]. After the '\0' of a text under 11 letters lie the bytes the
// stack held, which MuRet zeroes (P-C247). More than 12 letters would overwrite the step's stack: only a SetText that
// nobody does in the game puts them there, and it stops.
Core::Expected<Protocol::Events::SerialPieces, std::string> SerialPiecesOf(const Ui::OldEdit& edit)
{
    const std::string_view text = edit.text.Value();
    if (text.size() > SerialLimit)
        return Core::Unexpected(
            std::format("0x5e6d68: el EDIT tiene {} bytes y GetText pisaría la pila del paso", text.size()));
    Protocol::Events::SerialPieces pieces{};
    for (std::size_t at = 0; at < text.size(); ++at)
        pieces[at / Protocol::Events::SerialPieceBytes][at % Protocol::Events::SerialPieceBytes] =
            static_cast<std::uint8_t>(text[at]);
    return pieces;
}

// 0x5e6cb8..0x5e7248: the serial number entry's confirm button, at (ftol(0x1c2 + 35), ftol(corner + 290)), 120 × 22.
Result StepTheSerialNumber(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t left = Truncate(F32(static_cast<double>(WindowLeft) + static_cast<double>(SerialShift)));
    const std::int32_t top =
        Truncate(F32(static_cast<double>(runtime.hud.inventoryY) + static_cast<double>(ConfirmStepTop)));
    if (!InBox(context.mouseX, context.mouseY, left, top, ButtonWidth, ChipButtonHeight) ||
        !runtime.mouse.releasedClick)
        return {};
    if (CountInventoryItems(context.world.Inventory(), AnyItem, AnyItem) == 0)
    {
        auto text = TextOf(context.texts, InventoryFullText, "0x5e6d22");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, text.Value());
    }
    else
    {
        auto pieces = SerialPiecesOf(runtime.chat.input.dialogEdit);
        if (!pieces.HasValue())
            return Core::Unexpected(std::move(pieces).Error());
        if (auto sent = Send(context, Protocol::Events::WriteSerialNumber(pieces.Value()), "0x5e7184");
            !sent.HasValue())
            return sent;
    }
    // 0x5e7248.
    runtime.mouse.leftHeld = false;
    return {};
}

// 0x4dd8f2..0x4dd8fe, 0x56b719..0x56b725 and 0x5e74a3..0x5e74b0: the zen mode (0x77f3b6a), the typed zen (0x77f3b6c)
// and 0x78abacc at 0.
void ForgetTheMoneyField(Social::SocialState& social)
{
    social.moneyField = 0;
    social.typedMoney = 0;
    social.trade.field78abacc = 0;
}

// 0x5e7255..0x5e74c6: the close button.
Result StepTheCloseButton(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Kernel::MouseState& mouse = runtime.mouse;
    if (!InBox(context.mouseX, context.mouseY, runtime.hud.inventoryX + CloseLeft, runtime.hud.inventoryY + CloseTop,
               CloseSize, CloseSize) ||
        !mouse.pendingClick)
        return {};
    mouse.pendingClick = false;
    mouse.updateTime = 0;
    mouse.updateTimeMax = CloseMouseUpdateTimeMax;
    if (auto sent = Send(context, Protocol::Events::WriteEventWindowLeave(), "0x5e739e"); !sent.HasValue())
        return sent;
    runtime.panels.field78abb00 = 0;
    if (auto closed = panels.Close(Ui::InventoryPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    Chat::ResetTheTextBuffers(runtime.chat.input, false);
    // 0x5e749c..0x5e74b6.
    runtime.ui.chatOpen = false;
    ForgetTheMoneyField(runtime.social);
    runtime.events.serialEntry = false;
    // 0x5e74bf: SetFocus([0x5763544]), with 0x6c7b8c at 1. 0x553570 (0x5535bd) already emptied the EDIT's text.
    runtime.chat.input.focus = Chat::ChatFocus::Game;
    return {};
}

// ----- The drawing.

// The EDIT's vtable + 0x14, 0x468320, with the Windows focus on it.
void DrawTheSerialEdit(Ui::GameHudContext& context, Ui::OldEdit& edit, bool focused)
{
    const Ui::OldWindowDrawContext drawing{.sprites = context.sprites,
                                           .text = context.text,
                                           .dcFont = context.hud.dcFont,
                                           .textColors = context.hud.textColors,
                                           .arrowHeld = context.arrowHeld};
    Ui::OldDraw::DrawTheOldEdit(drawing, edit, focused);
}

// 0x5f696d..0x5f69e3: with 0x6c7b8c at 1, the EDIT of [0x5763504], reconfigured every frame and drawn.
void DrawTheSerialField(Ui::GameHudContext& context, std::int32_t left, std::int32_t top)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Chat::ChatInputState& input = runtime.chat.input;
    Ui::OldEdit copy = input.dialogEdit;
    Ui::OldEdit& edit = context.projecting ? copy : input.dialogEdit;
    edit.base.state = Ui::ShownWindow;
    edit.filter = Ui::UppercaseLettersAndDigitsEdit;
    edit.backgroundColor = 0;
    Ui::LimitTheEdit(edit, SerialLimit, context.arrowHeld);
    // 0x467f50: the width up to 0xee.
    edit.base.width = SerialEditWidth;
    edit.base.height = SerialEditHeight;
    edit.base.x = left + SerialEditLeft;
    edit.base.y = top + SerialEditTop;
    // 0x4682a0(0): the Windows focus and the EDIT's id (7) in 0x5762ec0, the list that takes the arrows.
    if (!context.projecting)
    {
        input.focus = Chat::ChatFocus::Dialog;
        runtime.ui.focusedList = edit.base.id;
    }
    DrawTheSerialEdit(context, edit, true);
}

// 0x5f66d6..0x5f6700 and 0x5f6806..0x5f6830: the top button, after the mouse color.
void DrawTheButtonQuad(Ui::GameHudContext& context, float top, float height)
{
    Ui::WindowDrawing::Quad(context.sprites, ButtonSlot, static_cast<float>(ButtonLeft), top,
                            static_cast<float>(ButtonWidth), height, ButtonDu, 1.0f);
}

// With the button pressed under the mouse, the normal font, the gray and, with the release, the click and the button
// cleared (0x5f6696..0x5f66d2, 0x5f677a..0x5f67b5 and 0x5f6aa4..0x5f6adf).
void PressTheButton(Ui::GameHudContext& context)
{
    Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
    context.sprites.SetColor(Pressed);
    Kernel::MouseState& mouse = context.world.Runtime().mouse;
    if (!mouse.releasedClick || context.projecting)
        return;
    mouse.pendingClick = false;
    mouse.leftHeld = false;
}

// 0x5f6646..0x5f6865: window 4's button, or the others' except 3.
Result DrawTheTopButton(Ui::GameHudContext& context, std::int32_t window, std::int32_t items)
{
    const bool click = context.world.Runtime().mouse.pendingClick;
    if (window == MutoNumberWindow)
    {
        const auto top = static_cast<float>(MutoButtonTop);
        const auto height = static_cast<float>(MutoButtonHeight);
        if (Hovered(context, static_cast<float>(ButtonLeft), top, static_cast<float>(ButtonWidth), height) && click)
            PressTheButton(context);
        DrawTheButtonQuad(context, top, height);
        auto text = TextOf(context.texts, MutoButtonText, "0x5f6705");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::WindowDrawing::DrawCenteredText(context, ButtonTextLeft, MutoButtonTextTop, text.Value());
        return {};
    }
    const auto top = static_cast<float>(ChipButtonTop);
    const auto height = static_cast<float>(ChipButtonHeight);
    if (Hovered(context, static_cast<float>(ButtonLeft), top, static_cast<float>(ButtonWidth), height) && click)
        PressTheButton(context);
    else
    {
        // 0x5f67b9..0x5f6804: in white and bold with items, in gray and normal without them.
        Ui::WindowDrawing::SelectFont(context, items > 0 ? FontKind::Bold : FontKind::Normal);
        context.sprites.SetColor(items > 0 ? White : Pressed);
    }
    DrawTheButtonQuad(context, top, height);
    if (window != RenaChipWindow && window != StoneChipWindow)
        return {};
    auto text = TextOf(context.texts, window == RenaChipWindow ? RenaButtonText : StoneButtonText, "0x5f6853");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    Ui::WindowDrawing::DrawCenteredText(context, ButtonTextLeft, ChipButtonTextTop, text.Value());
    return {};
}

// 0x5f6872..0x5f6b47: the serial number entry, with x = 450 + 35 and y = the corner + 250.
Result DrawTheSerialEntry(Ui::GameHudContext& context)
{
    const float left = F32(static_cast<double>(SerialLeft) + static_cast<double>(SerialShift));
    float top = F32(static_cast<double>(context.hud.inventoryY) + static_cast<double>(SerialTop));
    const std::int32_t row = Truncate(top);
    const std::int32_t middle = Truncate(F32(static_cast<double>(left) + static_cast<double>(SerialTextShift)));
    // 0x5f68a4..0x5f68fb: 893 at y − 0x28; 916 and 917 at y − 0xf and y − 5 in 0xff67bfdf; 894 in bold at y − 0x19.
    constexpr std::array<std::int32_t, 4> Above{0x28, 0xf, 5, 0x19};
    for (std::size_t line = 0; line < SerialTexts.size(); ++line)
    {
        if (line == 1)
            context.hud.textColors = SerialNoteColors;
        if (line == 3)
            Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
        auto text = TextOf(context.texts, SerialTexts[line], "0x5f68a7");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::WindowDrawing::DrawCenteredText(context, middle, row - Above[line], text.Value());
    }
    Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
    // 0x5f6918..0x5f695b: the frame, with 0x6c8e5c at 0xfff0ff96.
    context.hud.textColors = TitleColors;
    Ui::WindowDrawing::Quad(context.sprites, SerialFrameSlot, F32(static_cast<double>(left) + SerialFrameLeft),
                            F32(static_cast<double>(top) + SerialFrameTop), SerialFrameWidth, SerialFrameHeight,
                            SerialFrameDu, SerialFrameDv);
    DrawTheSerialField(context, Truncate(left), row);
    // 0x5f6a43..0x5f6aef: the confirm button, 40 lower.
    top = F32(static_cast<double>(top) + static_cast<double>(ConfirmBelow));
    if (Hovered(context, left, top, static_cast<float>(ButtonWidth), static_cast<float>(ChipButtonHeight)))
    {
        if (context.world.Runtime().mouse.pendingClick)
            PressTheButton(context);
        else
            context.hud.textColors = HoveredConfirmColors;
    }
    Ui::WindowDrawing::Quad(context.sprites, ButtonSlot, left, top, static_cast<float>(ButtonWidth),
                            static_cast<float>(ChipButtonHeight), ButtonDu, 1.0f);
    auto confirm = TextOf(context.texts, ConfirmText, "0x5f6b28");
    if (!confirm.HasValue())
        return Core::Unexpected(std::move(confirm).Error());
    Ui::WindowDrawing::DrawCenteredText(context, Truncate(F32(static_cast<double>(left) + ConfirmTextLeft)),
                                        Truncate(F32(static_cast<double>(top) + ConfirmTextTop)), confirm.Value());
    return {};
}

// 0x5f6d7e..0x5f6e55: the 9D result (0x78ab078) if its first byte is not that of 0x78abb84 (.bss at 0): a black bold
// shadow and the text in 0xff67d2ff, in gray or white by the sign of sin(clock · 0.01f).
void DrawTheSerialResult(Ui::GameHudContext& context, std::int32_t middle, std::int32_t top)
{
    const auto& serial = context.world.Runtime().events.serial;
    if (serial[0] == std::byte{0})
        return;
    std::string text;
    for (const std::byte letter : serial)
    {
        if (letter == std::byte{0})
            break;
        text.push_back(static_cast<char>(letter));
    }
    context.sprites.SetColor(Black);
    Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
    Ui::WindowDrawing::DrawCenteredText(context, middle + 1, top + SerialResultBelow + 1, text);
    const double wave = Math::RoundedSin(static_cast<double>(context.worldClock) * static_cast<double>(BlinkRate));
    context.sprites.SetColor(wave < 0.0 ? White : Dimmed);
    context.hud.textColors = SerialResultColors;
    Ui::WindowDrawing::DrawCenteredText(context, middle, top + SerialResultBelow, text);
    context.sprites.SetColor(White);
    Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = TitleColors;
}

// 0x5f6c6c..0x5f6eb0: the type's four texts, in steps of 15.
Result DrawTheTypeTexts(Ui::GameHudContext& context, std::int32_t window)
{
    const std::array<std::size_t, 4>* texts = nullptr;
    float top = TypeTextTop;
    if (window == RenaChipWindow)
        texts = &RenaTexts;
    else if (window == StoneChipWindow)
        texts = &StoneTexts;
    else if (window == SerialNumberWindow)
    {
        texts = &SerialStoryTexts;
        top = LowTypeTextTop;
    }
    else if (window == MutoNumberWindow)
    {
        texts = &MutoTexts;
        top = LowTypeTextTop;
    }
    if (texts == nullptr)
        return {};
    const std::int32_t middle =
        Truncate(F32(static_cast<double>(context.hud.inventoryX) + static_cast<double>(TypeTextLeft)));
    const std::int32_t first = Truncate(F32(static_cast<double>(context.hud.inventoryY) + static_cast<double>(top)));
    for (std::size_t line = 0; line < texts->size(); ++line)
    {
        auto text = TextOf(context.texts, (*texts)[line], "0x5f6cbf");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        Ui::WindowDrawing::DrawCenteredText(context, middle, first + static_cast<std::int32_t>(line) * TypeTextStep,
                                            text.Value());
    }
    if (window == SerialNumberWindow)
        DrawTheSerialResult(context, middle, first);
    return {};
}

// 0x5f6ec8..0x5f7047: the label of 1 or 2 in bold and, except with 3, the count with "x%d" in the big font.
Result DrawTheCount(Ui::GameHudContext& context, std::int32_t window, std::size_t renaLabel, std::size_t stoneLabel,
                    std::int32_t labelTop, std::int32_t count, float countTop, float fifthTop)
{
    Ui::WindowDrawing::SelectFont(context, FontKind::Bold);
    if (window == RenaChipWindow || window == StoneChipWindow)
    {
        auto label = TextOf(context.texts, window == RenaChipWindow ? renaLabel : stoneLabel, "0x5f6ef9");
        if (!label.HasValue())
            return Core::Unexpected(std::move(label).Error());
        Ui::WindowDrawing::DrawText(context, LabelLeft, labelTop, label.Value());
    }
    if (window == SerialNumberWindow)
        return {};
    Ui::WindowDrawing::SelectFont(context, FontKind::BigBold);
    const std::string text = "x" + std::to_string(count);
    context.hud.textColors = CountColors;
    // 0x5f6f4a: the table at 0x5f7078 (and 0x5f708c) only draws with 1, 2, 4 and 5.
    if (window == RenaChipWindow || window == StoneChipWindow || window == MutoNumberWindow)
        Ui::WindowDrawing::DrawText(context, CountLeft, Truncate(countTop), text);
    else if (window == FifthWindow)
        Ui::WindowDrawing::DrawText(context, CountLeft, Truncate(fifthTop), text);
    return {};
}
} // namespace

void OpenTheSerialEntry(Kernel::WorldRuntime& runtime)
{
    runtime.events.serial = {};
    Chat::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.ui.chatOpen = false;
    ForgetTheMoneyField(runtime.social);
    runtime.social.dialogFieldLimit = SerialFieldLimit;
    runtime.chat.input.fieldCount = 1;
    runtime.events.serialEntry = true;
}

void CloseTheSerialEntry(Kernel::WorldRuntime& runtime)
{
    Chat::ChatInputState& input = runtime.chat.input;
    input.field = Chat::MessageField;
    input.messageBuffer.fill('\0');
    input.messageLength = 0;
    // 0x56b6f6..0x56b70d: vtable + 0x4c with 0 of the two EDITs, [0x5763504] and [0x5763508].
    input.dialogEdit.text.SetText({});
    input.numberField.text.SetText({});
    runtime.ui.chatOpen = false;
    ForgetTheMoneyField(runtime.social);
    runtime.events.serialEntry = false;
}

Core::Expected<void, std::string> StepTheEventChipWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t window = runtime.panels.field78abb00;
    if (window == 0)
        return {};
    Kernel::MouseState& mouse = runtime.mouse;
    if (InBox(context.mouseX, context.mouseY, WindowLeft, WindowTop, WindowRight - WindowLeft,
              WindowBottom - WindowTop))
        runtime.ui.mouseOnUi = 1;
    if (window == MutoNumberWindow)
    {
        // 0x5e6594..0x5e6864.
        if (InBox(context.mouseX, context.mouseY, ButtonLeft, MutoButtonTop, ButtonWidth, MutoButtonHeight) &&
            mouse.releasedClick)
        {
            if (auto sent = Send(context, Protocol::Events::WriteMutoNumberRequest(), "0x5e67a0"); !sent.HasValue())
                return sent;
            mouse.releasedClick = false;
        }
    }
    else if (window != SerialNumberWindow)
    {
        // 0x5e687b..0x5e6c94: with 2 the stone; with 1 or any other, the Rena.
        if (InBox(context.mouseX, context.mouseY, ButtonLeft, ChipButtonTop, ButtonWidth, ChipButtonHeight) &&
            mouse.releasedClick)
        {
            mouse.releasedClick = false;
            const std::int32_t level = window == StoneChipWindow ? StoneLevel : 0;
            const std::int32_t place = FindInventoryItem(context.world.Inventory(), ChipItem, level);
            if (place != -1)
            {
                const auto type = static_cast<std::uint8_t>(static_cast<std::uint32_t>(window - 1) & 0xffu);
                if (auto sent =
                        Send(context, Protocol::Events::WriteEventChipRegister(type, static_cast<std::uint8_t>(place)),
                             "0x5e6bc9");
                    !sent.HasValue())
                    return sent;
            }
        }
    }
    else if (auto serial = StepTheSerialNumber(context); !serial.HasValue())
        return serial;
    if (auto closed = StepTheCloseButton(context, panels); !closed.HasValue())
        return closed;
    // 0x5e74cc..0x5e74d8.
    mouse.releasedClick = false;
    return {};
}

Core::Expected<void, std::string> DrawTheEventChipWindow(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t window = runtime.panels.field78abb00;
    if (window == 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.hud.inventoryX = WindowLeft;
    context.hud.inventoryY = WindowTop;
    Ui::WindowDrawing::DrawTheWindowBack(sprites, WindowLeft, WindowTop);
    context.hud.textColors = TitleColors;
    // 0x5f65df: 0x554180 with 0x54ebb0(0xec), the name of NPC 236 in the NpcName(%s).txt table (0x7681eb8, loaded by
    // 0x54e7f0, CL9 milestone 2). Without a record of that type, 0x54ebb0 gives NULL and 0x554180 draws nothing: it
    // measures lstrlen(NULL), 0 characters, like the quests' title (0x41be9e).
    const std::optional<std::string> title = Monsters::NpcNameOf(runtime.npcNames, TitleNpc);
    if (title)
        Ui::WindowDrawing::DrawTitle(context, WindowLeft + 0x23, WindowTop + 0xc, *title);
    else
        Ui::WindowDrawing::DrawText(context, WindowLeft + 0x23, WindowTop + 0xc, {});
    // 0x5f6609..0x5f661b: the count of 0x1c15, level 1 with window 2.
    const std::int32_t items =
        CountInventoryItems(context.world.Inventory(), ChipItem, window == StoneChipWindow ? StoneLevel : 0);
    context.hud.textColors = BodyColors;
    if (window == SerialNumberWindow)
    {
        if (auto entry = DrawTheSerialEntry(context); !entry.HasValue())
            return entry;
    }
    else if (auto button = DrawTheTopButton(context, window, items); !button.HasValue())
        return button;
    // 0x5f6b4c..0x5f6c66: the close button and its help; then the normal font and 0xffd2e6ff over 0.
    if (Ui::WindowDrawing::DrawTheCloseButton(context, context.hud.inventoryX, context.hud.inventoryY))
    {
        auto help = TextOf(context.texts, CloseHelpText, "0x5f6c25");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        Ui::WindowDrawing::DrawTheCloseHelp(context, context.hud.inventoryX, context.hud.inventoryY, help.Value());
    }
    Ui::WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = BodyColors;
    if (auto texts = DrawTheTypeTexts(context, window); !texts.HasValue())
        return texts;
    if (auto first = DrawTheCount(context, window, RenaItemLabel, StoneItemLabel, ItemLabelTop, items, ItemCountTop,
                                  FifthItemCountTop);
        !first.HasValue())
        return first;
    context.hud.textColors = BodyColors;
    if (auto second = DrawTheCount(context, window, RenaCountLabel, StoneCountLabel, CountLabelTop,
                                   runtime.panels.field78abb04, ChipCountTop, FifthChipCountTop);
        !second.HasValue())
        return second;
    sprites.SetColor(White);
    return {};
}

Core::Expected<void, std::string> DrawTheEventChipItems(Ui::GameHudContext& context)
{
    const std::int32_t window = context.world.Runtime().panels.field78abb00;
    std::int32_t type = ChipItem;
    std::uint32_t level = 0;
    if (window == StoneChipWindow || window == MutoNumberWindow)
        level = StoneModelLevel;
    else if (window == FifthWindow)
        type = 0;
    auto box = Hud::ItemSizeOf(context, type, "0x5f70e7");
    if (!box.HasValue())
        return Core::Unexpected(std::move(box).Error());
    box.Value().x = ItemModelLeft;
    if (window != MutoNumberWindow)
    {
        box.Value().y = ChipModelTop;
        if (auto drawn = Hud::DrawUiItem(context, box.Value(), type, level, 0, 0, false); !drawn.HasValue())
            return drawn;
    }
    box.Value().y = StoneModelTop;
    return Hud::DrawUiItem(context, box.Value(), type, level, 0, 0, false);
}
} // namespace Client::Events
