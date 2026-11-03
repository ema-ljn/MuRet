#include "Network/Connection.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"
#include "Network/Delivery.h"
#include "Network/EncryptedPacket.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Client::Network::Connection contra el cable del laboratorio (support/WireCapture.h,
// MURET_CLIENT_WIRE_DIR) y las claves reales del cliente, Enc1.dat y Dec2.dat (el plan de la CL4,
// CL4.4, PR A). Lo que contestó el GameServer, leído por un FakeSocket con las lecturas del cable,
// sale descifrado como lo dan DecryptPacket y el BeforeDispatch de FoxAnti. Lo que mandó el
// cliente original, descifrado con Dec1.dat, la del servidor, y mandado otra vez Encrypted, sale
// con los bytes del cable salvo el último bloque, que lleva el relleno. Cada test usa una sola
// Connection y la abre de nuevo para cada conexión del log: los seriales vuelven a 0 en cada Open.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable ni de lo descifrado, ni un
// aviso del log, que dice el opcode.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::Connection;
using Client::Network::ConnectionKeys;
using Client::Network::DecryptPacket;
using Client::Network::Delivery;
using Client::Network::Event;
using Client::Network::FoxAntiObfuscation;
using Client::Network::IdentityObfuscation;
using Client::Network::Opened;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::Undecryptable;
using Client::Network::Crypto::CipherBlockSize;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;
using Client::Tests::CutPackets;
using Client::Tests::FakeClock;
using Client::Tests::FakeSocket;
using Client::Tests::LoadKeyFile;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::RecordingSink;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

// El log que escribió el proxy del laboratorio.
constexpr std::string_view CaptureFile = "muproxy.log";

// El puerto del proxy delante del GameServer del laboratorio: los C3/C4 van y vienen por ahí.
constexpr std::uint16_t GameServerPort = 55901;

// Adónde abre la Connection: una dirección cualquiera, porque el FakeSocket no conecta.
constexpr std::string_view GameHost = "10.0.0.2";

// La semilla del relleno: fija, aunque acá el relleno no se compara.
constexpr std::uint32_t PaddingSeed = 1;

// Cuántos Poll tiene una conexión para leer todo lo suyo, de sobra: el log más largo no llega a
// mil lecturas, y cada Poll hace hasta ocho.
constexpr int MaxPolls = 1000;

// Dónde está un paquete, para los mensajes: su conexión en el log y su orden entre los de ella.
std::string Where(std::size_t connection, std::size_t index)
{
    return "conexión " + std::to_string(connection) + ", paquete " + std::to_string(index);
}

bool IsEncrypted(const Packet& packet)
{
    return packet.Type() == PacketType::C3 || packet.Type() == PacketType::C4;
}

// Las claves de los dos tests: las de la Connection, del cliente, y la del servidor para descifrar
// lo que manda el cliente.
struct Keys
{
    ConnectionKeys client; // Enc1.dat y Dec2.dat
    DecryptionKey server;  // Dec1.dat
};

// Cuántos C3/C4 llegaron del GameServer, y en cuántas conexiones.
struct EncryptedCount
{
    std::size_t c3 = 0;
    std::size_t c4 = 0;
    std::size_t connections = 0;
};

// Cada C3/C4 de lo que cortó el test (`cut`), descifrado con `decryption` y pasado por el
// BeforeDispatch de `foxAnti`, contra lo que dio la Connection en su lugar (`delivered`), con los
// seriales en orden desde 0. Los suma a `count`.
void ExpectDecryptedLikeDecryptPacket(const std::vector<Packet>& cut, const std::vector<Packet>& delivered,
                                      const DecryptionKey& decryption, const FoxAntiObfuscation& foxAnti,
                                      std::size_t index, EncryptedCount& count)
{
    ASSERT_EQ(delivered.size(), cut.size()) << "conexión " << index;
    PacketSerials serials;
    bool anyEncrypted = false;
    for (std::size_t packet = 0; packet < cut.size(); ++packet)
    {
        if (!IsEncrypted(cut[packet]))
        {
            continue;
        }
        anyEncrypted = true;
        if (cut[packet].Type() == PacketType::C3)
        {
            ++count.c3;
        }
        else
        {
            ++count.c4;
        }
        auto decrypted = DecryptPacket(cut[packet], decryption, serials);
        ASSERT_TRUE(decrypted.HasValue()) << Where(index, packet);
        EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << Where(index, packet);
        auto expected = foxAnti.BeforeDispatch(std::move(decrypted).Value().packet);
        ASSERT_TRUE(expected.HasValue()) << Where(index, packet);
        EXPECT_TRUE(SameBytesWithoutValues(delivered[packet].Bytes(), expected.Value().Bytes()))
            << Where(index, packet);
    }
    count.connections += anyEncrypted ? 1 : 0;
}

// Lo que mandó el cliente original en una conexión y lo que mandó la Connection con eso.
struct Resent
{
    std::vector<Packet> wire;  // los C3/C4 del cable
    std::vector<Packet> plain; // los mismos, descifrados con Dec1.dat
    std::vector<Packet> mine;  // lo que mandó la Connection, cortado
};

// Cada `mine` contra su `wire`: los mismos bytes salvo el último bloque, el del relleno, y,
// descifrado con `server`, el mismo `plain`, con los seriales en orden desde 0.
void ExpectLikeTheWire(const Resent& resent, const DecryptionKey& server, std::size_t index)
{
    ASSERT_EQ(resent.mine.size(), resent.wire.size()) << "conexión " << index;
    PacketSerials serverSerials;
    for (std::size_t packet = 0; packet < resent.wire.size(); ++packet)
    {
        const std::span<const std::byte> mine = resent.mine[packet].Bytes();
        const std::span<const std::byte> wire = resent.wire[packet].Bytes();
        ASSERT_EQ(mine.size(), wire.size()) << Where(index, packet);
        const std::size_t beforeLast = wire.size() - CipherBlockSize;
        EXPECT_TRUE(SameBytesWithoutValues(mine.first(beforeLast), wire.first(beforeLast))) << Where(index, packet);
        const auto again = DecryptPacket(resent.mine[packet], server, serverSerials);
        ASSERT_TRUE(again.HasValue()) << Where(index, packet);
        EXPECT_EQ(again.Value().serial, SerialCheck::InOrder) << Where(index, packet);
        EXPECT_TRUE(SameBytesWithoutValues(again.Value().packet.Bytes(), resent.plain[packet].Bytes()))
            << Where(index, packet);
    }
}

class GoldenConnection : public Client::Tests::GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        // Los if con FAIL, y no ASSERT_TRUE, para que clang-tidy vea que el optional se miró.
        const auto path = WireFile(CaptureFile);
        if (!path)
        {
            FAIL() << "no está " << CaptureFile;
        }
        const auto file = ReadFileBytes(*path);
        if (!file)
        {
            FAIL() << "no se pudo leer " << CaptureFile;
        }
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": la línea " << parsed.Error().line << " no se entiende";
        m_connections = std::move(parsed).Value();

        // Las del cliente, de la carpeta de datos, y la del servidor, de la del cable
        // (support/GoldenDir.h).
        const auto enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        const auto dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        const auto dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        if (!enc1 || !dec2 || !dec1)
        {
            FAIL() << "no cargan las claves";
        }
        m_keys.emplace(Keys{ConnectionKeys{*enc1, *dec2}, *dec1});
    }

    // Abre `connection` al GameServer con las claves del cliente, y el Poll que da el Opened.
    void OpenTheGameServer(Connection& connection, const ConnectionKeys& keys, std::size_t index)
    {
        connection.Open(GameHost, GameServerPort, keys);
        const std::vector<Event> opened = connection.Poll();
        ASSERT_EQ(opened.size(), 1u) << "conexión " << index;
        ASSERT_TRUE(std::holds_alternative<Opened>(opened.front())) << "conexión " << index;
    }

    // Los Poll hasta que el FakeSocket no tenga nada más que dar, y uno más: los paquetes que
    // salieron, en orden. Cualquier otro evento hace fallar el test.
    std::vector<Packet> PollEverything(Connection& connection, std::size_t index)
    {
        std::vector<Packet> packets;
        for (int poll = 0; poll < MaxPolls; ++poll)
        {
            const bool wasEmpty = m_socket.reads.empty();
            std::vector<Event> events = connection.Poll();
            for (Event& event : events)
            {
                if (Packet* packet = std::get_if<Packet>(&event))
                {
                    packets.push_back(std::move(*packet));
                }
                else
                {
                    ADD_FAILURE() << "conexión " << index << ": "
                                  << (std::holds_alternative<Undecryptable>(event) ? "un Undecryptable"
                                                                                   : "un evento que no es un paquete");
                }
            }
            if (wasEmpty)
            {
                return packets;
            }
        }
        ADD_FAILURE() << "conexión " << index << ": quedó algo sin leer";
        return packets;
    }

    // Descifra con `server` cada C3/C4 que mandó el original en la conexión `index` (`sent`) y lo
    // manda otra vez por `connection`, Encrypted: deja en `resent` lo del cable y lo que salió.
    void ResendEncrypted(Connection& connection, const std::vector<Packet>& sent, const DecryptionKey& server,
                         std::size_t index, Resent& resent)
    {
        const std::size_t sentBefore = m_socket.sent.size();
        PacketSerials wireSerials;
        for (const Packet& packet : sent)
        {
            if (!IsEncrypted(packet))
            {
                continue;
            }
            auto decrypted = DecryptPacket(packet, server, wireSerials);
            ASSERT_TRUE(decrypted.HasValue()) << Where(index, resent.wire.size());
            connection.Send(decrypted.Value().packet, Delivery::Encrypted);
            resent.wire.push_back(packet);
            resent.plain.push_back(std::move(decrypted).Value().packet);
        }
        const std::vector<std::byte> mine(m_socket.sent.begin() + static_cast<std::ptrdiff_t>(sentBefore),
                                          m_socket.sent.end());
        resent.mine = CutPackets({std::span<const std::byte>(mine)}, index);
    }

    // Cuántas líneas de aviso dejó la conexión: se cuentan y no se imprimen.
    [[nodiscard]] std::size_t WarningCount() const
    {
        return static_cast<std::size_t>(std::ranges::count(m_sink.levels, LogLevel::Warning));
    }

    std::vector<WireConnection> m_connections;
    std::optional<Keys> m_keys; // con un valor si SetUp no falló
    FakeSocket m_socket;
    FakeClock m_clock;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
};

// Los 25 C3/C4 que contestó el GameServer (18 C3 y 7 C4) en 7 conexiones, con el 0xAA de FoxAnti:
// la Connection los da descifrados, igual que DecryptPacket con Dec2.dat y el BeforeDispatch de
// FoxAnti, con los seriales en orden desde 0 en cada Open, sin un Undecryptable y sin avisos. Lo
// que llegó en claro sale también, en su lugar.
TEST_F(GoldenConnection, DeliversWhatTheGameServerAnswered)
{
    if (!m_keys)
    {
        FAIL() << "SetUp no cargó las claves";
    }
    const Keys& keys = *m_keys;
    const FoxAntiObfuscation foxAnti;
    Connection connection(m_socket, foxAnti, m_clock, m_log, PaddingSeed);
    EncryptedCount count;

    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != GameServerPort)
        {
            continue;
        }
        const std::vector<std::span<const std::byte>> reads = m_connections[index].Reads(WireDirection::ServerToClient);
        const std::vector<Packet> cut = CutPackets(reads, foxAnti, index);
        ASSERT_NO_FATAL_FAILURE(OpenTheGameServer(connection, keys.client, index));
        for (const std::span<const std::byte> read : reads)
        {
            m_socket.reads.emplace_back(read.begin(), read.end());
        }
        const std::vector<Packet> delivered = PollEverything(connection, index);
        ASSERT_NO_FATAL_FAILURE(
            ExpectDecryptedLikeDecryptPacket(cut, delivered, keys.client.decryption, foxAnti, index, count));
    }
    EXPECT_EQ(count.c3, 18u);
    EXPECT_EQ(count.c4, 7u);
    EXPECT_EQ(count.connections, 7u);
    EXPECT_EQ(WarningCount(), 0u);
}

// Los 183 C3 que mandó el cliente original en sus 8 conexiones al GameServer, descifrados con
// Dec1.dat y mandados otra vez por la Connection, Encrypted y sin ofuscación (lo descifrado ya
// trae el BeforeEncrypt de FoxAnti). Salen con Enc1.dat y el serial del original, desde 0 en cada
// Open: la cabecera y los bloques menos el último son los del cable. El último lleva el relleno,
// que el original saca de rand(), y detrás lo que tenía en la pila; descifrado con Dec1.dat, el
// paquete es el mismo, con el serial en orden.
TEST_F(GoldenConnection, EncryptsLikeTheClient)
{
    if (!m_keys)
    {
        FAIL() << "SetUp no cargó las claves";
    }
    const Keys& keys = *m_keys;
    Connection connection(m_socket, IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed);
    std::size_t total = 0;

    for (std::size_t index = 0; index < m_connections.size(); ++index)
    {
        if (m_connections[index].port != GameServerPort)
        {
            continue;
        }
        const std::vector<Packet> sent = CutPackets(m_connections[index].Reads(WireDirection::ClientToServer), index);
        ASSERT_NO_FATAL_FAILURE(OpenTheGameServer(connection, keys.client, index));
        Resent resent;
        ASSERT_NO_FATAL_FAILURE(ResendEncrypted(connection, sent, keys.server, index, resent));
        ASSERT_NO_FATAL_FAILURE(ExpectLikeTheWire(resent, keys.server, index));
        total += resent.wire.size();
    }
    EXPECT_EQ(total, 183u);
    EXPECT_EQ(WarningCount(), 0u);
}
} // namespace
