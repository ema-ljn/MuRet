// Client::Network::FoxAntiObfuscation: la capa de paquetes de FoxAnti, con los ejemplos del cable
// del 03, la máscara por paquete en el PacketReader (Q-C18), los remapeos, la ida y vuelta de los
// códigos de servidor (Q-C17) y los largos que el original no mira (Q-C09). Plan de la CL2, CL2.4.
//
// Lo que se espera de cada traducción sale de la FoxAnti mínima (analysis/foxanti/minimal/src/
// packet.c en el repo de research) compilada en el host con tests/vectors/net/tools/
// foxanti_cases.c: los nombres de los casos (A1, B.1E.iv, ...) son los de ese programa.

#include "Network/FoxAntiObfuscation.h"

#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/Xor32.h"
#include "Network/EncryptedPacket.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"

#include "support/BytePattern.h"
#include "support/Bytes.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetVectors.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using Client::Network::DecryptPacket;
using Client::Network::EncryptPacket;
using Client::Network::FoxAntiObfuscation;
using Client::Network::MaxPacketSize;
using Client::Network::ObfuscationError;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::PlainBlockSize;
using Client::Network::Crypto::Xor32Decode;
using Client::Network::Crypto::Xor32Encode;
using Client::Tests::BytePattern;
using Client::Tests::Bytes;
using Client::Tests::IdentityPair;
using Client::Tests::NoticeBytes;
using Client::Tests::NoticeSerial0;
using Client::Tests::ReadNetVector;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// El XOR que el servidor del laboratorio le aplica a cada byte de lo que manda.
constexpr std::byte LabMask{0xAA};

// Un Packet con estos bytes. Si no lo forman, el test falla (y Value() corta el programa).
Packet PacketOf(ByteVector bytes)
{
    auto built = Packet::FromBytes(std::move(bytes));
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}

ByteVector BytesOf(const Packet& packet)
{
    return {packet.Bytes().begin(), packet.Bytes().end()};
}

// Los bytes como los manda el servidor del laboratorio: cada uno con el 0xAA.
ByteVector Masked(ByteVector bytes)
{
    for (std::byte& value : bytes)
    {
        value ^= LabMask;
    }
    return bytes;
}

ByteVector Join(std::initializer_list<ByteVector> parts)
{
    ByteVector joined;
    for (const ByteVector& part : parts)
    {
        joined.insert(joined.end(), part.begin(), part.end());
    }
    return joined;
}

// Lo que sacó el lector hasta quedarse sin paquetes, o el error con que se paró.
struct Drained
{
    std::vector<ByteVector> packets;
    std::optional<PacketError> error;
};

Drained Drain(PacketReader& reader)
{
    Drained drained;
    while (true)
    {
        auto next = reader.Next();
        if (!next.HasValue())
        {
            drained.error = next.Error();
            return drained;
        }
        if (!next.Value().has_value())
        {
            return drained;
        }
        drained.packets.push_back(BytesOf(*next.Value()));
    }
}

// Un caso de la FoxAnti mínima: lo que entra y lo que sale.
struct ReferenceCase
{
    std::string_view name;
    ByteVector in;
    ByteVector out;
};

// La lista que manda el ConnectServer del laboratorio (04, "La lista de servidores"): un servidor,
// código 0, carga 0 y el 0xCC que FoxAnti descarta. Y cómo la lee main.exe.
ByteVector LabServerList()
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
}

ByteVector MainExeServerList()
{
    return Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01, 0x00, 0x00});
}

// La lista del laboratorio con una entrada de este código.
ByteVector LabListWithCode(std::uint16_t code)
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, code & 0xFF, code >> 8, 0x00, 0xCC});
}

// El código de servidor que elige el cliente, en C1 06 F4 03 <código LE>.
ByteVector ServerSelect(std::uint16_t code)
{
    return Bytes({0xC1, 0x06, 0xF4, 0x03, code & 0xFF, code >> 8});
}

// --- Los del cable del 03 ---

TEST(FoxAntiObfuscationTest, TheLabServerListBecomesTheOneMainExeReads)
{
    const auto list = FoxAntiObfuscation{}.AfterCut(PacketOf(LabServerList()));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(SameBytes(list.Value().Bytes(), MainExeServerList()));
}

// main.exe manda su código, 400, y al cable sale el 0 que espera el laboratorio.
TEST(FoxAntiObfuscationTest, TheChosenServerGoesOutWithTheLabCode)
{
    const Packet sent = FoxAntiObfuscation{}.BeforeSend(PacketOf(ServerSelect(400)));

    EXPECT_TRUE(SameBytes(sent.Bytes(), ServerSelect(0)));
}

// El primer F3 11 de la sesión real (04, "F3 11, S->C"): el handler encuentra el slot en pkt[6].
TEST(FoxAntiObfuscationTest, TheSkillListGetsTheByteMainExeExpects)
{
    const auto list =
        FoxAntiObfuscation{}.BeforeDispatch(PacketOf(Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68})));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(SameBytes(list.Value().Bytes(), Bytes({0xC1, 0x09, 0xF3, 0x11, 0xFE, 0x00, 0x04, 0x16, 0x68})));
}

// --- La máscara (Q-C18) ---

// 6B, 68, 69 y 6E son C1 a C4 con el 0xAA. Nada más lo lleva: tampoco un C1 a C4 en claro.
TEST(FoxAntiObfuscationTest, OnlyTheFourMaskedTypesAreMasked)
{
    const FoxAntiObfuscation foxAnti;
    for (int value = 0; value < 256; ++value)
    {
        const bool masked = value == 0x6B || value == 0x68 || value == 0x69 || value == 0x6E;
        EXPECT_EQ(foxAnti.InboundMask(static_cast<std::byte>(value)), masked ? LabMask : std::byte{0})
            << "byte " << value;
    }
}

// Los tres que el original deja pasar (C1 con opcode 00, C1 con F4 y C2 con F4: los del
// ConnectServer) llegan en claro y salen igual, también juntos en una lectura.
TEST(FoxAntiObfuscationTest, TheThreeExemptPacketsComeInClear)
{
    const ByteVector hello = Bytes({0xC1, 0x04, 0x00, 0x01});
    const ByteVector address = Bytes({0xC1, 0x16, 0xF4, 0x03, '1', '7',  '2',  '.',  '1',  '6',  '.',
                                      '1',  '9',  '8',  '.',  '1', 0x00, 0x00, 0x00, 0x00, 0x5D, 0xDA});
    const FoxAntiObfuscation foxAnti;
    PacketReader reader(foxAnti);

    reader.Feed(Join({hello, address, LabServerList()}));
    const Drained drained = Drain(reader);

    EXPECT_EQ(drained.error, std::nullopt);
    ASSERT_EQ(drained.packets.size(), 3u);
    EXPECT_TRUE(SameBytes(drained.packets[0], hello));
    EXPECT_TRUE(SameBytes(drained.packets[1], address));
    EXPECT_TRUE(SameBytes(drained.packets[2], LabServerList()));
}

// Un C3 (FIX_C3_SERIAL0) y un C4 (c4-serial2.bin, de 421 bytes, con el tamaño en dos bytes) con el
// 0xAA: el tamaño también viaja enmascarado.
TEST(FoxAntiObfuscationTest, MaskedC3AndC4AreCut)
{
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    const FoxAntiObfuscation foxAnti;
    PacketReader reader(foxAnti);

    reader.Feed(Join({Masked(NoticeSerial0()), Masked(c4)}));
    const Drained drained = Drain(reader);

    EXPECT_EQ(drained.error, std::nullopt);
    ASSERT_EQ(drained.packets.size(), 2u);
    EXPECT_TRUE(SameBytes(drained.packets[0], NoticeSerial0()));
    EXPECT_TRUE(SameBytes(drained.packets[1], c4));
    EXPECT_EQ(reader.Pending(), 0u);
}

// Q-C18: el original decide el 0xAA por el primero de la lectura, y deshace el del segundo como si
// fuera del primero. Por paquete salen los dos bien, en cualquier orden.
TEST(FoxAntiObfuscationTest, AnExemptPacketAndAMaskedOneInTheSameReadGiveBoth)
{
    const ByteVector exempt = Bytes({0xC1, 0x04, 0xF4, 0x05});
    const ByteVector chat = Bytes({0xC1, 0x05, 0x42, 0x99, 0x10});
    const FoxAntiObfuscation foxAnti;

    for (const bool exemptFirst : {true, false})
    {
        PacketReader reader(foxAnti);
        reader.Feed(exemptFirst ? Join({exempt, Masked(chat)}) : Join({Masked(chat), exempt}));
        const Drained drained = Drain(reader);

        SCOPED_TRACE(exemptFirst ? "el exento primero" : "el enmascarado primero");
        EXPECT_EQ(drained.error, std::nullopt);
        ASSERT_EQ(drained.packets.size(), 2u);
        EXPECT_TRUE(SameBytes(drained.packets[exemptFirst ? 0 : 1], exempt));
        EXPECT_TRUE(SameBytes(drained.packets[exemptFirst ? 1 : 0], chat));
    }
}

// Un paquete enmascarado partido entre dos lecturas sigue con el XOR que eligió su primer byte, y
// el que viene detrás en la segunda, en claro, no lo hereda. Un C2 y un C4 cortados después de su
// primer y de su segundo byte (la cabecera a medias), y un C1 a mitad del cuerpo.
TEST(FoxAntiObfuscationTest, AMaskedPacketSplitBetweenTwoReadsGivesTheSame)
{
    struct Split
    {
        std::string_view name;
        ByteVector packet;
        std::size_t firstRead;
    };
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    const ByteVector c1 = Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68});
    const std::vector<Split> splits{
        {"C2 tras 1", c2, 1}, {"C2 tras 2", c2, 2}, {"C4 tras 1", c4, 1}, {"C4 tras 2", c4, 2}, {"C1 tras 5", c1, 5},
    };
    const ByteVector next = Bytes({0xC1, 0x04, 0x00, 0x01});
    const FoxAntiObfuscation foxAnti;

    for (const Split& split : splits)
    {
        SCOPED_TRACE(split.name);
        const ByteVector wire = Masked(split.packet);
        PacketReader reader(foxAnti);

        reader.Feed(std::span<const std::byte>(wire).first(split.firstRead));
        const Drained first = Drain(reader);
        reader.Feed(Join({ByteVector(wire.begin() + static_cast<std::ptrdiff_t>(split.firstRead), wire.end()), next}));
        const Drained second = Drain(reader);

        EXPECT_EQ(first.error, std::nullopt);
        EXPECT_TRUE(first.packets.empty());
        EXPECT_EQ(second.error, std::nullopt);
        ASSERT_EQ(second.packets.size(), 2u);
        EXPECT_TRUE(SameBytes(second.packets[0], split.packet));
        EXPECT_TRUE(SameBytes(second.packets[1], next));
    }
}

// De a un byte también: la máscara se elige una vez por paquete y no se pierde entre lecturas.
TEST(FoxAntiObfuscationTest, MaskedPacketsOneByteAtATimeGiveTheSame)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector exempt = Bytes({0xC1, 0x04, 0xF4, 0x05});
    const ByteVector wire = Join({Masked(c2), exempt, Masked(NoticeSerial0())});
    const FoxAntiObfuscation foxAnti;
    PacketReader reader(foxAnti);
    std::vector<ByteVector> packets;

    for (std::size_t i = 0; i < wire.size(); ++i)
    {
        reader.Feed(std::span<const std::byte>(wire).subspan(i, 1));
        Drained drained = Drain(reader);
        ASSERT_EQ(drained.error, std::nullopt) << "byte " << i;
        packets.insert(packets.end(), drained.packets.begin(), drained.packets.end());
    }

    ASSERT_EQ(packets.size(), 3u);
    EXPECT_TRUE(SameBytes(packets[0], c2));
    EXPECT_TRUE(SameBytes(packets[1], exempt));
    EXPECT_TRUE(SameBytes(packets[2], NoticeSerial0()));
}

// Un primer byte que no es un tipo, ni en claro ni con el 0xAA, sigue siendo InvalidType.
TEST(FoxAntiObfuscationTest, AFirstByteThatIsNoTypeIsStillInvalid)
{
    const FoxAntiObfuscation foxAnti;
    PacketReader reader(foxAnti);

    reader.Feed(Bytes({0x6A, 0x04, 0x00, 0x01}));
    const Drained drained = Drain(reader);

    EXPECT_EQ(drained.error, PacketError::InvalidType);
}

// Un lector movido sigue con la ofuscación del original.
TEST(FoxAntiObfuscationTest, AMovedReaderKeepsItsObfuscation)
{
    const FoxAntiObfuscation foxAnti;
    PacketReader original(foxAnti);
    PacketReader moved(std::move(original));
    const ByteVector chat = Bytes({0xC1, 0x05, 0x42, 0x99, 0x10});

    moved.Feed(Masked(chat));
    const Drained drained = Drain(moved);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], chat));
}

// Q-C03 y Q-C18: una lectura que termina en los primeros bytes de un paquete enmascarado que va
// detrás de uno entero. El Feed siguiente compacta el buffer con ese paquete desenmascarado a
// medias, y no tiene que desenmascarar de nuevo lo que ya desenmascaró.
TEST(FoxAntiObfuscationTest, AMaskedPacketSplitBehindAWholeOneGivesTheSame)
{
    const ByteVector exempt = Bytes({0xC1, 0x04, 0xF4, 0x05});
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector wire = Join({exempt, Masked(c2), Masked(c2)});
    const FoxAntiObfuscation foxAnti;
    for (const std::size_t inFirstRead : {std::size_t{1}, std::size_t{2}, std::size_t{4}})
    {
        SCOPED_TRACE(inFirstRead);
        PacketReader reader(foxAnti);
        const std::size_t cut = exempt.size() + inFirstRead;

        reader.Feed(std::span<const std::byte>(wire).first(cut));
        const Drained first = Drain(reader);
        reader.Feed(std::span<const std::byte>(wire).subspan(cut));
        const Drained second = Drain(reader);

        ASSERT_EQ(first.packets.size(), 1u);
        EXPECT_TRUE(SameBytes(first.packets[0], exempt));
        EXPECT_EQ(second.error, std::nullopt);
        ASSERT_EQ(second.packets.size(), 2u);
        EXPECT_TRUE(SameBytes(second.packets[0], c2));
        EXPECT_TRUE(SameBytes(second.packets[1], c2));
    }
}

// Un lector movido a mitad de un paquete enmascarado, construido y asignado, sigue con lo que ya
// desenmascaró, con la máscara de ese paquete y con la ofuscación para el que viene detrás.
TEST(FoxAntiObfuscationTest, AReaderMovedInTheMiddleOfAMaskedPacketGoesOn)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector wire = Join({Masked(c2), Masked(c2)});
    const std::span<const std::byte> view(wire);
    const FoxAntiObfuscation foxAnti;

    PacketReader original(foxAnti);
    original.Feed(view.first(2));
    ASSERT_TRUE(Drain(original).packets.empty());
    PacketReader constructed(std::move(original));
    constructed.Feed(view.subspan(2, 2));
    ASSERT_TRUE(Drain(constructed).packets.empty());
    PacketReader assigned;
    assigned = std::move(constructed);
    assigned.Feed(view.subspan(4));
    const Drained drained = Drain(assigned);

    EXPECT_EQ(drained.error, std::nullopt);
    ASSERT_EQ(drained.packets.size(), 2u);
    EXPECT_TRUE(SameBytes(drained.packets[0], c2));
    EXPECT_TRUE(SameBytes(drained.packets[1], c2));
}

// Uno del que se movió, construyendo o asignando, queda como nuevo y con su ofuscación: un paquete
// enmascarado entero sale bien, sin lo que había desenmascarado antes.
TEST(FoxAntiObfuscationTest, AReaderMovedFromInTheMiddleOfAMaskedPacketStartsOver)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector masked = Masked(c2);
    const std::span<const std::byte> view(masked);
    const FoxAntiObfuscation foxAnti;

    PacketReader movedByConstruction(foxAnti);
    movedByConstruction.Feed(view.first(4));
    ASSERT_TRUE(Drain(movedByConstruction).packets.empty());
    const PacketReader constructed(std::move(movedByConstruction));
    PacketReader movedByAssignment(foxAnti);
    movedByAssignment.Feed(view.first(4));
    ASSERT_TRUE(Drain(movedByAssignment).packets.empty());
    PacketReader assigned;
    assigned = std::move(movedByAssignment);

    for (PacketReader* reader : {&movedByConstruction, &movedByAssignment})
    {
        reader->Feed(masked);
        const Drained drained = Drain(*reader);
        EXPECT_EQ(drained.error, std::nullopt);
        ASSERT_EQ(drained.packets.size(), 1u);
        EXPECT_TRUE(SameBytes(drained.packets[0], c2));
    }
    EXPECT_EQ(constructed.Pending(), 4u);
    EXPECT_EQ(assigned.Pending(), 4u);
}

// Un lector guarda un puntero a su ofuscación, así que no acepta una temporal, que moriría antes
// que él. Una Obfuscation tampoco se asigna a través de la base; una hija sí se copia.
static_assert(!std::is_constructible_v<PacketReader, FoxAntiObfuscation>);
static_assert(std::is_constructible_v<PacketReader, const FoxAntiObfuscation&>);
static_assert(!std::is_copy_assignable_v<Client::Network::Obfuscation>);
static_assert(std::is_copy_constructible_v<FoxAntiObfuscation>);

// --- Los remapeos, contra la FoxAnti mínima ---

// send_forward: lo que se cifra, [serial][opcode][campos con XOR32][relleno]. Con 0x10, 0x19 y
// 0x1E cambia el opcode, y los campos y el relleno, que se encadenan con él, también.
TEST(FoxAntiObfuscationTest, BeforeEncryptRemapsLikeTheMinimalFoxAnti)
{
    const std::vector<ReferenceCase> cases{
        {"B.10.i", Bytes({0x00, 0x10, 0xA5}), Bytes({0x00, 0x1D, 0xA8})},
        {"B.10.ii.pad00", Bytes({0x00, 0x10, 0x00, 0x00}), Bytes({0x00, 0x1D, 0x0D, 0x0D})},
        {"B.10.ii.padA5", Bytes({0x00, 0x10, 0x00, 0xA5}), Bytes({0x00, 0x1D, 0x0D, 0xA8})},
        {"B.10.iii", Bytes({0x07, 0x10, 0x9C, 0x32, 0x9F, 0x2C, 0x66, 0x03, 0xF8, 0x66, 0xBD, 0x8E, 0x5C}),
         Bytes({0x07, 0x1D, 0x91, 0x3F, 0x92, 0x21, 0x6B, 0x0E, 0xF5, 0x6B, 0xB0, 0x83, 0x51})},
        {"B.19.i", Bytes({0x00, 0x19, 0xA5}), Bytes({0x00, 0xA5, 0x19})},
        {"B.19.ii.pad00", Bytes({0x00, 0x19, 0x00, 0x00}), Bytes({0x00, 0xA5, 0xBC, 0xBC})},
        {"B.19.ii.padA5", Bytes({0x00, 0x19, 0x00, 0xA5}), Bytes({0x00, 0xA5, 0xBC, 0x19})},
        {"B.19.iii", Bytes({0x07, 0x19, 0x95, 0x3B, 0x96, 0x25, 0x6F, 0x0A, 0xF1, 0x6F, 0xB4, 0x87, 0x5C}),
         Bytes({0x07, 0xA5, 0x29, 0x87, 0x2A, 0x99, 0xD3, 0xB6, 0x4D, 0xD3, 0x08, 0x3B, 0xE0})},
        {"B.1E.i", Bytes({0x00, 0x1E, 0xA5}), Bytes({0x00, 0xA6, 0x1D})},
        {"B.1E.ii.pad00", Bytes({0x00, 0x1E, 0x00, 0x00}), Bytes({0x00, 0xA6, 0xB8, 0xB8})},
        {"B.1E.ii.padA5", Bytes({0x00, 0x1E, 0x00, 0xA5}), Bytes({0x00, 0xA6, 0xB8, 0x1D})},
        {"B.1E.iii", Bytes({0x07, 0x1E, 0x92, 0x3C, 0x91, 0x22, 0x68, 0x0D, 0xF6, 0x68, 0xB3, 0x80, 0x5C}),
         Bytes({0x07, 0xA6, 0x2A, 0x84, 0x29, 0x9A, 0xD0, 0xB5, 0x4E, 0xD0, 0x0B, 0x38, 0xE4})},
        // Como los tres A6 de la sesión real: el primer campo es 0x29 (C1 05 1E 29 00 con XOR32).
        {"B.1E.iv", Bytes({0x03, 0x1E, 0xBE, 0x02, 0xA5}), Bytes({0x03, 0xA6, 0x06, 0xBA, 0x1D})},
        {"B.1C.ii.padA5", Bytes({0x00, 0x1C, 0x00, 0xA5}), Bytes({0x00, 0x1C, 0x00, 0xA5})},
        {"B.1C.iii", Bytes({0x07, 0x1C, 0x90, 0x3E, 0x93, 0x20, 0x6A, 0x0F, 0xF4, 0x6A, 0xB1, 0x82, 0x5C}),
         Bytes({0x07, 0x1C, 0x90, 0x3E, 0x93, 0x20, 0x6A, 0x0F, 0xF4, 0x6A, 0xB1, 0x82, 0x5C})},
    };
    const FoxAntiObfuscation foxAnti;
    for (const ReferenceCase& reference : cases)
    {
        ByteVector sealed = reference.in;
        foxAnti.BeforeEncrypt(sealed);
        EXPECT_TRUE(SameBytes(sealed, reference.out)) << reference.name;
    }
}

// EncryptPacket con FoxAnti cifra lo que dejó BeforeEncrypt, relleno incluido: el C1 05 1E de
// B.1E.iv, con el serial 3 y el relleno A5, sale como un C3 de 13 cuyo bloque es [03 A6 06 BA 1D]
// y tres ceros, con 4 útiles (el relleno no cuenta).
TEST(FoxAntiObfuscationTest, EncryptPacketCiphersWhatBeforeEncryptLeft)
{
    const auto identity = IdentityPair();
    PacketSerials serials;
    for (int k = 0; k < 3; ++k)
    {
        [[maybe_unused]] const std::uint8_t serial = serials.TakeSend();
    }
    const FoxAntiObfuscation foxAnti;

    const auto encrypted = EncryptPacket(PacketOf(Bytes({0xC1, 0x05, 0x1E, 0xBE, 0x02})), identity.encryption, serials,
                                         foxAnti, std::byte{0xA5});

    ByteVector block = Bytes({0x03, 0xA6, 0x06, 0xBA, 0x1D, 0x00, 0x00, 0x00});
    const std::array cipher = EncryptBlock(identity.encryption, std::span(block).first<PlainBlockSize>(), 4);
    ByteVector expected = Bytes({0xC3, 0x0D});
    expected.insert(expected.end(), cipher.begin(), cipher.end());
    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), expected));
}

// encode_outbound: un C1 con XOR32 que sale al cable. Con D3 y D7 cambia el opcode y los campos,
// que se encadenan con él; con 20, nada. Los largos 3 y 4 son los bordes del XOR32: sin campos, y
// con uno solo. El de 40 da la vuelta a la clave de 32.
TEST(FoxAntiObfuscationTest, BeforeSendRemapsLikeTheMinimalFoxAnti)
{
    const std::vector<ReferenceCase> cases{
        {"A4.D3.L3", Bytes({0xC1, 0x03, 0xD3}), Bytes({0xC1, 0x03, 0xA3})},
        {"A4.D3.L4", Bytes({0xC1, 0x04, 0xD3, 0x42}), Bytes({0xC1, 0x04, 0xA3, 0x32})},
        {"A4.D3.L8", Bytes({0xC1, 0x08, 0xD3, 0x42, 0xE1, 0x75, 0xC7, 0x80}),
         Bytes({0xC1, 0x08, 0xA3, 0x32, 0x91, 0x05, 0xB7, 0xF0})},
        {"A4.D3.L40", Bytes({0xC1, 0x28, 0xD3, 0x42, 0xE1, 0x75, 0xC7, 0x80, 0x98, 0x72, 0xC5, 0x23, 0x3D, 0x3E,
                             0x62, 0x53, 0xAA, 0x1B, 0xF9, 0xFC, 0x99, 0x72, 0xC4, 0x3F, 0x57, 0x54, 0x04, 0x47,
                             0xA9, 0x36, 0x0B, 0x81, 0x85, 0x02, 0xC9, 0xB8, 0xFB, 0x4F, 0xDD, 0xBA}),
         Bytes({0xC1, 0x28, 0xA3, 0x32, 0x91, 0x05, 0xB7, 0xF0, 0xE8, 0x02, 0xB5, 0x53, 0x4D, 0x4E,
                0x12, 0x23, 0xDA, 0x6B, 0x89, 0x8C, 0xE9, 0x02, 0xB4, 0x4F, 0x27, 0x24, 0x74, 0x37,
                0xD9, 0x46, 0x7B, 0xF1, 0xF5, 0x72, 0xB9, 0xC8, 0x8B, 0x3F, 0xAD, 0xCA})},
        {"A4.D7.L3", Bytes({0xC1, 0x03, 0xD7}), Bytes({0xC1, 0x03, 0xA4})},
        {"A4.D7.L4", Bytes({0xC1, 0x04, 0xD7, 0x46}), Bytes({0xC1, 0x04, 0xA4, 0x35})},
        {"A4.D7.L8", Bytes({0xC1, 0x08, 0xD7, 0x46, 0xE5, 0x71, 0xC3, 0x84}),
         Bytes({0xC1, 0x08, 0xA4, 0x35, 0x96, 0x02, 0xB0, 0xF7})},
        {"A4.D7.L40", Bytes({0xC1, 0x28, 0xD7, 0x46, 0xE5, 0x71, 0xC3, 0x84, 0x9C, 0x76, 0xC1, 0x27, 0x39, 0x3A,
                             0x66, 0x57, 0xAE, 0x1F, 0xFD, 0xF8, 0x9D, 0x76, 0xC0, 0x3B, 0x53, 0x50, 0x00, 0x43,
                             0xAD, 0x32, 0x0F, 0x85, 0x81, 0x06, 0xCD, 0xBC, 0xFF, 0x4B, 0xD9, 0xBE}),
         Bytes({0xC1, 0x28, 0xA4, 0x35, 0x96, 0x02, 0xB0, 0xF7, 0xEF, 0x05, 0xB2, 0x54, 0x4A, 0x49,
                0x15, 0x24, 0xDD, 0x6C, 0x8E, 0x8B, 0xEE, 0x05, 0xB3, 0x48, 0x20, 0x23, 0x73, 0x30,
                0xDE, 0x41, 0x7C, 0xF6, 0xF2, 0x75, 0xBE, 0xCF, 0x8C, 0x38, 0xAA, 0xCD})},
        {"A4.20.L8", Bytes({0xC1, 0x08, 0x20, 0xB1, 0x12, 0x86, 0x34, 0x73}),
         Bytes({0xC1, 0x08, 0x20, 0xB1, 0x12, 0x86, 0x34, 0x73})},
        // Los de test_net_foxanti.c (mu-client), que pasan por encode_outbound sin el XOR32 del
        // juego.
        {"A6.net.c1", Bytes({0xC1, 0x04, 0xD3, 0x00}), Bytes({0xC1, 0x04, 0xA3, 0x70})},
        {"A6.net.partial", Bytes({0xC1, 0x0A, 0xD3, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}),
         Bytes({0xC1, 0x0A, 0xA3, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77})},
    };
    const FoxAntiObfuscation foxAnti;
    for (const ReferenceCase& reference : cases)
    {
        EXPECT_TRUE(SameBytes(foxAnti.BeforeSend(PacketOf(reference.in)).Bytes(), reference.out)) << reference.name;
    }
}

// Lo que hace un remapeo, en claro: sin el XOR32, el paquete es el mismo con el otro opcode. Cada
// opcode de 0 a 255 en un C1 de 40 bytes: solo D3 y D7 cambian.
TEST(FoxAntiObfuscationTest, BeforeSendOnlyChangesTheOpcodeOfD3AndD7)
{
    const FoxAntiObfuscation foxAnti;
    for (int opcode = 0; opcode < 256; ++opcode)
    {
        ByteVector plain = BytePattern(40, 7, 3);
        plain[0] = std::byte{0xC1};
        plain[1] = std::byte{40};
        plain[2] = static_cast<std::byte>(opcode);
        ByteVector expected = plain;
        expected[2] = opcode == 0xD3 ? std::byte{0xA3} : opcode == 0xD7 ? std::byte{0xA4} : plain[2];

        const Packet sent = foxAnti.BeforeSend(Xor32Encode(PacketOf(plain)));

        EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(BytesOf(sent))).Bytes(), expected)) << "opcode " << opcode;
    }
}

// Lo mismo cifrado, de punta a punta: un C1 con XOR32 pasa por EncryptPacket con FoxAnti y la clave
// identidad, se descifra, y sin el XOR32 es el mismo con el otro opcode. Cada opcode en un C1 de 13
// bytes (lo útil no llena el bloque, y el relleno va cifrado) y de 9 (lo útil llena el bloque, y el
// relleno queda afuera).
TEST(FoxAntiObfuscationTest, AnEncryptedPacketOnlyChangesTheOpcodeOf10And19And1E)
{
    const auto keys = IdentityPair();
    const FoxAntiObfuscation foxAnti;
    for (const std::size_t size : {std::size_t{13}, std::size_t{9}})
    {
        for (int opcode = 0; opcode < 256; ++opcode)
        {
            ByteVector plain = BytePattern(size, 13, 5);
            plain[0] = std::byte{0xC1};
            plain[1] = static_cast<std::byte>(size);
            plain[2] = static_cast<std::byte>(opcode);
            ByteVector expected = plain;
            expected[2] = opcode == 0x10   ? std::byte{0x1D}
                          : opcode == 0x19 ? std::byte{0xA5}
                          : opcode == 0x1E ? std::byte{0xA6}
                                           : plain[2];
            PacketSerials sending;
            PacketSerials receiving;

            const auto encrypted =
                EncryptPacket(Xor32Encode(PacketOf(plain)), keys.encryption, sending, foxAnti, std::byte{0xA5});
            ASSERT_TRUE(encrypted.HasValue());
            const auto decrypted = DecryptPacket(encrypted.Value(), keys.decryption, receiving);
            ASSERT_TRUE(decrypted.HasValue());

            EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(BytesOf(decrypted.Value().packet))).Bytes(), expected))
                << "tamaño " << size << ", opcode " << opcode;
        }
    }
}

// --- Lo que FoxAnti no toca ---

// encode_outbound solo mira los C1: un C3 (FIX_C3_SERIAL0) y un C4 (c4-serial2.bin) salen igual. El
// pedido de la lista sale en claro y sin remapeo, igual; y un C1 con XOR32 sin remapeo, el aviso,
// también. La escalera es solo del C1 F4 03 de 6 bytes: uno de 8, o uno de 6 con otro subcódigo,
// sale como vino aunque traiga un código de 400. Y un C3 o un C4 cuyo primer byte cifrado se lee
// como D3 o D7 tampoco cambia.
TEST(FoxAntiObfuscationTest, WhatIsNotRemappedGoesOutUntouched)
{
    const std::vector<ByteVector> untouched{
        NoticeSerial0(),
        ReadNetVector("c4-serial2.bin"),
        Bytes({0xC1, 0x04, 0xF4, 0x02}),
        NoticeBytes(),
        Bytes({0xC2, 0x00, 0x05, 0xD3, 0x01}),
        Bytes({0xC1, 0x08, 0xF4, 0x03, 0x90, 0x01, 0x00, 0x00}),
        Bytes({0xC1, 0x06, 0xF4, 0x02, 0x90, 0x01}),
        Bytes({0xC3, 0x05, 0xD3, 0x01, 0x02}),
        Bytes({0xC3, 0x05, 0xD7, 0x01, 0x02}),
        Bytes({0xC4, 0x00, 0x06, 0xD3, 0x01, 0x02}),
    };
    const FoxAntiObfuscation foxAnti;
    for (std::size_t k = 0; k < untouched.size(); ++k)
    {
        EXPECT_TRUE(SameBytes(foxAnti.BeforeSend(PacketOf(untouched[k])).Bytes(), untouched[k])) << "paquete " << k;
    }
}

// Lo que se cifra del aviso F1 03 y de un login (C1 31 F1 01, 49 bytes, con campos de prueba y
// XOR32), con su serial y su relleno: el opcode F1 no se remapea, y queda igual byte por byte.
TEST(FoxAntiObfuscationTest, TheNoticeAndALoginGoEncryptedUntouched)
{
    ByteVector login = BytePattern(49, 29, 17);
    login[0] = std::byte{0xC1};
    login[1] = std::byte{49};
    login[2] = std::byte{0xF1};
    login[3] = std::byte{0x01};
    const Packet loginWithXor32 = Xor32Encode(PacketOf(login));
    const FoxAntiObfuscation foxAnti;

    for (const ByteVector& c1 : {NoticeBytes(), BytesOf(loginWithXor32)})
    {
        ByteVector sealed(c1.begin() + 1, c1.end());
        sealed[0] = std::byte{0x05};
        sealed.push_back(std::byte{0xA5});
        const ByteVector before = sealed;

        foxAnti.BeforeEncrypt(sealed);

        EXPECT_TRUE(SameBytes(sealed, before)) << "un C1 de " << c1.size() << " bytes";
    }
}

// Lo que no es la lista ni el F3 11 pasa igual, al cortarse y antes del dispatcher.
TEST(FoxAntiObfuscationTest, OtherIncomingPacketsPassUntouched)
{
    const std::vector<ByteVector> untouched{
        Bytes({0xC1, 0x04, 0x00, 0x01}),             // el saludo del ConnectServer
        Bytes({0xC2, 0x00, 0x06, 0xF4, 0x03, 0x07}), // un C2 F4 que no es la lista
        Bytes({0xC2, 0x00, 0x04, 0xF4}),             // un C2 F4 sin subcódigo
        Bytes({0xC1, 0x05, 0xF4, 0x02, 0x01}),       // un C1 F4 02: la lista es un C2
        Bytes({0xC1, 0x06, 0xF4, 0x05, 0x02, 0x00}), // un C1 con 02 donde un C2 tiene el subcódigo
        Bytes({0xC2, 0x00, 0x06, 0xF3, 0x11, 0x00}), // el F3 11 de reset_queue es solo C1
        Bytes({0xC1, 0x03, 0xF3}),                   // un C1 F3 sin subcódigo
        Bytes({0xC1, 0x05, 0xF3, 0x12, 0x00}),       // otro subcódigo de F3
        NoticeSerial0(),                             // un C3: lo de adentro va cifrado
    };
    const FoxAntiObfuscation foxAnti;
    for (std::size_t k = 0; k < untouched.size(); ++k)
    {
        const auto afterCut = foxAnti.AfterCut(PacketOf(untouched[k]));
        ASSERT_TRUE(afterCut.HasValue()) << "paquete " << k;
        EXPECT_TRUE(SameBytes(afterCut.Value().Bytes(), untouched[k])) << "paquete " << k;
        const auto beforeDispatch = foxAnti.BeforeDispatch(PacketOf(untouched[k]));
        ASSERT_TRUE(beforeDispatch.HasValue()) << "paquete " << k;
        EXPECT_TRUE(SameBytes(beforeDispatch.Value().Bytes(), untouched[k])) << "paquete " << k;
    }
}

// --- Los códigos de servidor (Q-C17) ---

// La escalera de encode_outbound: los bordes de cada uno de sus 8 rangos, y uno debajo y otro
// arriba (la tabla de test_encode_outbound_ladder de mu-client, y los casos A2).
TEST(FoxAntiObfuscationTest, TheServerCodeLadderIsTheOriginalOne)
{
    struct Step
    {
        std::uint16_t chosen;
        std::uint16_t sent;
    };
    const std::vector<Step> steps{
        {0x103, 0x103}, {0x104, 0x064}, {0x117, 0x077}, {0x118, 0x05A}, {0x12B, 0x06D}, {0x12C, 0x050}, {0x13F, 0x063},
        {0x140, 0x046}, {0x153, 0x059}, {0x154, 0x03C}, {0x167, 0x04F}, {0x168, 0x032}, {0x17B, 0x045}, {0x17C, 0x00A},
        {0x18F, 0x01D}, {0x190, 0x000}, {0x1A3, 0x013}, {0x1A4, 0x1A4}, {0x000, 0x000},
    };
    const FoxAntiObfuscation foxAnti;
    for (const Step& step : steps)
    {
        EXPECT_TRUE(SameBytes(foxAnti.BeforeSend(PacketOf(ServerSelect(step.chosen))).Bytes(), ServerSelect(step.sent)))
            << "código " << step.chosen;
    }
}

// Paso 1 de la CL2.4 (TODO(human), lo resolvió Claude): la ida y vuelta del código 21, a mano con
// las dos tablas.
//
// - parse_f4_02: el grupo es (21 + 1) / 11 = 2, y el delta del grupo 2 es 0x154 (340): en la lista
//   de main.exe llega 21 + 340 = 361 (0x169).
// - main.exe elige ese servidor y manda su código, 361.
// - encode_outbound: 361 cae en el rango que empieza en 0x168 (360 a 379), que resta 0x136 (310):
//   al cable sale 361 - 310 = 51.
//
// Vuelve 51, no 21: la escalera no tiene el 0x154, así que cada grupo del 2 al 7 cae en el rango
// del grupo siguiente, que resta 30 menos. El grupo 8 (87 a 97) llega como 247 a 257, debajo del
// rango más bajo (0x104, 260), y sale sin restar: el código más 160. Los códigos 0 a 20 (grupos 0
// y 1) sí vuelven: 0 a 9 suman y restan 400, y 10 a 20 suman 370 y caen en el rango de 380 a 399,
// que resta 370.
//
// Contra la FoxAnti mínima (A3): cada código de 0 a 110 como única entrada de la lista, lo que
// queda en la lista de main.exe, y lo que sale al cable cuando main.exe lo elige. En cada rango los
// dos suben de a uno con el código, salvo desde el 98, que queda en 0.
TEST(FoxAntiObfuscationTest, ServerCodesComeBackTheSameOnlyFrom0To20)
{
    struct RoundTrip
    {
        std::uint16_t first;     // el primer código del rango
        std::uint16_t last;      // y el último
        std::uint16_t firstList; // lo que llega en la lista con el primero
        std::uint16_t firstSent; // y lo que sale al cable
        std::uint16_t step;      // cuánto suben los dos con cada código
    };
    const std::vector<RoundTrip> ranges{
        {0, 9, 0x190, 0, 1},       {10, 20, 0x17C, 10, 1},     {21, 31, 0x169, 0x33, 1}, {32, 42, 0x156, 0x3E, 1},
        {43, 53, 0x143, 0x49, 1},  {54, 64, 0x130, 0x54, 1},   {65, 75, 0x11D, 0x5F, 1}, {76, 86, 0x10A, 0x6A, 1},
        {87, 97, 0x0F7, 0x0F7, 1}, {98, 110, 0x000, 0x000, 0},
    };
    const FoxAntiObfuscation foxAnti;
    for (const RoundTrip& range : ranges)
    {
        for (std::uint16_t code = range.first; code <= range.last; ++code)
        {
            const auto offset = static_cast<std::uint16_t>((code - range.first) * range.step);
            const auto expectedList = static_cast<std::uint16_t>(range.firstList + offset);
            const auto expectedSent = static_cast<std::uint16_t>(range.firstSent + offset);

            const auto list = foxAnti.AfterCut(PacketOf(LabListWithCode(code)));
            ASSERT_TRUE(list.HasValue()) << "código " << code;
            const std::span<const std::byte> entry = list.Value().Bytes().subspan(7);
            const auto listed =
                static_cast<std::uint16_t>(std::to_integer<int>(entry[0]) | (std::to_integer<int>(entry[1]) << 8));
            const Packet sent = foxAnti.BeforeSend(PacketOf(ServerSelect(listed)));

            EXPECT_EQ(listed, expectedList) << "código " << code;
            EXPECT_TRUE(SameBytes(sent.Bytes(), ServerSelect(expectedSent))) << "código " << code;
            EXPECT_EQ(code <= 20, expectedSent == code) << "código " << code;
        }
    }
}

// Las listas de más de una entrada, con la carga copiada y el cuarto byte en 0 (C2 y C5), la
// vacía (C3), lo que viene detrás de las entradas, que queda en 0 (C4), y los dos casos de
// test_decode_inbound de mu-client, con el tamaño que miden.
TEST(FoxAntiObfuscationTest, ServerListsAreTranslatedLikeTheMinimalFoxAnti)
{
    const std::vector<ReferenceCase> cases{
        {"C1", LabServerList(), MainExeServerList()},
        {"C2",
         Bytes({0xC2, 0x00, 0x12, 0xF4, 0x02, 0x03, 0x09, 0x00, 0x11, 0xEE, 0x0A, 0x00, 0x22, 0xEE, 0x15, 0x00, 0x33,
                0xEE}),
         Bytes({0xC2, 0x00, 0x13, 0xF4, 0x06, 0x00, 0x03, 0x99, 0x01, 0x11, 0x00, 0x7C, 0x01, 0x22, 0x00, 0x69, 0x01,
                0x33, 0x00})},
        {"C3", Bytes({0xC2, 0x00, 0x06, 0xF4, 0x02, 0x00}), Bytes({0xC2, 0x00, 0x07, 0xF4, 0x06, 0x00, 0x00})},
        {"C4", Bytes({0xC2, 0x00, 0x0C, 0xF4, 0x02, 0x01, 0x05, 0x00, 0x07, 0xCC, 0x99, 0x88}),
         Bytes({0xC2, 0x00, 0x0D, 0xF4, 0x06, 0x00, 0x01, 0x95, 0x01, 0x07, 0x00, 0x00, 0x00})},
        {"C5",
         Bytes({0xC2, 0x00, 0x12, 0xF4, 0x02, 0x03, 0x61, 0x00, 0x44, 0xEE, 0x62, 0x00, 0x44, 0xEE, 0xFF, 0xFF, 0x44,
                0xEE}),
         Bytes({0xC2, 0x00, 0x13, 0xF4, 0x06, 0x00, 0x03, 0x01, 0x01, 0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
                0x44, 0x00})},
        {"mu-client 0x10", Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x10, 0x00, 0x5A, 0xEE}),
         Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x82, 0x01, 0x5A, 0x00})},
        {"mu-client 0x62", Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x62, 0x00, 0x33, 0x00}),
         Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x00, 0x00, 0x33, 0x00})},
    };
    const FoxAntiObfuscation foxAnti;
    for (const ReferenceCase& reference : cases)
    {
        const auto list = foxAnti.AfterCut(PacketOf(reference.in));
        ASSERT_TRUE(list.HasValue()) << reference.name;
        EXPECT_TRUE(SameBytes(list.Value().Bytes(), reference.out)) << reference.name;
    }
}

// Los F3 11 de reset_queue: los dos de la FoxAnti mínima con muestras vivas (D1 y D2), el más corto
// que se puede (D3) y uno de 20 bytes (D4), como el segundo de la sesión real.
TEST(FoxAntiObfuscationTest, SkillListsGetTheirByteLikeTheMinimalFoxAnti)
{
    const std::vector<ReferenceCase> cases{
        {"D1", Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68}),
         Bytes({0xC1, 0x09, 0xF3, 0x11, 0xFE, 0x00, 0x04, 0x16, 0x68})},
        {"D2", Bytes({0xC1, 0x08, 0xF3, 0x11, 0x01, 0x00, 0x2C, 0x04}),
         Bytes({0xC1, 0x09, 0xF3, 0x11, 0x01, 0x00, 0x00, 0x2C, 0x04})},
        {"D3", Bytes({0xC1, 0x05, 0xF3, 0x11, 0x07}), Bytes({0xC1, 0x06, 0xF3, 0x11, 0x07, 0x00})},
        {"D4", Bytes({0xC1, 0x14, 0xF3, 0x11, 0x05, 0x01, 0x0C, 0x17, 0x22, 0x2D,
                      0x38, 0x43, 0x4E, 0x59, 0x64, 0x6F, 0x7A, 0x85, 0x90, 0x9B}),
         Bytes({0xC1, 0x15, 0xF3, 0x11, 0x05, 0x00, 0x01, 0x0C, 0x17, 0x22, 0x2D,
                0x38, 0x43, 0x4E, 0x59, 0x64, 0x6F, 0x7A, 0x85, 0x90, 0x9B})},
    };
    const FoxAntiObfuscation foxAnti;
    for (const ReferenceCase& reference : cases)
    {
        const auto list = foxAnti.BeforeDispatch(PacketOf(reference.in));
        ASSERT_TRUE(list.HasValue()) << reference.name;
        EXPECT_TRUE(SameBytes(list.Value().Bytes(), reference.out)) << reference.name;
    }
}

// --- Los largos que el original no mira (Q-C09) ---

// C1 04 F3 11 tumba al original: reset_queue copia len - 5 bytes, que con 4 da la vuelta.
TEST(FoxAntiObfuscationTest, ASkillListWithoutPkt4IsInvalid)
{
    const auto list = FoxAntiObfuscation{}.BeforeDispatch(PacketOf(Bytes({0xC1, 0x04, 0xF3, 0x11})));

    ASSERT_FALSE(list.HasValue());
    EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket);
}

// Uno de 255 bytes pasaría a 256, que no entra en el byte del tamaño: el original lo deja en 0.
TEST(FoxAntiObfuscationTest, ASkillListOf255BytesIsInvalid)
{
    ByteVector bytes(255, std::byte{0x11});
    bytes[0] = std::byte{0xC1};
    bytes[1] = std::byte{0xFF};
    bytes[2] = std::byte{0xF3};
    bytes[3] = std::byte{0x11};

    const auto list = FoxAntiObfuscation{}.BeforeDispatch(PacketOf(bytes));

    ASSERT_FALSE(list.HasValue());
    EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket);
}

// El de 254, el más largo que entra, pasa a 255.
TEST(FoxAntiObfuscationTest, ASkillListOf254BytesBecomes255)
{
    ByteVector bytes(254, std::byte{0x11});
    bytes[0] = std::byte{0xC1};
    bytes[1] = std::byte{0xFE};
    bytes[2] = std::byte{0xF3};
    bytes[3] = std::byte{0x11};

    const auto list = FoxAntiObfuscation{}.BeforeDispatch(PacketOf(bytes));

    ASSERT_TRUE(list.HasValue());
    EXPECT_EQ(list.Value().Bytes().size(), 255u);
    EXPECT_EQ(list.Value().Bytes()[1], std::byte{0xFF});
    EXPECT_EQ(list.Value().Bytes()[5], std::byte{0x00});
}

// Una cantidad que no entra en el paquete: el original lee hasta ~1 KB de más. También la lista sin
// cantidad, y una a la que le falta el cuarto byte de su última entrada, que el original no lee
// pero es una lista mal armada.
TEST(FoxAntiObfuscationTest, AListWhoseCountDoesNotFitIsInvalid)
{
    const std::vector<ByteVector> lists{
        Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x02, 0x00, 0x00, 0x00, 0xCC}),
        Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0xFF, 0x00, 0x00, 0x00, 0xCC}),
        Bytes({0xC2, 0x00, 0x05, 0xF4, 0x02}),
        Bytes({0xC2, 0x00, 0x09, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00}),
    };
    const FoxAntiObfuscation foxAnti;
    for (std::size_t k = 0; k < lists.size(); ++k)
    {
        const auto list = foxAnti.AfterCut(PacketOf(lists[k]));
        ASSERT_FALSE(list.HasValue()) << "lista " << k;
        EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket) << "lista " << k;
    }
}

// Una lista de MaxPacketSize bytes pasaría a uno más, que el lector no deja pasar; una de uno menos
// queda justo en MaxPacketSize.
TEST(FoxAntiObfuscationTest, AListThatWouldNotFitIsInvalid)
{
    const FoxAntiObfuscation foxAnti;
    for (const std::size_t size : {MaxPacketSize, MaxPacketSize - 1})
    {
        ByteVector bytes(size);
        bytes[0] = std::byte{0xC2};
        bytes[1] = static_cast<std::byte>(size >> 8);
        bytes[2] = static_cast<std::byte>(size & 0xFF);
        bytes[3] = std::byte{0xF4};
        bytes[4] = std::byte{0x02};
        bytes[5] = std::byte{0x01};

        const auto list = foxAnti.AfterCut(PacketOf(bytes));

        ASSERT_EQ(list.HasValue(), size < MaxPacketSize) << "tamaño " << size;
        if (list.HasValue())
        {
            EXPECT_EQ(list.Value().Bytes().size(), MaxPacketSize);
            EXPECT_EQ(list.Value().Type(), PacketType::C2);
        }
        else
        {
            EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket);
        }
    }
}

// BeforeEncrypt con menos de dos bytes no tiene opcode: no toca nada.
TEST(FoxAntiObfuscationTest, BeforeEncryptWithoutAnOpcodeDoesNothing)
{
    ByteVector onlySerial = Bytes({0x10});

    FoxAntiObfuscation{}.BeforeEncrypt(onlySerial);
    FoxAntiObfuscation{}.BeforeEncrypt(std::span<std::byte>{});

    EXPECT_TRUE(SameBytes(onlySerial, Bytes({0x10})));
}

// Con el serial y el opcode solos, el opcode se remapea igual, como en send_forward.
TEST(FoxAntiObfuscationTest, BeforeEncryptRemapsAnOpcodeWithNothingAfterIt)
{
    ByteVector serialAndOpcode = Bytes({0x10, 0x10});

    FoxAntiObfuscation{}.BeforeEncrypt(serialAndOpcode);

    EXPECT_TRUE(SameBytes(serialAndOpcode, Bytes({0x10, 0x1D})));
}

} // namespace
