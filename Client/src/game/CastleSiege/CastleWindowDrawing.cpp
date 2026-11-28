#include "CastleSiege/CastleWindowSupport.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/ScreenScale.h"
#include "Render/Text/FontSet.h"
#include "CastleSiege/CastleWindowDrawing.h"
#include "Hud/GameHud.h"
#include "Hud/GameWindowDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/Widget.h"
#include "Math/Ftol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::CastleSiege
{
void CastleDraw::Center(Hud::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                        std::int32_t width)
{
    // FLD rx, FMUL width (float) and _ftol, unlike the integer DIV of the old 0x554180 windows.
    const auto box = Math::OriginalFtolLow(static_cast<double>(Render::ScaleX(1.0f, context.sprites.Screen())) *
                                           static_cast<double>(static_cast<float>(width)))
                         .value_or(0);
    Hud::WindowDrawing::DrawInBox(context, x, y, text, box);
}

Core::Expected<void, std::string> CastleDraw::Text(Hud::GameHudContext& context, std::int32_t x, std::int32_t y,
                                                   std::uint32_t address, std::int32_t width)
{
    auto text = CastleUi::Text(context.texts, address);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    CastleSiege::CastleDraw::Center(context, x, y, text.Value(), width);
    return {};
}

Core::Expected<void, std::string> CastleDraw::Button(Hud::GameHudContext& context, Ui::OldButton& button,
                                                     std::int32_t x, std::int32_t y, std::uint32_t label,
                                                     std::int32_t state)
{
    if (label != 0)
    {
        auto text = CastleUi::Text(context.texts, label);
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        button.label = std::move(text).Value();
    }
    button.base.x = x;
    button.base.y = y;
    button.base.state = state;
    Ui::DrawTheOldButton({.sprites = context.sprites,
                          .text = context.text,
                          .dcFont = context.hud.dcFont,
                          .textColors = context.hud.textColors},
                         button, {.mouseX = context.mouse.x, .mouseY = context.mouse.y});
    return {};
}

Core::Expected<void, std::string> CastleDraw::Back(Hud::GameHudContext& context, std::int32_t x, std::uint32_t title)
{
    context.sprites.SetColor({1, 1, 1, 1});
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    Hud::WindowDrawing::DrawTheWindowBack(context.sprites, x, 0);
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    context.hud.textColors = {.text = 0xffffffffu, .background = 0};
    Hud::WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    return CastleSiege::CastleDraw::Text(context, x, 15, title);
}

Core::Expected<void, std::string> CastleDraw::Close(Hud::GameHudContext& context, std::int32_t x)
{
    if (Hud::WindowDrawing::DrawTheCloseButton(context, x, 0))
    {
        auto help = CastleUi::Text(context.texts, 0x7706814);
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        Hud::WindowDrawing::DrawTheCloseHelp(context, x, 0, help.Value());
    }
    Hud::WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    context.sprites.SetColor({1, 1, 1, 1});
    return {};
}

Core::Expected<void, std::string> DrawTheCastleWindows(Hud::GameHudContext& context)
{
    auto& runtime = context.world.Runtime();
    const auto& delegated = runtime.panels.delegated;
    if (delegated.castleSiege == 0 && delegated.castleSiegeList == 0 && delegated.secondCastleSiege == 0)
        return {};
    // The original's draws write bounds, enabled and scroll; MuRet's own intermediate frame uses a copy.
    std::optional<CastleSiege::CastleSiegeState> projected;
    if (context.projecting)
        projected.emplace(runtime.castleSiege);
    auto& state = projected ? *projected : runtime.castleSiege;
    if (delegated.castleSiege != 0)
    {
        if (auto drawn = CastleDraw::Registration(context, state.registration); !drawn.HasValue())
            return drawn;
    }
    if (delegated.castleSiegeList != 0)
    {
        if (auto drawn = CastleDraw::Senior(context, state.senior, state.data, delegated.castleSiegeListMode);
            !drawn.HasValue())
            return drawn;
    }
    if (delegated.secondCastleSiege != 0)
        return CastleDraw::Hunt(context, state.huntZone);
    return {};
}
} // namespace Client::CastleSiege
