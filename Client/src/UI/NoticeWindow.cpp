#include "UI/NoticeWindow.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/GameText.h"
#include "UI/Notices.h"
#include "UI/Widget.h"

#include <optional>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x6429b0: la caja (message_back, slot 0x727) y el OK (message_ok_b_all, 0x724), de tres cuadros;
// los dos los sube 0x61ccb0, la carga de todo el juego.
constexpr Render::TextureSlot BoxSlot{0x727};
constexpr Render::TextureSlot OkSlot{0x724};
constexpr int BoxWidth = 352;
constexpr int BoxHeight = 113;
constexpr int OkLeft = 149;
constexpr int OkTop = 72;
constexpr int OkWidth = 54;
constexpr int OkHeight = 30;

// La base de 0x6429b0, con el slot -1: negra con alfa 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;

// 0x642bf0: una línea al medio de la caja, en y + 38 (el modo 2 de 0x467060); dos, a la izquierda,
// en (+25, +32) y (+25, +51). Con DAT_05763560 en blanco, sin fondo.
constexpr int OneLineTop = 38;
constexpr int TwoLinesLeft = 25;
constexpr int FirstLineTop = 32;
constexpr int SecondLineTop = 51;
constexpr Render::Text::TextStyle NoticeStyle{Render::Text::FontKind::OtherFace12,
                                              Render::Text::TextColors{0xffffffff, 0}};
} // namespace

NoticeLayout LayoutNotice(App::PixelSize screen)
{
    const int x = (screen.width - BoxWidth) / 2;
    const int y = (screen.height - BoxHeight) / 2;
    return NoticeLayout{.box = Rect{.x = x, .y = y, .width = BoxWidth, .height = BoxHeight},
                        .ok = Rect{.x = x + OkLeft, .y = y + OkTop, .width = OkWidth, .height = OkHeight}};
}

NoticeWindow::NoticeWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_ok(OkSlot, OkWidth, OkHeight)
{
}

void NoticeWindow::Open(NoticeId notice)
{
    m_notice = notice;
    m_ok = Button(OkSlot, OkWidth, OkHeight);
}

void NoticeWindow::Close()
{
    m_notice.reset();
}

bool NoticeWindow::IsOpen() const
{
    return m_notice.has_value();
}

std::optional<NoticeId> NoticeWindow::Notice() const
{
    return m_notice;
}

std::optional<NoticeId> NoticeWindow::Step(const Input::InputState& input, App::PixelSize screen)
{
    if (!m_notice)
    {
        return std::nullopt;
    }
    Place(screen);
    const bool ok = m_ok.Step(input, MouseInPixels(input, screen));
    if (ok || input.WasPressed(Input::Key::Enter) || input.WasPressed(Input::Key::Escape))
    {
        const NoticeId closed = *m_notice;
        Close();
        return closed;
    }
    return std::nullopt;
}

void NoticeWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
{
    if (!m_notice)
    {
        return;
    }
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.Image(
        Render::UiImage{.slot = std::nullopt,
                        .x = 0,
                        .y = 0,
                        .width = screen.width,
                        .height = screen.height,
                        .color = Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = BaseAlpha}});
    sprites.Image(PixelImage(BoxSlot, m_layout.box, m_sizes(BoxSlot)));
    sprites.Image(m_ok.Image(m_sizes));

    const std::optional<NoticeTexts> texts = TextsOf(*m_notice);
    if (!texts)
    {
        return;
    }
    const Rect& box = m_layout.box;
    const std::optional<std::string_view> first = TextAt(m_texts, texts->first);
    if (!texts->second)
    {
        if (first && !first->empty())
        {
            // 0x467060, el modo 2: (352 − cx) / 2 en píxeles; MuRet centra antes de pasar a
            // 640 × 480.
            const int width = text.Measure(NoticeStyle.font, *first).width;
            text.Draw(Render::Text::DesignX(box.x + (BoxWidth - width) / 2, screen),
                      Render::Text::DesignY(box.y + OneLineTop, screen), *first, NoticeStyle);
        }
        return;
    }
    const int x = Render::Text::DesignX(box.x + TwoLinesLeft, screen);
    if (first && !first->empty())
    {
        text.Draw(x, Render::Text::DesignY(box.y + FirstLineTop, screen), *first, NoticeStyle);
    }
    const std::optional<std::string_view> second = TextAt(m_texts, *texts->second);
    if (second && !second->empty())
    {
        text.Draw(x, Render::Text::DesignY(box.y + SecondLineTop, screen), *second, NoticeStyle);
    }
}

void NoticeWindow::Place(App::PixelSize screen)
{
    m_layout = LayoutNotice(screen);
    m_ok.MoveTo(Point{m_layout.ok.x, m_layout.ok.y});
}
} // namespace Client::UI
