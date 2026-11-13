#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <string>
#include <vector>

// La lista de servidores del ConnectServer: pedirla (F4 02), leerla (F4 06), elegir uno (F4 03) y
// leer adónde conectarse (la respuesta F4 03).
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión", y el plan de la CL2
// (CL2.5).
//
// Lo que se manda al ConnectServer sale en claro, sin el XOR32: 0x4c6070 arma el pedido sin el
// motor de envío, y el F4 03 llega al cable con su 03 sin tocar. Lo que se lee es lo que deja la
// capa de FoxAnti (CL2.4): la lista llega al cable como F4 02 y FoxAnti la pasa a F4 06, y el
// código que se elige vuelve a ella para que lo traduzca de vuelta.
namespace Client::Protocol::Connection
{
// F4 02: C1 04 F4 02, en claro.
[[nodiscard]] Outgoing WriteServerListRequest();

// C1 04 F4 06, en claro, sin rand(): lo arman el botón de los créditos del login (0x641203) y su cierre
// (0x63f9a0). Qué contesta el CS no está leído (08, P-C78).
[[nodiscard]] Outgoing WriteCreditsNotice();

// Una entrada de la lista.
struct ServerEntry
{
    std::uint16_t code; // el que se elige con F4 03; con FoxAnti, el del ConnectServer más su delta
    std::uint8_t load;  // la carga del servidor
};

// F4 06: C2, la cantidad en dos bytes big-endian (0x4c1860 la arma como pkt[5] * 0x100 + pkt[6]) y
// una entrada de 4 bytes por servidor: el código (u16 little-endian), la carga y un byte que no se
// lee. TooShort si la cantidad dice más entradas que las que entran en el paquete; lo que sobra
// detrás de la última no se lee.
[[nodiscard]] Core::Expected<std::vector<ServerEntry>, ProtocolError> ReadServerList(const Network::Packet& packet);

// F4 03: C1 06 F4 03 y el código, u16 little-endian, en claro. main.exe manda el de la lista (400
// para el 0 del laboratorio) y FoxAnti lo pasa de vuelta al del ConnectServer.
[[nodiscard]] Outgoing WriteServerSelect(std::uint16_t code);

// Adónde conectarse: el GameServer elegido.
struct ServerAddress
{
    std::string host;
    std::uint16_t port;
};

// La respuesta F4 03: C1 16 F4 03, el host en 16 bytes y el puerto, u16 little-endian. Del host se
// leen los primeros 15 bytes, como el original (0x4e3341), hasta el primer \0, o los 15 si no hay
// ninguno: el byte 16 no se lee nunca.
[[nodiscard]] Core::Expected<ServerAddress, ProtocolError> ReadServerAddress(const Network::Packet& packet);
} // namespace Client::Protocol::Connection
