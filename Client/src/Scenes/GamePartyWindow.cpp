#include "Scenes/GamePartyWindow.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameUi.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

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
using Render::Text::FontKind;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5e3cb5..0x5e3ce8: el mouse sobre la ventana, de 0xbe × 0x1b1 desde la esquina.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x5e40d4..0x5e40f2: el botón de cerrar, de 0x18 × 0x18 en (0x19, 0x18b) de la esquina.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x5e410d..0x5e4113.
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300): el título, los siete renglones de la party vacía
// (0x76cb1b0..0x76cb8b8) y la ayuda del botón de cerrar (0x76cd4d8).
constexpr std::size_t TitleText = 190;
constexpr std::size_t FirstEmptyText = 191;
constexpr std::size_t CloseHelpText = 221;
// 0x5f0d3f..0x5f0dc3: los renglones en x + 0x14 y en y + estas.
constexpr std::array<std::int32_t, 7> EmptyLineTops{0x32, 0x41, 0x50, 0x64, 0x73, 0x82, 0x91};
constexpr std::int32_t EmptyLineLeft = 0x14;
// 0x5f0cf3..0x5f0cfb: el título en (x + 0x23, y + 0xc).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;

// 0x6c8e5c y 0x6c8e64 del título (0x5f0cbe), de los renglones (0x5f0d14) y de la party cerrada (0x5f12eb).
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
constexpr Render::Text::TextColors LineColors{.text = 0xffe6e6e6u, .background = 0};
constexpr Render::Text::TextColors ClosedColors{.text = 0xffffffffu, .background = 0};

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}
} // namespace

Core::Expected<void, std::string> StepThePartyWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::PartyPanel))
        return {};
    WorldRuntime& runtime = context.world.Runtime();
    const std::int32_t x = runtime.hud.partyX;
    const std::int32_t y = runtime.hud.partyY;
    const std::int32_t mouseX = context.mouseX;
    const std::int32_t mouseY = context.mouseY;
    if (mouseX >= x && mouseX < x + WindowWidth && mouseY >= y && mouseY < y + WindowHeight)
        runtime.ui.mouseOnUi = 1;
    // 0x5e3cf0..0x5e3cfd: con miembros, cada uno con su nombre (strcmp con el del héroe) y su botón de echar.
    if (runtime.hud.partyCount > 0)
        return Core::Unexpected(
            std::string("0x5e3d03: los miembros de la party y el botón de echar (C1 04 43) son de la CL9.4"));
    if (mouseX < x + CloseLeft || mouseX >= x + CloseLeft + CloseSize || mouseY < y + CloseTop ||
        mouseY >= y + CloseTop + CloseSize || !runtime.movement.pendingClick)
        return {};
    runtime.movement.pendingClick = false;
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
    auto closed = panels.Close(UI::PartyPanel);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> DrawThePartyWindow(GameHudContext& context, UI::PanelRegistry& panels, std::int32_t x,
                                                     std::int32_t y)
{
    HudState& hud = context.hud;
    if (!panels.IsOpen(UI::PartyPanel))
    {
        // 0x5f12d0..0x5f1304: el ancho del mundo (la x de la lista, menos 50) y los colores, antes de mirar la cuenta.
        static_cast<void>(panels.WorldWidth());
        hud.textColors = ClosedColors;
        if (hud.partyCount != 0)
            return Core::Unexpected(
                std::string("0x5f130a: la lista de la party a la derecha del mundo es de la CL9.4"));
        return {};
    }
    Render::SpriteBatch& sprites = context.sprites;
    hud.partyX = x;
    hud.partyY = y;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    auto title = TextOf(context, TitleText, "0x5f0cf6");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    hud.textColors = TitleColors;
    WindowDrawing::SelectFont(context, FontKind::Bold);
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    WindowDrawing::SelectFont(context, FontKind::Normal);
    hud.textColors = LineColors;
    if (hud.partyCount == 0)
    {
        for (std::size_t line = 0; line < EmptyLineTops.size(); ++line)
        {
            auto text = TextOf(context, FirstEmptyText + line, "0x5f0d45");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            WindowDrawing::DrawText(context, x + EmptyLineLeft, y + EmptyLineTops[line], text.Value());
        }
    }
    // 0x5f0dde..0x5f0dea: con miembros, sus nombres, su vida y su botón de echar; con la cuenta negativa, nada.
    if (hud.partyCount > 0)
        return Core::Unexpected(std::string("0x5f0df0: los miembros de la ventana de la party son de la CL9.4"));
    // 0x5f11c2: la esquina de 0x78a7068 y 0x78a706c, la que acaba de poner.
    if (!WindowDrawing::DrawTheCloseButton(context, hud.partyX, hud.partyY))
        return {};
    auto help = TextOf(context, CloseHelpText, "0x5f12aa");
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    WindowDrawing::DrawTheCloseHelp(context, hud.partyX, hud.partyY, help.Value());
    return {};
}
} // namespace Client::Scenes
