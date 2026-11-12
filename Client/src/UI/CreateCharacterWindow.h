#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/NewUi.h"
#include "UI/TextField.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// La ventana de crear un personaje de la selección (+0x8498: 0x63d1a0 la arma, 0x63d4e0 la ubica,
// 0x63d7c0 cambia la clase, 0x63d880 la atiende y 0x63e0a0 la dibuja; 06 § La ventana de crear).
// Modal, con la base de toda la pantalla: las cinco clases, el nombre, los atributos y la
// descripción de la clase, OK y cancelar. Sin la vista previa en 3D (la PR que sigue). Los chequeos
// del nombre son de quien manda (UI::NoticeOfNewName). Se atiende en el paso (Q-C81). *Estático*, y
// *emulado* el lugar en cuatro pantallas.
namespace Client::UI
{
inline constexpr std::size_t CreatableClassCount = 5;
// Lo que copia GetWindowTextA en 0x7798bf8 (tope 0xff, con su cero).
inline constexpr std::size_t NameBufferSize = 0x100;

struct CreateWindowLayout
{
    Rect box;        // 454 × 406, al centro (no se dibuja)
    Rect nameBack;   // cha_id.tga, 346 × 38, en (x, y + 317)
    Rect attributes; // negra con alfa 0x8f, 108 × 80 (96 con el Dark Lord), en (x + 346, y + 50)
    Rect strip;      // negra con alfa 0x8f, 454 × 51, en (x, y + 355)
    std::array<Rect, CreatableClassCount> classes; // cha_bt.tga, 108 × 26
    Rect ok;                                       // 54 × 30, en (x + 346, y + 325)
    Rect cancel;                                   // 54 × 30, en (x + 400, y + 325)
    // Dónde va el texto del campo del nombre, en 640 × 480: (ftol((x + 78) / rx), ftol((y + 338) / ry)).
    Point nameField;
};

// 0x63d4e0 con ((W − 454) / 2, (H − 406) / 2), la división de C; los atributos con el alto de la
// clase `characterClass`.
[[nodiscard]] CreateWindowLayout LayoutCreateWindow(App::PixelSize screen, std::uint8_t characterClass);

enum class CreateWindowEvent : std::uint8_t
{
    Submitted, // OK o Enter: quien manda revisa el nombre y, si va, cierra la ventana (0x63d990)
    Cancelled, // cancelar o Esc: la ventana ya se cerró
};

class CreateCharacterWindow final : public NewUiWindow
{
public:
    // `texts` puede ser nullptr y tiene que vivir lo que la ventana. El armado (0x63d1a0) elige al
    // Dark Knight (+0x890 en 1) y habilita todas las clases hasta que llegue la lista.
    CreateCharacterWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // 0x63d7c0: habilita las clases de la 0 a `creatable` (pkt[4] de la lista); con 5 o más, las cinco
    // (el original escribe fuera de los botones: Q-C119).
    void EnableClasses(std::uint8_t creatable);

    // 0x63d670(1): el campo vacío. La clase queda la que estaba.
    void Open();
    // 0x63d670(0): el campo vacío.
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // De toda la pantalla, como la base (0x669c70(W, H, −1, 0)): tiene el mouse en toda ella.
    [[nodiscard]] Rect Bounds(App::PixelSize screen) const override;

    // La clase elegida, de 0 a 4 (+0x890).
    [[nodiscard]] std::uint8_t ChosenClass() const;
    // El nombre del campo. Vale hasta el próximo cambio; no va al log.
    [[nodiscard]] std::string_view Name() const;
    // 0x7798bf8, los 256 bytes donde el OK copia el campo con su cero (GetWindowTextA): detrás pueden
    // quedar los de un intento anterior más largo, porque solo se vacía al abrir (0x553570). Es lo
    // que mira 0x447ca0 (Q-C118).
    [[nodiscard]] std::string_view Buffer() const;
    [[nodiscard]] const Button& ClassButton(std::size_t index) const;
    [[nodiscard]] const Button& OkButton() const;
    // Con la ventana abierta, el campo tiene el teclado.
    [[nodiscard]] bool WantsTextInput() const;

    // 0x63d880: lo que se escribe va al campo; el clic en una clase habilitada la elige; después, el
    // OK, cancelar, Enter o Esc, en ese orden.
    // Sin el foco (un aviso arriba, 0x65fc30): los botones siguen con su estado, sin el mouse
    // encima y sin clic; el EDIT sigue con el foco de Win32 (nadie se lo saca), así que lo que se
    // escribe entra al nombre.
    std::optional<CreateWindowEvent> Step(const Input::InputState& input, App::PixelSize screen, NewUiTurn turn = {});

    // 0x66a050: la base negra de alfa 0x80 de toda la pantalla. La vista previa va entre esto y
    // Draw, que no se oscurece.
    void DrawBase(Render::SpriteBatch& sprites, App::PixelSize screen) const;
    // 0x63e0a0 después de la vista previa: el fondo del nombre, la caja de los atributos y la franja,
    // los siete botones, los atributos, la descripción y el campo.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    void Place(App::PixelSize screen);
    void Choose(std::uint8_t characterClass);
    void TypeInto(const Input::InputState& input);
    void CopyTheName();
    void DrawAttributes(Scenes::ITextDrawer& text, App::PixelSize screen) const;
    void DrawDescription(Scenes::ITextDrawer& text, App::PixelSize screen) const;
    void DrawTheName(Scenes::ITextDrawer& text, App::PixelSize screen) const;

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    bool m_open = false;
    std::uint8_t m_class = 1;
    std::array<Button, CreatableClassCount> m_classes;
    Button m_ok;
    Button m_cancel;
    TextField m_name;
    std::array<char, NameBufferSize> m_buffer{};
    CreateWindowLayout m_layout{};
};
} // namespace Client::UI
