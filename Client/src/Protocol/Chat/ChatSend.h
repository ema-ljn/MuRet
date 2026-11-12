#pragma once

#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Lo que manda la entrada del chat (CL8 hito 3; 04, "El chat"): el chat (C1 00) y el susurro (C1 02), como los arma
// el WndProc al apretar Enter (0x4b5b0d y 0x4b70a7) y las macros (0x5a1388): el C1 con el XOR32 desde el byte 3 y en
// claro por send(), sin la cola de los cifrados (Xor32C1). Del lado del GameServer, PMSG_CHATDATA y
// PMSG_CHATDATA_WHISPER: el nombre en +3 y el texto en +0xd, de hasta 60 bytes (CGChatRecv y CGChatWhisperRecv en
// la reconstrucción del GS). *Estático*.
//
// Lo que lleva lo escribió el jugador: no va al log.
namespace Client::Protocol::Chat
{
inline constexpr std::uint8_t ChatSendOpcode = 0x00;
inline constexpr std::uint8_t WhisperSendOpcode = 0x02;
// El nombre del que habla (+0x1d9 del héroe) o el del destinatario (0x7798cf8): diez bytes, con lo que haya
// detrás del '\0'.
inline constexpr std::size_t ChatNameSize = 10;
// 0x4b7aaf y 0x5a1515: el texto con su '\0' si entra en menos de 0x3c; si no, sus primeros 0x3c bytes, sin el
// '\0'.
inline constexpr std::size_t ChatTextLimit = 0x3c;

// Los bytes del texto que van en el paquete: `text` (hasta su '\0') y el '\0', cortados en ChatTextLimit.
[[nodiscard]] std::size_t ChatTextBytes(std::string_view text);

// C1 (0xd + n) 00, el nombre y el texto.
[[nodiscard]] Outgoing WriteChat(std::span<const std::byte, ChatNameSize> name, std::string_view text);
// C1 (0xd + n) 02, el destinatario y el texto.
[[nodiscard]] Outgoing WriteWhisper(std::span<const std::byte, ChatNameSize> target, std::string_view text);
} // namespace Client::Protocol::Chat
