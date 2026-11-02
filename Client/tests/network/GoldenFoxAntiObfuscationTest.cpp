// FoxAntiObfuscation contra el cable del laboratorio (support/WireCapture.h,
// MURET_CLIENT_WIRE_DIR): lo que contestaron el ConnectServer y el GameServer, cortado con el 0xAA
// por paquete, y las traducciones de la lista, del F3 11 y del código de servidor que eligió el
// cliente. Plan de la CL2, CL2.4, paso 4.
//
// El cable lleva el login del jugador: estos tests cuentan paquetes y comparan con
// SameBytesWithoutValues. Ningún mensaje imprime un byte del cable ni de lo descifrado.

#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"
#include "Network/EncryptedPacket.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/Bytes.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using Client::Network::DecryptPacket;
using Client::Network::FoxAntiObfuscation;
using Client::Network::Packet;
using Client::Network::PacketReader;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Tests::Bytes;
using Client::Tests::LoadKeyFile;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using ByteVector = std::vector<std::byte>;

// El log que escribió el proxy del laboratorio.
constexpr std::string_view CaptureFile = "muproxy.log";

// Los puertos del proxy: delante del ConnectServer y del GameServer del laboratorio.
constexpr std::uint16_t ConnectServerPort = 44405;
constexpr std::uint16_t GameServerPort = 55901;

// El XOR del laboratorio en lo que manda el GameServer.
constexpr std::byte LabMask{0xAA};

// Lo que dio cortar un sentido de una conexión con FoxAnti.
struct Cut
{
    std::vector<Packet> packets;
    std::size_t masked = 0;   // cuántos llegaron con el 0xAA
    std::size_t leftover = 0; // bytes que llegaron y no salieron en un paquete
    bool failed = false;      // el corte se paró con un error
    bool sameAsWire = true;   // cada paquete, con su máscara, es lo que pasó por el cable
};

// Corta `reads` con un PacketReader con FoxAnti, un Feed por lectura. Cada paquete que sale se
// compara con su lugar en el flujo del cable: igual si llegó en claro, o con el 0xAA en cada byte.
Cut CutWithFoxAnti(const std::vector<std::span<const std::byte>>& reads)
{
    const FoxAntiObfuscation foxAnti;
    PacketReader reader(foxAnti);
    Cut cut;
    ByteVector stream;
    for (const std::span<const std::byte> read : reads)
    {
        stream.insert(stream.end(), read.begin(), read.end());
    }
    std::size_t offset = 0;
    for (const std::span<const std::byte> read : reads)
    {
        reader.Feed(read);
        while (true)
        {
            auto next = reader.Next();
            if (!next.HasValue())
            {
                cut.failed = true;
                return cut;
            }
            if (!next.Value())
            {
                break;
            }
            Packet packet = std::move(*std::move(next).Value());
            const std::span<const std::byte> wire = std::span<const std::byte>(stream).subspan(offset);
            const bool masked = wire.front() != packet.Bytes().front();
            ByteVector remasked(packet.Bytes().begin(), packet.Bytes().end());
            for (std::byte& value : remasked)
            {
                value ^= masked ? LabMask : std::byte{0};
            }
            cut.sameAsWire = cut.sameAsWire && wire.size() >= remasked.size() &&
                             SameBytesWithoutValues(wire.first(remasked.size()), remasked);
            cut.masked += masked ? 1 : 0;
            offset += remasked.size();
            cut.packets.push_back(std::move(packet));
        }
    }
    cut.leftover = reader.Pending();
    return cut;
}

// El mismo flujo, de a un byte por lectura.
std::vector<std::span<const std::byte>> OneByteReads(std::span<const std::byte> stream)
{
    std::vector<std::span<const std::byte>> reads;
    for (std::size_t i = 0; i < stream.size(); ++i)
    {
        reads.push_back(stream.subspan(i, 1));
    }
    return reads;
}

// Un C1/C2 con este opcode y este subcódigo, en el byte que le sigue.
bool Is(const Packet& packet, PacketType type, std::uint8_t opcode, std::uint8_t subcode)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    const std::size_t subcodeIndex = type == PacketType::C1 ? 3 : 4;
    return packet.Type() == type && packet.Opcode() == opcode && bytes.size() > subcodeIndex &&
           bytes[subcodeIndex] == static_cast<std::byte>(subcode);
}

class GoldenFoxAntiObfuscation : public Client::Tests::GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
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

        // Lo que contesta el servidor el cliente lo descifra con Dec2.dat.
        dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(dec2);
    }

    std::vector<WireConnection> connections;
    std::optional<DecryptionKey> dec2;
};

// Lo que contestaron los dos servidores, en las 72 conexiones: 3.033 paquetes sin bytes de más.
// Los 144 del ConnectServer llegan en claro (la bienvenida y la lista o la dirección), y los 2.889
// del GameServer, todos con el 0xAA, también los C3/C4.
TEST_F(GoldenFoxAntiObfuscation, CutsWhatBothServersAnswered)
{
    std::size_t connectServer = 0;
    std::size_t gameServer = 0;
    std::size_t masked = 0;
    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        const WireConnection& connection = connections[index];
        const Cut cut = CutWithFoxAnti(connection.Reads(WireDirection::ServerToClient));
        EXPECT_FALSE(cut.failed) << "conexión " << index;
        EXPECT_EQ(cut.leftover, 0u) << "conexión " << index;
        EXPECT_TRUE(cut.sameAsWire) << "conexión " << index;
        (connection.port == ConnectServerPort ? connectServer : gameServer) += cut.packets.size();
        masked += cut.masked;
        if (connection.port == ConnectServerPort)
        {
            EXPECT_EQ(cut.masked, 0u) << "conexión " << index;
        }
        else
        {
            EXPECT_EQ(cut.masked, cut.packets.size()) << "conexión " << index;
        }
    }

    EXPECT_EQ(connectServer, 144u);
    EXPECT_EQ(gameServer, 2889u);
    EXPECT_EQ(masked, 2889u);
}

// De a un byte salen los mismos paquetes del GameServer: cada uno sigue con la máscara que eligió
// su primer byte, aunque le lleguen los demás en otras lecturas.
TEST_F(GoldenFoxAntiObfuscation, OneByteAtATimeGivesTheSamePackets)
{
    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        const WireConnection& connection = connections[index];
        if (connection.port != GameServerPort)
        {
            continue;
        }
        const Cut asRead = CutWithFoxAnti(connection.Reads(WireDirection::ServerToClient));
        ByteVector stream;
        for (const std::span<const std::byte> read : connection.Reads(WireDirection::ServerToClient))
        {
            stream.insert(stream.end(), read.begin(), read.end());
        }
        const Cut oneByte = CutWithFoxAnti(OneByteReads(stream));

        EXPECT_FALSE(oneByte.failed) << "conexión " << index;
        EXPECT_EQ(oneByte.leftover, 0u) << "conexión " << index;
        EXPECT_TRUE(oneByte.sameAsWire) << "conexión " << index;
        ASSERT_EQ(oneByte.packets.size(), asRead.packets.size()) << "conexión " << index;
        for (std::size_t k = 0; k < oneByte.packets.size(); ++k)
        {
            EXPECT_TRUE(SameBytesWithoutValues(oneByte.packets[k].Bytes(), asRead.packets[k].Bytes()))
                << "conexión " << index << ", paquete " << k;
        }
    }
}

// Las 72 listas F4 02 del ConnectServer pasan a la F4 06 de main.exe: un servidor, con el código 0
// del laboratorio como 400, la carga copiada y el cuarto byte en 0.
TEST_F(GoldenFoxAntiObfuscation, TheLabServerListsBecomeTheOnesMainExeReads)
{
    const FoxAntiObfuscation foxAnti;
    const ByteVector expectedStart = Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01});
    constexpr std::size_t ListSize = 11;      // la de main.exe, con su única entrada
    constexpr std::size_t LabLoadByte = 8;    // la carga en la lista del laboratorio
    constexpr std::size_t ListLoadByte = 9;   // y en la de main.exe, un byte más adelante
    constexpr std::size_t LastEntryByte = 10; // el cuarto byte de la entrada, que sale en 0
    std::size_t lists = 0;
    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        const WireConnection& connection = connections[index];
        if (connection.port != ConnectServerPort)
        {
            continue;
        }
        for (Packet& packet : CutWithFoxAnti(connection.Reads(WireDirection::ServerToClient)).packets)
        {
            if (!Is(packet, PacketType::C2, 0xF4, 0x02))
            {
                continue;
            }
            ++lists;
            ASSERT_GT(packet.Bytes().size(), LabLoadByte) << "conexión " << index;
            const std::byte labLoad = packet.Bytes()[LabLoadByte];
            const auto list = foxAnti.AfterCut(std::move(packet));
            ASSERT_TRUE(list.HasValue()) << "conexión " << index;
            const std::span<const std::byte> bytes = list.Value().Bytes();
            ASSERT_EQ(bytes.size(), ListSize) << "conexión " << index;
            EXPECT_TRUE(SameBytesWithoutValues(bytes.first(expectedStart.size()), expectedStart))
                << "conexión " << index;
            EXPECT_TRUE(bytes[ListLoadByte] == labLoad) << "conexión " << index;
            EXPECT_TRUE(bytes[LastEntryByte] == std::byte{0}) << "conexión " << index;
        }
    }

    EXPECT_EQ(lists, 72u);
}

// Todo lo que contestó el GameServer pasa por las tres etapas de recepción sin un InvalidPacket:
// el corte con el 0xAA, AfterCut, el descifrado de los C3/C4 con Dec2.dat, y BeforeDispatch. Los 7
// F3 11, todos de 8 bytes, salen con un byte más y el 00 en pkt[5].
TEST_F(GoldenFoxAntiObfuscation, EverythingTheGameServerAnsweredReachesTheDispatcher)
{
    const FoxAntiObfuscation foxAnti;
    constexpr std::size_t LabSkillListSize = 8; // los F3 11 del cable
    constexpr std::size_t InsertedByte = 5;     // donde reset_queue mete el 00
    std::size_t dispatched = 0;
    std::size_t skillLists = 0;
    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        const WireConnection& connection = connections[index];
        if (connection.port != GameServerPort)
        {
            continue;
        }
        PacketSerials serials;
        for (Packet& cut : CutWithFoxAnti(connection.Reads(WireDirection::ServerToClient)).packets)
        {
            auto afterCut = foxAnti.AfterCut(std::move(cut));
            ASSERT_TRUE(afterCut.HasValue()) << "conexión " << index;
            Packet packet = std::move(afterCut).Value();
            if (packet.Type() == PacketType::C3 || packet.Type() == PacketType::C4)
            {
                auto decrypted = DecryptPacket(packet, *dec2, serials);
                ASSERT_TRUE(decrypted.HasValue()) << "conexión " << index;
                packet = std::move(decrypted).Value().packet;
            }
            const bool skillList = Is(packet, PacketType::C1, 0xF3, 0x11);
            const std::size_t sizeBefore = packet.Bytes().size();
            const ByteVector before(packet.Bytes().begin(), packet.Bytes().end());

            const auto toDispatch = foxAnti.BeforeDispatch(std::move(packet));

            ASSERT_TRUE(toDispatch.HasValue()) << "conexión " << index;
            ++dispatched;
            if (!skillList)
            {
                continue;
            }
            ++skillLists;
            const std::span<const std::byte> after = toDispatch.Value().Bytes();
            EXPECT_EQ(sizeBefore, LabSkillListSize) << "conexión " << index;
            ASSERT_EQ(after.size(), sizeBefore + 1) << "conexión " << index;
            EXPECT_TRUE(after[InsertedByte] == std::byte{0}) << "conexión " << index;
            EXPECT_TRUE(SameBytesWithoutValues(after.subspan(InsertedByte + 1),
                                               std::span<const std::byte>(before).subspan(InsertedByte)))
                << "conexión " << index;
        }
    }

    EXPECT_EQ(dispatched, 2889u);
    EXPECT_EQ(skillLists, 7u);
}

// Los 8 F4 03 que mandó el cliente al ConnectServer son lo que da BeforeSend con el código 400 de
// main.exe: el 0 que espera el laboratorio.
TEST_F(GoldenFoxAntiObfuscation, TheChosenServerIsWhatTheOriginalClientSent)
{
    const FoxAntiObfuscation foxAnti;
    const Packet chosen = foxAnti.BeforeSend(Packet::FromBytes(Bytes({0xC1, 0x06, 0xF4, 0x03, 0x90, 0x01})).Value());
    std::size_t selects = 0;
    for (std::size_t index = 0; index < connections.size(); ++index)
    {
        const WireConnection& connection = connections[index];
        if (connection.port != ConnectServerPort)
        {
            continue;
        }
        for (const Packet& packet : CutWithFoxAnti(connection.Reads(WireDirection::ClientToServer)).packets)
        {
            if (!Is(packet, PacketType::C1, 0xF4, 0x03))
            {
                continue;
            }
            ++selects;
            EXPECT_TRUE(SameBytesWithoutValues(packet.Bytes(), chosen.Bytes())) << "conexión " << index;
        }
    }

    EXPECT_EQ(selects, 8u);
}

} // namespace
