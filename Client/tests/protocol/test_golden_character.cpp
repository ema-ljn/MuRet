#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterSelect.h"
#include "Protocol/Character/Opcodes.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/SameBytes.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Los paquetes de la selección de personaje contra el cable del laboratorio (support/WireCapture.h,
// MURET_CLIENT_WIRE_DIR; el plan de la CL6, CL6.3, paso 2): los F3 00 y F3 03 que mandó el cliente
// original, en claro con su XOR32, contra los de MuRet; y las listas que contestó el GameServer, sin
// el 0xAA de FoxAnti, leídas con ReadCharacterList.
//
// El cable lleva los nombres de los personajes del jugador: los tests cuentan, miran largos y
// lugares, y comparan con SameBytesWithoutValues. El nombre de cada F3 03 sale del cable en el test,
// y no se escribe ni se imprime.
namespace
{
using Client::Network::Packet;
using Client::Network::PacketType;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::Character::CharacterList;
using Client::Protocol::Character::CharacterOpcode;
using Client::Protocol::Character::EntrySize;
using Client::Protocol::Character::ListSubcode;
using Client::Protocol::Character::MaxCharacterSlot;
using Client::Protocol::Character::NameSize;
using Client::Protocol::Character::ReadCharacterList;
using Client::Protocol::Character::SelectSubcode;
using Client::Protocol::Character::WriteCharacterListRequest;
using Client::Protocol::Character::WriteCharacterSelect;
using Client::Tests::CutPackets;
using Client::Tests::ParseWireCapture;
using Client::Tests::ReadFileBytes;
using Client::Tests::SameBytesWithoutValues;
using Client::Tests::Unmasked;
using Client::Tests::WireConnection;
using Client::Tests::WireDirection;

using ByteVector = std::vector<std::byte>;

constexpr std::string_view CaptureFile = "muproxy.log";

// El puerto del proxy delante del GameServer del laboratorio.
constexpr std::uint16_t GameServerPort = 55901;

// La máscara de FoxAnti en todo lo que manda el GameServer del laboratorio, y lo que manda el
// cliente, sin máscara.
constexpr std::byte FoxAntiMask{0xAA};
constexpr std::byte NoMask{0x00};

// Dónde está el subcódigo de un C1, y dónde empieza el nombre del F3 03.
constexpr std::size_t SubcodeOffset = 3;
constexpr std::size_t SelectNameOffset = 4;

// Lo que mide la cabecera de la lista: C1, el tamaño, F3, 00, pkt[4], pkt[5] y la cantidad.
constexpr std::size_t ListHeaderSize = 7;
constexpr std::size_t ListCountOffset = 6;

// Si `packet` es un C1 F3 con el subcódigo `subcode`.
bool IsCharacterPacket(const Packet& packet, std::uint8_t subcode)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    return packet.Type() == PacketType::C1 && packet.Opcode() == CharacterOpcode && bytes.size() > SubcodeOffset &&
           std::to_integer<std::uint8_t>(bytes[SubcodeOffset]) == subcode;
}

class GoldenCharacterPackets : public Client::Tests::WireDataTest
{
protected:
    void SetUp() override
    {
        WireDataTest::SetUp();
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

    // Los paquetes de un sentido de cada conexión con el GameServer, con la máscara deshecha, en
    // orden.
    std::vector<Packet> GameServerPackets(WireDirection direction, std::byte mask) const
    {
        std::vector<Packet> packets;
        for (std::size_t index = 0; index < m_connections.size(); ++index)
        {
            if (m_connections[index].port != GameServerPort)
            {
                continue;
            }
            const std::vector<ByteVector> unmasked = Unmasked(m_connections[index].Reads(direction), mask);
            const std::vector<std::span<const std::byte>> reads(unmasked.begin(), unmasked.end());
            std::vector<Packet> cut = CutPackets(reads, index);
            packets.insert(packets.end(), std::make_move_iterator(cut.begin()), std::make_move_iterator(cut.end()));
        }
        return packets;
    }

    std::vector<WireConnection> m_connections;
};

// Cada F3 00 del cable es el de WriteCharacterListRequest byte a byte, y cada F3 03, el de
// WriteCharacterSelect con el nombre que trae: el XOR32 y el largo son los del original.
TEST_F(GoldenCharacterPackets, TheRequestsHaveTheShapeOfTheCable)
{
    std::size_t requests = 0;
    std::size_t selects = 0;
    for (const Packet& wire : GameServerPackets(WireDirection::ClientToServer, NoMask))
    {
        const Packet clear = Xor32Decode(wire);
        if (IsCharacterPacket(clear, ListSubcode))
        {
            ++requests;
            EXPECT_TRUE(SameBytesWithoutValues(wire.Bytes(), WriteCharacterListRequest().packet.Bytes()))
                << "el F3 00 número " << requests;
        }
        else if (IsCharacterPacket(clear, SelectSubcode) && clear.Bytes().size() == SelectNameOffset + NameSize)
        {
            ++selects;
            const auto name = clear.Bytes().subspan(SelectNameOffset).first<NameSize>();
            EXPECT_TRUE(SameBytesWithoutValues(wire.Bytes(), WriteCharacterSelect(name).packet.Bytes()))
                << "el F3 03 número " << selects;
        }
    }

    EXPECT_GE(requests, 1u);
    EXPECT_EQ(selects, requests) << "un F3 03 por cada F3 00";
}

// Cada lista que mandó el GameServer se lee, con una entrada por cada 0x22 bytes, todas en un lugar
// de 0 a 4: el mismo largo que mira el original.
TEST_F(GoldenCharacterPackets, TheListsOfTheCableRead)
{
    std::size_t lists = 0;
    for (const Packet& packet : GameServerPackets(WireDirection::ServerToClient, FoxAntiMask))
    {
        if (!IsCharacterPacket(packet, ListSubcode))
        {
            continue;
        }
        ++lists;
        const auto list = ReadCharacterList(packet);
        ASSERT_TRUE(list.HasValue()) << "la lista número " << lists << " no se lee";
        const std::size_t count = std::to_integer<std::uint8_t>(packet.Bytes()[ListCountOffset]);
        EXPECT_EQ(packet.Bytes().size(), ListHeaderSize + EntrySize * count) << "la lista número " << lists;
        const CharacterList& read = list.Value();
        EXPECT_EQ(read.characters.size(), count) << "la lista número " << lists << " descartó una entrada";
        for (const auto& entry : read.characters)
        {
            EXPECT_LE(entry.slot, MaxCharacterSlot) << "la lista número " << lists;
        }
    }

    EXPECT_GE(lists, 1u);
}
} // namespace
