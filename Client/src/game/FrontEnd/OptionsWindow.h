#pragma once

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Slider.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The options of the login and the selection (+0x7e8, vtable 0x6ba988; CL8, milestone 8; 06 § Las opciones):
// 0x644120 builds them (from 0x65f300 and 0x65f4b0, each time the UI is built), 0x644520 places them,
// 0x644640 opens them, 0x644730 handles them and 0x644860 draws them. Full-screen, like the Esc one, with
// a 30-line frame (op2_back1 on top), three checkboxes, two sliders and the close button.
namespace Client::FrontEnd
{
// What they keep: the globals 0x6446e0 reads when building them and 0x644730 writes.
struct OptionsSettings
{
    // 0x6c8e3c (text 386).
    bool autoAttack = true;
    // 0x77f3b78: the sound of the filter's notices (text 387).
    bool noticeSound = false;
    // 0x6c7a8c: the Slide.bmd banners (text 919).
    bool slidesOn = true;
    // 0x7d3a118: 0 to 9 (text 389).
    int volume = 5;
    // 0x6c9dcc: the items' glow detail, 0 to 4 (text 1840, which shows 2 · d + 5).
    std::uint8_t glowDetail = 4;

    friend bool operator==(const OptionsSettings&, const OptionsSettings&) = default;
};

// What changed in a step of 0x644730.
struct OptionsEvent
{
    enum class Kind : std::uint8_t
    {
        AutoAttack,
        NoticeSound,
        SlidesOn,
        // And 0x632d80 with the new volume (the DirectSound of 0x41eed0), from CL10.
        Volume,
        GlowDetail,
        // The close button, without sound, or Esc with sound 0x19 (0x41ed10, from CL10).
        Closed,
    };

    Kind kind = Kind::Closed;
    // The new value: 0 or 1 for the checkboxes, the slider's for the other two.
    int value = 0;

    friend bool operator==(const OptionsEvent&, const OptionsEvent&) = default;
};

// Aplica `event` a `settings`; Closed no cambia nada.
void ApplyOption(OptionsSettings& settings, const OptionsEvent& event);

// The frame, the checkboxes, the button and the sliders, in pixels.
struct OptionsWindowLayout
{
    Ui::Rect frame;
    std::array<Ui::Rect, 3> checks{};
    Ui::Rect close;
    std::array<Ui::Point, 2> sliders{};
};

// 0x644120 and 0x644520 with lines of `lineHeight` (the height of op1_back3's buffer).
[[nodiscard]] OptionsWindowLayout LayoutOptionsWindow(Core::PixelSize screen, int lineHeight);

class OptionsWindow final : public Ui::NewUiWindow
{
public:
    OptionsWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // 0x644120: the checkboxes, the button and the sliders (volume up to 9, glow up to 4), and
    // 0x6446e0 with `settings`. Closed.
    void Arm(const OptionsSettings& settings);

    // 0x644640 with 1.
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // Full-screen (0x669c70(W, H, −1, 0)).
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;

    [[nodiscard]] const Ui::Button& Check(std::size_t index) const;
    [[nodiscard]] const Ui::Button& CloseButton() const;
    [[nodiscard]] const Ui::Slider& SliderAt(std::size_t index) const;

    // The checkboxes and the button (0x63c490) and, with the focus, 0x644730 with the step's `elapsedMs`:
    // it compares the sliders with `current`, the globals.
    std::optional<OptionsEvent> Step(const Input::InputState& input, Core::PixelSize screen, double elapsedMs,
                                     const OptionsSettings& current, Ui::NewUiTurn turn = {});
    // 0x66a050 and 0x644860: the background, the frame, the texts with the values of `current` (the
    // globals, not the sliders), the sliders and, at the end (0x66a090), the checkboxes and the button.
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen,
              const OptionsSettings& current);

private:
    // Places the frame and its contents. A slider only moves if its place changed: 0x646890 leaves
    // the knob at its value's place, and the original calls it only when building them.
    void Place(Core::PixelSize screen);

    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts = nullptr;
    std::array<Ui::Button, 3> m_checks;
    Ui::Button m_close;
    std::array<Ui::Slider, 2> m_sliders;
    OptionsWindowLayout m_layout;
    bool m_open = false;
};
} // namespace Client::FrontEnd
