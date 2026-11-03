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
};

[[nodiscard]] NoticeFollowUp FollowUpOf(NoticeId notice);
} // namespace Client::UI
