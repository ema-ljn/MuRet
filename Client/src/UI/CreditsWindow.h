#pragma once

#include "App/WindowState.h"
#include "Data/Text/Credits.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Los créditos del login (+0x4578, vtable 0x6ba848; CL8, hito 8; 06 § Los créditos): los arma 0x63f020
// (desde 0x65f300, al armar la UI del login), los ubica 0x63f3a0, los abre 0x63f5a0 (y 0x63fbf0 los
// empieza), los atiende 0x63f630 y los dibuja 0x63f6b0. De toda la pantalla, negra y opaca, con dos
// ilustraciones que se funden (0x63fe20), el adorno, el logo, tres renglones de texto de credit.bmd que
// tres barras negras tapan y destapan (0x6400b0, 0x63ffe0) y el botón de cerrar. La ilustración y las
// barras van en el espacio de 800 × 600 (W / 800 y H / 600 en float); el adorno y el botón, en píxeles.
namespace Client::UI
{
// Lo que pasó al abrirlos o en un paso: cada cierre (0x63f9a0) y la ilustración que se cargó (0x63fc50).
struct CreditsStep
{
    // 0x63f9a0: 0x65f6c0, el C1 04 F4 06 al ConnectServer y la música (0x4acf60 con Mutheme.mp3 y
    // 0x4acfd0 con main_theme.mp3). Un paso puede cerrarlos dos veces: con el botón o Esc, y otra al llegar
    // al final de la tabla en el mismo paso.
    int closes = 0;
    // 0x63fc50 con +0x5c8: la escena sube Interface\im{n + 1}_1.jpg y _2.jpg en 0x6f8 y 0x6f9.
    std::optional<std::uint8_t> illustration;

    friend bool operator==(const CreditsStep&, const CreditsStep&) = default;
};

// Dónde va cada cosa, en píxeles salvo lo que se dice.
struct CreditsLayout
{
    // W / 800 y H / 600, guardados en float (fild, fmul por el float de 0x6ba884 y 0x6ba880, fstp).
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // 0x63f3a0: (W − 189, _ftol(H · 0,878333f) − 103), con el float de 0x6ba888.
    Rect deco;
    // A (122, 63) del adorno.
    Rect close;
};

[[nodiscard]] CreditsLayout LayoutCredits(App::PixelSize screen);

class CreditsWindow final : public NewUiWindow
{
public:
    // `credits`: la tabla de credit.bmd (GameData), que tiene que vivir más que la ventana.
    CreditsWindow(TextureSizes sizes, std::span<const Data::Text::CreditEntry> credits);

    // 0x63f020: las barras opacas (0x647050 con alfa 0xff) y el botón; cerrada. No toca los índices de
    // los renglones: en el original son de la ventana, que vive en la .bss del administrador.
    void Arm();

    // 0x63f5a0 con 1 y 0x63fbf0: la primera ilustración, los tres renglones en 1 y el primer grupo
    // (0x63ffe0), que con la tabla vacía (o un 0 al principio) los vuelve a cerrar.
    CreditsStep Open();
    // 0x63f5a0 con 0: cerrada y la ilustración quieta (+0x5b8 en 0).
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] Rect Bounds(App::PixelSize screen) const override;

    // El botón (0x63c490) y, con el foco, 0x63f630 con los `elapsedMs` del paso.
    CreditsStep Step(const Input::InputState& input, App::PixelSize screen, double elapsedMs, NewUiTurn turn = {});
    // 0x66a050 y 0x63f6b0.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

    [[nodiscard]] const Button& CloseButton() const;
    // +0x5b8, +0x5c8 y el alfa de las dos ilustraciones (+0xa0 y +0x148).
    [[nodiscard]] int IllustrationState() const;
    [[nodiscard]] std::uint8_t Illustration() const;
    [[nodiscard]] std::uint8_t IllustrationAlpha() const;
    // +0x39c0..+0x39c8 y el alfa de las tres barras (+0x340, +0x3e8 y +0x490).
    [[nodiscard]] int RowState(std::size_t row) const;
    [[nodiscard]] std::uint8_t BarAlpha(std::size_t row) const;
    // +0x39a8, +0x39ac, +0x39b0.. y +0x39a4: los registros que se muestran; +0x39a0, el que sigue.
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
    // 0x6400b0 y 0x63fe20.
    void StepRow(std::size_t row, double elapsedMs, CreditsStep& step);
    void StepIllustration(double elapsedMs, CreditsStep& step);
    [[nodiscard]] std::uint8_t KindAt(std::size_t index) const;
    void DrawTexts(Scenes::ITextDrawer& text, App::PixelSize screen) const;

    TextureSizes m_sizes;
    std::span<const Data::Text::CreditEntry> m_credits;
    Button m_close;
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
} // namespace Client::UI
