#include "Warehouse/WarehouseKeypad.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/FloatConstants.h"
#include "Math/X87.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Ui/GameHudContext.h"
#include "Ui/GameUiContext.h"
#include "Hud/GameWindowDrawing.h"
#include "Npc/NpcWindowState.h"
#include "Npc/NpcWindowSteps.h"
#include "Kernel/MouseState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/GameText.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Ui/Widget.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Chat/ChatRuntime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Warehouse
{
namespace
{
using Ui::WindowDrawing::DrawCenteredText;
using Ui::WindowDrawing::DrawText;
using Ui::WindowDrawing::Quad;
using Ui::WindowDrawing::SelectFont;
using Render::Text::FontKind;

namespace Npc = Protocol::Npc;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5e7c6d: 0x41ed10(0x19, 0, 0), the keys' sound.
constexpr Audio::SoundId KeySound = Sound::ButtonClickSound;
// 0x5e7d39 and 0x5e7e7e: the dialogs for four equal digits and for a different repeated password.
constexpr std::int32_t SameDigitsDialog = 0x87;
constexpr std::int32_t WrongRepeatDialog = 0x88;
// 0x5e7dfb and 0x5e7e62: the modes that follow 4 and 5.
constexpr std::int32_t RepeatPasswordMode = 5;
constexpr std::int32_t SetPasswordMode = 2;
// The 83's types for modes 2 and 3 (the mode minus 1, 0x5e7f0b and 0x5e8c80): CGWarehouseCheckPass sets the password
// with 1 and removes it with 2, both with the personal number.
constexpr std::uint8_t SetPasswordType = 1;
constexpr std::uint8_t RemovePasswordType = 2;
// The padding's rand() and the serial's of a C1 in C3.
constexpr std::uint32_t C3SendRandoms = 2;

// 0x5e7b50: the keys of 32 (0x20) from (0xdf, 0x96), every 40 (0x28) in both directions, five per row.
constexpr std::int32_t KeyColumns = 5;
constexpr std::int32_t KeyLeft = 0xdf;
constexpr std::int32_t KeyHitTop = 0x96;
constexpr std::int32_t KeyStep = 40;
constexpr std::int32_t KeySize = 0x20;
constexpr std::int32_t KeyCount = 11;
// The OK and cancel: 0x46 × 0x15 from (0x109, 0xeb), every 0x4e.
constexpr std::int32_t ButtonLeft = 0x109;
constexpr std::int32_t ButtonStep = 0x4e;
constexpr std::int32_t ButtonWidth = 0x46;
constexpr std::int32_t ButtonTop = 0xeb;
constexpr std::int32_t ButtonBottom = 0x100;
constexpr std::int32_t ButtonCount = 2;

// 0x5e9b45..0x5e9bf8: the frame, Message_box2 (0xfc) of 213 × 5 at (213, 100) and at (213, 265) and four rows of
// Message_box1 (0xfb) of 213 × 40 from y = 0x69 every 40, with du = 0.83203125 and dv = 0.625.
constexpr std::uint16_t EdgeSlot = 0xfc;
constexpr std::uint16_t RowSlot = 0xfb;
constexpr float FrameLeft = 213.0f;
constexpr float FrameTop = 100.0f;
constexpr float FrameWidth = 213.0f;
constexpr float EdgeHeight = 5.0f;
constexpr float RowHeight = 40.0f;
constexpr std::int32_t FirstRowTop = 0x69;
constexpr std::int32_t FrameRows = 4;
constexpr float FrameDu = 0.83203125f;
constexpr float FrameDv = 0.625f;

// 0x5e9bfd..0x5e9cb3: the two lines centered on 0x140, at y = 0x6e and 0x7a, in the bold font in 0xffffc4c4 on a
// transparent background. The titles are 690 to 694 (0x76efa74 − 0x76bd1dc, over 300) and the prompts 695 to 697.
constexpr Render::Text::TextColors HeadingColors{.text = 0xffffc4c4u, .background = 0};
constexpr std::int32_t CenterX = 0x140;
constexpr std::int32_t TitleTop = 0x6e;
constexpr std::int32_t PromptTop = 0x7a;
constexpr std::size_t FirstTitleText = 690;
constexpr std::int32_t LastTitleOffset = 4;
constexpr std::int32_t RepeatedTitleMode = 6;
constexpr std::size_t ChangeTitleText = 691;
constexpr std::size_t NumberPromptText = 697;
constexpr std::size_t RepeatPromptText = 696;
constexpr std::size_t PasswordPromptText = 695;

// 0x5e9cbb..0x5e9d4f: the typed bar, bar.jpg (0xfd) in gray 0.3, 52 (0x42500000) wide, or (digits − 4) · 13
// (0x6b8834) + 52 (0x6ba060) with more than four, centered on 320 (0x6ba22c) with the half (0x6b882c), at y = 134 and
// 16 high, with du = 0.625 and dv = 0.5625.
constexpr Render::Color BarColor{0.3f, 0.3f, 0.3f, 1.0f};
constexpr std::uint16_t BarSlot = 0xfd;
constexpr float BarBaseWidth = 52.0f;
constexpr double BarDigitWidth = 13.0;
constexpr double BarCenter = 320.0;
constexpr double Half = Math::HalfAsDouble;
constexpr float BarTop = 134.0f;
constexpr float BarHeight = 16.0f;
constexpr float BarDu = 0.625f;
constexpr float BarDv = 0.5625f;
// 0x5e9d7b..0x5e9e12: the typed digits as asterisks ("*", 0x6c9cdc) after the byte at 0x78abb78, which nobody writes
// (the .bss 0: the string starts empty), in the big bold font in 0xffc48080 on transparent, at
// (digits − _ftol(half − 320), 0x84).
constexpr Render::Text::TextColors TypedColors{.text = 0xffc48080u, .background = 0};
constexpr std::int32_t TypedTop = 0x84;

// 0x5e9e1c..0x5e9fb8: the keys at ((i % 5) · 40 + 0xdf, (i / 5) · 38 + 0x9a), 32 × 32 with du = dv = 1: Item_box
// (0x115) and Item_box again on top, or InventoryBox2 (0x116) for the hovered one pressed; the erase key, back1 (0xfe),
// back2 (0xff) hovered or back3 (0x100) pressed. The hovered one in (1, 1, 0.8) with the text in 0xff00c4c4; the
// others in (0.85, 0.85, 0.65) with 0xffc48080. The number at (x + 0xc, y + 6), one higher when pressed.
constexpr std::int32_t KeyDrawTop = 0x9a;
constexpr std::int32_t KeyDrawStep = 38;
constexpr float KeyQuadSize = 32.0f;
constexpr std::uint16_t KeySlot = 0x115;
constexpr std::uint16_t PressedKeySlot = 0x116;
constexpr std::uint16_t EraseSlot = 0xfe;
constexpr Render::Color HoveredKeyColor{1.0f, 1.0f, 0.8f, 1.0f};
constexpr Render::Color KeyColor{0.85f, 0.85f, 0.65f, 1.0f};
constexpr std::uint32_t HoveredDigitColor = 0xff00c4c4u;
constexpr std::uint32_t DigitColor = 0xffc48080u;
constexpr std::int32_t DigitLeft = 0xc;
constexpr std::int32_t DigitTop = 6;
// 0x5e9fbe..0x5ea042: the OK (ok 0xf1, ok2 0xf2 hovered) and cancel (cancel 0xf3, cancel2 0xf4) of 70 × 21 at
// (0x109 + 0x4e · j, 239), with du = 0.546875 and dv = 0.65625.
constexpr std::array<std::uint16_t, ButtonCount> ButtonSlots{0xf1, 0xf3};
constexpr float ButtonQuadTop = 239.0f;
constexpr float ButtonQuadWidth = 70.0f;
constexpr float ButtonQuadHeight = 21.0f;
constexpr float ButtonDu = 0.546875f;
constexpr float ButtonDv = 0.65625f;

using Math::F32;
using Math::Truncate;

Core::Expected<std::string_view, std::string> TextOf(const Ui::GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x5e9c15..0x5e9c4e
std::size_t TitleOf(std::int32_t mode)
{
    if (mode == RepeatedTitleMode)
        return ChangeTitleText;
    return FirstTitleText + static_cast<std::size_t>(std::clamp(mode - 1, 0, LastTitleOffset));
}

// 0x5e9c70..0x5e9c96, the table at 0x5ea04c.
std::size_t PromptOf(std::int32_t mode)
{
    switch (mode)
    {
    case 2:
    case 3:
    case 6:
        return NumberPromptText;
    case 5:
        return RepeatPromptText;
    default:
        return PasswordPromptText;
    }
}

// 0x5e9ccc..0x5e9ceb: modes 2, 3 and 6 ask for the personal number.
std::int32_t DigitsOf(std::int32_t mode)
{
    return mode == 3 || mode == 2 || mode == 6 ? PersonalNumberDigits : PasswordDigits;
}

// strlen of 0x78ab9fc, which never runs past its array: nobody types more than seven.
std::size_t TypedLength(const std::array<std::uint8_t, 11>& typed)
{
    return static_cast<std::size_t>(std::ranges::find(typed, std::uint8_t{0}) - typed.begin());
}

// 0x6a0c01, the CRT's atoi, over the typed digits: only digits from the keys (0x7879fc8 goes from 0 to 9 once
// shuffled), so there are no spaces or sign; it stops at the 0.
std::int32_t TypedValue(const std::array<std::uint8_t, 11>& typed)
{
    std::int32_t value = 0;
    for (const std::uint8_t digit : typed)
    {
        if (digit < '0' || digit > '9')
            break;
        value = value * 10 + (digit - '0');
    }
    return value;
}

Protocol::Npc::WarehouseNumber NumberOf(std::span<const std::uint8_t> bytes)
{
    Protocol::Npc::WarehouseNumber number{};
    for (std::size_t at = 0; at < number.size(); ++at)
        number[at] = static_cast<std::byte>(bytes[at]);
    return number;
}

Core::Expected<void, std::string> Send(const Ui::GameUiContext& context, Protocol::Outgoing packet,
                                       std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// A C1 with the XOR32 in C3: the padding's rand() (0x69fd70) and the serial's (0x40b0a0, or inline 0x40b3f0), before
// sending.
Core::Expected<void, std::string> SendInC3(const Ui::GameUiContext& context, Protocol::Outgoing packet,
                                           std::string_view what)
{
    context.random.Discard(C3SendRandoms);
    return Send(context, std::move(packet), what);
}

// 0x5e7d80..0x5e9a88: the OK with all the digits and not four equal, by the mode it had (already at 0).
Core::Expected<void, std::string> Confirm(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                          std::int32_t mode)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Client::Npc::NpcWindowState& npc = runtime.npcWindows;
    const auto typed = NumberOf(npc.typedNumber);
    switch (mode)
    {
    case 2:
        // 0x5e7e90..0x5e854d.
        return SendInC3(context, Npc::WriteWarehousePassword(SetPasswordType, npc.warehousePassword, typed),
                        "0x5e7e90: el C1 10 83 01");
    case 3:
        // 0x5e8c02..0x5e931a: the type is the mode minus 1 (0x5e8c80) and the password, 0 (0x5e8d78).
        return SendInC3(context, Npc::WriteWarehousePassword(RemovePasswordType, 0, typed), "0x5e8c02: el C1 10 83 02");
    case 4:
        // 0x5e7d80..0x5e7e1e: the typed digits to 0x78aba07, the keypad shuffled as in 0x5e7aa0 and the mode at 5.
        std::ranges::copy_n(npc.typedNumber.begin(), static_cast<std::ptrdiff_t>(npc.newPassword.size()),
                            npc.newPassword.begin());
        Client::Npc::OpenTheKeypad(runtime, context.random, RepeatPasswordMode);
        return {};
    case 5:
        // 0x5e7e23..0x5e7e8b: a four-byte memcmp; when equal, its atoi in 0x78abb24, the typed digits at 0 and the mode
        // at 2, without shuffling.
        if (!std::ranges::equal(std::span(npc.typedNumber).first(npc.newPassword.size()), npc.newPassword))
        {
            Ui::PushDialog(runtime.dialogs, WrongRepeatDialog);
            return {};
        }
        npc.warehousePassword = static_cast<std::uint16_t>(TypedValue(npc.typedNumber) & 0xffff);
        npc.typedNumber.fill(0);
        runtime.ui.field78abb20 = SetPasswordMode;
        return {};
    case 6:
    {
        // 0x5e8552..0x5e86ee: C1 03 31 in the clear, without rand(); 0x5e86f4..0x5e8701: 0x657a90; 0x5e8706..: the 99.
        if (auto sent = Send(context, Protocol::Session::WriteWindowClose(), "0x5e8552: el C1 03 31"); !sent.HasValue())
            return sent;
        if (auto closed = panels.CloseAll(); !closed.HasValue())
            return closed;
        return SendInC3(context, Npc::WritePersonalNumber(typed), "0x5e8706: el C1 0D 99");
    }
    default:
    {
        // 0x5e931c..0x5e9a88 (mode 1 and the modes outside 2..6): the type is the mode minus 1 (0x5e939a), the password
        // the atoi of the typed digits (0x5e9491) and the number, the first ten bytes of the chat message buffer
        // (0x7798bf8, 0x5e95b1..0x5e95d2; Q-C924).
        const auto chat = std::as_bytes(std::span(runtime.chat.input.messageBuffer));
        Protocol::Npc::WarehouseNumber number{};
        std::ranges::copy_n(chat.begin(), static_cast<std::ptrdiff_t>(number.size()), number.begin());
        const auto type = static_cast<std::uint8_t>((mode - 1) & 0xff);
        const auto password = static_cast<std::uint16_t>(TypedValue(npc.typedNumber) & 0xffff);
        return SendInC3(context, Npc::WriteWarehousePassword(type, password, number), "0x5e931c: el C1 10 83");
    }
    }
}

// 0x5e7c69..0x5e9a88: the key with the click.
Core::Expected<void, std::string> PressTheKey(const Ui::GameUiContext& context, Ui::PanelRegistry& panels,
                                              std::int32_t key)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Client::Npc::NpcWindowState& npc = runtime.npcWindows;
    auto& typed = npc.typedNumber;
    const std::int32_t mode = runtime.ui.field78abb20;
    const auto digits = static_cast<std::size_t>(DigitsOf(mode));
    // 0x41ed10(0x19, 0, 0) (CL10).
    runtime.RequestSound(KeySound);
    const std::size_t length = TypedLength(typed);
    switch (key)
    {
    case KeypadEraseKey:
        // 0x5e9a8a..0x5e9aa7: the last byte at 0.
        if (length != 0)
            typed[length - 1] = 0;
        return {};
    case KeypadCancelKey:
        // 0x5e7cea.
        runtime.ui.field78abb20 = 0;
        return {};
    case KeypadOkKey:
    {
        // 0x5e7cf9..0x5e7d0a: with another length, nothing.
        if (length != digits)
            return {};
        // 0x5e7d10..0x5e7d5b: four equal digits, only where four are asked for.
        if (digits == PasswordDigits && typed[0] == typed[1] && typed[2] == typed[3] && typed[1] == typed[2])
        {
            typed.fill(0);
            Ui::PushDialog(runtime.dialogs, SameDigitsDialog);
            return {};
        }
        // 0x5e7d60..0x5e7d79: the mode at 0 before the table.
        runtime.ui.field78abb20 = 0;
        return Confirm(context, panels, mode);
    }
    default:
        break;
    }
    // 0x5e7c88..0x5e7ce5: with fewer digits than the mode's, wsprintfA("%d") of the signed word and strcat.
    if (length >= digits)
        return {};
    const std::string number = std::format("{}", static_cast<std::int16_t>(npc.keypad[static_cast<std::size_t>(key)]));
    // The error leaves out the digit: it is part of the password.
    if (length + number.size() >= typed.size())
        return Core::Unexpected(std::format("0x5e7cdc: el strcat de {} bytes después de {} pasa los {} de 0x78ab9fc",
                                            number.size(), length, typed.size()));
    std::ranges::copy(number, typed.begin() + static_cast<std::ptrdiff_t>(length));
    typed[length + number.size()] = 0;
    return {};
}
} // namespace

std::int32_t KeypadKeyAt(std::int32_t mouseX, std::int32_t mouseY)
{
    for (std::int32_t key = 0; key < KeyCount; ++key)
    {
        const std::int32_t left = (key % KeyColumns) * KeyStep + KeyLeft;
        const std::int32_t top = (key / KeyColumns) * KeyStep + KeyHitTop;
        if (left <= mouseX && mouseX < left + KeySize && top <= mouseY && mouseY < top + KeySize)
            return key;
    }
    for (std::int32_t button = 0; button < ButtonCount; ++button)
    {
        const std::int32_t left = ButtonLeft + ButtonStep * button;
        if (left <= mouseX && mouseX < left + ButtonWidth && mouseY >= ButtonTop && mouseY < ButtonBottom)
            return KeypadOkKey + button;
    }
    return NoKeypadKey;
}

Core::Expected<bool, std::string> StepTheKeypad(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field78abb20 == 0)
        return false;
    Kernel::MouseState& mouse = runtime.mouse;
    const std::int32_t key = KeypadKeyAt(context.mouseX, context.mouseY);
    if (mouse.pendingClick)
    {
        if (key >= 0)
        {
            if (auto pressed = PressTheKey(context, panels, key); !pressed.HasValue())
                return Core::Unexpected(std::move(pressed).Error());
        }
        // 0x5e9aad..0x5e9ab8: reads the click again.
        if (mouse.pendingClick)
            runtime.npcWindows.keypadPressed = 1;
    }
    // 0x5e9abf..0x5e9ae1.
    if (mouse.releasedClick)
        runtime.npcWindows.keypadPressed = 0;
    mouse.pendingClick = false;
    mouse.leftHeld = false;
    return true;
}

Core::Expected<void, std::string> DrawTheKeypad(Ui::GameHudContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t mode = runtime.ui.field78abb20;
    if (mode == 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    Kernel::HudState& hud = context.hud;
    const Client::Npc::NpcWindowState& npc = runtime.npcWindows;
    const std::int32_t hovered = KeypadKeyAt(context.mouse.x, context.mouse.y);

    sprites.SetColor(White);
    Quad(sprites, EdgeSlot, FrameLeft, FrameTop, FrameWidth, EdgeHeight, FrameDu, FrameDv);
    std::int32_t rowTop = FirstRowTop;
    for (std::int32_t row = 0; row < FrameRows; ++row, rowTop += static_cast<std::int32_t>(RowHeight))
        Quad(sprites, RowSlot, FrameLeft, static_cast<float>(rowTop), FrameWidth, RowHeight, FrameDu, FrameDv);
    Quad(sprites, EdgeSlot, FrameLeft, static_cast<float>(rowTop), FrameWidth, EdgeHeight, FrameDu, FrameDv);

    SelectFont(context, FontKind::Bold);
    hud.textColors = HeadingColors;
    auto title = TextOf(context, TitleOf(mode), "0x5e9c63");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    DrawCenteredText(context, CenterX, TitleTop, title.Value());
    auto prompt = TextOf(context, PromptOf(mode), "0x5e9cab");
    if (!prompt.HasValue())
        return Core::Unexpected(prompt.Error());
    DrawCenteredText(context, CenterX, PromptTop, prompt.Value());

    sprites.SetColor(BarColor);
    const std::int32_t digits = DigitsOf(mode);
    float barWidth = BarBaseWidth;
    if (digits > PasswordDigits)
        barWidth =
            F32(static_cast<double>(digits - PasswordDigits) * BarDigitWidth + static_cast<double>(BarBaseWidth));
    const float half = F32(static_cast<double>(barWidth) * Half);
    Quad(sprites, BarSlot, F32(BarCenter - static_cast<double>(half)), BarTop, barWidth, BarHeight, BarDu, BarDv);
    sprites.SetColor(White);
    SelectFont(context, FontKind::Normal);
    hud.textColors = TypedColors;
    SelectFont(context, FontKind::BigBold);
    const std::string typed(TypedLength(npc.typedNumber), '*');
    DrawText(context, digits - Truncate(static_cast<double>(half) - BarCenter), TypedTop, typed);

    for (std::int32_t key = 0; key < KeyCount; ++key)
    {
        bool pressed = false;
        if (hovered == key)
        {
            pressed = npc.keypadPressed != 0;
            sprites.SetColor(HoveredKeyColor);
            hud.textColors.text = HoveredDigitColor;
        }
        else
        {
            sprites.SetColor(KeyColor);
            hud.textColors.text = DigitColor;
        }
        const std::int32_t left = (key % KeyColumns) * KeyStep + KeyLeft;
        const std::int32_t top = (key / KeyColumns) * KeyDrawStep + KeyDrawTop;
        const auto x = static_cast<float>(left);
        const auto y = static_cast<float>(top);
        Quad(sprites, KeySlot, x, y, KeyQuadSize, KeyQuadSize, 1.0f, 1.0f);
        if (key == KeypadEraseKey)
        {
            // 0x5e9ee8..0x5e9f1b: 0xfe, plus 1 hovered and 2 pressed.
            std::uint16_t erase = EraseSlot;
            if (hovered == KeypadEraseKey)
                erase = static_cast<std::uint16_t>(EraseSlot + (pressed ? 2 : 1));
            Quad(sprites, erase, x, y, KeyQuadSize, KeyQuadSize, 1.0f, 1.0f);
            continue;
        }
        Quad(sprites, hovered == key && pressed ? PressedKeySlot : KeySlot, x, y, KeyQuadSize, KeyQuadSize, 1.0f, 1.0f);
        // wsprintfA("%d") of the signed word (movsx).
        const std::string number =
            std::format("{}", static_cast<std::int16_t>(npc.keypad[static_cast<std::size_t>(key)]));
        hud.textColors.background = 0;
        DrawText(context, left + DigitLeft, top + DigitTop - (pressed ? 1 : 0), number);
    }

    sprites.SetColor(White);
    for (std::int32_t button = 0; button < ButtonCount; ++button)
    {
        const auto slot = static_cast<std::uint16_t>(ButtonSlots[static_cast<std::size_t>(button)] +
                                                     (hovered == KeypadOkKey + button ? 1 : 0));
        Quad(sprites, slot, static_cast<float>(ButtonLeft + ButtonStep * button), ButtonQuadTop, ButtonQuadWidth,
             ButtonQuadHeight, ButtonDu, ButtonDv);
    }
    return {};
}
} // namespace Client::Warehouse
