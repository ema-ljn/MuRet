#include "Scenes/CastleWindowDrawing.h"

#include "Core/Expected.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Scenes/CastleWindowSupport.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/WorldState.h"
#include "Scenes/ZenText.h"
#include "UI/CastleSiegeState.h"
#include "UI/OldWindowDrawParts.h"
#include "Crt/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes::CastleDraw
{
namespace
{
constexpr std::int32_t Left = 450;
constexpr std::uint32_t SufficientMoney = 0xff64ffffu;
constexpr std::uint32_t InsufficientMoney = 0xff3264ffu;

Core::Expected<void, std::string> MoneyText(GameHudContext& context, std::int32_t x, std::int32_t y, std::uint32_t text,
                                            std::uint32_t value)
{
    return Formatted(context, x, y, text, std::string_view(FormatZen(value)));
}

Core::Expected<void, std::string> Admin(GameHudContext& context, UI::HuntZoneWindow& window)
{
    if (auto line = Text(context, Left, 55, 0x7732158); !line.HasValue())
        return line;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    constexpr std::array<std::uint32_t, 3> Lines{0x77340fc, 0x7734228, 0x7734354};
    for (std::size_t i = 0; i < Lines.size(); ++i)
        if (auto line = Text(context, Left, 75 + static_cast<std::int32_t>(i) * 10, Lines[i]); !line.HasValue())
            return line;
    UI::OldDraw::DrawTheCheckbox(context.sprites, 485, 115, window.allowed != 0);
    auto label = CastleUi::Text(context.texts, 0x7732284);
    if (!label.HasValue())
        return Core::Unexpected(std::move(label).Error());
    WindowDrawing::DrawText(context, 505, 115, label.Value());
    auto fee = CastleUi::Text(context.texts, 0x7732734);
    if (!fee.HasValue())
        return Core::Unexpected(std::move(fee).Error());
    WindowDrawing::DrawText(context, 485, 133, UI::Sprintf(fee.Value(), std::string_view(FormatZen(window.fee))));
    context.sprites.SetColor({0, 0, 0, 0.3f});
    context.sprites.ColorQuad(470, 185, 90, 30);
    context.sprites.SetBlend(Render::BlendMode::Alpha); // 0x64a2bb, después del fondo sin textura.
    context.sprites.SetColor({1, 1, 1, 1});
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Bold);
    if (auto line = Text(context, Left, 163, 0x77323b0); !line.HasValue())
        return line;
    WindowDrawing::SelectFont(context, Render::Text::FontKind::Normal);
    // 0x5542f0(..., 0, 4, 0), alineado a la derecha en x=540 y x=550.
    const auto edited = FormatZen(static_cast<std::uint32_t>(window.editedFee));
    WindowDrawing::DrawText(
        context,
        Render::Text::EndingAt(540, context.text.Measure(context.hud.dcFont, edited), context.sprites.Screen()), 195,
        edited);
    auto zen = CastleUi::Text(context.texts, 0x76cd85c);
    if (!zen.HasValue())
        return Core::Unexpected(std::move(zen).Error());
    WindowDrawing::DrawText(
        context,
        Render::Text::EndingAt(550, context.text.Measure(context.hud.dcFont, zen.Value()), context.sprites.Screen()),
        195, zen.Value());
    if (auto plus = Button(context, window.plus, 570, 183, 0, window.plus.base.state); !plus.HasValue())
        return plus;
    if (auto minus = Button(context, window.minus, 570, 203, 0, window.minus.base.state); !minus.HasValue())
        return minus;
    context.sprites.SetBlend(Render::BlendMode::Alpha); // 0x64a3b4, después del botón menos.
    UI::OldDraw::Quad(context.sprites, 9, 570, 185, 16, 10, 0, 0.99f, 1, -0.96875f);
    UI::OldDraw::Quad(context.sprites, 9, 570, 206, 16, 10, 0, 0, 1, 1);
    if (auto commit = Button(context, window.commit, 520, 228, 0x7733b20, window.commit.base.state); !commit.HasValue())
        return commit;
    context.sprites.SetBlend(Render::BlendMode::Alpha); // 0x64a43a, después de aplicar el precio.
    context.hud.textColors = {.text = 0xffffffffu, .background = 0};
    if (auto line = MoneyText(context, Left, 253, 0x77324dc, static_cast<std::uint32_t>(window.max)); !line.HasValue())
        return line;
    if (auto line = Text(context, Left, 266, 0x7732608); !line.HasValue())
        return line;
    if (auto line = MoneyText(context, Left, 279, 0x77339f4, static_cast<std::uint32_t>(window.step)); !line.HasValue())
        return line;
    return Button(context, window.enter, 520, 329, 0x7731ca8, window.enter.base.state);
}

Core::Expected<void, std::string> Visitor(GameHudContext& context, UI::HuntZoneWindow& window)
{
    if (window.allowed == 0)
    {
        constexpr std::array<std::uint32_t, 4> Lines{0x7734480, 0x77346d8, 0x7734804, 0x7734930};
        for (std::size_t i = 0; i < Lines.size(); ++i)
            if (auto line = Text(context, Left, 55 + static_cast<std::int32_t>(i) * 10, Lines[i]); !line.HasValue())
                return line;
        auto button = Button(context, window.enter, 520, 95, 0x7731ca8, CastleUi::Disabled);
        window.enter.base.state = 0; // El dibujo sólo apaga el botón mientras lo pinta.
        return button;
    }
    const auto amount = FormatZen(window.fee);
    {
        const Render::Models::LogicalAccess access(context.ownRecordDepth, context.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        context.hud.textColors.text =
            context.world.OwnCharacter().zen < window.fee ? InsufficientMoney : SufficientMoney;
    }
    if (auto line = Formatted(context, Left, 55, 0x7734a5c, std::string_view(amount)); !line.HasValue())
        return line;
    context.hud.textColors = {.text = 0xffffffffu, .background = 0};
    if (auto line = Text(context, Left, 65, 0x7734b88); !line.HasValue())
        return line;
    if (auto line = Text(context, Left, 75, 0x7734cb4); !line.HasValue())
        return line;
    auto button = Button(context, window.enter, 520, 95, 0x7731ca8, window.enter.base.state);
    window.enter.base.state = 0;
    return button;
}
} // namespace

Core::Expected<void, std::string> Hunt(GameHudContext& context, UI::HuntZoneWindow& window)
{
    if (window.mode == 0)
        return {};
    window.x = Left;
    window.y = 0;
    window.right = 770;
    window.bottom = 300;
    if (auto back = Back(context, Left, 0x773202c); !back.HasValue())
        return back;
    Core::Expected<void, std::string> body;
    if (window.mode == 3)
        body = Admin(context, window);
    else if (window.mode == 2)
    {
        if (auto line = Text(context, Left, 55, 0x7734b88); !line.HasValue())
            return line;
        body = Button(context, window.enter, 520, 105, 0x7731ca8, window.enter.base.state);
    }
    else if (window.mode == 1)
        body = Visitor(context, window);
    if (!body.HasValue())
        return body;
    return Close(context, Left);
}
} // namespace Client::Scenes::CastleDraw
