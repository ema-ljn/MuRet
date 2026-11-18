#include "Scenes/WarehouseKeypad.h"

#include "Core/Expected.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/NpcWindowSteps.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"
#include "World/OriginalFtol.h"

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

namespace Client::Scenes
{
namespace
{
using Render::Text::FontKind;
using WindowDrawing::DrawCenteredText;
using WindowDrawing::DrawText;
using WindowDrawing::Quad;
using WindowDrawing::SelectFont;

namespace Npc = Protocol::NpcWindows;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5e7c6d: 0x41ed10(0x19, 0, 0), el sonido de las teclas.
constexpr std::int32_t KeySound = 0x19;
// 0x5e7d39 y 0x5e7e7e: los diálogos de cuatro cifras iguales y de la contraseña repetida distinta.
constexpr std::int32_t SameDigitsDialog = 0x87;
constexpr std::int32_t WrongRepeatDialog = 0x88;
// 0x5e7dfb y 0x5e7e62: los modos que siguen al 4 y al 5.
constexpr std::int32_t RepeatPasswordMode = 5;
constexpr std::int32_t SetPasswordMode = 2;
// Los tipos del 83 de los modos 2 y 3 (el modo menos 1, 0x5e7f0b y 0x5e8c80): CGWarehouseCheckPass pone la contraseña
// con 1 y la saca con 2, los dos con el número personal.
constexpr std::uint8_t SetPasswordType = 1;
constexpr std::uint8_t RemovePasswordType = 2;
// El rand() del relleno y el del serial de un C1 en C3.
constexpr std::uint32_t C3SendRandoms = 2;

// 0x5e7b50: las teclas de 32 (0x20) desde (0xdf, 0x96), de a 40 (0x28) en las dos direcciones, cinco por fila.
constexpr std::int32_t KeyColumns = 5;
constexpr std::int32_t KeyLeft = 0xdf;
constexpr std::int32_t KeyHitTop = 0x96;
constexpr std::int32_t KeyStep = 40;
constexpr std::int32_t KeySize = 0x20;
constexpr std::int32_t KeyCount = 11;
// El OK y cancelar: 0x46 × 0x15 desde (0x109, 0xeb), de a 0x4e.
constexpr std::int32_t ButtonLeft = 0x109;
constexpr std::int32_t ButtonStep = 0x4e;
constexpr std::int32_t ButtonWidth = 0x46;
constexpr std::int32_t ButtonTop = 0xeb;
constexpr std::int32_t ButtonBottom = 0x100;
constexpr std::int32_t ButtonCount = 2;

// 0x5e9b45..0x5e9bf8: el marco, Message_box2 (0xfc) de 213 × 5 en (213, 100) y en (213, 265) y cuatro filas de
// Message_box1 (0xfb) de 213 × 40 desde y = 0x69 de a 40, con du = 0,83203125 y dv = 0,625.
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

// 0x5e9bfd..0x5e9cb3: los dos renglones centrados en 0x140, en y = 0x6e y 0x7a, con la fuente gruesa en 0xffffc4c4
// sobre un fondo transparente. Los títulos son 690 a 694 (0x76efa74 − 0x76bd1dc, sobre 300) y los pedidos 695 a 697.
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

// 0x5e9cbb..0x5e9d4f: la barra de lo tecleado, bar.jpg (0xfd) en gris 0,3, de 52 (0x42500000) de ancho, o (cifras − 4)
// · 13 (0x6b8834) + 52 (0x6ba060) con más de cuatro, centrada en 320 (0x6ba22c) con la mitad (0x6b882c), en y = 134 y
// de 16 de alto, con du = 0,625 y dv = 0,5625.
constexpr Render::Color BarColor{0.3f, 0.3f, 0.3f, 1.0f};
constexpr std::uint16_t BarSlot = 0xfd;
constexpr float BarBaseWidth = 52.0f;
constexpr double BarDigitWidth = 13.0;
constexpr double BarCenter = 320.0;
constexpr double Half = 0.5;
constexpr float BarTop = 134.0f;
constexpr float BarHeight = 16.0f;
constexpr float BarDu = 0.625f;
constexpr float BarDv = 0.5625f;
// 0x5e9d7b..0x5e9e12: lo tecleado como asteriscos ("*", 0x6c9cdc) detrás del byte de 0x78abb78, que nadie escribe (el
// 0 de la .bss: la cadena empieza vacía), con la fuente gruesa grande en 0xffc48080 sobre transparente, en
// (cifras − _ftol(mitad − 320), 0x84).
constexpr Render::Text::TextColors TypedColors{.text = 0xffc48080u, .background = 0};
constexpr std::int32_t TypedTop = 0x84;

// 0x5e9e1c..0x5e9fb8: las teclas en ((i % 5) · 40 + 0xdf, (i / 5) · 38 + 0x9a), de 32 × 32 con du = dv = 1: Item_box
// (0x115) y encima Item_box otra vez, o InventoryBox2 (0x116) la del mouse apretada; la de borrar, back1 (0xfe), back2
// (0xff) con el mouse o back3 (0x100) apretada. La del mouse en (1, 1, 0,8) con el texto en 0xff00c4c4; las demás en
// (0,85; 0,85; 0,65) con 0xffc48080. El número en (x + 0xc, y + 6), uno más arriba apretada.
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
// 0x5e9fbe..0x5ea042: el OK (ok 0xf1, ok2 0xf2 con el mouse) y cancelar (cancel 0xf3, cancel2 0xf4) de 70 × 21 en
// (0x109 + 0x4e · j, 239), con du = 0,546875 y dv = 0,65625.
constexpr std::array<std::uint16_t, ButtonCount> ButtonSlots{0xf1, 0xf3};
constexpr float ButtonQuadTop = 239.0f;
constexpr float ButtonQuadWidth = 70.0f;
constexpr float ButtonQuadHeight = 21.0f;
constexpr float ButtonDu = 0.546875f;
constexpr float ButtonDv = 0.65625f;

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x5e9c15..0x5e9c4e: el título por el modo.
std::size_t TitleOf(std::int32_t mode)
{
    if (mode == RepeatedTitleMode)
        return ChangeTitleText;
    return FirstTitleText + static_cast<std::size_t>(std::clamp(mode - 1, 0, LastTitleOffset));
}

// 0x5e9c70..0x5e9c96, la tabla de 0x5ea04c: el pedido por el modo.
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

// 0x5e9ccc..0x5e9ceb: los modos 2, 3 y 6 piden el número personal.
std::int32_t DigitsOf(std::int32_t mode)
{
    return mode == 3 || mode == 2 || mode == 6 ? PersonalNumberDigits : PasswordDigits;
}

// strlen de 0x78ab9fc, que no pasa de su arreglo: nadie teclea más de siete.
std::size_t TypedLength(const std::array<std::uint8_t, 11>& typed)
{
    return static_cast<std::size_t>(std::ranges::find(typed, std::uint8_t{0}) - typed.begin());
}

// 0x6a0c01, el atoi de la CRT, sobre lo tecleado: solo cifras de las teclas (0x7879fc8 va de 0 a 9 desde que se
// baraja), así que no hay espacios ni signo; para en el 0.
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

Protocol::NpcWindows::WarehouseNumber NumberOf(std::span<const std::uint8_t> bytes)
{
    Protocol::NpcWindows::WarehouseNumber number{};
    for (std::size_t at = 0; at < number.size(); ++at)
        number[at] = static_cast<std::byte>(bytes[at]);
    return number;
}

Core::Expected<void, std::string> Send(const GameUiContext& context, Protocol::Outgoing packet, std::string_view what)
{
    if (!context.sendNpcWindow)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return context.sendNpcWindow(std::move(packet));
}

// Un C1 con el XOR32 en C3: el rand() del relleno (0x69fd70) y el del serial (0x40b0a0, o en línea 0x40b3f0), antes
// del envío.
Core::Expected<void, std::string> SendInC3(const GameUiContext& context, Protocol::Outgoing packet,
                                           std::string_view what)
{
    context.random.Discard(C3SendRandoms);
    return Send(context, std::move(packet), what);
}

// 0x5e7d80..0x5e9a88: el OK con todas las cifras y sin cuatro iguales, por el modo que tenía (ya en 0).
Core::Expected<void, std::string> Confirm(const GameUiContext& context, UI::PanelRegistry& panels, std::int32_t mode)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    const auto typed = NumberOf(npc.typedNumber);
    switch (mode)
    {
    case 2:
        // 0x5e7e90..0x5e854d.
        return SendInC3(context, Npc::WriteWarehousePassword(SetPasswordType, npc.warehousePassword, typed),
                        "0x5e7e90: el C1 10 83 01");
    case 3:
        // 0x5e8c02..0x5e931a: el tipo es el modo menos 1 (0x5e8c80) y la contraseña, 0 (0x5e8d78).
        return SendInC3(context, Npc::WriteWarehousePassword(RemovePasswordType, 0, typed), "0x5e8c02: el C1 10 83 02");
    case 4:
        // 0x5e7d80..0x5e7e1e: lo tecleado a 0x78aba07, el teclado barajado como en 0x5e7aa0 y el modo en 5.
        std::ranges::copy_n(npc.typedNumber.begin(), static_cast<std::ptrdiff_t>(npc.newPassword.size()),
                            npc.newPassword.begin());
        OpenTheKeypad(runtime, context.random, RepeatPasswordMode);
        return {};
    case 5:
        // 0x5e7e23..0x5e7e8b: memcmp de cuatro bytes; la misma, su atoi en 0x78abb24, lo tecleado en 0 y el modo en
        // 2, sin barajar.
        if (!std::ranges::equal(std::span(npc.typedNumber).first(npc.newPassword.size()), npc.newPassword))
        {
            UI::PushDialog(runtime.dialogs, WrongRepeatDialog);
            return {};
        }
        npc.warehousePassword = static_cast<std::uint16_t>(TypedValue(npc.typedNumber) & 0xffff);
        npc.typedNumber.fill(0);
        runtime.ui.field78abb20 = SetPasswordMode;
        return {};
    case 6:
    {
        // 0x5e8552..0x5e86ee: C1 03 31 en claro, sin rand(); 0x5e86f4..0x5e8701: 0x657a90; 0x5e8706..: el 99.
        if (auto sent = Send(context, Protocol::Connection::WriteWindowClose(), "0x5e8552: el C1 03 31");
            !sent.HasValue())
            return sent;
        if (auto closed = panels.CloseAll(); !closed.HasValue())
            return closed;
        return SendInC3(context, Npc::WritePersonalNumber(typed), "0x5e8706: el C1 0D 99");
    }
    default:
    {
        // 0x5e931c..0x5e9a88 (el 1 y los modos fuera de 2..6): el tipo es el modo menos 1 (0x5e939a), la contraseña
        // el atoi de lo tecleado (0x5e9491) y el número, los diez primeros bytes del buffer del mensaje del chat
        // (0x7798bf8, 0x5e95b1..0x5e95d2; Q-C924).
        const auto chat = std::as_bytes(std::span(runtime.chat.input.messageBuffer));
        Protocol::NpcWindows::WarehouseNumber number{};
        std::ranges::copy_n(chat.begin(), static_cast<std::ptrdiff_t>(number.size()), number.begin());
        const auto type = static_cast<std::uint8_t>((mode - 1) & 0xff);
        const auto password = static_cast<std::uint16_t>(TypedValue(npc.typedNumber) & 0xffff);
        return SendInC3(context, Npc::WriteWarehousePassword(type, password, number), "0x5e931c: el C1 10 83");
    }
    }
}

// 0x5e7c69..0x5e9a88: la tecla con el clic.
Core::Expected<void, std::string> PressTheKey(const GameUiContext& context, UI::PanelRegistry& panels, std::int32_t key)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    auto& typed = npc.typedNumber;
    const std::int32_t mode = runtime.ui.field78abb20;
    const auto digits = static_cast<std::size_t>(DigitsOf(mode));
    // 0x41ed10(0x19, 0, 0) (CL10).
    runtime.lastSoundId = KeySound;
    const std::size_t length = TypedLength(typed);
    switch (key)
    {
    case KeypadEraseKey:
        // 0x5e9a8a..0x5e9aa7: el último byte en 0.
        if (length != 0)
            typed[length - 1] = 0;
        return {};
    case KeypadCancelKey:
        // 0x5e7cea.
        runtime.ui.field78abb20 = 0;
        return {};
    case KeypadOkKey:
    {
        // 0x5e7cf9..0x5e7d0a: con otro largo, nada.
        if (length != digits)
            return {};
        // 0x5e7d10..0x5e7d5b: cuatro cifras iguales, solo donde se piden cuatro.
        if (digits == PasswordDigits && typed[0] == typed[1] && typed[2] == typed[3] && typed[1] == typed[2])
        {
            typed.fill(0);
            UI::PushDialog(runtime.dialogs, SameDigitsDialog);
            return {};
        }
        // 0x5e7d60..0x5e7d79: el modo en 0 antes de la tabla.
        runtime.ui.field78abb20 = 0;
        return Confirm(context, panels, mode);
    }
    default:
        break;
    }
    // 0x5e7c88..0x5e7ce5: con menos cifras que las del modo, wsprintfA("%d") de la word con signo y strcat.
    if (length >= digits)
        return {};
    const std::string number = std::format("{}", static_cast<std::int16_t>(npc.keypad[static_cast<std::size_t>(key)]));
    // El error va sin la cifra: es parte de la contraseña.
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

Core::Expected<bool, std::string> StepTheKeypad(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field78abb20 == 0)
        return false;
    GameMovementState& movement = runtime.movement;
    const std::int32_t key = KeypadKeyAt(context.mouseX, context.mouseY);
    if (movement.pendingClick)
    {
        if (key >= 0)
        {
            if (auto pressed = PressTheKey(context, panels, key); !pressed.HasValue())
                return Core::Unexpected(std::move(pressed).Error());
        }
        // 0x5e9aad..0x5e9ab8: vuelve a leer el clic.
        if (movement.pendingClick)
            runtime.npcWindows.keypadPressed = 1;
    }
    // 0x5e9abf..0x5e9ae1.
    if (movement.releasedClick)
        runtime.npcWindows.keypadPressed = 0;
    movement.pendingClick = false;
    movement.leftHeld = false;
    return true;
}

Core::Expected<void, std::string> DrawTheKeypad(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t mode = runtime.ui.field78abb20;
    if (mode == 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    const NpcWindowState& npc = runtime.npcWindows;
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
            // 0x5e9ee8..0x5e9f1b: 0xfe, más 1 con el mouse y 2 apretada.
            std::uint16_t erase = EraseSlot;
            if (hovered == KeypadEraseKey)
                erase = static_cast<std::uint16_t>(EraseSlot + (pressed ? 2 : 1));
            Quad(sprites, erase, x, y, KeyQuadSize, KeyQuadSize, 1.0f, 1.0f);
            continue;
        }
        Quad(sprites, hovered == key && pressed ? PressedKeySlot : KeySlot, x, y, KeyQuadSize, KeyQuadSize, 1.0f, 1.0f);
        // wsprintfA("%d") de la word con signo (movsx).
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
} // namespace Client::Scenes
