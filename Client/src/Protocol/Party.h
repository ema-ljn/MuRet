#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// 08 P-C92 (CL8 hito 6): la party. Por ahora solo el pedido de la lista, que 0x6599a0(4) manda al abrir la ventana.
// La respuesta (C1 42, 0x4d67e0) y el resto de la party son de la CL9.4.
namespace Client::Protocol::Party
{
inline constexpr std::uint8_t PartyListOpcode = 0x42;

// 0x659aa6..0x659b94: C1 03 42 armado en la pila y mandado con send si el socket de 0x6c82a0 (+0xc) no es −1. No
// pasa por el XOR32 ni por el cifrado del C3: va en claro.
[[nodiscard]] Outgoing WritePartyListRequest();
} // namespace Client::Protocol::Party
