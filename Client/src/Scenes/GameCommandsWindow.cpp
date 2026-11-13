#include "Scenes/GameCommandsWindow.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"
#include "World/OriginalFtol.h"

#include <array>
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
using Render::BlendMode;
using Render::Color;
using Render::Text::FontKind;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Black{0.0f, 0.0f, 0.0f, 1.0f};
// glColor3f(0,5; 0,5; 0,5): el botón del elegido (0x40a726) y el nombre con el clic (0x40a967).
constexpr Color Gray{0.5f, 0.5f, 0.5f, 1.0f};
// glColor3f(1; 0,8; 0,2): el nombre del elegido o con el mouse encima, sin el clic (0x40a978).
constexpr Color Highlight{1.0f, 0.8f, 0.2f, 1.0f};
// 0x491960(0, 1): glColor4ub(0x92, 0x86, 0x79, 0xff), el marco de la casilla (0x4630a5).
constexpr Color CheckboxFrame{static_cast<float>(0x92) / 255.0f, static_cast<float>(0x86) / 255.0f,
                              static_cast<float>(0x79) / 255.0f, 1.0f};

// 0x40a668..0x40a676: la esquina que pone el dibujo.
constexpr std::int32_t WindowLeft = 0x1c2;
constexpr std::int32_t WindowTop = 0;
// 0x40a463..0x40a48f: el mouse sobre la ventana, de 0xbe × 0x1b1 desde la esquina.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x40a51f..0x40a548: el botón de cerrar, de 0x18 × 0x18 en (0x19, 0x18b) de la esquina.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x40a573..0x40a57d.
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
// 0x40a553..0x40a56b: los dos sonidos del botón de cerrar.
constexpr std::int32_t FirstCloseSound = 0x19;
constexpr std::int32_t SecondCloseSound = 0x1c;

// Los botones: x + 95 (0x6b87e4) − 50 (0x6b86b0) e y + 53 (0x6b87e0), de 28 en 28 (0x6b880c). El paso los toca entre
// los bordes, con 108 (0x6c) × 20 (0x14); el dibujo los pinta de 100 × 25 y les mira el mouse con 108 (0x6b87fc) ×
// 20 (0x6b86d0).
constexpr double ButtonsRight = 95.0;
constexpr double ButtonsBack = 50.0;
constexpr double ButtonsTop = 53.0;
constexpr std::int32_t ButtonStep = 0x1c;
constexpr float ButtonStepF = 28.0f;
constexpr std::int32_t ButtonHitWidth = 0x6c;
constexpr std::int32_t ButtonHitHeight = 0x14;
constexpr float ButtonHoverWidth = 108.0f;
constexpr float ButtonHoverHeight = 20.0f;
// 0x40a74f..0x40a774: Interface\Message_box.jpg (0xf0) de 100 × 25 con du = 0,83203125.
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr float ButtonWidth = 100.0f;
constexpr float ButtonHeight = 25.0f;
constexpr float ButtonDu = 0.83203125f;
// 0x40aa0f y 0x40aa06: el nombre, centrado en _ftol(x) + 0x32 y en _ftol(y) + 8; 0x40aa3a: la casilla en _ftol(x) −
// 0x14.
constexpr std::int32_t NameCenter = 0x32;
constexpr std::int32_t NameTop = 8;
constexpr std::int32_t CheckboxBack = 0x14;
// 0x463090: la casilla de 9 × 9 y la marca, Interface\win_check.tga (0x64a), de 5 × 5 en (x + 2, y + 2).
constexpr float CheckboxSize = 9.0f;
constexpr std::int32_t CheckboxFar = 8;
constexpr std::uint16_t CheckSlot = 0x64a;
constexpr std::int32_t CheckInset = 2;
constexpr float CheckSize = 5.0f;
constexpr float CheckUv = 0.625f;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
constexpr std::size_t TitleText = 938;
constexpr std::size_t CloseHelpText = 927;
// 0x40a998: la tabla 0x40aca4, el nombre de los comandos del 1 al 11.
constexpr std::array<std::size_t, UI::CommandCount - 1> CommandTexts{943,  1124, 944, 945, 946, 1352,
                                                                     1321, 1322, 947, 948, 949};
// 0x40a853: el "D" (0x6c6358) que sprintf pone en el texto 927, en un buffer de 100 bytes de la pila (EBP − 0x6c).
constexpr std::string_view CloseKey = "D";
constexpr std::size_t HelpBufferSize = 100;
// 0x40a6c9..0x40a6f2: el título en (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
// 0x40ac60..0x40ac7b: el aviso centrado en (x + 0x5f, y + 0x44).
constexpr std::int32_t NoticeCenter = 0x5f;
constexpr std::int32_t NoticeTop = 0x44;

// 0x6c8e5c y 0x6c8e64 del título (0x40a6b5) y de los nombres (0x40a89f).
constexpr Render::Text::TextColors TitleColors{.text = 0xffffffffu, .background = 0xff141414u};
constexpr Render::Text::TextColors NameColors{.text = 0xffffffffu, .background = 0x00141414u};

// El aviso de 0x40abdb..0x40ac2e: con el estado del elegido en 0, el texto del comando 5, 6, 10 o 3; con 0xff o 0xfe,
// los de esos estados.
constexpr std::uint8_t RefusedState = 0;
constexpr std::uint8_t NotMasterState = 0xff;
constexpr std::uint8_t NotMemberState = 0xfe;

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

// 0x40a4ad y 0x40a705: fild de la esquina, fadd de 95 y fsub de 50.
double ButtonsLeftOf(std::int32_t x)
{
    return static_cast<double>(x) + ButtonsRight - ButtonsBack;
}

// 0x406050 hasta 0x406097: los doce estados en 1 y, sin comando elegido o sin personaje elegido, nada más.
Core::Expected<void, std::string> PrepareTheCommand(WorldRuntime& runtime)
{
    UI::CommandsWindowState& window = runtime.panels.delegated.commandsWindow;
    window.states.fill(UI::CommandReady);
    if (window.chosen == -1 || runtime.selectedTargets[2] < 0)
        return {};
    return Core::Unexpected(
        std::format("0x406097: el comando {} sobre el personaje elegido es de la CL9 hito 7, fase 2", window.chosen));
}

// 0x463090(x, y, marcada): el marco de la casilla con cuatro rectángulos sin textura, sin mezcla, y la marca con la
// mezcla normal. Deja el color en blanco, la textura prendida y la mezcla normal.
void DrawTheCheckbox(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool checked)
{
    sprites.SetBlend(BlendMode::Opaque);
    sprites.SetColor(CheckboxFrame);
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    sprites.ColorQuad(left, top, CheckboxSize, 1.0f);
    sprites.ColorQuad(left, static_cast<float>(y + CheckboxFar), CheckboxSize, 1.0f);
    sprites.ColorQuad(left, top, 1.0f, CheckboxSize);
    sprites.ColorQuad(static_cast<float>(x + CheckboxFar), top, 1.0f, CheckboxSize);
    // 0x459ec0: glColor4f(1, 1, 1, 1) y glEnable(GL_TEXTURE_2D).
    sprites.SetColor(White);
    sprites.EnableTexture();
    sprites.SetBlend(BlendMode::Alpha);
    if (checked)
        WindowDrawing::Quad(sprites, CheckSlot, static_cast<float>(x + CheckInset), static_cast<float>(y + CheckInset),
                            CheckSize, CheckSize, CheckUv, CheckUv);
}

// 0x40a840..0x40a875: sprintf(buffer, texto 927, "D"). MuRet no usa el texto como formato: sin '%' y con menos de 100
// bytes, sprintf lo copia tal cual; con un '%' o más largo, corta (Q-C650).
Core::Expected<std::string_view, std::string> CloseHelpOf(const GameHudContext& context)
{
    auto format = TextOf(context, CloseHelpText, "0x40a85b");
    if (!format.HasValue())
        return format;
    const std::string_view text = format.Value();
    if (text.find('%') != std::string_view::npos)
        return Core::Unexpected(std::format("0x40a875: el texto {} es el formato de sprintf con \"{}\" y tiene un '%'",
                                            CloseHelpText, CloseKey));
    if (text.size() >= HelpBufferSize)
        return Core::Unexpected(std::format("0x40a875: el texto {} no entra en los {} bytes de la pila de sprintf",
                                            CloseHelpText, HelpBufferSize));
    return text;
}

// 0x40abdb..0x40ac2e: el texto del aviso del comando elegido, o 0.
std::size_t NoticeOf(const UI::CommandsWindowState& window)
{
    const auto chosen = static_cast<std::size_t>(window.chosen);
    const std::uint8_t state = window.states[chosen];
    if (state == RefusedState)
    {
        switch (window.chosen)
        {
        case 5:
            return 255;
        case 6:
            return 1313;
        case 10:
            return 937;
        case 3:
            return 257;
        default:
            return 0;
        }
    }
    if (state == NotMasterState)
        return 507;
    if (state == NotMemberState)
        return 1320;
    return 0;
}
} // namespace

Core::Expected<void, std::string> StepTheCommandsWindow(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    UI::DelegatedPanels& delegated = runtime.panels.delegated;
    if (delegated.commands == 0)
        return {};
    GameMovementState& movement = runtime.movement;
    const std::int32_t x = runtime.hud.commandsX;
    const std::int32_t y = runtime.hud.commandsY;
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    if (mouseX >= x && mouseX < x + WindowWidth && mouseY >= y && mouseY < y + WindowHeight)
        runtime.ui.mouseOnUi = 1;
    if (auto prepared = PrepareTheCommand(runtime); !prepared.HasValue())
        return prepared;
    // 0x40a49e..0x40a51d: el 0 no se elige; el clic se gasta en el primero que lo toma.
    const std::int32_t left = Truncate(ButtonsLeftOf(x));
    std::int32_t top = Truncate(static_cast<double>(y) + ButtonsTop);
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(UI::CommandCount); ++command)
    {
        if (command != 0 && mouseX > left && mouseX < left + ButtonHitWidth && mouseY > top &&
            mouseY < top + ButtonHitHeight && movement.pendingClick)
        {
            movement.pendingClick = false;
            movement.leftHeld = false;
            delegated.commandsWindow.chosen = command;
        }
        top += ButtonStep;
    }
    if (mouseX < x + CloseLeft || mouseX >= x + CloseLeft + CloseSize || mouseY < y + CloseTop ||
        mouseY >= y + CloseTop + CloseSize || !movement.pendingClick)
        return {};
    movement.pendingClick = false;
    runtime.lastSoundId = FirstCloseSound;
    runtime.lastSoundId = SecondCloseSound;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
    UI::ResetTheCommands(delegated);
    return {};
}

Core::Expected<void, std::string> DrawTheCommandsWindow(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const UI::DelegatedPanels& delegated = runtime.panels.delegated;
    if (delegated.commands == 0)
        return {};
    const UI::CommandsWindowState& window = delegated.commandsWindow;
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    hud.commandsX = WindowLeft;
    hud.commandsY = WindowTop;
    const std::int32_t x = hud.commandsX;
    const std::int32_t y = hud.commandsY;
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    auto title = TextOf(context, TitleText, "0x40a6ed");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    WindowDrawing::SelectFont(context, FontKind::Bold);
    hud.textColors = TitleColors;
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());

    // 0x40a6f9..0x40a78c: los once botones, la y sumada en float también por el 0.
    const auto buttonsLeft = static_cast<float>(ButtonsLeftOf(x));
    auto top = static_cast<float>(static_cast<double>(y) + ButtonsTop);
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(UI::CommandCount); ++command)
    {
        if (command != 0)
        {
            sprites.SetColor(window.chosen == command ? Gray : White);
            WindowDrawing::Quad(sprites, ButtonSlot, buttonsLeft, top, ButtonWidth, ButtonHeight, ButtonDu, 1.0f);
        }
        top = static_cast<float>(static_cast<double>(top) + static_cast<double>(ButtonStepF));
    }

    if (WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto help = CloseHelpOf(context);
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    }

    // 0x40a89f..0x40aa56: los nombres, con la fuente y el color del elegido o del que tiene el mouse encima, y la
    // casilla.
    hud.textColors = NameColors;
    top = static_cast<float>(static_cast<double>(y) + ButtonsTop);
    const UI::Point mouse = context.mouse;
    const bool click = runtime.movement.pendingClick;
    for (std::int32_t command = 0; command < static_cast<std::int32_t>(UI::CommandCount); ++command)
    {
        if (command != 0)
        {
            const auto mouseX = static_cast<double>(mouse.x);
            const auto mouseY = static_cast<double>(mouse.y);
            const bool hovered = mouseX > static_cast<double>(buttonsLeft) &&
                                 mouseX < static_cast<double>(buttonsLeft) + static_cast<double>(ButtonHoverWidth) &&
                                 mouseY > static_cast<double>(top) &&
                                 mouseY < static_cast<double>(top) + static_cast<double>(ButtonHoverHeight);
            if (window.chosen != command && !hovered)
            {
                WindowDrawing::SelectFont(context, FontKind::Normal);
                sprites.SetColor(White);
            }
            else
            {
                WindowDrawing::SelectFont(context, FontKind::Bold);
                sprites.SetColor(click ? Gray : Highlight);
            }
            auto name = TextOf(context, CommandTexts[static_cast<std::size_t>(command - 1)], "0x40a9f6");
            if (!name.HasValue())
                return Core::Unexpected(std::move(name).Error());
            const std::int32_t left = Truncate(static_cast<double>(buttonsLeft));
            const std::int32_t row = Truncate(static_cast<double>(top));
            WindowDrawing::DrawCenteredText(context, left + NameCenter, row + NameTop, name.Value());
            DrawTheCheckbox(sprites, left - CheckboxBack, row + NameTop, window.chosen == command);
        }
        top = static_cast<float>(static_cast<double>(top) + static_cast<double>(ButtonStepF));
    }

    sprites.SetColor(White);
    WindowDrawing::SelectFont(context, FontKind::BigBold);
    // 0x40aa80..0x40abd8: con un personaje elegido, su nombre en un cartel (Interface\CursorID_Wnd.jpg, 0x65b) junto
    // al mouse, en celeste con el comando listo o gris si no.
    if (runtime.selectedTargets[2] >= 0)
        return Core::Unexpected(
            std::string("0x40aa8d: el nombre del personaje elegido junto al mouse (0x65b) es de la CL9.4"));
    std::size_t notice = 0;
    if (window.chosen != -1)
    {
        if (window.chosen < 0 || window.chosen >= static_cast<std::int32_t>(UI::CommandCount))
            return Core::Unexpected(
                std::format("0x40abe5: el comando {} lee fuera de los doce estados", window.chosen));
        notice = NoticeOf(window);
    }
    sprites.SetColor(Black);
    if (notice != 0)
    {
        auto text = TextOf(context, notice, "0x40ac6c");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        WindowDrawing::SelectFont(context, FontKind::Bold);
        hud.textColors.background = 0;
        WindowDrawing::DrawCenteredText(context, x + NoticeCenter, y + NoticeTop, text.Value());
    }
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}
} // namespace Client::Scenes
