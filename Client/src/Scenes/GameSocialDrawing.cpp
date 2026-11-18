#include "Scenes/GameSocialDrawing.h"

#include "Core/Expected.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "UI/Widget.h"

#include <algorithm>
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
using Render::BlendMode;
using Render::Text::FontKind;
using WindowDrawing::DrawCenteredText;
using WindowDrawing::Quad;
using WindowDrawing::SelectFont;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x5a6cf9..0x5a6dbd: la franja (glColor4f(0, 0, 0, 0,5)) y su marco (glColor4f(0,65; 0,53; 0,34; 1)), desde
// centro ∓ 150 (0x6b87ac) en y 20, de 300 × 40.
constexpr Render::Color ScoreBand{0.0f, 0.0f, 0.0f, 0.5f};
constexpr Render::Color ScoreFrame{0.65f, 0.53f, 0.34f, 1.0f};
constexpr float ScoreHalfWidth = 150.0f;
constexpr float ScoreTop = 20.0f;
constexpr float ScoreBottom = 60.0f;
constexpr float ScoreWidth = 300.0f;
constexpr float ScoreHeight = 40.0f;
constexpr float ScoreLine = 1.0f;
// 0x5a6e25..0x5a6fa8: los colores del texto (0x6c8e5c) y las x desde la mitad del ancho del mundo (· 0,5,
// 0x6b882c): 79 (0x6ba450), 80 (0x6b9ffc) y 81 (0x6ba44c).
constexpr std::uint32_t ShadowColor = 0xff000000u;
constexpr std::uint32_t HeroColor = 0xffff9600u;
constexpr std::uint32_t VersusColor = 0xff0096ffu;
constexpr std::uint32_t RivalColor = 0xff1919ffu;
constexpr double ShadowNear = 79.0;
constexpr double NameOffset = 80.0;
constexpr double ShadowFar = 81.0;
constexpr std::int32_t NameShadowTop = 0x1f;
constexpr std::int32_t NameTop = 0x1e;
constexpr std::int32_t PointsTop = 0x30;
// 0x6c9284.
constexpr std::string_view Versus = "vs";

// 0x5f3807..0x5f38cb: la caja, Message_box2 (0xfc) arriba y abajo de 5 y Message_box1 (0xfb) cuatro veces de 40,
// desde y 110 (0x6ba184) + 5, en x 213 de 213 de ancho, con du 0,83203125 y dv 0,625.
constexpr std::uint16_t BoxEdgeSlot = 0xfc;
constexpr std::uint16_t BoxMiddleSlot = 0xfb;
constexpr float BoxLeft = 213.0f;
constexpr float BoxWidth = 213.0f;
constexpr double BoxTop = 110.0;
constexpr float BoxEdgeHeight = 5.0f;
constexpr float BoxMiddleHeight = 40.0f;
constexpr std::int32_t BoxMiddleRows = 4;
constexpr float BoxDu = 0.83203125f;
constexpr float BoxDv = 0.625f;
// 0x5f38ce..0x5f3915: Interface\DuelWindow.jpg (0x650) sumada, de 128 × 128 en (256, 120).
constexpr std::uint16_t DuelImageSlot = 0x650;
constexpr float DuelImageLeft = 256.0f;
constexpr float DuelImageTop = 120.0f;
constexpr float DuelImageSize = 128.0f;
// 0x5f3922..0x5f3a2e: ok.jpg y ok2.jpg (0xf1 y 0xf2) en (237, 250) y cancel.jpg y cancel2.jpg (0xf3 y 0xf4) en (332,
// 250), de 70 × 21 con du 0,546875 y dv 0,65625; el segundo con el mouse encima, en las cajas del paso (0x5ebca0).
constexpr std::uint16_t YesSlot = 0xf1;
constexpr std::uint16_t YesHoverSlot = 0xf2;
constexpr std::uint16_t NoSlot = 0xf3;
constexpr std::uint16_t NoHoverSlot = 0xf4;
constexpr float YesX = 237.0f;
constexpr float NoX = 332.0f;
constexpr float ButtonY = 250.0f;
constexpr float ButtonWidth = 70.0f;
constexpr float ButtonHeight = 21.0f;
constexpr float ButtonDu = 0.546875f;
constexpr float ButtonDv = 0.65625f;
constexpr std::int32_t YesLeft = 0xed;
constexpr std::int32_t YesRight = 0x133;
constexpr std::int32_t NoLeft = 0x14c;
constexpr std::int32_t NoRight = 0x192;
constexpr std::int32_t ButtonTop = 0xfa;
constexpr std::int32_t ButtonBottom = 0x10f;
// 0x5f3a55..0x5f3b26: los textos, con su sombra un punto abajo a la derecha.
constexpr std::uint32_t RequestNameColor = 0xff18cfffu;
constexpr std::uint32_t RequestTextColor = 0xffdcdcdcu;
constexpr std::int32_t ShadowX = 0x140;
constexpr std::int32_t TextX = 0x13f;
constexpr std::int32_t RequestNameTop = 0x8c;
constexpr std::int32_t FirstLineTop = 0xb4;
constexpr std::int32_t SecondLineTop = 0xc8;
// Text.bmd 910 y 911 (0x76ffc44 y 0x76ffd70).
constexpr std::size_t FirstRequestText = 910;
constexpr std::size_t SecondRequestText = 911;

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

std::string_view NameView(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}

// 0x5bb8d0 (con sus consultas) · 0,5 + `offset`, con _ftol: los anchos son pares y las sumas, enteras.
std::int32_t FromTheMiddle(UI::PanelRegistry& panels, double offset)
{
    return static_cast<std::int32_t>(static_cast<double>(panels.WorldWidth()) * 0.5 + offset);
}

void SetTextColor(GameHudContext& context, std::uint32_t color)
{
    context.hud.textColors.text = color;
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x5f3ac7: sin el texto {} de Text.bmd", index));
    return *text;
}
} // namespace

void DrawTheDuelScore(GameHudContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const DuelState& duel = runtime.social.duel;
    if (duel.active == 0)
        return;
    Render::SpriteBatch& sprites = context.sprites;
    // 0x5a6cd6..0x5a6ce8: la mitad con CDQ y SAR, hacia el 0.
    const auto middle = static_cast<float>(panels.WorldWidth() / 2);
    const float left = F32(static_cast<double>(middle) - ScoreHalfWidth);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(ScoreBand);
    sprites.ColorQuad(left, ScoreTop, ScoreWidth, ScoreHeight);
    sprites.SetColor(ScoreFrame);
    sprites.ColorQuad(left, ScoreTop, ScoreWidth, ScoreLine);
    sprites.ColorQuad(left, ScoreBottom, ScoreWidth, ScoreLine);
    sprites.ColorQuad(left, ScoreTop, ScoreLine, ScoreHeight);
    sprites.ColorQuad(F32(static_cast<double>(middle) + ScoreHalfWidth), ScoreTop, ScoreLine, ScoreHeight);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    // 0x5a6dd8..0x5a6e02: sprintf("%d") de los dos puntos.
    const std::string ownPoints = std::format("{}", duel.ownScore);
    const std::string rivalPoints = std::format("{}", duel.rivalScore);
    const std::string_view hero = NameView(context.world.Actors()[runtime.hero].name);
    const std::string_view rival = NameView(duel.rivalName);
    SelectFont(context, FontKind::BigBold);
    context.hud.textColors.background = 0;
    SetTextColor(context, ShadowColor);
    DrawCenteredText(context, FromTheMiddle(panels, -ShadowNear), NameShadowTop, hero);
    SetTextColor(context, HeroColor);
    DrawCenteredText(context, FromTheMiddle(panels, -NameOffset), NameTop, hero);
    SetTextColor(context, VersusColor);
    DrawCenteredText(context, FromTheMiddle(panels, 0.0), NameTop, Versus);
    SetTextColor(context, ShadowColor);
    DrawCenteredText(context, FromTheMiddle(panels, ShadowFar), NameShadowTop, rival);
    SetTextColor(context, RivalColor);
    DrawCenteredText(context, FromTheMiddle(panels, NameOffset), NameTop, rival);
    SelectFont(context, FontKind::Normal);
    SetTextColor(context, HeroColor);
    DrawCenteredText(context, FromTheMiddle(panels, -NameOffset), PointsTop, ownPoints);
    SetTextColor(context, RivalColor);
    DrawCenteredText(context, FromTheMiddle(panels, NameOffset), PointsTop, rivalPoints);
    sprites.SetBlend(BlendMode::Opaque);
}

Core::Expected<void, std::string> DrawTheDuelRequest(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field78abad9 == 0)
        return {};
    auto first = TextOf(context, FirstRequestText);
    if (!first.HasValue())
        return Core::Unexpected(std::move(first).Error());
    auto second = TextOf(context, SecondRequestText);
    if (!second.HasValue())
        return Core::Unexpected(std::move(second).Error());
    Render::SpriteBatch& sprites = context.sprites;
    Quad(sprites, BoxEdgeSlot, BoxLeft, F32(BoxTop), BoxWidth, BoxEdgeHeight, BoxDu, BoxDv);
    std::int32_t row = 5;
    for (std::int32_t at = 0; at < BoxMiddleRows; ++at)
    {
        Quad(sprites, BoxMiddleSlot, BoxLeft, F32(static_cast<double>(row) + BoxTop), BoxWidth, BoxMiddleHeight, BoxDu,
             BoxDv);
        row += static_cast<std::int32_t>(BoxMiddleHeight);
    }
    Quad(sprites, BoxEdgeSlot, BoxLeft, F32(static_cast<double>(row) + BoxTop), BoxWidth, BoxEdgeHeight, BoxDu, BoxDv);
    sprites.SetBlend(BlendMode::Additive);
    sprites.SetColor(White);
    Quad(sprites, DuelImageSlot, DuelImageLeft, DuelImageTop, DuelImageSize, DuelImageSize, 1.0f, 1.0f);
    sprites.SetBlend(BlendMode::Opaque);
    const UI::Point mouse = context.mouse;
    const auto over = [&](std::int32_t left, std::int32_t right)
    { return mouse.x >= left && mouse.x < right && mouse.y >= ButtonTop && mouse.y < ButtonBottom; };
    Quad(sprites, over(YesLeft, YesRight) ? YesHoverSlot : YesSlot, YesX, ButtonY, ButtonWidth, ButtonHeight, ButtonDu,
         ButtonDv);
    Quad(sprites, over(NoLeft, NoRight) ? NoHoverSlot : NoSlot, NoX, ButtonY, ButtonWidth, ButtonHeight, ButtonDu,
         ButtonDv);
    sprites.SetBlend(BlendMode::Alpha);
    // 0x5f3a3d..0x5f3ab0: sprintf("[%s]") con el nombre del rival (0x78abae0).
    const std::string name = std::format("[{}]", NameView(runtime.social.duel.rivalName));
    SelectFont(context, FontKind::BigBold);
    context.hud.textColors.background = 0;
    SetTextColor(context, ShadowColor);
    DrawCenteredText(context, ShadowX, RequestNameTop + 1, name);
    SetTextColor(context, RequestNameColor);
    DrawCenteredText(context, TextX, RequestNameTop, name);
    SelectFont(context, FontKind::Bold);
    SetTextColor(context, ShadowColor);
    DrawCenteredText(context, ShadowX, FirstLineTop + 1, first.Value());
    DrawCenteredText(context, ShadowX, SecondLineTop + 1, second.Value());
    SetTextColor(context, RequestTextColor);
    DrawCenteredText(context, TextX, FirstLineTop, first.Value());
    DrawCenteredText(context, TextX, SecondLineTop, second.Value());
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}
} // namespace Client::Scenes
