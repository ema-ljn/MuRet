#pragma once

#include "Core/Expected.h"
#include "UI/ChatInput.h"
#include "UI/OldWindows.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// La ventana de amigos (CL9 hito 9; 06 § HUD y ventanas, "Los amigos"): lo que el juego y los paquetes C0..C4 le
// piden al administrador de las ventanas viejas. La ventana, sus pestañas y la caja de agregar un amigo son clases
// del administrador (OldWindowKind) y corren por su paso (UI/OldWindows); acá está lo que se llama de afuera.
namespace Client::UI
{
// La x y la y de la primera vez (0x6599a0 con el panel 1, 0x659f3d, y 0x4906a0).
inline constexpr std::int32_t FriendsWindowX = 0x186;
inline constexpr std::int32_t FriendsWindowY = 0x106;
// +0xb0 de la de amigos, que es también el lugar en OldWindow::tabs: los amigos, las cartas y las ventanas.
inline constexpr std::int32_t FriendTabNumber = 0;
inline constexpr std::int32_t LettersTabNumber = 1;
inline constexpr std::int32_t WindowsTabNumber = 2;
inline constexpr std::size_t FriendsTabCount = 3;
// 0x491080: el nivel que hace falta (+0xe del registro propio) y el texto si no se tiene.
inline constexpr std::uint16_t FriendsLevel = 6;
inline constexpr std::size_t FriendsLevelText = 1067;
// 0x491080: el cartel de ayuda de Text.bmd (0x77183ac) en la cola normal (tipo 2), a 2,5 y en 0xffc8dce6.
inline constexpr std::size_t FriendsHelpText = 1244;

// 0x491080 desde el nivel: con la de amigos oculta (3), la muestra (0x490670) con los sonidos 0x19 y 0x1c; a la vista
// (0), la suelta (0x4914e0); con otro estado, nada; sin ella, la arma (0x48ee60(3)) en (x, y) la primera vez o donde
// estaba, la sube, anota las ventanas (0x4914f0) y, si estaba, le devuelve el tamaño y lo estirado, la acomoda y corre
// un paso entero de las ventanas (0x490490). Devuelve si la armó: entonces 0x491080 pone el cartel de ayuda (0x46b720)
// si las colas están quietas.
[[nodiscard]] Core::Expected<bool, std::string> OpenTheFriendsWindow(OldWindowManager& manager, std::int32_t x,
                                                                     std::int32_t y, const OldWindowGlobals& globals,
                                                                     const OldWindowStepView& view);

// 0x49ddd0 sobre la pestaña de amigos (+0xb8) de la ventana, si está: copia el modelo a la lista (0x49bd60) y deja
// elegido al último con el nombre del que estaba elegido.
void RefreshTheFriendTab(OldWindowManager& manager);

// 0x4918d0: +0xe8 y el título, 990 con la lista o 1066 sin ella; cambia el de la ventana si está.
void MarkTheFriendList(OldWindowManager& manager, std::int32_t received, const OldWindowTexts& texts);

// 0x4a4440: el nombre del que pidió ser amigo, en la caja del C2.
void RememberTheFriendRequest(OldWindowManager& manager, std::int32_t box, std::string_view name);

// El EDIT de las ventanas viejas con el foco (SetFocus de 0x4682a0): lo que escribe el jugador (WM_CHAR), el borrar
// y Enter, que la subclase 0x467b10 le manda a su padre como el 7 si la lista de ventanas no está armada (+0x150).
[[nodiscard]] bool HasAnOldEditFocused(const OldWindowManager& manager, ChatFocus focus);
void TypeInTheOldEdit(OldWindowManager& manager, std::string_view typed);
void BackspaceInTheOldEdit(OldWindowManager& manager);
void PressEnterInTheOldEdit(OldWindowManager& manager);

// Lo que el dibujo de la ventana de amigos (0x4a2620), de sus pestañas (0x49cc20 y 0x49e7f0) y de la caja de agregar
// (0x4a3750) hace antes de dibujar: arrastradas o cambiando de tamaño, vuelven a ubicar y medir a sus hijas (el 8 y el
// 9 con 0x45aa50), y la de amigos les pasa el estado. El dibujo en sí está en UI/FriendWindowsDraw.
void SettleTheFriendsForTheDraw(OldWindowManager& manager, OldWindow& window);
} // namespace Client::UI
