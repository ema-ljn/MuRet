#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/NewUi.h"
#include "UI/Slider.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Las opciones del login y de la selección (+0x7e8, vtable 0x6ba988; CL8, hito 8; 06 § Las opciones):
// las arma 0x644120 (desde 0x65f300 y 0x65f4b0, cada vez que se arma la UI), las ubica 0x644520, las
// abre 0x644640, las atiende 0x644730 y las dibuja 0x644860. De toda la pantalla, como la de Esc, con
// un marco de 30 líneas (op2_back1 arriba), tres casillas, dos deslizadores y el botón de cerrar.
namespace Client::UI
{
// Lo que guardan: los globales que 0x6446e0 lee al armarlas y que 0x644730 escribe.
struct OptionsSettings
{
    // 0x6c8e3c: el ataque automático (texto 386).
    bool autoAttack = true;
    // 0x77f3b78: el sonido de los avisos del filtro (texto 387).
    bool noticeSound = false;
    // 0x6c7a8c: los carteles de Slide.bmd (texto 919).
    bool slidesOn = true;
    // 0x7d3a118: el volumen, de 0 a 9 (texto 389).
    int volume = 5;
    // 0x6c9dcc: el detalle del brillo de los ítems, de 0 a 4 (texto 1840, que muestra 2 · d + 5).
    std::uint8_t glowDetail = 4;

    friend bool operator==(const OptionsSettings&, const OptionsSettings&) = default;
};

// Lo que cambió en un paso de 0x644730.
struct OptionsEvent
{
    enum class Kind : std::uint8_t
    {
        AutoAttack,
        NoticeSound,
        SlidesOn,
        // Y 0x632d80 con el volumen nuevo (el DirectSound de 0x41eed0), de la CL10.
        Volume,
        GlowDetail,
        // El botón de cerrar, sin sonido, o Esc con el sonido 0x19 (0x41ed10, de la CL10).
        Closed,
    };

    Kind kind = Kind::Closed;
    // El valor nuevo: 0 o 1 en las casillas, el del deslizador en los otros dos.
    int value = 0;

    friend bool operator==(const OptionsEvent&, const OptionsEvent&) = default;
};

// Aplica `event` a `settings`; Closed no cambia nada.
void ApplyOption(OptionsSettings& settings, const OptionsEvent& event);

// El marco, las casillas, el botón y los deslizadores, en píxeles.
struct OptionsWindowLayout
{
    Rect frame;
    std::array<Rect, 3> checks{};
    Rect close;
    std::array<Point, 2> sliders{};
};

// 0x644120 y 0x644520 con líneas de `lineHeight` (el alto del buffer de op1_back3).
[[nodiscard]] OptionsWindowLayout LayoutOptionsWindow(App::PixelSize screen, int lineHeight);

class OptionsWindow final : public NewUiWindow
{
public:
    OptionsWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // 0x644120: las casillas, el botón y los deslizadores (el volumen hasta 9, el brillo hasta 4), y
    // 0x6446e0 con `settings`. Cerrada.
    void Arm(const OptionsSettings& settings);

    // 0x644640 con 1.
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // De toda la pantalla (0x669c70(W, H, −1, 0)).
    [[nodiscard]] Rect Bounds(App::PixelSize screen) const override;

    [[nodiscard]] const Button& Check(std::size_t index) const;
    [[nodiscard]] const Button& CloseButton() const;
    [[nodiscard]] const Slider& SliderAt(std::size_t index) const;

    // Las casillas y el botón (0x63c490) y, con el foco, 0x644730 con los `elapsedMs` del paso:
    // compara los deslizadores con `current`, los globales.
    std::optional<OptionsEvent> Step(const Input::InputState& input, App::PixelSize screen, double elapsedMs,
                                     const OptionsSettings& current, NewUiTurn turn = {});
    // 0x66a050 y 0x644860: el fondo, el marco, los textos con los valores de `current` (los
    // globales, no los deslizadores), los deslizadores y, al final (0x66a090), las casillas y el botón.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen,
              const OptionsSettings& current);

private:
    // Ubica el marco y lo de adentro. Un deslizador solo se mueve si cambió su lugar: 0x646890 deja
    // la perilla en el lugar de su valor, y el original lo llama solo al armarlas.
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts = nullptr;
    std::array<Button, 3> m_checks;
    Button m_close;
    std::array<Slider, 2> m_sliders;
    OptionsWindowLayout m_layout;
    bool m_open = false;
};
} // namespace Client::UI
