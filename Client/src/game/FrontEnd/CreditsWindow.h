#pragma once

#include "Core/PixelSize.h"
#include "Tables/Credits.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The login's credits (+0x4578, vtable 0x6ba848; CL8, milestone 8; 06 § Los créditos): 0x63f020 builds them
// (from 0x65f300, when building the login UI), 0x63f3a0 places them, 0x63f5a0 opens them (and 0x63fbf0
// starts them), 0x63f630 handles them and 0x63f6b0 draws them. Full-screen, black and opaque, with two
// illustrations that fade (0x63fe20), the ornament, the logo, three rows of text from credit.bmd that
// three black bars cover and uncover (0x6400b0, 0x63ffe0) and the close button. The illustration and the
// bars go in the 800 × 600 space (W / 800 and H / 600 as floats); the ornament and the button, in pixels.
namespace Client::FrontEnd
{
// What happened when opening them or in a step: each close (0x63f9a0) and the illustration that was loaded
// (0x63fc50).
struct CreditsStep
{
    // 0x63f9a0: 0x65f6c0, C1 04 F4 06 to the ConnectServer and the music (0x4acf60 with Mutheme.mp3 and
    // 0x4acfd0 with main_theme.mp3). A step can close them twice: with the button or Esc, and again on
    // reaching the end of the table in the same step.
    int closes = 0;
    // 0x63ffe0: when the table ends, 0x19 sounds before F4 06.
    bool buttonClickSound = false;
    // 0x63fc50 with +0x5c8: the scene uploads Interface\im{n + 1}_1.jpg and _2.jpg into 0x6f8 and 0x6f9.
    std::optional<std::uint8_t> illustration;

    friend bool operator==(const CreditsStep&, const CreditsStep&) = default;
};

// Where each thing goes, in pixels unless stated.
struct CreditsLayout
{
    // W / 800 and H / 600, stored as floats (fild, fmul by the float of 0x6ba884 and 0x6ba880, fstp).
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // 0x63f3a0: (W − 189, _ftol(H · 0.878333f) − 103), with the float of 0x6ba888.
    Ui::Rect deco;
    // At (122, 63) of the ornament.
    Ui::Rect close;
};

[[nodiscard]] CreditsLayout LayoutCredits(Core::PixelSize screen);

class CreditsWindow final : public Ui::NewUiWindow
{
public:
    // `credits`: the credit.bmd table (GameData), which has to outlive the window.
    CreditsWindow(Ui::TextureSizes sizes, std::span<const Tables::CreditEntry> credits);

    // 0x63f020: the opaque bars (0x647050 with alpha 0xff) and the button; closed. It does not touch the
    // rows' indices: in the original they belong to the window, which lives in the manager's .bss.
    void Arm();

    // 0x63f5a0 with 1 and 0x63fbf0: the first illustration, the three rows at 1 and the first group
    // (0x63ffe0), which with an empty table (or a 0 at the start) closes them again.
    CreditsStep Open();
    // 0x63f5a0 with 0: closed and the illustration still (+0x5b8 at 0).
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;

    // The button (0x63c490) and, with the focus, 0x63f630 with the step's `elapsedMs`.
    CreditsStep Step(const Input::InputState& input, Core::PixelSize screen, double elapsedMs, Ui::NewUiTurn turn = {});
    // 0x66a050 and 0x63f6b0.
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen);

    [[nodiscard]] const Ui::Button& CloseButton() const;
    // +0x5b8, +0x5c8 and the alpha of the two illustrations (+0xa0 and +0x148).
    [[nodiscard]] int IllustrationState() const;
    [[nodiscard]] std::uint8_t Illustration() const;
    [[nodiscard]] std::uint8_t IllustrationAlpha() const;
    // +0x39c0..+0x39c8 and the alpha of the three bars (+0x340, +0x3e8 and +0x490).
    [[nodiscard]] int RowState(std::size_t row) const;
    [[nodiscard]] std::uint8_t BarAlpha(std::size_t row) const;
    // +0x39a8, +0x39ac, +0x39b0.. and +0x39a4: the records shown; +0x39a0, the next one.
    [[nodiscard]] std::size_t TitleIndex() const;
    [[nodiscard]] std::size_t RoleIndex() const;
    [[nodiscard]] std::span<const std::size_t> NameIndices() const;
    [[nodiscard]] std::size_t NextIndex() const;

private:
    // 0x63f9a0.
    void Shut(CreditsStep& step);
    // 0x63fc50.
    void LoadTheIllustration(CreditsStep& step);
    // 0x63ffe0.
    void NextGroup(CreditsStep& step);
    // 0x6400b0 and 0x63fe20.
    void StepRow(std::size_t row, double elapsedMs, CreditsStep& step);
    void StepIllustration(double elapsedMs, CreditsStep& step);
    [[nodiscard]] std::uint8_t KindAt(std::size_t index) const;
    void DrawTexts(Render::Frame::ITextDrawer& text, Core::PixelSize screen) const;

    Ui::TextureSizes m_sizes;
    std::span<const Tables::CreditEntry> m_credits;
    Ui::Button m_close;
    CreditsLayout m_layout;
    bool m_open = false;
    int m_illustrationState = 0;
    double m_illustrationHold = 0.0;
    std::uint8_t m_illustration = 0;
    std::uint8_t m_illustrationAlpha = 0;
    std::array<int, 3> m_rowStates{};
    std::array<std::uint8_t, 3> m_barAlphas{};
    double m_rowHold = 0.0;
    std::size_t m_next = 0;
    std::size_t m_title = 0;
    std::size_t m_role = 0;
    std::array<std::size_t, 4> m_names{};
    std::size_t m_nameCount = 0;
};
} // namespace Client::FrontEnd
