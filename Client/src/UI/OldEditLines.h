#pragma once

#include "App/WindowState.h"
#include "Render/Text/FontSet.h"
#include "UI/OldControls.h"

#include <cstdint>
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
namespace Client::UI
{
// La fuente del EDIT: WM_SETFONT con la normal ([0x5763554], 0x49812c).
inline constexpr Render::Text::FontKind EditFont = Render::Text::FontKind::Normal;

// 0x467fe0 con +0xc4 en 1 (0x4675e0 con 1 antes): +0xd0, ftol(h · ry / cy), con cy el alto de GetTextExtentPointA de
// "1" (0x6c7af0) en el DC; y el ancho del área del texto: el de la ventana, ftol((w + ftol(−5 · rx)) · rx), menos su
// barra de Windows.
void MakeTheEditMultiline(OldEdit& edit, const Render::Text::ITextMeasure& measure, Render::Text::FontKind dcFont,
                          App::PixelSize screen);

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

// ES_AUTOVSCROLL: después de un cambio del texto, el renglón del cursor (el último: el cursor de MuRet está siempre al
// final) a la vista.
void FollowTheEditCaret(OldEdit& edit, std::int32_t lineCount);

// WM_CHAR con un carácter, con el retroceso o con Enter (0x467b10): +0x80 en 0 (0x467b9d) y, al EDIT de Windows. Enter
// solo llega con +0xc4 en 1 (0x467c8f), y es un "\r\n" si entra entero.
void TypeInTheEdit(OldEdit& edit, std::string_view typed);
void BackspaceInTheEdit(OldEdit& edit);
void BreakTheEditLine(OldEdit& edit);
} // namespace Client::UI
