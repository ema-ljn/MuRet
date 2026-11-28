#pragma once

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Ui/OldControls.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Render::Text
{
class ITextMeasure;
}

// El EDIT de varios renglones de las ventanas viejas (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el chat con un
// amigo"): el cuerpo de la carta que se escribe (+0x958 de 0x497f50) es el único EDIT con +0xc4 en 1. El original le
// deja a la ventana EDIT de Windows (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, sin ES_AUTOHSCROLL) los renglones,
// el corrimiento y el cursor, y los lee con EM_GETLINECOUNT, GetScrollPos y el bitmap que pinta (0x468320); MuRet no
// tiene esa ventana y los arma acá. Lo que es de Windows es *inferido* y va contra la VM (08, P-C217): el corte de
// las palabras, el ancho del área del texto (el de la ventana menos los 16 de su barra) y los topes del corrimiento.
namespace Client::Ui
{
// La fuente del EDIT: WM_SETFONT con la normal ([0x5763554], 0x49812c).
inline constexpr Render::Text::FontKind EditFont = Render::Text::FontKind::Normal;

// El margen que el bitmap del EDIT SBCS observado en XP deja antes del texto. Con +0xc4 en 1, el área donde se cortan
// los renglones lo pierde a cada lado (*inferido*: el derecho igual al izquierdo, P-C217).
inline constexpr std::int32_t EditTextMargin = 3;

// 0x467fe0 con +0xc4 en 1 (0x4675e0 con 1 antes): +0xd0, ftol(h · ry / cy), con cy el alto de GetTextExtentPointA de
// "1" (0x6c7af0) en el DC; y el ancho del área del texto: el de la ventana, ftol((w + ftol(−5 · rx)) · rx), menos su
// barra de Windows.
void MakeTheEditMultiline(OldEdit& edit, const Render::Text::ITextMeasure& measure, Render::Text::FontKind dcFont,
                          Core::PixelSize screen);

// Los renglones del EDIT: uno solo sin +0xc4; con +0xc4 en 1, cortados en cada "\r\n" y, donde no entran en el ancho
// del área del texto, en el último espacio que entra (los espacios de más quedan en el renglón) o, sin espacio, en la
// última letra que entra. Un EDIT vacío o que termina en "\r\n" tiene un renglón vacío al final. Las vistas son del
// texto del EDIT y valen hasta que cambie.
[[nodiscard]] std::vector<std::string_view> EditLines(const OldEdit& edit, const Render::Text::ITextMeasure& measure);

// EM_GETLINECOUNT: cuántos renglones tiene.
[[nodiscard]] std::int32_t EditLineCount(const OldEdit& edit, const Render::Text::ITextMeasure& measure);

// EM_LINESCROLL(0, lines): el primer renglón a la vista corrido, entre 0 y renglones − los que entran.
void ScrollTheEditLines(OldEdit& edit, std::int32_t lineCount, std::int32_t lines);

// EM_SCROLL: SB_LINEUP (0) y SB_LINEDOWN (1) corren uno; SB_PAGEUP (2) y SB_PAGEDOWN (3), los que entran.
inline constexpr std::int32_t EditLineUp = 0;
inline constexpr std::int32_t EditLineDown = 1;
inline constexpr std::int32_t EditPageUp = 2;
inline constexpr std::int32_t EditPageDown = 3;
void ScrollTheEdit(OldEdit& edit, std::int32_t lineCount, std::int32_t code);

// El renglón del cursor (TextField::Cursor): el último que empieza en él o antes. Con el cursor en un corte de
// renglón, el de abajo (*inferido*, P-C217). `lines` son los de EditLines del mismo texto.
[[nodiscard]] std::int32_t EditCaretLine(const OldEdit& edit, std::span<const std::string_view> lines);

// ES_AUTOVSCROLL: después de un cambio del texto, el renglón del cursor a la vista (sin `caretLine`, el último).
void FollowTheEditCaret(OldEdit& edit, std::int32_t lineCount, std::int32_t caretLine = -1);

// WM_CHAR con el retroceso o con Enter (0x467b10): +0x80 en 0 (0x467b9d) y, al EDIT de Windows. Enter solo llega con
// +0xc4 en 1 (0x467c8f), y es un "\r\n" si entra entero. Un carácter es Ui::TypeInTheEdit (UI/OldControls.h). Con
// +0xc4 en 1, el retroceso borra entero el "\r\n" de antes del cursor y Supr, el de después (*inferido*, P-C217).
void BackspaceInTheEdit(OldEdit& edit);
void DeleteInTheEdit(OldEdit& edit);
void BreakTheEditLine(OldEdit& edit);

// Las teclas que el EDIT de Windows usa para mover el cursor con +0xc4 en 1 (*inferido*, P-C217): las flechas de los
// costados saltan el "\r\n" entero; las de arriba y abajo van al renglón de al lado, a la letra más cerca de la misma
// x; Inicio y Fin, al principio y al final del renglón; RePág y AvPág, tantos renglones como entran, y corren la vista
// lo mismo. Con Shift, eligen. Después, el renglón del cursor queda a la vista (ES_AUTOVSCROLL).
enum class EditCaretKey : std::uint8_t
{
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
};
void MoveTheEditCaret(OldEdit& edit, const Render::Text::ITextMeasure& measure, EditCaretKey key, bool shift);

// WM_PASTE en el EDIT de Windows con +0xc4 en 1 (*inferido*, P-C217): el texto del portapapeles en CP1252 hasta el
// primer NUL, con cada fin de renglón ("\r\n", "\n" o "\r") como "\r\n", hasta el tope.
void PasteInTheEdit(OldEdit& edit, std::string_view utf8);
} // namespace Client::Ui
