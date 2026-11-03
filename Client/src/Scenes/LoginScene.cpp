#include "Scenes/LoginScene.h"

#include "App/SessionState.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/TextureSets.h"

#include <chrono>
#include <cstdint>

namespace Client::Scenes
{
namespace
{
// Lo que deja 0x620a00(1) con el blanco, antes de los fondos.
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor4f(1, 1, 1, 0,5) con la mezcla normal, antes de pasar a la aditiva: nada se dibuja con él.
constexpr Render::Color HalfWhite{1.0f, 1.0f, 1.0f, 0.5f};
// glColor3f(0,25; 0,1; 0,1), el de la arena (0x3e800000 y 0x3dcccccd), y glColor3f(0,8; 0,7; 0,6),
// el de los textos (0x3f4ccccd, 0x3f333333 y 0x3f19999a). glColor3f deja el alfa en 1.
constexpr Render::Color SandColor{0.25f, 0.1f, 0.1f, 1.0f};
constexpr Render::Color TextColor{0.8f, 0.7f, 0.6f, 1.0f};

// Un fondo: 0x621b60(slot, x, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0).
Render::OriginalQuad Background(std::uint16_t slot, float x)
{
    return Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                .x = x,
                                .y = 25.0f,
                                .width = 320.0f,
                                .height = 320.0f,
                                .scaleSize = true,
                                .scalePosition = true};
}

// Una arena de 0x622000 en (0, 25, 640, 430), v = 0, y su velocidad: la u es ((int) T % 100000) ·
// velocidad (0x6b8a94, 0,0001, y 0x6b8a68, 0,0005).
struct Sand
{
    std::uint16_t slot = 0;
    float speed = 0.0f;
    float du = 1.0f;
    float dv = 1.0f;
};
constexpr Sand SlowSand{.slot = 0x5c2, .speed = 0.0001f, .du = 0.3f, .dv = 0.3f};
constexpr Sand FastSand{.slot = 0x5c3, .speed = 0.0005f, .du = 3.0f, .dv = 2.0f};
// El módulo de la u (0x186a0).
constexpr std::int64_t SandPeriodMs = 100000;

// La x87 hace fild del resto y fmul con la velocidad en float, y guarda en float: con un resto de
// menos de 2¹⁷ y una velocidad de 24 bits, el producto entra exacto en un double, así que da lo
// mismo.
Render::OriginalSkewedQuad SandQuad(const Sand& sand, Core::Duration frameClock)
{
    const std::int64_t remainder =
        std::chrono::duration_cast<std::chrono::milliseconds>(frameClock).count() % SandPeriodMs;
    return Render::OriginalSkewedQuad{
        .slot = Render::TextureSlot{sand.slot},
        .x = 0.0f,
        .y = 25.0f,
        .width = 640.0f,
        .height = 430.0f,
        .u = static_cast<float>(static_cast<double>(remainder) * static_cast<double>(sand.speed)),
        .v = 0.0f,
        .du = sand.du,
        .dv = sand.dv};
}

// La fuente DAT_05763554, el texto en blanco (DAT_006c8e5c) y el fondo en 0x80000000
// (DAT_006c8e64).
constexpr Render::Text::TextStyle LoginTextStyle{Render::Text::FontKind::Normal,
                                                 Render::Text::TextColors{0xffffffff, 0x80000000}};
// Dónde se apoyan: la y de abajo, y la x donde termina el 454 y empieza el 455 (0x14f).
constexpr int TextBottom = 479;
constexpr int TextSplit = 335;

constexpr std::string_view VersionMark = "%s";
} // namespace

std::string PlaceVersion(std::string_view text, std::string_view version)
{
    const std::size_t mark = text.find(VersionMark);
    if (mark == std::string_view::npos)
    {
        return std::string(text);
    }
    std::string placed(text.substr(0, mark));
    placed.append(version);
    placed.append(text.substr(mark + VersionMark.size()));
    return placed;
}

LoginScene::LoginScene(const SceneContext& context, const GameData& data, App::ServerSession* session,
                       std::string_view version)
    : m_context(context), m_session(session), m_textures(context.textures, LoginTextureSet),
      m_world(WorldOf(SceneId::Login))
{
    if (data.text)
    {
        m_left = data.text->Text(LoginLeftText);
        m_right = data.text->Text(LoginRightText);
        if (const auto format = data.text->Text(LoginVersionText))
        {
            m_version = PlaceVersion(*format, version);
        }
    }
}

Core::Expected<void, std::string> LoginScene::Enter()
{
    auto loaded = m_textures.Load(m_context.dataRoot);
    if (!loaded.HasValue())
    {
        return loaded;
    }
    if (m_session != nullptr)
    {
        m_session->Start();
    }
    return {};
}

void LoginScene::Step(const Input::InputState& /*input*/) {}

void LoginScene::Render(SceneCanvas& canvas, float alpha)
{
    Render::SpriteBatch& sprites = canvas.sprites;
    // 1 y 2: la mezcla normal, el blanco y los dos fondos.
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(White);
    sprites.Quad(Background(0x15, 0.0f));
    sprites.Quad(Background(0x16, 320.0f));
    // 3 y 4: el fin del 2D (0x621a90) y el pase 3D. El pase que no dibuja, de 0x620db0(0, 0, w,
    // 48), no va.
    canvas.flushSprites();
    if (m_world)
    {
        canvas.world.Draw(*m_world, alpha);
    }
    // 5: las dos arenas, aditivas.
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(HalfWhite);
    sprites.SetBlend(Render::BlendMode::Additive);
    sprites.SetColor(SandColor);
    sprites.SkewedQuad(SandQuad(SlowSand, canvas.frameClock));
    sprites.SkewedQuad(SandQuad(FastSand, canvas.frameClock));
    // 6: los textos, con la mezcla aditiva que quedó (0x554000 no la cambia).
    sprites.SetColor(TextColor);
    DrawTexts(canvas);
    // 7: el fundido no dibuja nada (Q-C66). 8: la UI, en la CL5.5.
}

void LoginScene::DrawTexts(SceneCanvas& canvas) const
{
    ITextDrawer& text = canvas.text;
    if (m_left)
    {
        const Render::Text::TextExtent extent = text.Measure(LoginTextStyle.font, *m_left);
        text.Draw(Render::Text::RightAlignedAt(TextSplit, extent, canvas.screen),
                  Render::Text::BottomAlignedAt(TextBottom, extent, canvas.screen), *m_left, LoginTextStyle);
    }
    if (m_right)
    {
        const Render::Text::TextExtent extent = text.Measure(LoginTextStyle.font, *m_right);
        text.Draw(TextSplit, Render::Text::BottomAlignedAt(TextBottom, extent, canvas.screen), *m_right,
                  LoginTextStyle);
    }
    if (m_version)
    {
        const Render::Text::TextExtent extent = text.Measure(LoginTextStyle.font, *m_version);
        text.Draw(0, Render::Text::BottomAlignedAt(TextBottom, extent, canvas.screen), *m_version, LoginTextStyle);
    }
}

void LoginScene::Leave()
{
    m_textures.Release();
}

std::optional<SceneId> LoginScene::Next() const
{
    if (m_session != nullptr && m_session->State() == App::SessionState::LoggedIn)
    {
        return SceneId::CharacterSelect;
    }
    return std::nullopt;
}
} // namespace Client::Scenes
