#pragma once

#include "Core/Expected.h"
#include "UI/LetterModel.h"
#include "UI/OldWindows.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// El chat con un amigo y las cartas en las ventanas viejas (CL9 hito 10; 06 § HUD y ventanas, "Las cartas y el chat
// con un amigo"): lo que los paquetes (Scenes/GameLetters) y la de amigos le piden a las ventanas de los casos 1, 2, 6
// y 7 de 0x48ee60, a la lista de ventanas de [0x5763524] y a la pestaña de las cartas. La vtable de esas ventanas
// está en UI/LetterWindows.cpp, detrás de UI/OldWindowSteps.h.
namespace Client::UI
{
// Text.bmd.
inline constexpr std::size_t ChatTitleText = 994;
inline constexpr std::size_t ChatClosedText = 995;
inline constexpr std::size_t ChatLeftText = 402;
inline constexpr std::size_t LetterReadTitleText = 1054;
// 0x41ed10: un mensaje en un chat que no está a la vista (0x4a5e50).
inline constexpr std::int32_t ChatWaitingSound = 0x28;
// El primer estado fuera de línea de un amigo para el chat (0x4942d0 y 0x4dc81e miran desde 0xfd; 0xfc es el de todos).
inline constexpr std::uint8_t FirstOfflineFriendState = 0xfd;

// 0x4a6430: el chat con el amigo `name` entre los de la lista de ventanas (0x495530 de 10 bytes, salteando los de un
// tipo 0 o 2); −1 si el amigo de uno está vacío; 0 sin ninguno.
[[nodiscard]] std::int32_t FindTheChatWith(OldWindowManager& manager, std::string_view name, std::string_view hero);
// 0x4a6af0, 0x4a6910 y 0x4a6bc0: los amigos con el C1 CA pedido (+0x158).
[[nodiscard]] bool IsAPendingChat(const OldWindowManager& manager, std::string_view name);
void AddAPendingChat(OldWindowManager& manager, std::string_view name);
void ForgetAPendingChat(OldWindowManager& manager, std::string_view name);
// 0x4a4990: el chat al final de +0xb4.
void AddTheChatToTheList(OldWindowManager& manager, std::int32_t id);
// 0x4a5e50: el sonido 0x28 y, si no estaba, el chat al final de +0x118.
void MarkTheChatWaiting(OldWindowManager& manager, std::int32_t id, std::optional<std::int32_t>& sound);
// 0x495530: el amigo del chat (el primer miembro que no es el héroe con menos de 3; tipo 1), ninguno (tipo 0) o la sala
// de 3 o más (tipo 2).
[[nodiscard]] std::string ChatFriend(const OldWindow& chat, std::string_view hero, std::int32_t& kind);
// 0x493f80: un renglón con el nombre del miembro `speaker` (0x461530; 0xff o ninguno, vacío).
void AddAChatLine(OldWindow& chat, std::uint8_t speaker, std::string_view text, std::int32_t kind, std::int32_t extra);
// 0x493c90: el miembro nuevo o su número y estado; el título (994 y los nombres, 0x461600), la pestaña de las ventanas
// (0x4914f0), 0x495630(0) con 2 o más y la lista de renglones más angosta con 3 o más. Da cuántos miembros hay.
std::int32_t AddAChatMember(OldWindowManager& manager, OldWindow& chat, std::string_view name, std::uint8_t index,
                            std::uint8_t state, const OldWindowTexts& texts, std::string_view hero);
// 0x493e90: con 3 o más, saca al miembro y rehace el título; con menos de 3 después, la lista de renglones ancha.
void RemoveAChatMember(OldWindowManager& manager, OldWindow& chat, std::string_view name, const OldWindowTexts& texts,
                       std::string_view hero);
// 0x4942d0: los amigos que se pueden invitar: los del modelo, menos los miembros y los que no están en línea (0xfd+).
void FillTheInvites(OldWindowManager& manager, OldWindow& chat, std::string_view hero);
// 0x495630: con 1, el EDIT apagado y el título con el 995 adelante; con 0, el EDIT prendido y el título sin él.
void CloseTheChat(OldWindow& chat, std::int32_t closed, const OldWindowTexts& texts);
// 0x4a6840 y 0x4a75c0 sobre los chats de +0xb4: los que tienen la lista de invitar la rehacen; todos reciben el 402 y
// se cierran.
void RefreshTheInvites(OldWindowManager& manager, std::string_view hero);
void CloseTheChats(OldWindowManager& manager, const OldWindowTexts& texts);
// 0x4a0960 sobre la pestaña de las cartas de la de amigos, si está.
void RefreshTheLettersTab(OldWindowManager& manager);
// 0x499cb0: la carta (0x138 bytes a +0x778) y el cuerpo partido por '\n' (strtok, sin los vacíos) a la lista de +0x8b0,
// con el 0xe.
void SetTheLetter(OldWindowManager& manager, OldWindow& read, const LetterEntry& entry, std::string_view body);
// 0x493a70, desde el destructor del chat (0x493160): el C1 03 01 por la conexión de la sala (0x493510) y 0x49dfd0.
[[nodiscard]] Core::Expected<void, std::string> LeaveTheChatRoom(OldWindowManager& manager, const OldWindow& chat);
// 0x4a6550, el timer 1002 (WM_TIMER, 0x4b1a5b): cada chat de +0xb4 de la lista de ventanas, en su orden, que esté
// (0x490a00) y tenga la conexión de su sala (0x493510), le manda el C1 03 05.
void PingTheChatRooms(OldWindowManager& manager);
// Lo que el dibujo del chat (0x493fc0), de la de leer (0x499d60) y de la de escribir (0x498480) hace antes de dibujar:
// arrastradas o cambiando de tamaño (el estado en 4 o 1), el 8 de 0x45aa50 a sus hijas y, cambiando de tamaño, el 9 a
// sus listas (la de escribir no tiene listas ni manda el 9). El dibujo en sí está en UI/LetterWindowsDraw.
void SettleTheLetterKindsForTheDraw(OldWindowManager& manager, OldWindow& window);

// 0x497890 (la vtable + 0x40 del retrato): con el personaje armado, sus ángulos (Scenes, PortraitCallKind::SetAngle) y
// +0x6ac y +0x6b0 en `angle`; sin armar, nada.
[[nodiscard]] Core::Expected<void, std::string>
SetThePortraitAngle(const OldWindowStepView* view, OldPortraitPart& portrait, float angle, std::string_view where);
// 0x4978d0 (la vtable + 0x44): +0x6b8 y +0x6bc en `zoom`.
void SetThePortraitZoom(OldPortraitPart& portrait, float zoom);
} // namespace Client::UI
