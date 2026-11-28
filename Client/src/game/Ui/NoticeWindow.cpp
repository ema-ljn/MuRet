#include "Ui/NoticeWindow.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/GameText.h"
#include "Ui/NewUi.h"
#include "Ui/Notices.h"
#include "Ui/TextField.h"
#include "Ui/Widget.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Ui
{
namespace
{
// 0x6429b0: the box (message_back, slot 0x727), the OK (message_ok_b_all, 0x724) and the cancel (loding_cancel_b_all,
// 0x725), of three frames, and the code box (delete_secret_number, 0x728); all four uploaded by 0x61ccb0, the whole
// game's load.
constexpr Render::TextureSlot BoxSlot{0x727};
constexpr Render::TextureSlot OkSlot{0x724};
constexpr Render::TextureSlot CancelSlot{0x725};
constexpr Render::TextureSlot CodeBoxSlot{0x728};
constexpr int BoxWidth = 352;
constexpr int BoxHeight = 113;
constexpr int ButtonWidth = 54;
constexpr int ButtonHeight = 30;
constexpr int ButtonTop = 72;
// 0x642bf0: the buttons' x for each kind.
constexpr int OkLeft = 149;
constexpr int OkCancelOkLeft = 98;
constexpr int OkCancelCancelLeft = 200;
constexpr int CodeOkLeft = 209;
constexpr int CodeCancelLeft = 264;
// The 171 × 23 code box at (+32, +76), and 0x6436c0's EDIT at (+42, +84): ES_PASSWORD, digits only, EM_LIMITTEXT(7).
constexpr int CodeBoxLeft = 32;
constexpr int CodeBoxTop = 76;
constexpr int CodeBoxWidth = 171;
constexpr int CodeBoxHeight = 23;
constexpr int CodeLeft = 42;
constexpr int CodeTop = 84;
constexpr std::size_t CodeDigits = 7;
// 0x6429b0's base, with slot -1: black with alpha 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;
// 0x642bf0: one line centered in the box, at y + 38 (0x467060's mode 2); two, on the left, at (+25, +32) and (+25,
// +51). With DAT_05763560 in white, no background. 0x643100 splits a single text into two rows under 52 bytes
// (0x632c90).
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

// 0x467b10 with [+0x4c] at 1: 0 to 9.
bool IsDigit(char byte)
{
    return byte >= '0' && byte <= '9';
}
} // namespace

NoticeLayout LayoutNotice(Core::PixelSize screen)
{
    const int x = (screen.width - BoxWidth) / 2;
    const int y = (screen.height - BoxHeight) / 2;
    const Rect box{.x = x, .y = y, .width = BoxWidth, .height = BoxHeight};
    return NoticeLayout{.box = box, .ok = ButtonAt(box, OkLeft)};
}

NoticeWindow::NoticeWindow(TextureSizes sizes, const Tables::TextTable* texts)
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
    // 0x553570(1) in 0x6436c0: the code empties on opening.
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

Rect NoticeWindow::Bounds(Core::PixelSize screen) const
{
    return Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height};
}

std::optional<NoticeId> NoticeWindow::Step(const Input::InputState& input, Core::PixelSize screen, NewUiTurn turn)
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
    const std::optional<Point> mouse = MouseOfTheTurn(turn, input, screen);
    const bool ok = m_ok.Step(input, mouse, turn.buttonClicked);
    const bool withCancel = kind == NoticeKind::OkCancel || kind == NoticeKind::Code;
    const bool cancel = withCancel && m_cancel.Step(input, mouse, turn.buttonClicked);
    if (!turn.logic)
    {
        return std::nullopt;
    }
    const bool escape = input.TakePress(Input::Key::Escape);
    const bool enter = !ok && input.TakePress(Input::Key::Enter);
    if (!ok && !cancel && (enter || escape))
    {
        if (turn.buttonClicked)
        {
            turn.buttonClicked();
        }
    }
    if (ok || enter || (escape && !withCancel))
    {
        const NoticeId closed = *m_notice;
        Close();
        return closed;
    }
    if (cancel || escape)
    {
        // 0x6436a0: the id at −1 and closes; the code field hides and empties (the original leaves it with the focus:
        // Q-C116, not reproduced).
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

void NoticeWindow::Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen)
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
    // 0x642f00: the box, the rows, the code box and its field, and the buttons.
    if (const std::optional<NoticeTexts> texts = TextsOf(*m_notice))
    {
        const std::optional<std::string_view> first = TextAt(m_texts, texts->first);
        if (!texts->second)
        {
            if (first && !first->empty())
            {
                // Only the 0x5e is a format, with the chosen one's name (sprintf in 0x6431b0).
                const std::string_view argument = m_argument;
                const std::string placed = *m_notice == ConfirmDeletionNotice
                                               ? PlaceArguments(*first, std::span(&argument, 1))
                                               : std::string(*first);
                const std::vector<std::string> lines = SplitIntoLines(placed, MaxLines, LineBytes);
                if (lines.size() == 1)
                {
                    // 0x642f44..0x642f95: 0x467060 in mode 2, the 352 box from the box's x in 640 × 480, with the text
                    // at (352 − cx) / 2 in pixels.
                    const Render::Text::CenteredInBox centered =
                        Render::Text::CenterInBox(text, NoticeStyle.font, lines[0], BoxWidth);
                    text.DrawColumns(Render::Text::DesignX(box.x, screen),
                                     Render::Text::DesignY(box.y + OneLineTop, screen), lines[0], NoticeStyle,
                                     -centered.textColumn, centered.width);
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

void NoticeWindow::Place(Core::PixelSize screen)
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
} // namespace Client::Ui
