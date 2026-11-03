#pragma once

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "UI/Button.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>

// La barra de abajo del login (+0xe40; 06 § Las ventanas de la UI nueva): la arma 0x641000, la
// ubican 0x65f300 y 0x6410c0, la dibuja 0x641440 y atiende los clics 0x6411b0. Es una ventana sin
// fondo de W − 60 × 30 con dos botones de 54 × 30 y tres cuadros: el del menú a la izquierda y el
// de los créditos a la derecha, este adentro del adorno de deco.tga. La abre 0x4c1860 con la lista
// y queda abierta hasta que se sale del login. *Estático*.
//
// Los clics se ven, pero la escena todavía no hace nada con ellos: el del menú abre en el original
// la ventana de Esc (+0x3d0) y el de los créditos manda C1 04 F4 06 al CS y abre la de +0x4578,
// las dos sin leer (P-C13). Se atiende en el paso (Q-C81).
namespace Client::UI
{
// Dónde va cada cosa, en píxeles con la y desde arriba.
struct MenuBarLayout
{
    Rect bar;
    Rect menu;
    Rect credit;
    // El adorno, ya corrido por su punto (105, 59).
    Rect ornament;
};

// 0x65f300: la barra en (30, −11 − (int) (H · −0,945f) − 30), con el producto en la precisión de
// la x87 y truncado hacia el 0, y de W − 60 de ancho. 0x6410c0: el menú en su esquina, los créditos
// en (x + ancho − 54, y) y el adorno con su punto en la esquina de los créditos.
[[nodiscard]] MenuBarLayout LayoutMenuBar(App::PixelSize screen);

// Qué botón de la barra tuvo un clic (+0xea o +0x1ca, 0x6411b0).
enum class MenuBarClick : std::uint8_t
{
    Menu,
    Credit,
};

class MenuBar
{
public:
    explicit MenuBar(TextureSizes sizes);

    // 0x65f690: abre la barra. Una que ya está abierta queda como estaba.
    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const;

    // 0x63c490 en los dos botones y 0x6411b0: el botón que tuvo un clic, el menú primero.
    std::optional<MenuBarClick> Step(const Input::InputState& input, App::PixelSize screen);
    // 0x641440 con la mezcla normal (Q-C80): el adorno y después los dos botones, sin rótulo.
    void Draw(Render::SpriteBatch& sprites, App::PixelSize screen);

private:
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    bool m_open = false;
    Button m_menu;
    Button m_credit;
    MenuBarLayout m_layout;
};
} // namespace Client::UI
