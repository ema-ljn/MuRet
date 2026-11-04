#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Los avisos del login, por su número (CL5.5; 06 § Los avisos). 0x6431b0 elige el texto de cada uno
// con una tabla de saltos; el resultado del F1 01 elige el aviso en 0x4e5ebc y 0x4e5f04, y el OK de
// cada uno sigue en 0x643560. Todos los del login son del tipo 2: un OK. Los textos van por índice
// de Text.bmd: acá no hay ninguno escrito.
namespace Client::UI
{
// El número del aviso, el que recibe 0x65ee60.
using NoticeId = std::uint8_t;

// Los que abre el login por su cuenta.
// El servidor de la lista con carga de 100 a 127 (0x645e10): el texto 416.
inline constexpr NoticeId ServerFullNotice = 0x63;
// La cuenta vacía y la clave vacía (0x641bf0): el 403 y el 404.
inline constexpr NoticeId EmptyAccountNotice = 0x64;
inline constexpr NoticeId EmptyPasswordNotice = 0x65;
// La versión del F1 00 que no es la del cliente (0x4c1e20): el 405 y el 406.
inline constexpr NoticeId VersionMismatchNotice = 0x6f;
// No conectó, o el F1 00 no aceptó (0x4c1e20): el 402.
inline constexpr NoticeId ConnectFailedNotice = 0x70;

// El de la selección: entrar con un personaje bloqueado (el bit 0x1 de sus banderas, 0x6342c0): el
// 434 (CL6.3).
inline constexpr NoticeId LockedCharacterNotice = 0x84;

// Los de crear y borrar (CL6.5; 06 § La selección de personaje, "Los avisos de la selección"):
// las respuestas del F3 01 (0x4c2290) y del F3 02 (0x4e2b10), la espera, las revisiones del botón de
// borrar (0x63edf0) y las del nombre (0x63d990).
inline constexpr NoticeId CreateRefusedNotice = 0x36;    // 1716, el F3 01 con 0
inline constexpr NoticeId CharacterLimitNotice = 0x37;   // 396, el F3 01 con 2
inline constexpr NoticeId PleaseWaitNotice = 0x5d;       // 471, sin botones, después de mandar
inline constexpr NoticeId ConfirmDeletionNotice = 0x5e;  // 1712 con el nombre, OK y cancelar
inline constexpr NoticeId DeletionCodeNotice = 0x5f;     // 1713, con el campo del código
inline constexpr NoticeId CharacterDeletedNotice = 0x60; // 1714, el F3 02 con 1
inline constexpr NoticeId BlockedAccountNotice = 0x61;   // 417, banderas con 0x12
inline constexpr NoticeId BlockedCharacterNotice = 0x62; // 439, el F3 02 con 3
inline constexpr NoticeId ForbiddenNameNotice = 0x72;    // 1715, un nombre filtrado
inline constexpr NoticeId ShortNameNotice = 0x79;        // 390, menos de 4 bytes
inline constexpr NoticeId SymbolNameNotice = 0x7a;       // 391, un carácter fuera de rango
inline constexpr NoticeId HighLevelNotice = 0x83;        // 1711, nivel 80 o más en otro grupo
inline constexpr NoticeId InvalidCodeNotice = 0x89;      // 401, el F3 02 con otro
inline constexpr NoticeId GuildMemberNotice = 0x92;      // 1654, en un gremio, o el F3 02 con 0

// El tipo de 0x6431b0, que ubica los botones (0x642bf0) y dice qué hacen Enter y Esc (0x642e30).
enum class NoticeKind : std::uint8_t
{
    NoButton, // 0: ni Enter ni Esc lo cierran
    Ok,       // 2: OK, Enter y Esc lo dan
    OkCancel, // 3: OK y Enter lo dan; cancelar y Esc lo cierran sin darlo
    Code,     // 4: como el 3, con el campo del código
};

[[nodiscard]] NoticeKind KindOf(NoticeId notice);

// Los textos de un aviso: uno, o dos líneas.
struct NoticeTexts
{
    std::size_t first = 0;
    std::optional<std::size_t> second;
};

// Los de 0x6431b0 para los avisos del login; los demás, nada.
[[nodiscard]] std::optional<NoticeTexts> TextsOf(NoticeId notice);

// El aviso del resultado del F1 01 (0x4e5ebc y 0x4e5f04), o nada con el 1, el aceptado.
[[nodiscard]] std::optional<NoticeId> NoticeOfLoginResult(std::uint8_t result);

// El de la respuesta del F3 01 (0x4c2290): 0x36 con 0 y 0x37 con 2; nada con 1 (creado) y con otro.
[[nodiscard]] std::optional<NoticeId> NoticeOfCreateResult(std::uint8_t result);

// El de la respuesta del F3 02 (0x4e2b10): 0x92 con 0, 0x60 con 1, 0x62 con 3 y 0x89 con otro.
[[nodiscard]] NoticeId NoticeOfDeleteResult(std::uint8_t result);

// Lo que pasa con el OK de un aviso (0x643560).
enum class NoticeFollowUp : std::uint8_t
{
    // Solo se cierra.
    Close,
    // Se abre la cuenta con el foco en la cuenta, en el estado 2.
    ReopenAtAccount,
    // Se abre la cuenta con el foco en la clave, en el estado 2.
    ReopenAtPassword,
    // El juego se cierra: PostMessage con WM_CLOSE (Q-C69).
    Quit,
    // El de la confirmación del borrado (0x5e): se abre el del código (0x5f).
    AskForTheCode,
    // El del código (0x5f): se manda el F3 02 con lo que se escribió (0x643740) y se abre el 0x5d.
    SendTheDeletion,
};

[[nodiscard]] NoticeFollowUp FollowUpOf(NoticeId notice);
} // namespace Client::UI
