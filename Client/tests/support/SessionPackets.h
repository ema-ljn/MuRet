#pragma once

#include "Protocol/Connection/ServerList.h"

#include "support/Bytes.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

// Los paquetes de la sesión con los servidores, escritos a mano como los dice el 04 (El flujo de
// conexión): lo que manda el cliente, para compararlo con lo que salió, y lo que contestan el
// ConnectServer y el GameServer, para el guion del FakeSocket. Los usan los tests de la
// ServerSession (CL4.3 y CL4.4), del AutoLogin y de la LoginScene con la sesión (CL5.5).
namespace Client::Tests
{
// Lo que mide el host en la respuesta F4 03.
inline constexpr std::size_t AddressHostSize = 16;

// El pedido de la información de la mascota, como llega al cable con su XOR32.
inline std::vector<std::byte> PetInfoRequest()
{
    return Bytes({0xC1, 0x06, 0xA9, 0x20, 0x9C, 0x2F});
}

// El saludo del ConnectServer.
inline std::vector<std::byte> Hello()
{
    return Bytes({0xC1, 0x04, 0x00, 0x01});
}

// El pedido de la lista, F4 02.
inline std::vector<std::byte> ListRequest()
{
    return Bytes({0xC1, 0x04, 0xF4, 0x02});
}

// Pega varios paquetes, como llegan en una lectura o como salen en `sent`.
inline std::vector<std::byte> Join(std::initializer_list<std::vector<std::byte>> parts)
{
    std::vector<std::byte> joined;
    for (const std::vector<std::byte>& part : parts)
    {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return joined;
}

// La lista como la lee main.exe (F4 06): la cantidad en dos bytes big-endian y, por servidor, el
// código en little-endian, la carga y un byte que no se lee.
inline std::vector<std::byte> ServerListWithLoads(std::span<const Protocol::Connection::ServerEntry> entries)
{
    const std::size_t size = 7 + 4 * entries.size();
    std::vector<std::byte> bytes =
        Bytes({0xC2, static_cast<int>(size >> 8), static_cast<int>(size & 0xFF), 0xF4, 0x06,
               static_cast<int>(entries.size() >> 8), static_cast<int>(entries.size() & 0xFF)});
    for (const Protocol::Connection::ServerEntry& entry : entries)
    {
        const std::vector<std::byte> written = Bytes({entry.code & 0xFF, entry.code >> 8, entry.load, 0x00});
        bytes.insert(bytes.end(), written.begin(), written.end());
    }
    return bytes;
}

// La lista con la carga en 0.
inline std::vector<std::byte> ServerList(std::initializer_list<std::uint16_t> codes)
{
    std::vector<Protocol::Connection::ServerEntry> entries;
    for (const std::uint16_t code : codes)
    {
        entries.push_back(Protocol::Connection::ServerEntry{code, 0});
    }
    return ServerListWithLoads(entries);
}

// La respuesta F4 03: el host en 16 bytes con \0 detrás y el puerto en little-endian.
inline std::vector<std::byte> AddressReply(std::string_view host, std::uint16_t port)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x16, 0xF4, 0x03});
    const std::span<const std::byte> text = BytesOf(host);
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.resize(4 + AddressHostSize);
    const std::vector<std::byte> portBytes = Bytes({port & 0xFF, port >> 8});
    bytes.insert(bytes.end(), portBytes.begin(), portBytes.end());
    return bytes;
}

// La respuesta F4 05 de un servidor ocupado, con el byte del servidor, que nadie lee.
inline std::vector<std::byte> BusyReply()
{
    return Bytes({0xC1, 0x05, 0xF4, 0x05, 0x00});
}

// El saludo del GameServer (F1 00): el resultado, el índice del jugador (5, en big-endian) y la
// versión, 5 bytes sin \0.
inline std::vector<std::byte> GameHello(int result, std::string_view version)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x0C, 0xF1, 0x00, result, 0x00, 0x05});
    const std::span<const std::byte> text = BytesOf(version);
    bytes.insert(bytes.end(), text.begin(), text.end());
    return bytes;
}

// La respuesta del login (F1 01).
inline std::vector<std::byte> LoginResult(int result)
{
    return Bytes({0xC1, 0x05, 0xF1, 0x01, result});
}

// La respuesta de la salida (F1 02): con 0, la orden de cerrar el juego (Q-C106).
inline std::vector<std::byte> LogoutResult(int result)
{
    return Bytes({0xC1, 0x05, 0xF1, 0x02, result});
}
} // namespace Client::Tests
