#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// La ventana de Esc del login y de la selección (+0x3d0; CL8, hito 8; 06 § La ventana de Esc): la
// arma 0x6478c0, la ubica 0x647c00, la abre 0x647cd0, la atiende 0x647d40 y la dibuja 0x647ed0.
// De toda la pantalla (0x669c70(W, H, −1, 0), negra con alfa 0x80, como el aviso), con un marco de
// piedra de 213 de ancho al centro (0x66a1c0) y cuatro botones de 108 × 30 (0x63c330): salir del
// juego (381), elegir otro servidor (382, solo en la selección), las opciones (385) y cerrar (388).
namespace Client::UI
{
// 0x647ef0: "...en% d segundos.", con lo que falta de la cuenta.
inline constexpr std::size_t LeavingTheGameText = 380;

// El marco y los botones, en píxeles.
struct EscapeWindowLayout
{
    // 213 × (64 + líneas · alto de op1_back3 + 43), al centro.
    Rect frame;
    // Salir, otro servidor, opciones y cerrar.
    std::array<Rect, 4> buttons{};
};

// 0x6478c0 y 0x647c00 con `lines` líneas del marco (6 en el login, 10 en la selección) de
// `lineHeight` (el alto del buffer de op1_back3, 0x66a4e0).
[[nodiscard]] EscapeWindowLayout LayoutEscapeWindow(App::PixelSize screen, int lines, int lineHeight);

// 0x66a960: el fondo de piedra, el borde de arriba, el de abajo y los dos lados, en ese orden.
[[nodiscard]] std::array<Render::UiImage, 5> EscapeFrameImages(Rect frame, const TextureSizes& sizes);

// Lo que pasó en un paso de 0x647d40.
struct EscapeWindowEvent
{
    enum class Kind : std::uint8_t
    {
        // 0x647ef0: el 380 con lo que falta, a los mensajes (0x555190, de sistema y sin nombre).
        Countdown,
        // La cuenta llegó a 0: "> Menu - Exit game." al log y WM_CLOSE.
        Quit,
        // El 1: "> Menu - Join another server.", el C3 F1 02 02 (0x647f60) y se cierran esta y la
        // barra de la selección (+0x7f50).
        JoinAnotherServer,
        // El 2: se cierra esta y se abren las opciones (+0x7e8).
        Options,
        // El 3, o Esc con el sonido 0x19: se cierra.
        Closed,
    };

    Kind kind = Kind::Closed;
    // Con Countdown, lo que falta.
    int seconds = 0;

    friend bool operator==(const EscapeWindowEvent&, const EscapeWindowEvent&) = default;
};

class EscapeWindow final : public NewUiWindow
{
public:
    EscapeWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // 0x6478c0 para la escena del modo: en el login (2) el 1 deshabilitado (y sin dibujar: su
    // cuadro de deshabilitado es −1) y 6 líneas; en la selección (4), 10. Sin cuenta (+0x408 en −1
    // y +0x410 en 0). Cerrada.
    void Arm(NewUiMode mode);

    // 0x647cd0 con 1. La cuenta no vuelve a empezar.
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // De toda la pantalla: 0x647c00 mueve el marco y los botones, no el rectángulo.
    [[nodiscard]] Rect Bounds(App::PixelSize screen) const override;

    // +0x408: los segundos que faltan para cerrar el juego, o nada sin cuenta.
    [[nodiscard]] std::optional<int> Countdown() const;
    [[nodiscard]] const Button& ButtonAt(std::size_t index) const;

    // Los botones (0x63c490) y, con el foco, 0x647d40 con los `elapsedMs` de este paso (el original
    // le pasa los del frame, hasta 200).
    std::optional<EscapeWindowEvent> Step(const Input::InputState& input, App::PixelSize screen, double elapsedMs,
                                          NewUiTurn turn = {});
    // 0x66a050 y 0x647ed0: el fondo negro, el marco y los botones con su rótulo (0x63c710, que no
    // dibuja el deshabilitado).
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts = nullptr;
    std::array<Button, 4> m_buttons;
    EscapeWindowLayout m_layout;
    int m_lines = 1;
    bool m_open = false;
    // +0x408 (un WORD, −1 sin cuenta) y +0x410.
    std::optional<int> m_countdown;
    double m_elapsed = 0.0;
};
} // namespace Client::UI
