#pragma once

#include "Network/Connection.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/LoopbackServer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// El ConnectServer y el GameServer falsos de la integración de la CL4.4 (el plan de la CL4, PR C):
// dos LoopbackServer en 127.0.0.1, con los puertos que elige el sistema, que el test consulta en
// el mismo hilo que el cliente. Siguen el guion de 04-protocolo.md, "El flujo de conexión", con
// los bytes del cable del laboratorio y su FoxAnti: el CS manda la lista en la forma del
// laboratorio y espera la elección ya pasada al código 0, y el GS manda todo cifrado y con el
// 0xAA en cada byte. El F4 03 da 127.0.0.1 y el puerto del GS falso, no la dirección del cable.
// Después del login, el GS solo acepta keepalives (CL4.5), y los cuenta.
//
// Cada uno contesta solo lo que sigue en el guion. Con algo que no está en el guion, el servidor lo
// anota en Complaints(), sin un byte, corta al cliente y deja de contestar: la sesión falla
// enseguida, sin esperar el tope de tiempo, y el test dice qué vio el servidor. Necesita un
// SocketApi::NetworkInit vivo.
namespace Client::Tests
{
class FakeServers
{
public:
    // Las claves del lado del servidor (GameServerKeys() de TestGameServer.h): con ellas el GS
    // descifra el login y cifra el saludo y el resultado.
    explicit FakeServers(Network::ConnectionKeys serverKeys);

    [[nodiscard]] std::uint16_t ConnectServerPort() const;
    [[nodiscard]] std::uint16_t GameServerPort() const;

    // Una vuelta de los dos: acepta, lee lo que mandó el cliente y contesta lo que toca.
    void Poll();

    // Si el GS recibió un login que descifra, con el serial 0, y lo aceptó.
    [[nodiscard]] bool SawLogin() const;
    // Ese login, descifrado y todavía con su XOR32, para compararlo con WriteLoginRequest con
    // SameBytesWithoutValues; vacío hasta que llega.
    [[nodiscard]] const std::optional<Network::Packet>& Login() const;
    // Los keepalives que le llegaron al GS después del login, cifrados y en orden.
    [[nodiscard]] std::size_t Keepalives() const;
    // Si el CS, ya contestada la dirección, vio que el cliente cortó (04: "el cliente corta con el
    // CS").
    [[nodiscard]] bool ClientLeftTheConnectServer() const;
    // Lo que vieron fuera del guion, en palabras; vacío si todo fue como dice el 04.
    [[nodiscard]] const std::vector<std::string>& Complaints() const;

private:
    enum class ConnectStep : std::uint8_t
    {
        WaitingClient,
        WaitingPetInfoRequest, // el A9
        WaitingListRequest,    // el F4 02
        WaitingServerSelect,   // el F4 03
        Answered,              // la dirección salió: lo que sigue es el corte
        Stopped,               // algo fuera del guion: cortó al cliente
    };

    enum class GameStep : std::uint8_t
    {
        WaitingClient,
        WaitingLogin, // el saludo salió
        LoggedIn,     // el resultado salió: lo que sigue son keepalives
        Stopped,      // algo fuera del guion: cortó al cliente
    };

    void PollConnectServer();
    void PollGameServer();
    void OnConnectServerPacket(const Network::Packet& packet);
    void OnGameServerPacket(const Network::Packet& packet);
    void OnKeepalive(const Network::Packet& packet);

    // El próximo paquete de `reader`, o std::nullopt si no hay uno entero o si el corte falló.
    [[nodiscard]] std::optional<Network::Packet> NextPacket(Network::PacketReader& reader, bool& failed);
    // Si `packet` es `expected`; si no, el CS para con una queja que nombra `what`.
    bool Expect(const Network::Packet& packet, const std::vector<std::byte>& expected, std::string_view what);
    // Anotan `complaint`, cortan al cliente y dejan de contestar.
    void StopConnectServer(std::string complaint);
    void StopGameServer(std::string complaint);
    // Lo que contesta el GS: cifrado con su clave y sus seriales, y con el 0xAA de FoxAnti.
    void SendFromTheGameServer(const std::vector<std::byte>& plain);

    Network::ConnectionKeys m_keys;
    LoopbackServer m_connectServer;
    LoopbackServer m_gameServer;
    Network::PacketReader m_connectReader;
    Network::PacketReader m_gameReader;
    Network::PacketSerials m_gameSerials; // los dos sentidos del GS, desde 0
    ConnectStep m_connectStep = ConnectStep::WaitingClient;
    GameStep m_gameStep = GameStep::WaitingClient;
    std::optional<Network::Packet> m_login;
    std::size_t m_keepalives = 0;
    std::vector<std::string> m_complaints;
};
} // namespace Client::Tests
