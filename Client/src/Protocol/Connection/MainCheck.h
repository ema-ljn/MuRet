#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// El 03 del GameServer: la clave con la que el cliente suma su propio ejecutable, y la suma que
// vuelve. El GS la manda al entrar al mundo (GCCheckMainExeKeySend, desde gObjSetCharacter) y, si en
// tres segundos no llega la respuesta, cierra la conexión (gObjSecondProc, `CheckSumTime`).
// Contrato: Client/docs/research/04-protocolo.md, "La suma del ejecutable (03)", y Q-C319 en
// 07-quirks-y-bugs.md. Estático, ASM c32bbd5f.
namespace Client::Protocol::Connection
{
// La clave: C1 06 03, un byte de relleno y la clave (u16 little-endian) en pkt[4..5], que 0x4e16c0
// lee sin mirar el tamaño (0x4e1843..0x4e1846). WrongPacket si no es un C1 03, y TooShort si no
// trae la clave: el original la lee igual, de lo que haya en su buffer.
[[nodiscard]] Core::Expected<std::uint16_t, ProtocolError> ReadMainCheckKey(const Network::Packet& packet);

// 0x4ad3d0: la tabla que esconde la clave. El GS la arma con la tabla en los bits 0..3 y 6..15 y seis
// bits de rand() en el medio (EncryptCheckSumKey), y la cifra con 0xB479; el cliente la descifra y
// junta los dos pedazos.
[[nodiscard]] std::uint16_t MainCheckTable(std::uint16_t key);

// 0x4ad400: la suma de la imagen del ejecutable con la tabla. Recorre de a cuatro bytes (u32
// little-endian) desde el principio mientras el offset no pase de tamaño − 4, así que los bytes que
// sobran al final no cuentan. Con menos de cuatro bytes el original lee después del buffer (el
// tamaño − 4 da la vuelta sin signo): MuRet devuelve vacío (Q-C319).
[[nodiscard]] Core::Expected<std::uint32_t, std::string> MainCheckSum(std::span<const std::byte> image,
                                                                      std::uint16_t table);

// 0x4ad270: el nombre del ejecutable a partir de la línea de comandos (GetCommandLineA en 0x4ad4d6):
// lo que sigue a la última barra invertida de toda la línea (si hay alguna), cortado en el primer
// espacio, comilla, barra o barra invertida. Es relativo a la carpeta actual, donde lo abre
// CreateFileA (0x4ad51b y 0x4aec13).
[[nodiscard]] std::string MainCheckFileName(std::string_view commandLine);

// La respuesta, 0x4e16c0: C1 08 03, el byte 0 que alinea la suma y la suma (u32 little-endian), con
// el XOR32 y para cifrar (sale como C3, 0x4e1ad6).
[[nodiscard]] Outgoing WriteMainCheckReply(std::uint32_t sum);
} // namespace Client::Protocol::Connection
