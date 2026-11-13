#include "Scenes/EventChipWindow.h"

#include "Core/Expected.h"
#include "Protocol/Events.h"
#include "Protocol/Outgoing.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/EventNpcWindows.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/PanelRegistry.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Result = Core::Expected<void, std::string>;
using Render::Text::FontKind;

// La esquina de la ventana (0x5f658d), la del inventario: la escribe el dibujo en 0x78a5a14 y 0x78a5a10.
constexpr std::int32_t WindowLeft = 0x1c2;
constexpr std::int32_t WindowTop = 0;
// 0x5e655b..0x5e657d: el mouse en [0x1c2, 0x280) × [0, 0x1b1) es de la UI.
constexpr std::int32_t WindowRight = 0x280;
constexpr std::int32_t WindowBottom = 0x1b1;
// Los botones: Interface\... 0xf0 de 120 de ancho (0x42f00000) desde x = 485 (0x6ba6a4 en el dibujo, 0x1e5 en el paso),
// con du = 0,83203125 (0x3f550000).
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr std::int32_t ButtonLeft = 0x1e5;
constexpr std::int32_t ButtonWidth = 0x78;
constexpr float ButtonDu = 0.83203125f;
// El del 4 en y 220 (0x6ba69c) de 24 de alto; el de los otros en 231 (0x6ba694) de 22.
constexpr std::int32_t MutoButtonTop = 0xdc;
constexpr std::int32_t MutoButtonHeight = 0x18;
constexpr std::int32_t ChipButtonTop = 0xe7;
constexpr std::int32_t ChipButtonHeight = 0x16;
// La entrada del número de serie: el dibujo parte de 450 (0x43e10000) + 35 (0x6b8974) y la y de la esquina + 250
// (0x6b8920); el botón de confirmar va 40 (0x6b8bf4) más abajo, en la y + 290 que mira el paso (0x6ba63c).
constexpr float SerialLeft = 450.0f;
constexpr float SerialShift = 35.0f;
constexpr float SerialTop = 250.0f;
constexpr float ConfirmBelow = 40.0f;
constexpr float ConfirmStepTop = 290.0f;
// 0x5f6895: los textos centrados en x + 56,5 (0x6ba68c).
constexpr float SerialTextShift = 56.5f;
// 0x5f6918..0x5f695b: el marco 0x10f en (x + 3, y + 10) de 113 × 18 con du 0,8828125 y dv 0,5625.
constexpr std::uint16_t SerialFrameSlot = 0x10f;
constexpr float SerialFrameLeft = 3.0f;
constexpr float SerialFrameTop = 10.0f;
constexpr float SerialFrameWidth = 113.0f;
constexpr float SerialFrameHeight = 18.0f;
constexpr float SerialFrameDu = 0.8828125f;
constexpr float SerialFrameDv = 0.5625f;
// 0x5f6994..0x5f69c8: el EDIT, 150 × 14 en (ftol(x) + 10, y + 15), con el tope en 12 y el filtro 2.
constexpr std::size_t SerialLimit = 0xc;
constexpr std::int32_t SerialEditWidth = 0x96;
constexpr std::int32_t SerialEditHeight = 0xe;
constexpr std::int32_t SerialEditLeft = 0xa;
constexpr std::int32_t SerialEditTop = 0xf;
// 0x5f6b1f..0x5f6b42: el texto del botón de confirmar en (x + 60, y + 5).
constexpr float ConfirmTextLeft = 60.0f;
constexpr float ConfirmTextTop = 5.0f;
// 0x5f6b3c..0x5f6b4a: el texto de los botones de arriba, centrado en x 0x221 y en y 0xe1 (el 4) o 0xec.
constexpr std::int32_t ButtonTextLeft = 0x221;
constexpr std::int32_t MutoButtonTextTop = 0xe1;
constexpr std::int32_t ChipButtonTextTop = 0xec;
// 0x5f6c6c..0x5f6c93: los textos de cada tipo, centrados en x + 95 (0x6b87e4) desde y + 100 (0x6b86b4), de a 15; con 3
// y 4 desde y + 70 (0x6ba1b0). El resultado del 9D, 0x104 más abajo (y una sombra en x + 1, y + 0x105).
constexpr float TypeTextLeft = 95.0f;
constexpr float TypeTextTop = 100.0f;
constexpr float LowTypeTextTop = 70.0f;
constexpr std::int32_t TypeTextStep = 0xf;
constexpr std::int32_t SerialResultBelow = 0x104;
// 0x5f6ef4 y 0x5f6f72: los rótulos en x 0x1e5 (y 0x122 y 0x145) y las cuentas en x 0x230 (y 300 o 335; con 5, 290 y
// 325, 0x6ba63c y 0x6b8978).
constexpr std::int32_t LabelLeft = 0x1e5;
constexpr std::int32_t ItemLabelTop = 0x122;
constexpr std::int32_t CountLabelTop = 0x145;
constexpr std::int32_t CountLeft = 0x230;
constexpr float ItemCountTop = 300.0f;
constexpr float ChipCountTop = 335.0f;
constexpr float FifthItemCountTop = 290.0f;
constexpr float FifthChipCountTop = 325.0f;
constexpr std::int32_t FifthWindow = 5;
// 0x5f6dd4..0x5f6de7: sin(0x5767b88 · 0,01f) contra 0.
constexpr float BlinkRate = 0.01f;
// 0x5e72ac y 0x5e74af: el botón de cerrar (0x19, 0x18b de la esquina del inventario, 24 × 24).
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;

// La Rena y la piedra (14/21, de nivel 0 y 1) que cuentan y registran el 1 y el 2.
constexpr std::uint16_t ChipItem = 0x1c15;
constexpr std::int32_t StoneLevel = 1;
// 0x5f70c7: el +4 de los modelos del 2 y del 4.
constexpr std::uint32_t StoneModelLevel = 0xf;
// 0x5f7131 y 0x5f7150: los dos modelos en (540, 298) y (540, 333).
constexpr float ItemModelLeft = 540.0f;
constexpr float ChipModelTop = 298.0f;
constexpr float StoneModelTop = 333.0f;
// 0x5e6d16: 0x558740(−1, −1), las casillas libres.
constexpr std::int32_t AnyItem = -1;

// Text.bmd (la dirección − 0x76bd1dc, sobre 300).
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

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(float value)
{
    return World::OriginalFtolLow(static_cast<double>(value)).value_or(0);
}

bool InBox(std::int32_t mouseX, std::int32_t mouseY, std::int32_t left, std::int32_t top, std::int32_t width,
           std::int32_t height)
{
    return mouseX >= left && mouseX < left + width && mouseY >= top && mouseY < top + height;
}

// Las comparaciones de la x87 del dibujo: left ≤ mouse < left + ancho y top ≤ mouse < top + alto.
bool Hovered(const GameHudContext& context, float left, float top, float width, float height)
{
    const auto x = static_cast<double>(context.mouse.x);
    const auto y = static_cast<double>(context.mouse.y);
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(width) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(height);
}

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// El send() del paso, por la sesión.
Result Send(const GameUiContext& context, Protocol::Outgoing packet, std::string_view where)
{
    if (context.chat == nullptr)
        return Core::Unexpected(std::format("{}: el send() sin el contexto de la sesión", where));
    if (context.chat->send)
        context.chat->send(std::move(packet));
    return {};
}

// 0x5e6d34..0x5e6dab con 0x6c7b8c en 1: GetText del EDIT (la vtable + 0x50, hasta 0xff bytes) a la pila y los tres
// dwords de [ebp-0x58], [ebp-0x54] y [ebp-0x50]. Detrás del '\0' de un texto de menos de 11 letras quedan los bytes que
// tenía la pila, que MuRet pone en 0 (P-C247). Más de 12 letras pisarían la pila del paso: solo las pone un SetText
// que nadie hace en el juego, y corta.
Core::Expected<Protocol::Events::SerialPieces, std::string> SerialPiecesOf(const UI::OldEdit& edit)
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

// 0x5e6cb8..0x5e7248: el botón de confirmar de la entrada del número de serie, en (ftol(0x1c2 + 35), ftol(esquina +
// 290)) de 120 × 22.
Result StepTheSerialNumber(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t left = Truncate(F32(static_cast<double>(WindowLeft) + static_cast<double>(SerialShift)));
    const std::int32_t top =
        Truncate(F32(static_cast<double>(runtime.hud.inventoryY) + static_cast<double>(ConfirmStepTop)));
    if (!InBox(context.mouseX, context.mouseY, left, top, ButtonWidth, ChipButtonHeight) ||
        !runtime.movement.releasedClick)
        return {};
    if (CountInventoryItems(context.world.Inventory(), AnyItem, AnyItem) == 0)
    {
        auto text = TextOf(context.texts, InventoryFullText, "0x5e6d22");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, text.Value());
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
    runtime.movement.leftHeld = false;
    return {};
}

// 0x5e7255..0x5e74c6: el botón de cerrar.
Result StepTheCloseButton(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& movement = runtime.movement;
    if (!InBox(context.mouseX, context.mouseY, runtime.hud.inventoryX + CloseLeft, runtime.hud.inventoryY + CloseTop,
               CloseSize, CloseSize) ||
        !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
    if (auto sent = Send(context, Protocol::Events::WriteEventWindowLeave(), "0x5e739e"); !sent.HasValue())
        return sent;
    runtime.panels.field78abb00 = 0;
    if (auto closed = panels.Close(UI::InventoryPanel); !closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    // 0x5e749c..0x5e74b6: 0x77f3b6a, 0x77f3b6c y 0x78abacc en 0 no los tiene MuRet.
    runtime.ui.chatOpen = false;
    runtime.events.serialEntry = false;
    // 0x5e74bf: SetFocus([0x5763544]), con 0x6c7b8c en 1. El texto del EDIT queda.
    runtime.chat.input.focus = UI::ChatFocus::Game;
    return {};
}

// ----- El dibujo.

// La vtable + 0x14 del EDIT, 0x468320, con el foco de Windows en él.
void DrawTheSerialEdit(GameHudContext& context, UI::OldEdit& edit, bool focused)
{
    const UI::OldWindowDrawContext drawing{.sprites = context.sprites,
                                           .text = context.text,
                                           .dcFont = context.hud.dcFont,
                                           .textColors = context.hud.textColors};
    UI::OldDraw::DrawTheOldEdit(drawing, edit, focused);
}

// 0x5f696d..0x5f69e3: con 0x6c7b8c en 1, el EDIT de [0x5763504] reconfigurado en cada frame y dibujado.
void DrawTheSerialField(GameHudContext& context, std::int32_t left, std::int32_t top)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::ChatInputState& input = runtime.chat.input;
    UI::OldEdit copy = input.dialogEdit;
    UI::OldEdit& edit = context.projecting ? copy : input.dialogEdit;
    edit.base.state = UI::ShownWindow;
    edit.filter = UI::UppercaseLettersAndDigitsEdit;
    edit.backgroundColor = 0;
    edit.text.SetLimit(SerialLimit);
    // 0x467f50: el ancho hasta 0xee.
    edit.base.width = SerialEditWidth;
    edit.base.height = SerialEditHeight;
    edit.base.x = left + SerialEditLeft;
    edit.base.y = top + SerialEditTop;
    // 0x4682a0(0): el foco de Windows (y 0x5762ec0, que MuRet no lee para los EDIT).
    if (!context.projecting)
        input.focus = UI::ChatFocus::Dialog;
    DrawTheSerialEdit(context, edit, true);
}

// 0x5f66d6..0x5f6700 y 0x5f6806..0x5f6830: el botón de arriba, después del color del mouse.
void DrawTheButtonQuad(GameHudContext& context, float top, float height)
{
    WindowDrawing::Quad(context.sprites, ButtonSlot, static_cast<float>(ButtonLeft), top,
                        static_cast<float>(ButtonWidth), height, ButtonDu, 1.0f);
}

// Con el botón apretado bajo el mouse, la fuente normal, el gris y, con el soltado, el clic y el botón apagados
// (0x5f6696..0x5f66d2, 0x5f677a..0x5f67b5 y 0x5f6aa4..0x5f6adf).
void PressTheButton(GameHudContext& context)
{
    WindowDrawing::SelectFont(context, FontKind::Normal);
    context.sprites.SetColor(Pressed);
    GameMovementState& movement = context.world.Runtime().movement;
    if (!movement.releasedClick || context.projecting)
        return;
    movement.pendingClick = false;
    movement.leftHeld = false;
}

// 0x5f6646..0x5f6865: el botón del 4, o el de los otros salvo el 3.
Result DrawTheTopButton(GameHudContext& context, std::int32_t window, std::int32_t items)
{
    const bool click = context.world.Runtime().movement.pendingClick;
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
        WindowDrawing::DrawCenteredText(context, ButtonTextLeft, MutoButtonTextTop, text.Value());
        return {};
    }
    const auto top = static_cast<float>(ChipButtonTop);
    const auto height = static_cast<float>(ChipButtonHeight);
    if (Hovered(context, static_cast<float>(ButtonLeft), top, static_cast<float>(ButtonWidth), height) && click)
        PressTheButton(context);
    else
    {
        // 0x5f67b9..0x5f6804: en blanco y en negrita con ítems, en gris y con la normal sin ellos.
        WindowDrawing::SelectFont(context, items > 0 ? FontKind::Bold : FontKind::Normal);
        context.sprites.SetColor(items > 0 ? White : Pressed);
    }
    DrawTheButtonQuad(context, top, height);
    if (window != RenaChipWindow && window != StoneChipWindow)
        return {};
    auto text = TextOf(context.texts, window == RenaChipWindow ? RenaButtonText : StoneButtonText, "0x5f6853");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    WindowDrawing::DrawCenteredText(context, ButtonTextLeft, ChipButtonTextTop, text.Value());
    return {};
}

// 0x5f6872..0x5f6b47: la entrada del número de serie, con x = 450 + 35 e y = la esquina + 250.
Result DrawTheSerialEntry(GameHudContext& context)
{
    const float left = F32(static_cast<double>(SerialLeft) + static_cast<double>(SerialShift));
    float top = F32(static_cast<double>(context.hud.inventoryY) + static_cast<double>(SerialTop));
    const std::int32_t row = Truncate(top);
    const std::int32_t middle = Truncate(F32(static_cast<double>(left) + static_cast<double>(SerialTextShift)));
    // 0x5f68a4..0x5f68fb: 893 en y − 0x28; 916 y 917 en y − 0xf e y − 5 en 0xff67bfdf; 894 en negrita en y − 0x19.
    constexpr std::array<std::int32_t, 4> Above{0x28, 0xf, 5, 0x19};
    for (std::size_t line = 0; line < SerialTexts.size(); ++line)
    {
        if (line == 1)
            context.hud.textColors = SerialNoteColors;
        if (line == 3)
            WindowDrawing::SelectFont(context, FontKind::Bold);
        auto text = TextOf(context.texts, SerialTexts[line], "0x5f68a7");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        WindowDrawing::DrawCenteredText(context, middle, row - Above[line], text.Value());
    }
    WindowDrawing::SelectFont(context, FontKind::Normal);
    // 0x5f6918..0x5f695b: el marco, con 0x6c8e5c en 0xfff0ff96.
    context.hud.textColors = TitleColors;
    WindowDrawing::Quad(context.sprites, SerialFrameSlot, F32(static_cast<double>(left) + SerialFrameLeft),
                        F32(static_cast<double>(top) + SerialFrameTop), SerialFrameWidth, SerialFrameHeight,
                        SerialFrameDu, SerialFrameDv);
    DrawTheSerialField(context, Truncate(left), row);
    // 0x5f6a43..0x5f6aef: el botón de confirmar, 40 más abajo.
    top = F32(static_cast<double>(top) + static_cast<double>(ConfirmBelow));
    if (Hovered(context, left, top, static_cast<float>(ButtonWidth), static_cast<float>(ChipButtonHeight)))
    {
        if (context.world.Runtime().movement.pendingClick)
            PressTheButton(context);
        else
            context.hud.textColors = HoveredConfirmColors;
    }
    WindowDrawing::Quad(context.sprites, ButtonSlot, left, top, static_cast<float>(ButtonWidth),
                        static_cast<float>(ChipButtonHeight), ButtonDu, 1.0f);
    auto confirm = TextOf(context.texts, ConfirmText, "0x5f6b28");
    if (!confirm.HasValue())
        return Core::Unexpected(std::move(confirm).Error());
    WindowDrawing::DrawCenteredText(context, Truncate(F32(static_cast<double>(left) + ConfirmTextLeft)),
                                    Truncate(F32(static_cast<double>(top) + ConfirmTextTop)), confirm.Value());
    return {};
}

// 0x5f6d7e..0x5f6e55: el resultado del 9D (0x78ab078) si su primer byte no es el de 0x78abb84 (.bss en 0): una sombra
// negra en negrita y el texto en 0xff67d2ff, en gris o en blanco con el signo de sin(reloj · 0,01f).
void DrawTheSerialResult(GameHudContext& context, std::int32_t middle, std::int32_t top)
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
    WindowDrawing::SelectFont(context, FontKind::Bold);
    WindowDrawing::DrawCenteredText(context, middle + 1, top + SerialResultBelow + 1, text);
    const double wave = std::sin(static_cast<double>(context.worldClock) * static_cast<double>(BlinkRate));
    context.sprites.SetColor(wave < 0.0 ? White : Dimmed);
    context.hud.textColors = SerialResultColors;
    WindowDrawing::DrawCenteredText(context, middle, top + SerialResultBelow, text);
    context.sprites.SetColor(White);
    WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = TitleColors;
}

// 0x5f6c6c..0x5f6eb0: los cuatro textos del tipo, de a 15.
Result DrawTheTypeTexts(GameHudContext& context, std::int32_t window)
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
        WindowDrawing::DrawCenteredText(context, middle, first + static_cast<std::int32_t>(line) * TypeTextStep,
                                        text.Value());
    }
    if (window == SerialNumberWindow)
        DrawTheSerialResult(context, middle, first);
    return {};
}

// 0x5f6ec8..0x5f7047: el rótulo del 1 o del 2 en negrita y, salvo con 3, la cuenta con "x%d" en la fuente grande.
Result DrawTheCount(GameHudContext& context, std::int32_t window, std::size_t renaLabel, std::size_t stoneLabel,
                    std::int32_t labelTop, std::int32_t count, float countTop, float fifthTop)
{
    WindowDrawing::SelectFont(context, FontKind::Bold);
    if (window == RenaChipWindow || window == StoneChipWindow)
    {
        auto label = TextOf(context.texts, window == RenaChipWindow ? renaLabel : stoneLabel, "0x5f6ef9");
        if (!label.HasValue())
            return Core::Unexpected(std::move(label).Error());
        WindowDrawing::DrawText(context, LabelLeft, labelTop, label.Value());
    }
    if (window == SerialNumberWindow)
        return {};
    WindowDrawing::SelectFont(context, FontKind::BigBold);
    const std::string text = "x" + std::to_string(count);
    context.hud.textColors = CountColors;
    // 0x5f6f4a: la tabla de 0x5f7078 (y 0x5f708c) solo dibuja con 1, 2, 4 y 5.
    if (window == RenaChipWindow || window == StoneChipWindow || window == MutoNumberWindow)
        WindowDrawing::DrawText(context, CountLeft, Truncate(countTop), text);
    else if (window == FifthWindow)
        WindowDrawing::DrawText(context, CountLeft, Truncate(fifthTop), text);
    return {};
}
} // namespace

void OpenTheSerialEntry(WorldRuntime& runtime)
{
    runtime.events.serial = {};
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.ui.chatOpen = false;
    runtime.chat.input.fieldCount = 1;
    runtime.events.serialEntry = true;
}

void CloseTheSerialEntry(WorldRuntime& runtime)
{
    UI::ChatInputState& input = runtime.chat.input;
    input.field = UI::MessageField;
    input.messageBuffer.fill('\0');
    input.messageLength = 0;
    input.dialogEdit.text.SetText({});
    runtime.ui.chatOpen = false;
    runtime.events.serialEntry = false;
}

Core::Expected<void, std::string> StepTheEventChipWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t window = runtime.panels.field78abb00;
    if (window == 0)
        return {};
    GameMovementState& movement = runtime.movement;
    if (InBox(context.mouseX, context.mouseY, WindowLeft, WindowTop, WindowRight - WindowLeft,
              WindowBottom - WindowTop))
        runtime.ui.mouseOnUi = 1;
    if (window == MutoNumberWindow)
    {
        // 0x5e6594..0x5e6864.
        if (InBox(context.mouseX, context.mouseY, ButtonLeft, MutoButtonTop, ButtonWidth, MutoButtonHeight) &&
            movement.releasedClick)
        {
            if (auto sent = Send(context, Protocol::Events::WriteMutoNumberRequest(), "0x5e67a0"); !sent.HasValue())
                return sent;
            movement.releasedClick = false;
        }
    }
    else if (window != SerialNumberWindow)
    {
        // 0x5e687b..0x5e6c94: con 2 la piedra; con 1 o cualquier otro, la Rena.
        if (InBox(context.mouseX, context.mouseY, ButtonLeft, ChipButtonTop, ButtonWidth, ChipButtonHeight) &&
            movement.releasedClick)
        {
            movement.releasedClick = false;
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
    movement.releasedClick = false;
    return {};
}

Core::Expected<void, std::string> DrawTheEventChipWindow(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t window = runtime.panels.field78abb00;
    if (window == 0)
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    sprites.SetBlend(Render::BlendMode::Alpha);
    context.hud.inventoryX = WindowLeft;
    context.hud.inventoryY = WindowTop;
    WindowDrawing::DrawTheWindowBack(sprites, WindowLeft, WindowTop);
    context.hud.textColors = TitleColors;
    // 0x5f65df: 0x54ebb0(0xec), el nombre del NPC 236 en la tabla de NpcName(%s).txt.
    // TODO(hito 2): en el juego este corte salta con cada 94 hasta que el hito 2 porte 0x54e7f0 y 0x54e8d0 (P-C174);
    // al juntar el hito 2 en el hito 11, GameSceneUi le pasa esa tabla a npcName (P-C248).
    const std::optional<std::string_view> title = context.npcName ? context.npcName(TitleNpc) : std::nullopt;
    if (!title)
        return Core::Unexpected(std::string("0x5f65df: el título es el NPC 236 de 0x54ebb0, la tabla de "
                                            "Data\\Local\\NpcName(%s).txt (0x54e7f0), que MuRet no carga (hito 2)"));
    WindowDrawing::DrawTitle(context, WindowLeft + 0x23, WindowTop + 0xc, *title);
    // 0x5f6609..0x5f661b: la cuenta del 0x1c15, de nivel 1 con el 2.
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
    // 0x5f6b4c..0x5f6c66: el botón de cerrar y su ayuda; después la fuente normal y 0xffd2e6ff sobre 0.
    if (WindowDrawing::DrawTheCloseButton(context, context.hud.inventoryX, context.hud.inventoryY))
    {
        auto help = TextOf(context.texts, CloseHelpText, "0x5f6c25");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, context.hud.inventoryX, context.hud.inventoryY, help.Value());
    }
    WindowDrawing::SelectFont(context, FontKind::Normal);
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

Core::Expected<void, std::string> DrawTheEventChipItems(GameHudContext& context)
{
    const std::int32_t window = context.world.Runtime().panels.field78abb00;
    std::int32_t type = ChipItem;
    std::uint32_t level = 0;
    if (window == StoneChipWindow || window == MutoNumberWindow)
        level = StoneModelLevel;
    else if (window == FifthWindow)
        type = 0;
    auto box = ItemSizeOf(context, type, "0x5f70e7");
    if (!box.HasValue())
        return Core::Unexpected(std::move(box).Error());
    box.Value().x = ItemModelLeft;
    if (window != MutoNumberWindow)
    {
        box.Value().y = ChipModelTop;
        if (auto drawn = DrawUiItem(context, box.Value(), type, level, 0, 0, false); !drawn.HasValue())
            return drawn;
    }
    box.Value().y = StoneModelTop;
    return DrawUiItem(context, box.Value(), type, level, 0, 0, false);
}
} // namespace Client::Scenes
