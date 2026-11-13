#pragma once

#include "Core/Expected.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"

#include <string>

// El dibujo del chat con un amigo y de las ventanas de una carta (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el
// chat con un amigo"): su vtable + 0x50, que el marco (0x492500) llama con la fuente normal en el DC después de la
// sombra y antes de la barra del título. Las piezas que comparten con las demás ventanas viejas están en
// UI/OldWindowDrawParts, y las listas de amigos (los miembros y los que se pueden invitar), en UI/FriendWindowsDraw.
namespace Client::UI
{
// 0x493fc0: con la mezcla normal, arrastrado o cambiando de tamaño ubica a sus hijas
// (SettleTheLetterKindsForTheDraw); los renglones (0x45f9c0, con la barra 0x45fac0 y los renglones 0x460550); con más
// de dos miembros o con la lista de invitar (+0xb0 en 2), los miembros y su raya (0x491ba0) y, con la de invitar,
// también ella y su raya; la raya del 2 y el negro (el 7) de abajo, el botón de invitar, el EDIT (0x468320) y, con la
// de invitar, el de mandar la invitación. Termina sin mezcla (0x620980).
void DrawTheChatWindow(OldWindowManager& manager, OldWindow& chat, const OldWindowDrawContext& context,
                       const OldWindowDrawView& view);

// 0x499d60: con la mezcla normal, arrastrada o cambiando de tamaño ubica a sus hijas; el cuerpo (0x464d90, con la
// barra 0x464e90 y los renglones 0x465770); abajo, la raya del 2 y el negro (el 7) de los botones, con +0x774 en 2 o
// más solo hasta el retrato (0x78 antes del borde derecho) y con una raya al costado; arriba, la raya y el negro del
// renglón del 1014 con el que la manda, la fecha y la hora; y los cinco botones. Termina sin mezcla.
void DrawTheReadWindow(OldWindowManager& manager, OldWindow& read, const OldWindowDrawContext& context,
                       const OldWindowDrawView& view);

// La vtable + 0x54 de las de una carta, al final del marco: 0x49a1d0 (la de leer, con +0x774 en 2 o más) y 0x498800
// (la de escribir, con +0xb0 en 1) dibujan el retrato (su vtable + 0x14, 0x497c10), que es de Scenes y corta
// (P-C212). Sin el retrato, nada.
[[nodiscard]] Core::Expected<void, std::string> DrawTheLetterPortrait(const OldWindow& window);
} // namespace Client::UI
