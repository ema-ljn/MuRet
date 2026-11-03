#pragma once

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>
#include <string>

// Un botón de la UI nueva (CL5.5): 0x63c330 lo arma, 0x63c490 lo atiende y 0x63c710 dibuja su
// rótulo (06 § Las ventanas de la UI nueva). Su textura tiene los cuadros uno abajo del otro, del
// alto del botón: el k empieza en la fila k · h (0x63c330 arma la tabla con (0, k · h)), y
// 0x647560 corre las UV (UiImage::frameTop). Los de la lista y los de la cuenta y los avisos usan
// el 0 normal, el 1 con el mouse encima y el 2 apretado; los de los grupos, el 3 marcados.
//
// La captura: con el botón izquierdo apretado encima, el botón la toma (DAT_07d53534); soltado
// encima y capturado, es un clic (+0xaa), y soltado en cualquier lado, la suelta. El original
// tiene una captura para toda la UI; acá cada botón tiene la suya, y "encima" es encima y con el
// botón del mouse arriba: con el mouse apretado afuera de todo y arrastrado encima, el original
// muestra el cuadro 1 y MuRet el 0. Se atiende en el paso, no en el frame (Q-C81).
namespace Client::UI
{
// Los colores del rótulo por estado (la tabla de +0xd0, 0xAABBGGRR), sin los de deshabilitado:
// ningún botón del login se deshabilita.
struct ButtonColors
{
    std::uint32_t normal = 0xffffffff;
    std::uint32_t pressed = 0xffffffff;
    std::uint32_t hover = 0xffffffff;
    std::uint32_t checkedNormal = 0xffffffff;
    std::uint32_t checkedPressed = 0xffffffff;
    std::uint32_t checkedHover = 0xffffffff;
};

class Button
{
public:
    // `checkedFrame`: el cuadro de marcado (+0xbc), o nada si el botón no se marca.
    Button(Render::TextureSlot slot, int width, int height, std::optional<int> checkedFrame = std::nullopt);

    // 0x6472e0(x, y, 3): la esquina de arriba a la izquierda, en píxeles.
    void MoveTo(Point topLeft);
    [[nodiscard]] Rect Area() const;

    // Un paso de 0x63c490 con el mouse en `mouse`: true si fue un clic.
    bool Step(const Input::InputState& input, Point mouse);

    void SetChecked(bool checked);
    [[nodiscard]] bool Checked() const;

    // El cuadro del último paso.
    [[nodiscard]] int Frame() const;
    // Encima y capturado: el cuadro 2, y el rótulo un punto más abajo.
    [[nodiscard]] bool Pressed() const;

    // La imagen del botón con su cuadro, en píxeles.
    [[nodiscard]] Render::UiImage Image(const TextureSizes& sizes) const;

    // El rótulo (+0xcc) y sus colores (+0xd0). Vacío no se dibuja.
    void SetLabel(std::string label, ButtonColors colors);
    // 0x63c710: con la fuente DAT_05763560, el color del estado y el fondo en 0, centrado en el
    // ancho del botón (el modo 2 de 0x467060). La y en 640 × 480 es ((h − cy) · 0,5 + y) / ry más
    // 0,5, o 1,5 apretado (+0xd8), truncada. La x la centra 0x467060 en píxeles, después de pasar
    // la del botón a 640 × 480; MuRet centra en píxeles y después pasa a 640 × 480 (DesignX), que
    // puede correr el rótulo un píxel fuera de 640 × 480. Sin pantalla (un lado en 0), nada. Da el
    // color con que lo dibujó, o nada si no lo dibujó: 0x63c710 lo deja como el color del texto
    // (DAT_006c8e5c), y lo que se dibuja después sin poner otro sale con él (Q-C83).
    std::optional<std::uint32_t> DrawLabel(Scenes::ITextDrawer& text, App::PixelSize screen) const;

private:
    [[nodiscard]] std::uint32_t LabelColor() const;

    Render::TextureSlot m_slot;
    Rect m_area;
    std::optional<int> m_checkedFrame;
    bool m_checked = false;
    bool m_captured = false;
    bool m_over = false;
    bool m_hover = false;
    std::string m_label;
    ButtonColors m_colors;
};
} // namespace Client::UI
