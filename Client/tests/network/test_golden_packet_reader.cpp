#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "Network/PacketReader.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// El PacketReader contra el cable del laboratorio (support/WireCapture.h, MURET_CLIENT_WIRE_DIR):
// cada lectura entra tal como la devolvió recv, y lo que sale tiene que ser el flujo entero,
// cortado en paquetes, sin bytes de más.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes por tipo y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable.
//
// El S->C del GameServer no está: cada byte lleva el XOR 0xAA de FoxAnti, y se corta con
// FoxAntiObfuscation en test_golden_fox_anti_obfuscation.cpp.
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
    std::vector<std::size_t> sizes;   // lo que midió cada paquete, en orden
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
            cut.sizes.push_back(bytes.size());
            ++cut.counts[next.Value()->Type()];
        }
    }
    cut.leftover = reader.Pending();
    return cut;
}

// El mismo flujo de a un byte: cada cabecera llega en varios Feed.
std::vector<std::span<const std::byte>> OneByteReads(std::span<const std::byte> stream)
{
    std::vector<std::span<const std::byte>> reads;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        reads.push_back(stream.subspan(i, 1));
    }
    return reads;
}

// El mismo flujo en lecturas que terminan `into` bytes adentro del paquete siguiente: cada una trae
// el resto de un paquete y el principio del otro. `sizes` son los paquetes del flujo, en orden.
std::vector<std::span<const std::byte>>
ReadsEndingInsideTheNext(std::span<const std::byte> stream, const std::vector<std::size_t>& sizes, std::size_t into)
{
    std::vector<std::span<const std::byte>> reads;
    std::size_t start = 0;
    std::size_t boundary = 0;
    for (std::size_t k = 0; k + 1 < sizes.size(); ++k)
    {
        boundary += sizes[k];
        const std::size_t end = boundary + std::min(into, sizes[k + 1]);
        reads.push_back(stream.subspan(start, end - start));
        start = end;
    }
    reads.push_back(stream.subspan(start));
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
        m_connections = std::move(parsed).Value();
    }

    // Corta el sentido `direction` de cada conexión al puerto `port`, y suma los tipos. Cada corte
    // tiene que terminar sin error, sin bytes de más, y con los paquetes iguales al flujo.
    TypeCounts CutAll(std::uint16_t port, WireDirection direction, std::size_t& cutConnections) const
    {
        TypeCounts total;
        cutConnections = 0;
        for (std::size_t index = 0; index < m_connections.size(); ++index)
        {
            const WireConnection& connection = m_connections[index];
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

    // Los sentidos que van en claro: lo que mandó el cliente, a los dos servidores, y lo que
    // contestó el ConnectServer, que tiene los C2.
    template <typename Check> void ForEachClearDirection(Check check) const
    {
        for (std::size_t index = 0; index < m_connections.size(); ++index)
        {
            const WireConnection& connection = m_connections[index];
            {
                SCOPED_TRACE("C->S");
                check(index, connection, WireDirection::ClientToServer);
            }
            if (connection.port == ConnectServerPort)
            {
                SCOPED_TRACE("S->C");
                check(index, connection, WireDirection::ServerToClient);
            }
        }
    }

    std::vector<WireConnection> m_connections;
};

// Que `resplit` dé los mismos paquetes que `asRead`, el flujo tal como llegó. Sin imprimir bytes.
void ExpectTheSamePackets(const Cut& resplit, const Cut& asRead, std::size_t index)
{
    EXPECT_EQ(resplit.error, std::nullopt) << "conexión " << index;
    EXPECT_EQ(resplit.leftover, 0u) << "conexión " << index;
    EXPECT_EQ(resplit.counts, asRead.counts) << "conexión " << index;
    EXPECT_TRUE(resplit.sizes == asRead.sizes) << "conexión " << index << ": otros cortes";
    EXPECT_TRUE(SameBytesWithoutValues(resplit.rejoined, asRead.stream)) << "conexión " << index;
}

TEST_F(GoldenPacketReaderCapture, HasTheLabConnections)
{
    std::size_t connectServer = 0;
    std::size_t gameServer = 0;
    for (const WireConnection& connection : m_connections)
    {
        connectServer += connection.port == ConnectServerPort ? 1 : 0;
        gameServer += connection.port == GameServerPort ? 1 : 0;
    }

    EXPECT_EQ(m_connections.size(), 72u);
    EXPECT_EQ(connectServer, 64u);
    EXPECT_EQ(gameServer, 8u);
}

// Todo lo que mandó el cliente original: al ConnectServer, 80 F4 en claro (72 F4 02 y 8 F4 03) y
// 64 A9 con el XOR32; al GameServer, el login y lo demás, con los C3 sin descifrar.
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

// De a un byte salen los mismos paquetes: el lector espera cada cabecera partida. El original acá
// no falla, porque con menos de 3 bytes no corta (0x4e8927) y nunca le quedan dos paquetes en una
// pasada.
TEST_F(GoldenPacketReaderCapture, OneByteAtATimeGivesTheSamePackets)
{
    ForEachClearDirection(
        [](std::size_t index, const WireConnection& connection, WireDirection direction)
        {
            const Cut asRead = CutReads(connection.Reads(direction));
            ExpectTheSamePackets(CutReads(OneByteReads(asRead.stream)), asRead, index);
        });
}

// Lecturas que terminan 1 y 2 bytes adentro del paquete siguiente: es lo que en el original dispara
// Q-C03, una cabecera incompleta detrás de un paquete entero de la misma lectura. Con 2, a los C2
// del ConnectServer les falta el tercer byte de la cabecera.
TEST_F(GoldenPacketReaderCapture, ReadsEndingInsideTheNextHeaderGiveTheSamePackets)
{
    for (const std::size_t into : {std::size_t{1}, std::size_t{2}})
    {
        ForEachClearDirection(
            [into](std::size_t index, const WireConnection& connection, WireDirection direction)
            {
                const Cut asRead = CutReads(connection.Reads(direction));
                const Cut resplit = CutReads(ReadsEndingInsideTheNext(asRead.stream, asRead.sizes, into));
                SCOPED_TRACE(into == 1 ? "1 byte adentro" : "2 bytes adentro");
                ExpectTheSamePackets(resplit, asRead, index);
            });
    }
}
} // namespace
