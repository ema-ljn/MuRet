#pragma once

#include "Core/Expected.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindows.h"

#include <string>

// El dibujo del chat con un amigo y de las ventanas de una carta (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el
// chat con un amigo"): su vtable + 0x50, que el marco (0x492500) llama con la fuente normal en el DC después de la
// sombra y antes de la barra del título, y los renglones de los chats en la lista de ventanas de F5. Las piezas que
// comparten con las demás ventanas viejas están en UI/OldWindowDrawParts, y las listas de amigos (los miembros y los
// que se pueden invitar) y la pestaña de las cartas, en UI/FriendWindowsDraw.
namespace Client::UI
{
// 0x493fc0: con la mezcla normal, arrastrado o cambiando de tamaño ubica a sus hijas
// (SettleTheLetterKindsForTheDraw); los renglones (0x45f9c0, con la barra 0x45fac0 y los renglones 0x460550); con más
// de dos miembros o con la lista de invitar (+0xb0 en 2), los miembros y su raya (0x491ba0) y, con la de invitar,
// también ella y su raya; la raya del 2 y el negro (el 7) de abajo, el botón de invitar, el EDIT (0x468320) y, con la
// de invitar, el de mandar la invitación. Termina sin mezcla (0x620980). El error es el de volver a partir los
// renglones (el 9 de SettleTheLetterKindsForTheDraw).
[[nodiscard]] Core::Expected<void, std::string> DrawTheChatWindow(OldWindowManager& manager, OldWindow& chat,
                                                                  const OldWindowDrawContext& context,
                                                                  const OldWindowDrawView& view);

// 0x499d60: con la mezcla normal, arrastrada o cambiando de tamaño ubica a sus hijas; el cuerpo (0x464d90, con la
// barra 0x464e90 y los renglones 0x465770); abajo, la raya del 2 y el negro (el 7) de los botones, con +0x774 en 2 o
// más solo hasta el retrato (0x78 antes del borde derecho) y con una raya al costado; arriba, la raya y el negro del
// renglón del 1014 con el que la manda, la fecha y la hora; y los cinco botones. Termina sin mezcla. El error, como
// el del chat.
[[nodiscard]] Core::Expected<void, std::string> DrawTheReadWindow(OldWindowManager& manager, OldWindow& read,
                                                                  const OldWindowDrawContext& context,
                                                                  const OldWindowDrawView& view);

// 0x498480: con la mezcla normal, arrastrada o cambiando de tamaño ubica a sus hijas (sin el 9); arriba, el negro (el
// 7) de los dos renglones y las rayas del 2 debajo de cada uno; abajo, el negro y la raya de los botones; el 1000 y el
// 1005 alineados a la derecha en el ancho del 1000 (el modo 3 de 0x467060); los tres EDIT (0x468320); sin mezcla, los
// botones +0xa44 y +0xacc y, con +0xb0 en 1, +0xb54 y +0xbdc.
[[nodiscard]] Core::Expected<void, std::string> DrawTheWriteWindow(OldWindowManager& manager, OldWindow& write,
                                                                   const OldWindowDrawContext& context,
                                                                   const OldWindowDrawView& view);

// 0x4a5537..0x4a5739, desde 0x4a5490 con la mezcla normal: con chats en +0xb4 y la lista a la vista, los que no están
// minimizados; con alguno, el fondo (el 3) y el borde (el 15) con el parpadeo, de (renglón + 4) · cuántos de alto sobre
// +0x104, y sus renglones (0x4a5750): el elegido y los que esperan con su franja (el 0x10 y el 0x11), el amigo del
// título (strtok y 0x459c70) y, con más de dos miembros, la marca de win_mark.tga (0x64c) en rojo. El error es el de
// 0x459c70.
[[nodiscard]] Core::Expected<void, std::string> DrawTheChatsInTheList(OldWindowManager& manager,
                                                                      const OldWindow& window,
                                                                      const OldWindowDrawContext& context,
                                                                      const OldWindowDrawView& view);

// La vtable + 0x54 de las de una carta, al final del marco: 0x49a1d0 (la de leer, con +0x774 en 2 o más) y 0x498800
// (la de escribir, con +0xb0 en 1) dibujan el retrato (su vtable + 0x14, 0x497c10) con OldWindowDrawView::portrait,
// que da el juego (Scenes::DrawTheLetterPortrait); sin él, corta. Sin el retrato, nada.
[[nodiscard]] Core::Expected<void, std::string> DrawTheLetterPortrait(OldWindow& window, const OldWindowDrawView& view);
} // namespace Client::UI
