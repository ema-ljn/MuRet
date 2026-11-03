#pragma once

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/Notices.h"
#include "UI/Widget.h"

#include <optional>

// El aviso de la UI nueva del tipo 2, con un OK (CL5.5; 06 § Los avisos): lo arman 0x6429b0 y
// 0x642bf0, lo dibuja 0x642f00 y lo atiende 0x642e30. Oscurece la pantalla con una base negra de
// alfa 0x80, muestra su caja con el texto del aviso (Notices.h) y se cierra con OK, Enter o Esc.
// Se atiende en el paso (Q-C81). *Estático*.
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
// +72).
[[nodiscard]] NoticeLayout LayoutNotice(App::PixelSize screen);

class NoticeWindow
{
public:
    // `texts` puede ser nullptr (la caja sin texto) y tiene que vivir lo que la ventana.
    NoticeWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // Abre el aviso `notice` (0x65ee60). Uno abierto lo cambia.
    void Open(NoticeId notice);
    void Close();
    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] std::optional<NoticeId> Notice() const;

    // 0x642e30 en el paso: con OK, Enter o Esc se cierra y da el aviso que se cerró, para lo que
    // sigue (FollowUpOf).
    std::optional<NoticeId> Step(const Input::InputState& input, App::PixelSize screen);
    // La base, la caja, el OK y el texto, con la mezcla normal (Q-C80).
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    void Place(App::PixelSize screen);

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    std::optional<NoticeId> m_notice;
    Button m_ok;
    NoticeLayout m_layout;
};
} // namespace Client::UI
