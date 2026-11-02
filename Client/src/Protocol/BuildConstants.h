// Las constantes del build c32bbd5f que viajan en el protocolo, en un solo lugar.
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión" (F1 00 y F1 01), y el plan
// de la CL2 (CL2.5).
//
// En el original están guardadas cada una por su lado: la versión en 0x6c8284, ofuscada, y el
// serial como texto, en el emisor del login (0x641bf0). El cliente del muproxy.log del
// laboratorio es de otro build: manda otra versión, 09908, en el mismo lugar del login.

#pragma once

#include <cstddef>
#include <string_view>

namespace Client::Protocol
{

// Lo que mide la versión en el cable, sin \0: el saludo F1 00 la trae en +7 y el login F1 01 la
// manda en +28.
inline constexpr std::size_t VersionSize = 5;

// La versión del cliente. En 0x6c8284 está guardada con cada byte más su índice más 1
// (32 32 35 34 3E), y 0x4c1e20 la compara así, byte a byte, con la que trae el saludo.
inline constexpr std::string_view ClientVersion = "10209";
static_assert(ClientVersion.size() == VersionSize);

// Lo que mide el serial en el login, sin \0: va en +33, al final.
inline constexpr std::size_t SerialSize = 16;

// El serial del cliente, que el GameServer compara con el suyo (en el research del servidor,
// PMSG_IDPASS de gameserver/src/reconstructed/).
inline constexpr std::string_view ClientSerial = "MULORDSVERSAO102";
static_assert(ClientSerial.size() == SerialSize);

} // namespace Client::Protocol
