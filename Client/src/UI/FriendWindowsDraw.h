#pragma once

#include "UI/FriendModel.h"
#include "UI/OldControls.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"

#include <cstddef>
#include <cstdint>
#include <deque>

// El dibujo de la ventana de amigos y de la caja de agregar un amigo (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"):
// su vtable + 0x50, que el marco (0x492500) llama con la fuente normal en el DC después de la sombra y antes de la
// barra del título. Las piezas que comparten con las demás ventanas viejas están en UI/OldWindowDrawParts.
namespace Client::UI
{
// 0x4a2620: arrastrada o cambiando de tamaño, ubica las pestañas (SettleTheFriendsForTheDraw); la pestaña a la vista
// (0x49cc20, 0x4a0a30 o 0x49e7f0; la de las cartas es del hito 10); con la mezcla normal, la franja negra de
// los encabezados (el 7 de la paleta, 20 de alto), los encabezados (0x4a2460), la raya del 2 desde 0x9f, los títulos de
// las pestañas centrados en su lugar (en blanco el de la vista o el del mouse), el texto del botón del estado contra el
// borde derecho y su casilla (0x463090), marcada con +0xe0 del administrador en 1.
void DrawTheFriendsWindow(OldWindowManager& manager, OldWindow& window, const OldWindowDrawContext& context,
                          const OldWindowDrawView& view);

// 0x4a3750: arrastrada o cambiando de tamaño, ubica a sus hijas; los dos botones (0x45b2e0) y, con la mezcla normal,
// el EDIT (0x468320). Termina sin mezcla (0x620980).
void DrawTheAddFriendBox(OldWindowManager& manager, OldWindow& box, const OldWindowDrawContext& context,
                         const OldWindowDrawView& view);

// 0x461cd0, la vtable + 0x80 de la lista de amigos (0x460970): el renglón (DrawTheChosenRow), el nombre (sprintf
// "%s", 0x6c7084) y, con +0x130 en 1, el lugar. Da 1.
std::int32_t DrawTheFriendRow(const OldWindowDrawContext& context, const OldList& list,
                              const std::deque<FriendEntry>& friends, std::int32_t row, std::size_t at,
                              const OldWindowDrawView& view);

// 0x47a810, la vtable + 0x14 de una lista de 0x460970 (la de la pestaña de amigos y la de los miembros y la de invitar
// del chat con un amigo): la barra (0x461890) y los renglones de 0x461cd0.
void DrawTheFriendList(const OldWindowDrawContext& context, OldList& list, const std::deque<FriendEntry>& friends,
                       const OldWindowDrawView& view);
} // namespace Client::UI
