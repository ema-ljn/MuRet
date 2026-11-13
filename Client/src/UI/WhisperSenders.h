#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Los que susurraron mientras el nivel propio era menor que 10 (CL8 hito 3; 06 § El chat, Q-C453): la lista de la
// .bss en 0x77997f8, que llena 0x554d20 al recibir un 02 y lee 0x554a40 al mandar uno. Son diez lugares de 10 bytes
// seguidos, que strcmp y strcpy leen y escriben como textos: un nombre de 10 bytes deja su '\0' en el lugar que sigue
// y se lee seguido de él.
namespace Client::UI
{
inline constexpr std::size_t WhisperSenderSlots = 10;
inline constexpr std::size_t WhisperSenderSlotBytes = 10;
// 0x555030 vacía 110 bytes (0x1b DWORD y un WORD): los diez lugares y los 10 que siguen, donde cae el '\0' del
// último.
inline constexpr std::size_t WhisperSendersBytes = 110;
// 0x554d20(10, nombre): el nivel con el que deja de anotar.
inline constexpr std::uint16_t WhisperSenderLevel = 10;

struct WhisperSenders
{
    std::array<char, WhisperSendersBytes> bytes{};
    // 0x77f3b9c: el lugar que se escribe después, de 0 a 9.
    std::int32_t next = 0;
};

// El lugar `slot` como lo lee strcmp: hasta su '\0', que puede estar en el lugar que sigue.
[[nodiscard]] std::string_view WhisperSenderAt(const WhisperSenders& senders, std::size_t slot);

// 0x554f89..0x55501f, con el nivel ya leído: con menos de 10, si ningún lugar tiene el nombre (strcmp), lo copia
// con strcpy en el próximo y lo avanza de 0 a 9. `name` va hasta su '\0'; MuRet lo corta en los bytes que quedan
// desde el lugar (Q-C512).
void RememberTheWhisperSender(WhisperSenders& senders, std::uint16_t level, std::string_view name);

// 0x555030: los 110 bytes en 0; el próximo lugar queda.
void ClearTheWhisperSenders(WhisperSenders& senders);
} // namespace Client::UI
