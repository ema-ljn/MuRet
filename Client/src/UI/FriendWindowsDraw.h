#pragma once

#include "Core/Expected.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"

#include <string>

// El dibujo de la ventana de amigos y de la caja de agregar un amigo (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"):
// su vtable + 0x50, que el marco (0x492500) llama con la fuente normal en el DC después de la sombra y antes de la
// barra del título. Las piezas que comparten con las demás ventanas viejas están en UI/OldWindowDrawParts.
namespace Client::UI
{
// 0x4a2620: arrastrada o cambiando de tamaño, ubica las pestañas (SettleTheFriendsForTheDraw); la pestaña a la vista
// (0x49cc20 o 0x49e7f0; la de las cartas, 0x4a0a30, es del hito 10 y corta); con la mezcla normal, la franja negra de
// los encabezados (el 7 de la paleta, 20 de alto), los encabezados (0x4a2460), la raya del 2 desde 0x9f, los títulos de
// las pestañas centrados en su lugar (en blanco el de la vista o el del mouse), el texto del botón del estado contra el
// borde derecho y su casilla (0x463090), marcada con +0xe0 del administrador en 1.
[[nodiscard]] Core::Expected<void, std::string> DrawTheFriendsWindow(OldWindowManager& manager, OldWindow& window,
                                                                     const OldWindowDrawContext& context,
                                                                     const OldWindowDrawView& view);

// 0x4a3750: arrastrada o cambiando de tamaño, ubica a sus hijas; los dos botones (0x45b2e0) y, con la mezcla normal,
// el EDIT (0x468320). Termina sin mezcla (0x620980).
void DrawTheAddFriendBox(OldWindowManager& manager, OldWindow& box, const OldWindowDrawContext& context,
                         const OldWindowDrawView& view);
} // namespace Client::UI
