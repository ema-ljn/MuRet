#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>
#include <string_view>

// La barra de la selección (la ventana +0x7f50 del modo 4: 0x63e820 la arma, 0x63ec00 la dibuja y
// 0x63eb90 la atiende; 06 § La selección de personaje, "La barra"). No es modal: tapa solo su
// rectángulo, y los personajes no se eligen debajo. El texto del primer 0D con 1 que llega en la
// selección (+0x511) lo guarda el juego (WorldRuntime): la barra del original se arma una vez. Se
// atiende en el paso (Q-C81). *Estático*, y *emulado* el lugar en cuatro pantallas.
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

class SelectionBar final : public NewUiWindow
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

    // 0x65f690 en 0x65f4b0, al armar la selección; la cierra el 1 de la de Esc (0x647e48).
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // La barra (`bar` de LayoutSelectionBar).
    [[nodiscard]] Rect Bounds(App::PixelSize screen) const override;

    // 0x63c490 en los cuatro botones y, con el foco, 0x63eb90: el que tuvo un clic. Sin el mouse
    // (una ventana arriba, 0x65fc30), los botones siguen con su estado, sin encima ni clic.
    std::optional<SelectionBarClick> Step(const Input::InputState& input, App::PixelSize screen, NewUiTurn turn = {});

    // El adorno y la franja, el aviso del servidor (+0x511, `serverNotice`), el texto 1710, los 436
    // y 437 con la cuenta bloqueada y los cuatro botones, con la mezcla normal.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen,
              std::string_view serverNotice = {});

    [[nodiscard]] const Button& CreateButton() const;
    [[nodiscard]] const Button& ConnectButton() const;
    [[nodiscard]] const Button& DeleteButton() const;

private:
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    bool m_blockedAccount = false;
    bool m_open = false;
    Button m_create;
    Button m_menu;
    Button m_connect;
    Button m_delete;
    SelectionBarLayout m_layout;
};
} // namespace Client::UI
