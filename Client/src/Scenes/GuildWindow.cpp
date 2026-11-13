#include "Scenes/GuildWindow.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Protocol/Guild.h"
#include "Render/OriginalQuad.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/GuildState.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLogWindow.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/OldWindowBase.h"
#include "UI/PanelRegistry.h"
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
using Render::Text::FontKind;
using WindowDrawing::DrawText;
using WindowDrawing::SelectFont;

constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};
// 0x65202d y 0x6520fe: la pestaña que no está elegida, en 0,6.
constexpr Render::Color DimTab{.red = 0.6f, .green = 0.6f, .blue = 0.6f, .alpha = 1.0f};
// 0x651f22: glColor4ub(0x92, 0x90, 0x8d, 0xff), las rayas bajo las pestañas que no están elegidas.
constexpr Render::Color TabLine{
    .red = 146.0f / 255.0f, .green = 144.0f / 255.0f, .blue = 141.0f / 255.0f, .alpha = 1.0f};

// 0x651840: el rectángulo del mouse de la UI, 0xbe (+0x58) × 0x1b1, con el modo 1.
constexpr std::int32_t MouseHeight = 0x1b1;
constexpr std::int32_t TopLeftRect = 1;
// 0x651895..0x6518e3: el botón de cerrar, lo que deja en 0x6c8dcc y su sonido (CL10).
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
constexpr std::int32_t CloseMouseUpdateTimeMax = 6;
constexpr std::int32_t ClickSound = 0x19;
// +0x368 del héroe sin gremio (0x511290).
constexpr std::uint8_t NoGuildStatus = 0xff;
// Las pestañas: el paso las mira de a 0x34 desde x + 0xf (0x651913..0x65199d) y el dibujo las pone de a 0x36.
constexpr std::int32_t TabCount = 3;
constexpr std::int32_t MembersTab = 1;
constexpr std::int32_t UnionTab = 2;
constexpr std::array<std::int32_t, TabCount> TabMouseLefts{0xf, 0x43, 0x77};
constexpr std::int32_t TabMouseTop = 0x4b;
constexpr std::int32_t TabMouseWidth = 0x34;
constexpr std::int32_t TabMouseHeight = 0x10;
constexpr std::int32_t TabLeft = 0xf;
constexpr std::int32_t TabStep = 0x36;
constexpr std::int32_t TabTop = 0x4b;
constexpr std::int32_t TabLineTop = 0x5b;
constexpr float TabLineWidth = 54.0f;
constexpr float TabLineHeight = 1.0f;
// 0x652092: Interface\..., el 0x646 de 52 × (16 + 2 la elegida), du 0,75 y dv 0,9375, 2 más arriba la elegida.
constexpr std::uint16_t TabSlot = 0x646;
constexpr float TabWidth = 52.0f;
constexpr float TabHeight = 16.0f;
constexpr float TabDu = 0.75f;
constexpr float TabDv = 0.9375f;
constexpr std::int32_t ChosenTabRise = 2;
// 0x6520c8: el texto de la pestaña en y + 4, uno más arriba la elegida, centrado en _ftol(rx · 52) (0x6ba060).
constexpr std::int32_t TabTextBelow = 4;
constexpr double TabTextBox = 52.0;
constexpr std::array<std::size_t, TabCount> TabTexts{946, 1330, 1352};
// 0x651cd2..0x651d5d y 0x651d73..0x651dd6: el título y el gremio con el puntaje, en negrita sobre el fondo 0.
constexpr std::size_t TitleText = 180;
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
constexpr std::int32_t ScoreTop = 0x35;
constexpr Render::Text::TextColors TitleColors{.text = 0xffdcdcdcu, .background = 0};
constexpr std::uint32_t ScoreColor = 0xff64ffc8u;
// 0x651dfa..0x651e4f: sin gremio, los textos 185, 186 y 187 en la normal, de a 0xf desde (x + 0x14, y + 0x32).
constexpr std::array<std::size_t, 3> NoGuildTexts{185, 186, 187};
constexpr std::int32_t NoGuildLeft = 0x14;
constexpr std::int32_t NoGuildTop = 0x32;
constexpr std::int32_t NoGuildStep = 0xf;
constexpr std::uint32_t NoGuildColor = 0xffe6e6e6u;
// 0x651ee6: la ayuda del botón de cerrar.
constexpr std::size_t CloseHelpText = 220;

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x5542f0(x, y, texto, _ftol(rx · 52), 2, 0): el texto centrado en la caja (0x467060 en el modo 2), sin medir.
void DrawInTheTabBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    const auto rx = static_cast<double>(Render::ScaleX(1.0f, context.sprites.Screen()));
    const std::int32_t box = Truncate(rx * TabTextBox);
    if (text.empty() && box == 0)
        return;
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.hud.dcFont, text, box);
    context.text.DrawColumns(x, y, text,
                             Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                             -placed.textColumn, placed.width);
}

// 0x6519ad..0x651ba4: la primera vez que se elige la pestaña de la alianza, con alianza (+4 de la entrada del gremio
// del héroe, sin mirar el −1: Q-C1281), C1 03 E9 en claro por el socket (0x6c82a0) y +0x84 en 1.
Core::Expected<void, std::string> AskForTheUnionList(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    if (guild.window.unionRequested != 0)
        return {};
    const auto index = static_cast<std::int16_t>(context.world.Actors()[runtime.hero].word1f2);
    const auto unionByte = UnionByteAt(guild, index);
    if (!unionByte)
        return Core::Unexpected(std::format("0x6519cc: el gremio {} del héroe cae fuera de la tabla", index));
    if (*unionByte == '\0')
        return {};
    if (!context.send)
        return Core::Unexpected(std::string("0x651ac8: el pedido de la alianza (E9) sin el cable"));
    context.send(Protocol::Guild::WriteUnionListRequest());
    guild.window.unionRequested = 1;
    return {};
}

// 0x651bbf..0x651c05: el paso de la pestaña, que no está portado.
Core::Expected<void, std::string> StepTheTab(std::int32_t tab)
{
    switch (tab)
    {
    case 0:
        return Core::Unexpected(std::string("0x64fc00: el paso de la pestaña 0 del gremio no está portado (P-C287)"));
    case MembersTab:
        return Core::Unexpected(
            std::string("0x650350: el paso de la pestaña de los miembros no está portado (P-C287)"));
    case UnionTab:
        return Core::Unexpected(
            std::string("0x650c10: el paso de la pestaña de la alianza no está portado (P-C287, P-C288)"));
    default:
        return {};
    }
}

// 0x652291..0x6522c0: el dibujo de la pestaña, que no está portado.
Core::Expected<void, std::string> DrawTheTab(std::int32_t tab)
{
    switch (tab)
    {
    case 0:
        return Core::Unexpected(std::string("0x64fdf0: el dibujo de la pestaña 0 del gremio no está portado (P-C287)"));
    case MembersTab:
        return Core::Unexpected(
            std::string("0x6507d0: el dibujo de la pestaña de los miembros no está portado (P-C287)"));
    case UnionTab:
        return Core::Unexpected(
            std::string("0x651400: el dibujo de la pestaña de la alianza no está portado (P-C287, P-C288)"));
    default:
        return {};
    }
}

// 0x651f1c..0x652275: las rayas de 0x621aa0 bajo las pestañas que no están elegidas, 0x459ec0, la negrita en blanco
// sobre 0 y cada pestaña: blanca la elegida (2 más alta y más arriba) y en 0,6 las otras, con su texto.
Core::Expected<void, std::string> DrawTheTabs(GameHudContext& context, std::int32_t tab, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(TabLine);
    for (std::int32_t index = 0; index < TabCount; ++index)
    {
        if (index == tab)
            continue;
        sprites.ColorQuad(static_cast<float>(x + TabLeft + index * TabStep), static_cast<float>(y + TabLineTop),
                          TabLineWidth, TabLineHeight);
    }
    // 0x459ec0: glColor4f(1, 1, 1, 1) y la textura.
    sprites.SetColor(White);
    sprites.EnableTexture();
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
    for (std::int32_t index = 0; index < TabCount; ++index)
    {
        const bool chosen = index == tab;
        const std::int32_t left = x + TabLeft + index * TabStep;
        const std::int32_t top = y + TabTop;
        const std::int32_t rise = chosen ? ChosenTabRise : 0;
        sprites.SetColor(chosen ? White : DimTab);
        sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{TabSlot},
                                          .x = static_cast<float>(left),
                                          .y = static_cast<float>(top - rise),
                                          .width = TabWidth,
                                          .height = F32(static_cast<double>(rise) + static_cast<double>(TabHeight)),
                                          .u = 0.0f,
                                          .v = 0.0f,
                                          .du = TabDu,
                                          .dv = TabDv,
                                          .scaleSize = true,
                                          .scalePosition = true,
                                          .alpha = 0.0f});
        auto text = TextOf(context.texts, TabTexts[static_cast<std::size_t>(index)], "0x6520cd");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        DrawInTheTabBox(context, left, top - (chosen ? 1 : 0) + TabTextBelow, text.Value());
    }
    sprites.SetColor(White);
    return {};
}
} // namespace

bool PassesTheGuildWindowGate(const UI::OldWindowBase& base, UI::GameUiState& ui, std::int32_t mouseX,
                              std::int32_t mouseY)
{
    if (!UI::MouseInRect(mouseX, mouseY, base.x, base.y, base.width, base.height, base.rectMode))
        return true;
    if (ui.hovered == 0)
    {
        if (ui.capture != 0 && ui.capture != base.id)
            return false;
        ui.hovered = base.id;
    }
    return base.state != UI::ShownWindow || ui.hovered == base.id || ui.capture == base.id;
}

Core::Expected<void, std::string> StepTheGuildWindow(const GuildWindowStepContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildWindowState& window = runtime.guild.window;
    const UI::OldWindowBase& base = window.base;
    if (!PassesTheGuildWindowGate(base, runtime.ui, context.mouseX, context.mouseY))
        return {};
    if (UI::MouseInRect(context.mouseX, context.mouseY, base.x, base.y, base.width, MouseHeight, TopLeftRect))
        runtime.ui.mouseOnUi = 1;
    if (runtime.movement.pendingClick && UI::MouseInRect(context.mouseX, context.mouseY, base.x + CloseLeft,
                                                         base.y + CloseTop, CloseSize, CloseSize, TopLeftRect))
    {
        runtime.movement.pendingClick = false;
        runtime.movement.mouseUpdateTime = 0;
        runtime.movement.mouseUpdateTimeMax = CloseMouseUpdateTimeMax;
        runtime.lastSoundId = ClickSound; // 0x41ed10 (CL10)
        // 0x6518e3: la vtable + 0x2c, 0x6522f0. El paso sigue con la ventana cerrada.
        if (auto closed = CloseTheGuildWindow(context.world, context.send); !closed.HasValue())
            return closed;
    }
    if (context.world.Actors()[runtime.hero].guildStatus == NoGuildStatus)
        return {};
    if (runtime.movement.leftHeld)
    {
        const auto inTab = [&](std::int32_t tab)
        {
            return window.tab != tab &&
                   UI::MouseInRect(context.mouseX, context.mouseY,
                                   base.x + TabMouseLefts[static_cast<std::size_t>(tab)], base.y + TabMouseTop,
                                   TabMouseWidth, TabMouseHeight, TopLeftRect);
        };
        std::optional<std::int32_t> chosen;
        if (inTab(0))
            chosen = 0;
        else if (inTab(MembersTab))
            chosen = MembersTab;
        else if (inTab(UnionTab))
        {
            if (auto asked = AskForTheUnionList(context); !asked.HasValue())
                return asked;
            chosen = UnionTab;
        }
        if (chosen)
        {
            if (auto cancelled = CancelTheGuildWindowDialog(context.world, context.send); !cancelled.HasValue())
                return cancelled;
            window.tab = *chosen;
        }
    }
    return StepTheTab(window.tab);
}

void LendTheGuildWindowStepTo(UI::PanelContext& panels, const GuildWindowStepContext& context)
{
    panels.guildStep = [context]() { return StepTheGuildWindow(context); };
}

Core::Expected<void, std::string> DrawTheGuildWindow(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const GuildState& guild = runtime.guild;
    const GuildWindowState& window = guild.window;
    const std::int32_t x = window.base.x;
    const std::int32_t y = window.base.y;
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    sprites.SetBlend(BlendMode::Alpha);
    auto title = TextOf(context.texts, TitleText, "0x651cd5");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    context.hud.textColors = TitleColors;
    SelectFont(context, FontKind::Bold);
    WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, title.Value());
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const bool inAGuild = hero.guildStatus != NoGuildStatus;
    if (inAGuild)
    {
        const auto index = static_cast<std::int16_t>(hero.word1f2);
        const auto name = GuildNameAt(guild, index);
        if (!name)
            return Core::Unexpected(std::format("0x651da0: el gremio {} del héroe cae fuera de la tabla", index));
        context.hud.textColors.text = ScoreColor;
        WindowDrawing::DrawTitle(context, x + TitleLeft, y + ScoreTop,
                                 std::format("{} ( Score:{} )", *name, guild.score));
    }
    else
    {
        SelectFont(context, FontKind::Normal);
        context.hud.textColors.text = NoGuildColor;
        std::int32_t top = y + NoGuildTop;
        for (const std::size_t index : NoGuildTexts)
        {
            auto text = TextOf(context.texts, index, "0x651e25");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            DrawText(context, x + NoGuildLeft, top, text.Value());
            top += NoGuildStep;
        }
    }
    if (WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto help = TextOf(context.texts, CloseHelpText, "0x651ee6");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    }
    if (!inAGuild)
        return {};
    if (auto tabs = DrawTheTabs(context, window.tab, x, y); !tabs.HasValue())
        return tabs;
    return DrawTheTab(window.tab);
}
} // namespace Client::Scenes
