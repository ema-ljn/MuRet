// El PacketReader contra el cable del laboratorio (support/WireCapture.h, MURET_CLIENT_WIRE_DIR):
// cada lectura entra tal como la devolvió recv, y lo que sale tiene que ser el flujo entero,
// cortado en paquetes, sin bytes de más.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes por tipo y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable.
//
// El S->C del GameServer no está: lleva el 0xAA de FoxAnti delante de cada paquete, y se corta en
// la CL2.4.

#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "Network/PacketReader.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketType;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using GoldenPacketReader = Client::Tests::WireDataTest;

// El log que escribió el proxy del laboratorio.
constexpr std::string_view CaptureFile = "muproxy.log";

// Los puertos del proxy: delante del ConnectServer y del GameServer del laboratorio.
constexpr std::uint16_t ConnectServerPort = 44405;
constexpr std::uint16_t GameServerPort = 55901;

// Los cuatro tipos, de C1 a C4.
constexpr std::size_t PacketTypeCount = 4;

// Cuántos paquetes de cada tipo, en el orden C1, C2, C3, C4.
struct TypeCounts
{
    std::array<std::size_t, PacketTypeCount> byType{};

    std::size_t& operator[](PacketType type)
    {
        return byType[static_cast<std::size_t>(type) - static_cast<std::size_t>(PacketType::C1)];
    }
    bool operator==(const TypeCounts&) const = default;

    // Para los mensajes de GoogleTest, que sin esto imprimen la memoria del struct.
    friend void PrintTo(const TypeCounts& counts, std::ostream* out)
    {
        *out << "C1=" << counts.byType[0] << " C2=" << counts.byType[1] << " C3=" << counts.byType[2]
             << " C4=" << counts.byType[3];
    }
};

// Lo que dio cortar un sentido de una conexión.
struct Cut
{
    TypeCounts counts;
    std::size_t leftover = 0;         // bytes que llegaron y no salieron en un paquete
    std::optional<PacketError> error; // el primero, si hubo
    std::vector<std::byte> stream;    // todo lo que entró, en orden
    std::vector<std::byte> rejoined;  // los paquetes que salieron, uno detrás del otro
};

// Corta `reads` con un PacketReader: un Feed por lectura, y todos los paquetes que haya después de
// cada una.
Cut CutReads(const std::vector<std::span<const std::byte>>& reads)
{
    PacketReader reader;
    Cut cut;
    for (const std::span<const std::byte> read : reads)
    {
        cut.stream.insert(cut.stream.end(), read.begin(), read.end());
        reader.Feed(read);
        while (true)
        {
            auto next = reader.Next();
            if (!next.HasValue())
            {
                cut.error = next.Error();
                return cut;
            }
            if (!next.Value())
            {
                break;
            }
            const auto bytes = next.Value()->Bytes();
            cut.rejoined.insert(cut.rejoined.end(), bytes.begin(), bytes.end());
            ++cut.counts[next.Value()->Type()];
        }
    }
    cut.leftover = reader.Pending();
    return cut;
}

// El mismo flujo de a un byte: la cabecera llega partida en cada paquete (Q-C03).
std::vector<std::span<const std::byte>> OneByteReads(std::span<const std::byte> stream)
{
    std::vector<std::span<const std::byte>> reads;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        reads.push_back(stream.subspan(i, 1));
    }
    return reads;
}

TypeCounts Counts(std::size_t c1, std::size_t c2, std::size_t c3, std::size_t c4)
{
    return TypeCounts{{c1, c2, c3, c4}};
}

class GoldenPacketReaderCapture : public GoldenPacketReader
{
protected:
    void SetUp() override
    {
        GoldenPacketReader::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        const auto path = WireFile(CaptureFile);
        ASSERT_TRUE(path.has_value());
        const auto file = ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value()) << "no se pudo leer " << CaptureFile;
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": la línea " << parsed.Error().line << " no se entiende";
        connections = std::move(parsed).Value();
    }

    // Corta el sentido `direction` de cada conexión al puerto `port`, y suma los tipos. Cada corte
    // tiene que terminar sin error, sin bytes de más, y con los paquetes iguales al flujo.
    TypeCounts CutAll(std::uint16_t port, WireDirection direction, std::size_t& cutConnections) const
    {
        TypeCounts total;
        cutConnections = 0;
        for (std::size_t index = 0; index < connections.size(); ++index)
        {
            const WireConnection& connection = connections[index];
            if (connection.port != port)
            {
                continue;
            }
            ++cutConnections;
            const Cut cut = CutReads(connection.Reads(direction));
            EXPECT_EQ(cut.error, std::nullopt) << "conexión " << index;
            EXPECT_EQ(cut.leftover, 0u) << "conexión " << index;
            EXPECT_TRUE(SameBytesWithoutValues(cut.rejoined, cut.stream)) << "conexión " << index;
            for (std::size_t type = 0; type < PacketTypeCount; ++type)
            {
                total.byType[type] += cut.counts.byType[type];
            }
        }
        return total;
    }

    std::vector<WireConnection> connections;
};

TEST_F(GoldenPacketReaderCapture, HasTheLabConnections)
{
    std::size_t connectServer = 0;
    std::size_t gameServer = 0;
    for (const WireConnection& connection : connections)
    {
        connectServer += connection.port == ConnectServerPort ? 1 : 0;
        gameServer += connection.port == GameServerPort ? 1 : 0;
    }

    EXPECT_EQ(connections.size(), 72u);
    EXPECT_EQ(connectServer, 64u);
    EXPECT_EQ(gameServer, 8u);
}

// Todo lo que mandó el cliente original: al ConnectServer, los F4 en claro; al GameServer, el login
// y lo demás, con los C3 sin descifrar.
TEST_F(GoldenPacketReaderCapture, CutsWhatTheClientSent)
{
    std::size_t cutConnections = 0;

    EXPECT_EQ(CutAll(ConnectServerPort, WireDirection::ClientToServer, cutConnections), Counts(144, 0, 0, 0));
    EXPECT_EQ(cutConnections, 64u);
    EXPECT_EQ(CutAll(GameServerPort, WireDirection::ClientToServer, cutConnections), Counts(362, 0, 183, 0));
    EXPECT_EQ(cutConnections, 8u);
}

// Lo que el ConnectServer le contestó, en claro: la bienvenida y la lista o la dirección de un
// servidor.
TEST_F(GoldenPacketReaderCapture, CutsWhatTheConnectServerAnswered)
{
    std::size_t cutConnections = 0;

    EXPECT_EQ(CutAll(ConnectServerPort, WireDirection::ServerToClient, cutConnections), Counts(72, 72, 0, 0));
    EXPECT_EQ(cutConnections, 64u);
}

// De a un byte salen los mismos paquetes: cada cabecera llega partida, que es Q-C03 en el
// original.
TEST_F(GoldenPacketReaderCapture, OneByteAtATimeGivesTheSamePackets)
{
    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        const WireConnection& connection = connections[index];
        const Cut asRead = CutReads(connection.Reads(WireDirection::ClientToServer));
        const Cut byByte = CutReads(OneByteReads(asRead.stream));

        EXPECT_EQ(byByte.error, std::nullopt) << "conexión " << index;
        EXPECT_EQ(byByte.leftover, 0u) << "conexión " << index;
        EXPECT_EQ(byByte.counts, asRead.counts) << "conexión " << index;
        EXPECT_TRUE(SameBytesWithoutValues(byByte.rejoined, asRead.rejoined)) << "conexión " << index;
    }
}

} // namespace
