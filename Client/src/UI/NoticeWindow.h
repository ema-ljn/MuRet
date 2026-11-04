#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/Notices.h"
#include "UI/TextField.h"
#include "UI/Widget.h"

#include <optional>
#include <string>
#include <string_view>

// El aviso de la UI nueva (CL5.5 y CL6.5; 06 § Los avisos y "Los avisos de la selección"): lo arman
// 0x6429b0 y 0x642bf0, lo dibuja 0x642f00 y lo atiende 0x642e30. Oscurece la pantalla con una base
// negra de alfa 0x80 y muestra su caja con el texto del aviso (Notices.h), partido en dos renglones
// de menos de 52 bytes (0x643100). Los botones van por su tipo (KindOf): ninguno, el OK, el OK y el
// cancelar, o esos dos con el campo del código del borrado, que toma hasta 7 dígitos enmascarados y
// se vacía al abrir (0x6436c0). Se atiende en el paso (Q-C81). *Estático*.
//
// No se llama MessageBox: <windows.h> define una macro con ese nombre.
namespace Client::UI
{
struct NoticeLayout
{
    Rect box;
    Rect ok;
};

// 0x6429b0: la caja de 352 × 113 en ((W − 352) / 2, (H − 113) / 2), y el OK de 54 × 30 en (+149,
// +72), el del tipo 2.
[[nodiscard]] NoticeLayout LayoutNotice(App::PixelSize screen);

// Lo que tiene el aviso abierto por su tipo (0x642bf0), en píxeles: la caja, el OK y el cancelar de
// 54 × 30 y la caja del código de 171 × 23 (delete_secret_number, el slot 0x728).
struct NoticeControls
{
    Rect box;
    std::optional<Rect> ok;
    std::optional<Rect> cancel;
    std::optional<Rect> codeBox;
};

class NoticeWindow
{
public:
    // `texts` puede ser nullptr (la caja sin texto) y tiene que vivir lo que la ventana.
    NoticeWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // Abre el aviso `notice` (0x65ee60). Uno abierto lo cambia. `argument` va en el "%s" del texto
    // del 0x5e, el único que 0x6431b0 arma con sprintf (el nombre del elegido); los otros no lo usan.
    // El código se vacía.
    void Open(NoticeId notice, std::string_view argument = {});
    void Close();
    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] std::optional<NoticeId> Notice() const;

    // 0x642e30 en el paso: con OK o Enter (y Esc en el tipo 2) se cierra y da el aviso que se cerró,
    // para lo que sigue (FollowUpOf); con cancelar o Esc en los tipos 3 y 4 se cierra sin darlo; el
    // tipo 0 no se cierra. En el 4, lo que se escribe va al campo del código: solo dígitos.
    std::optional<NoticeId> Step(const Input::InputState& input, App::PixelSize screen);

    // Lo del aviso abierto, en la pantalla del último paso o dibujo.
    [[nodiscard]] const NoticeControls& Layout() const;

    // Los dígitos del campo del código, hasta que se abre otro aviso: los lee 0x643740 después del OK.
    [[nodiscard]] std::string_view Code() const;

    // Con el campo del código abierto.
    [[nodiscard]] bool WantsTextInput() const;
    // La base, la caja, el OK y el texto, con la mezcla normal (Q-C80).
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    std::optional<NoticeId> m_notice;
    std::string m_argument;
    Button m_ok;
    Button m_cancel;
    TextField m_code;
    NoticeControls m_layout;
};
} // namespace Client::UI
