#include "UI/NoticeWindow.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
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
#include "UI/TextField.h"
#include "UI/Widget.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::UI
{
namespace
{
// 0x6429b0: la caja (message_back, slot 0x727), el OK (message_ok_b_all, 0x724) y el cancelar
// (loding_cancel_b_all, 0x725), de tres cuadros, y la caja del código (delete_secret_number,
// 0x728); los cuatro los sube 0x61ccb0, la carga de todo el juego.
constexpr Render::TextureSlot BoxSlot{0x727};
constexpr Render::TextureSlot OkSlot{0x724};
constexpr Render::TextureSlot CancelSlot{0x725};
constexpr Render::TextureSlot CodeBoxSlot{0x728};
constexpr int BoxWidth = 352;
constexpr int BoxHeight = 113;
constexpr int ButtonWidth = 54;
constexpr int ButtonHeight = 30;
constexpr int ButtonTop = 72;
// 0x642bf0, la x de los botones de cada tipo.
constexpr int OkLeft = 149;
constexpr int OkCancelOkLeft = 98;
constexpr int OkCancelCancelLeft = 200;
constexpr int CodeOkLeft = 209;
constexpr int CodeCancelLeft = 264;
// La caja del código, de 171 × 23, en (+32, +76), y el EDIT de 0x6436c0 en (+42, +84): ES_PASSWORD,
// solo dígitos, EM_LIMITTEXT(7).
constexpr int CodeBoxLeft = 32;
constexpr int CodeBoxTop = 76;
constexpr int CodeBoxWidth = 171;
constexpr int CodeBoxHeight = 23;
constexpr int CodeLeft = 42;
constexpr int CodeTop = 84;
constexpr std::size_t CodeDigits = 7;
// La base de 0x6429b0, con el slot -1: negra con alfa 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;
// 0x642bf0: una línea al medio de la caja, en y + 38 (el modo 2 de 0x467060); dos, a la izquierda,
// en (+25, +32) y (+25, +51). Con DAT_05763560 en blanco, sin fondo. 0x643100 parte un texto solo
// en dos renglones de menos de 52 bytes (0x632c90).
constexpr int OneLineTop = 38;
constexpr int TwoLinesLeft = 25;
constexpr int FirstLineTop = 32;
constexpr int SecondLineTop = 51;
constexpr std::size_t LineBytes = 52;
constexpr std::size_t MaxLines = 2;
constexpr Render::Text::TextStyle NoticeStyle{Render::Text::FontKind::OtherFace12,
                                              Render::Text::TextColors{0xffffffff, 0}};

Rect ButtonAt(const Rect& box, int left)
{
    return Rect{.x = box.x + left, .y = box.y + ButtonTop, .width = ButtonWidth, .height = ButtonHeight};
}

// 0x467b10 con [+0x4c] en 1: del 0 al 9.
bool IsDigit(char byte)
{
    return byte >= '0' && byte <= '9';
}
} // namespace

NoticeLayout LayoutNotice(App::PixelSize screen)
{
    const int x = (screen.width - BoxWidth) / 2;
    const int y = (screen.height - BoxHeight) / 2;
    const Rect box{.x = x, .y = y, .width = BoxWidth, .height = BoxHeight};
    return NoticeLayout{.box = box, .ok = ButtonAt(box, OkLeft)};
}

NoticeWindow::NoticeWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_ok(OkSlot, ButtonWidth, ButtonHeight),
      m_cancel(CancelSlot, ButtonWidth, ButtonHeight), m_code(CodeDigits, true)
{
}

void NoticeWindow::Open(NoticeId notice, std::string_view argument)
{
    m_notice = notice;
    m_argument = argument;
    m_ok = Button(OkSlot, ButtonWidth, ButtonHeight);
    m_cancel = Button(CancelSlot, ButtonWidth, ButtonHeight);
    // 0x553570(1) en 0x6436c0: el código se vacía al abrir.
    m_code.Clear();
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
    const NoticeKind kind = KindOf(*m_notice);
    if (kind == NoticeKind::NoButton)
    {
        return std::nullopt;
    }
    if (kind == NoticeKind::Code)
    {
        for (const char byte : input.Text())
        {
            if (byte == Input::BackspaceChar)
            {
                m_code.Backspace();
            }
            else if (IsDigit(byte))
            {
                m_code.Type(std::string_view(&byte, 1));
            }
        }
    }
    const Point mouse = MouseInPixels(input, screen);
    const bool ok = m_ok.Step(input, mouse);
    const bool withCancel = kind == NoticeKind::OkCancel || kind == NoticeKind::Code;
    const bool cancel = withCancel && m_cancel.Step(input, mouse);
    const bool escape = input.WasPressed(Input::Key::Escape);
    if (ok || input.WasPressed(Input::Key::Enter) || (escape && !withCancel))
    {
        const NoticeId closed = *m_notice;
        Close();
        return closed;
    }
    if (cancel || escape)
    {
        // 0x6436a0: el id en −1 y cierra; el campo del código se esconde y se vacía (el original lo
        // deja con el foco: Q-C116, no se reproduce).
        Close();
        m_code.Clear();
    }
    return std::nullopt;
}

const NoticeControls& NoticeWindow::Layout() const
{
    return m_layout;
}

std::string_view NoticeWindow::Code() const
{
    return m_code.Value();
}

std::string NoticeWindow::TakeCode()
{
    std::string code(m_code.Value());
    m_code.Clear();
    return code;
}

bool NoticeWindow::WantsTextInput() const
{
    return m_notice && KindOf(*m_notice) == NoticeKind::Code;
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
    const Rect& box = m_layout.box;
    sprites.Image(PixelImage(BoxSlot, box, m_sizes(BoxSlot)));
    // 0x642f00: la caja, los renglones, la caja del código y su campo, y los botones.
    if (const std::optional<NoticeTexts> texts = TextsOf(*m_notice))
    {
        const std::optional<std::string_view> first = TextAt(m_texts, texts->first);
        if (!texts->second)
        {
            if (first && !first->empty())
            {
                // Solo el 0x5e es un formato, con el nombre del elegido (sprintf en 0x6431b0).
                const std::string_view argument = m_argument;
                const std::string placed = *m_notice == ConfirmDeletionNotice
                                               ? PlaceArguments(*first, std::span(&argument, 1))
                                               : std::string(*first);
                const std::vector<std::string> lines = SplitIntoLines(placed, MaxLines, LineBytes);
                if (lines.size() == 1)
                {
                    // 0x467060, el modo 2: (352 − cx) / 2 en píxeles; MuRet centra antes de pasar a
                    // 640 × 480.
                    const int width = text.Measure(NoticeStyle.font, lines[0]).width;
                    text.Draw(Render::Text::DesignX(box.x + (BoxWidth - width) / 2, screen),
                              Render::Text::DesignY(box.y + OneLineTop, screen), lines[0], NoticeStyle);
                }
                else
                {
                    const int x = Render::Text::DesignX(box.x + TwoLinesLeft, screen);
                    text.Draw(x, Render::Text::DesignY(box.y + FirstLineTop, screen), lines[0], NoticeStyle);
                    text.Draw(x, Render::Text::DesignY(box.y + SecondLineTop, screen), lines[1], NoticeStyle);
                }
            }
        }
        else
        {
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
    }
    if (m_layout.codeBox)
    {
        sprites.Image(PixelImage(CodeBoxSlot, *m_layout.codeBox, m_sizes(CodeBoxSlot)));
        const std::string shown = m_code.Shown();
        if (!shown.empty())
        {
            text.Draw(Render::Text::DesignX(box.x + CodeLeft, screen), Render::Text::DesignY(box.y + CodeTop, screen),
                      shown, NoticeStyle);
        }
    }
    if (m_layout.ok)
    {
        sprites.Image(m_ok.Image(m_sizes));
    }
    if (m_layout.cancel)
    {
        sprites.Image(m_cancel.Image(m_sizes));
    }
}

void NoticeWindow::Place(App::PixelSize screen)
{
    const NoticeLayout layout = LayoutNotice(screen);
    m_layout = NoticeControls{.box = layout.box, .ok = std::nullopt, .cancel = std::nullopt, .codeBox = std::nullopt};
    switch (m_notice ? KindOf(*m_notice) : NoticeKind::Ok)
    {
    case NoticeKind::NoButton:
        break;
    case NoticeKind::Ok:
        m_layout.ok = ButtonAt(layout.box, OkLeft);
        break;
    case NoticeKind::OkCancel:
        m_layout.ok = ButtonAt(layout.box, OkCancelOkLeft);
        m_layout.cancel = ButtonAt(layout.box, OkCancelCancelLeft);
        break;
    case NoticeKind::Code:
        m_layout.ok = ButtonAt(layout.box, CodeOkLeft);
        m_layout.cancel = ButtonAt(layout.box, CodeCancelLeft);
        m_layout.codeBox = Rect{.x = layout.box.x + CodeBoxLeft,
                                .y = layout.box.y + CodeBoxTop,
                                .width = CodeBoxWidth,
                                .height = CodeBoxHeight};
        break;
    }
    if (m_layout.ok)
    {
        m_ok.MoveTo(Point{m_layout.ok->x, m_layout.ok->y});
    }
    if (m_layout.cancel)
    {
        m_cancel.MoveTo(Point{m_layout.cancel->x, m_layout.cancel->y});
    }
}
} // namespace Client::UI
