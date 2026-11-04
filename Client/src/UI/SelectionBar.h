#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>

// La barra de la selección (la ventana +0x7f50 del modo 4: 0x63e820 la arma, 0x63ec00 la dibuja y
// 0x63eb90 la atiende; 06 § La selección de personaje, "La barra"). No es modal: tapa solo su
// rectángulo, y los personajes no se eligen debajo. Sin el string del primer aviso 0x0D del servidor
// (+0x511), que MuRet no lee. Se atiende en el paso (Q-C81). *Estático*, y *emulado* el lugar en
// cuatro pantallas.
namespace Client::UI
{
struct SelectionBarLayout
{
    Rect bar;   // (22, y) de (W − 44) × 54
    Rect deco;  // deco.tga, 189 × 103, en (W − 189, y − 46)
    Rect strip; // negra con alfa 0x8f, (W − 44) × 21, en (22, y + 33)
    Rect create;
    Rect menu;
    Rect connect;
    Rect remove;
};

// 0x65f4b0 con 0x63e9e0: x = 22 e y = −ftol(H · −0,945f) − 54, con el producto en la x87; los botones
// de 54 × 30 en (22, y), (77, y), (W − 131, y) y (W − 76, y).
[[nodiscard]] SelectionBarLayout LayoutSelectionBar(App::PixelSize screen);

// El clic que atiende 0x63eb90, uno por paso, en este orden.
enum class SelectionBarClick : std::uint8_t
{
    Connect, // 0x633c10, lo de Enter
    Menu,    // abre el menú (+0x3d0)
    Create,  // abre la ventana de crear (+0x8498)
    Delete,  // 0x63edf0, las revisiones del borrado
};

class SelectionBar
{
public:
    // `texts` puede ser nullptr y tiene que vivir lo que la barra.
    SelectionBar(TextureSizes sizes, const Data::Text::TextTable* texts);

    // El +0x543 que calcula el armado (0x63e820) una vez, con la lista: algún personaje de los
    // lugares 0 a 4 tiene el bit 0x10 de sus banderas. Con él se dibujan los textos 436 y 437.
    void SetBlockedAccount(bool blocked);

    // 0x63eb40: crear, con un lugar libre de los cinco; conectar y borrar, con uno elegido. El menú,
    // siempre.
    void Enable(bool aFreeSlot, bool aChosenOne);

    std::optional<SelectionBarClick> Step(const Input::InputState& input, App::PixelSize screen);

    // Si `mouse`, en píxeles, cae en el rectángulo de la barra.
    [[nodiscard]] bool Covers(Point mouse, App::PixelSize screen) const;

    // El adorno y la franja, el texto 1710, los 436 y 437 con la cuenta bloqueada y los cuatro
    // botones, con la mezcla normal.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

    [[nodiscard]] const Button& CreateButton() const;
    [[nodiscard]] const Button& ConnectButton() const;
    [[nodiscard]] const Button& DeleteButton() const;

private:
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    bool m_blockedAccount = false;
    Button m_create;
    Button m_menu;
    Button m_connect;
    Button m_delete;
    SelectionBarLayout m_layout;
};
} // namespace Client::UI
