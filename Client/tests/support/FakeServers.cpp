#include "support/FakeServers.h"

#include "Network/Crypto/Xor32.h"
#include "Network/EncryptedPacket.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Opcodes.h"

#include "support/Bytes.h"
#include "support/Packets.h"

#include <algorithm>
#include <format>
#include <span>
#include <utility>

namespace Client::Tests
{
namespace
{
// El host que da el F4 03: el del GS falso.
constexpr std::string_view Loopback = "127.0.0.1";
// Lo que mide el host en el F4 03, con su \0 (04 § El flujo de conexión).
constexpr std::size_t AddressHostSize = 16;
// El índice del jugador que da el saludo del GS: uno cualquiera.
constexpr int PlayerIndex = 5;
// El XOR que el GS del laboratorio le aplica a cada byte de lo que manda (03 § La capa de FoxAnti).
constexpr std::byte FoxAntiXor{0xAA};
// El largo del login, sin cifrar (04: "login (49 B lógicos)").
constexpr std::size_t LoginSize = 0x31;
// El del keepalive: C1 0C 0E 00, el tick y las dos velocidades (04).
constexpr std::size_t KeepaliveSize = 0x0C;
// Dónde está el subcódigo de un C1: después del tipo, el largo y el opcode.
constexpr std::size_t SubcodeIndex = 3;

// Lo que manda el cliente, como llega al cable del 04: el A9 con su XOR32, el F4 02, y el F4 03
// con el código 400 que FoxAnti pasó a 0.
std::vector<std::byte> PetInfoRequest()
{
    return Bytes({0xC1, 0x06, 0xA9, 0x20, 0x9C, 0x2F});
}

std::vector<std::byte> ListRequest()
{
    return Bytes({0xC1, 0x04, 0xF4, 0x02});
}

std::vector<std::byte> ServerSelect()
{
    return Bytes({0xC1, 0x06, 0xF4, 0x03, 0x00, 0x00});
}

// Lo que contesta el CS: el saludo, la lista del laboratorio (un servidor, el 0) y la dirección.
std::vector<std::byte> ConnectHello()
{
    return Bytes({0xC1, 0x04, 0x00, 0x01});
}

std::vector<std::byte> LabServerList()
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
}

std::vector<std::byte> AddressReply(std::uint16_t port)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x16, 0xF4, 0x03});
    const std::span<const std::byte> host = BytesOf(Loopback);
    bytes.insert(bytes.end(), host.begin(), host.end());
    bytes.resize(bytes.size() + AddressHostSize - host.size());
    const std::vector<std::byte> portBytes = Bytes({port & 0xFF, port >> 8});
    bytes.insert(bytes.end(), portBytes.begin(), portBytes.end());
    return bytes;
}

// Lo que contesta el GS, en claro: el saludo aceptado, con la versión del cliente, y el login
// aceptado.
std::vector<std::byte> GameHello()
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x0C, 0xF1, 0x00, 0x01, 0x00, PlayerIndex});
    const std::span<const std::byte> version = BytesOf(Protocol::ClientVersion);
    bytes.insert(bytes.end(), version.begin(), version.end());
    return bytes;
}

std::vector<std::byte> LoginAccepted()
{
    return Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01});
}

bool IsEncrypted(const Network::Packet& packet)
{
    return packet.Type() == Network::PacketType::C3 || packet.Type() == Network::PacketType::C4;
}

// Si `plain`, sin su XOR32, es un F1 01 del largo del login.
bool IsLoginRequest(const Network::Packet& plain)
{
    const Network::Packet clear = Network::Crypto::Xor32Decode(plain);
    const std::span<const std::byte> bytes = clear.Bytes();
    return clear.Type() == Network::PacketType::C1 && bytes.size() == LoginSize &&
           clear.Opcode() == Protocol::Connection::JoinOpcode &&
           std::to_integer<std::uint8_t>(bytes[SubcodeIndex]) == Protocol::Connection::LoginSubcode;
}
// Si `plain`, sin su XOR32, es un keepalive (0E) de su largo.
bool IsKeepalive(const Network::Packet& plain)
{
    const Network::Packet clear = Network::Crypto::Xor32Decode(plain);
    return clear.Type() == Network::PacketType::C1 && clear.Bytes().size() == KeepaliveSize &&
           clear.Opcode() == Protocol::Connection::KeepaliveOpcode;
}
} // namespace

FakeServers::FakeServers(Network::ConnectionKeys serverKeys) : m_keys(serverKeys) {}

std::uint16_t FakeServers::ConnectServerPort() const
{
    return m_connectServer.Port();
}

std::uint16_t FakeServers::GameServerPort() const
{
    return m_gameServer.Port();
}

void FakeServers::Poll()
{
    PollConnectServer();
    PollGameServer();
}

bool FakeServers::SawLogin() const
{
    return m_gameStep == GameStep::LoggedIn;
}

const std::optional<Network::Packet>& FakeServers::Login() const
{
    return m_login;
}

std::size_t FakeServers::Keepalives() const
{
    return m_keepalives;
}

bool FakeServers::ClientLeftTheConnectServer() const
{
    return m_connectStep == ConnectStep::Answered && m_connectServer.ClientClosed();
}

const std::vector<std::string>& FakeServers::Complaints() const
{
    return m_complaints;
}

// El CS no saluda al aceptar: espera el A9, como en el cable del 04. Un solo cliente: no acepta
// otro, porque Accept soltaría el que tiene.
void FakeServers::PollConnectServer()
{
    if (m_connectStep == ConnectStep::WaitingClient)
    {
        if (!m_connectServer.Accept())
        {
            return;
        }
        m_connectStep = ConnectStep::WaitingPetInfoRequest;
    }
    if (m_connectStep == ConnectStep::Stopped || m_connectServer.ClientClosed())
    {
        return;
    }
    m_connectReader.Feed(m_connectServer.Read());
    bool failed = false;
    while (m_connectStep != ConnectStep::Stopped)
    {
        const std::optional<Network::Packet> packet = NextPacket(m_connectReader, failed);
        if (failed)
        {
            StopConnectServer("el ConnectServer no puede cortar lo que mandó el cliente");
        }
        if (!packet)
        {
            return;
        }
        OnConnectServerPacket(*packet);
    }
}

// El GS saluda primero, apenas acepta.
void FakeServers::PollGameServer()
{
    if (m_gameStep == GameStep::WaitingClient)
    {
        if (!m_gameServer.Accept())
        {
            return;
        }
        m_gameStep = GameStep::WaitingLogin;
        SendFromTheGameServer(GameHello());
    }
    if (m_gameStep == GameStep::Stopped || m_gameServer.ClientClosed())
    {
        return;
    }
    m_gameReader.Feed(m_gameServer.Read());
    bool failed = false;
    while (m_gameStep != GameStep::Stopped)
    {
        const std::optional<Network::Packet> packet = NextPacket(m_gameReader, failed);
        if (failed)
        {
            StopGameServer("el GameServer no puede cortar lo que mandó el cliente");
        }
        if (!packet)
        {
            return;
        }
        OnGameServerPacket(*packet);
    }
}

void FakeServers::OnConnectServerPacket(const Network::Packet& packet)
{
    switch (m_connectStep)
    {
    case ConnectStep::WaitingPetInfoRequest:
        if (Expect(packet, PetInfoRequest(), "el A9"))
        {
            m_connectServer.Write(ConnectHello());
            m_connectStep = ConnectStep::WaitingListRequest;
        }
        return;
    case ConnectStep::WaitingListRequest:
        if (Expect(packet, ListRequest(), "el pedido de la lista (F4 02)"))
        {
            m_connectServer.Write(LabServerList());
            m_connectStep = ConnectStep::WaitingServerSelect;
        }
        return;
    case ConnectStep::WaitingServerSelect:
        if (Expect(packet, ServerSelect(), "la elección del servidor 0 (F4 03)"))
        {
            m_connectServer.Write(AddressReply(m_gameServer.Port()));
            m_connectStep = ConnectStep::Answered;
        }
        return;
    case ConnectStep::Answered:
        StopConnectServer("el ConnectServer recibió un paquete después de dar la dirección");
        return;
    case ConnectStep::WaitingClient:
    case ConnectStep::Stopped:
        return;
    }
}

// El login llega como C3 y se descifra con la clave del servidor, con el serial 0. Lo que sigue
// son keepalives.
void FakeServers::OnGameServerPacket(const Network::Packet& packet)
{
    if (m_gameStep == GameStep::LoggedIn)
    {
        OnKeepalive(packet);
        return;
    }
    if (!IsEncrypted(packet))
    {
        StopGameServer("el GameServer esperaba el login cifrado y llegó un paquete en claro");
        return;
    }
    auto decrypted = Network::DecryptPacket(packet, m_keys.decryption, m_gameSerials);
    if (!decrypted.HasValue())
    {
        StopGameServer("el GameServer no descifra lo que llegó con su clave");
        return;
    }
    if (decrypted.Value().serial != Network::SerialCheck::InOrder)
    {
        StopGameServer("el login no trae el serial 0");
        return;
    }
    if (!IsLoginRequest(decrypted.Value().packet))
    {
        StopGameServer("el GameServer esperaba el login (F1 01) y llegó otra cosa");
        return;
    }
    m_login = std::move(decrypted).Value().packet;
    m_gameStep = GameStep::LoggedIn;
    SendFromTheGameServer(LoginAccepted());
}

// Después del login, lo único que manda el cliente es el keepalive, cifrado y con el serial que
// sigue: el primero, el 1.
void FakeServers::OnKeepalive(const Network::Packet& packet)
{
    if (!IsEncrypted(packet))
    {
        StopGameServer("el GameServer esperaba un keepalive cifrado y llegó un paquete en claro");
        return;
    }
    const auto decrypted = Network::DecryptPacket(packet, m_keys.decryption, m_gameSerials);
    if (!decrypted.HasValue() || decrypted.Value().serial != Network::SerialCheck::InOrder)
    {
        StopGameServer("el GameServer no descifra en orden lo que llegó después del login");
        return;
    }
    if (!IsKeepalive(decrypted.Value().packet))
    {
        StopGameServer("el GameServer recibió después del login algo que no es un keepalive");
        return;
    }
    ++m_keepalives;
}

std::optional<Network::Packet> FakeServers::NextPacket(Network::PacketReader& reader, bool& failed)
{
    auto next = reader.Next();
    if (!next.HasValue())
    {
        failed = true;
        return std::nullopt;
    }
    return std::move(next).Value();
}

bool FakeServers::Expect(const Network::Packet& packet, const std::vector<std::byte>& expected, std::string_view what)
{
    if (std::ranges::equal(packet.Bytes(), expected))
    {
        return true;
    }
    StopConnectServer(std::format("el ConnectServer esperaba {} del 04 y llegó otra cosa", what));
    return false;
}

void FakeServers::StopConnectServer(std::string complaint)
{
    m_complaints.push_back(std::move(complaint));
    m_connectServer.CloseClient();
    m_connectStep = ConnectStep::Stopped;
}

void FakeServers::StopGameServer(std::string complaint)
{
    m_complaints.push_back(std::move(complaint));
    m_gameServer.CloseClient();
    m_gameStep = GameStep::Stopped;
}

void FakeServers::SendFromTheGameServer(const std::vector<std::byte>& plain)
{
    auto encrypted = Network::EncryptPacket(PacketOf(plain), m_keys.encryption, m_gameSerials);
    if (!encrypted.HasValue())
    {
        StopGameServer("el GameServer no pudo cifrar lo que contesta");
        return;
    }
    std::vector<std::byte> bytes = std::move(encrypted).Value().TakeBytes();
    for (std::byte& value : bytes)
    {
        value ^= FoxAntiXor;
    }
    m_gameServer.Write(bytes);
}
} // namespace Client::Tests
