#include "Network/XorRemap/XorRemapObfuscation.h"

#include "App/LabXorRemapTables.h"
#include "Crypto/SimpleModulus.h"
#include "Network/EncryptedPacket.h"
#include "Network/IObfuscation.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketError.h"
#include "Network/PacketReader.h"
#include "Network/PacketSerials.h"
#include "Network/Xor32Packet.h"
#include "Network/XorRemap/XorRemapTables.h"

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

// Client::Network::XorRemap::XorRemapObfuscation: FoxAnti's packet layer, with the 03 wire examples, the per-packet
// mask in the PacketReader (Q-C18), the remaps, the round trip of the server codes (Q-C17) and the lengths the
// original does not check (Q-C09). CL2 plan, CL2.4.
//
// What each translation should give comes from the minimal FoxAnti (analysis/foxanti/minimal/src/packet.c in the
// research repo) built on the host with tests/vectors/net/tools/foxanti_cases.c: the case names (A1, B.1E.iv, ...)
// are that program's.
namespace
{
using Client::App::LabXorRemapTables;
using Client::Crypto::EncryptBlock;
using Client::Crypto::PlainBlockSize;
using Client::Network::DecryptPacket;
using Client::Network::EncryptPacket;
using Client::Network::MaxPacketSize;
using Client::Network::ObfuscationError;
using Client::Network::Packet;
using Client::Network::PacketError;
using Client::Network::PacketReader;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::Xor32Decode;
using Client::Network::Xor32Encode;
using Client::Network::XorRemap::XorRemapObfuscation;
using Client::Network::XorRemap::XorRemapTables;
using Client::Tests::BytePattern;
using Client::Tests::Bytes;
using Client::Tests::IdentityPair;
using Client::Tests::NoticeBytes;
using Client::Tests::NoticeSerial0;
using Client::Tests::ReadNetVector;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// The XOR the lab server applies to each byte it sends.
constexpr std::byte LabMask{0xAA};

// The layer with the lab server's tables, the only server it is tested against.
XorRemapObfuscation LabXorRemap()
{
    return XorRemapObfuscation{LabXorRemapTables()};
}

// If they do not form one, the test fails (and Value() aborts the program).
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

// The bytes as the lab server sends them: each one with the 0xAA.
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

// What the reader got until it ran out of packets, or the error it stopped with.
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

// A case of the minimal FoxAnti: what goes in and what comes out.
struct ReferenceCase
{
    std::string_view name;
    ByteVector in;
    ByteVector out;
};

// The list the lab's ConnectServer sends (04, "La lista de servidores"): one server, code 0, load 0 and the 0xCC that
// FoxAnti drops. And how main.exe reads it.
ByteVector LabServerList()
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
}

ByteVector MainExeServerList()
{
    return Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01, 0x00, 0x00});
}

ByteVector LabListWithCode(std::uint16_t code)
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, code & 0xFF, code >> 8, 0x00, 0xCC});
}

// The server code the client chooses, in C1 06 F4 03 <code LE>.
ByteVector ServerSelect(std::uint16_t code)
{
    return Bytes({0xC1, 0x06, 0xF4, 0x03, code & 0xFF, code >> 8});
}

// --- From the 03 wire ---

TEST(XorRemapObfuscationTest, TheLabServerListBecomesTheOneMainExeReads)
{
    const auto list = LabXorRemap().AfterCut(PacketOf(LabServerList()));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(SameBytes(list.Value().Bytes(), MainExeServerList()));
}

// main.exe sends its code, 400, and the 0 the lab expects goes out on the wire.
TEST(XorRemapObfuscationTest, TheChosenServerGoesOutWithTheLabCode)
{
    const Packet sent = LabXorRemap().BeforeSend(PacketOf(ServerSelect(400)));

    EXPECT_TRUE(SameBytes(sent.Bytes(), ServerSelect(0)));
}

// The first F3 11 of the real session (04, "F3 11, S->C"): the handler finds the slot at pkt[6].
TEST(XorRemapObfuscationTest, TheSkillListGetsTheByteMainExeExpects)
{
    const auto list = LabXorRemap().BeforeDispatch(PacketOf(Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68})));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(SameBytes(list.Value().Bytes(), Bytes({0xC1, 0x09, 0xF3, 0x11, 0xFE, 0x00, 0x04, 0x16, 0x68})));
}

// --- The mask (Q-C18) ---

// 6B, 68, 69 and 6E are C1 to C4 with the 0xAA. Nothing else carries it: not a C1 to C4 in clear either.
TEST(XorRemapObfuscationTest, OnlyTheFourMaskedTypesAreMasked)
{
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (int value = 0; value < 256; ++value)
    {
        const bool masked = value == 0x6B || value == 0x68 || value == 0x69 || value == 0x6E;
        EXPECT_EQ(xorRemap.InboundMask(static_cast<std::byte>(value)), masked ? LabMask : std::byte{0})
            << "byte " << value;
    }
}

// The three the original lets through (C1 with opcode 00, C1 with F4 and C2 with F4: the ConnectServer's) arrive in
// clear and come out the same, also together in one read.
TEST(XorRemapObfuscationTest, TheThreeExemptPacketsComeInClear)
{
    const ByteVector hello = Bytes({0xC1, 0x04, 0x00, 0x01});
    const ByteVector address = Bytes({0xC1, 0x16, 0xF4, 0x03, '1', '7',  '2',  '.',  '1',  '6',  '.',
                                      '1',  '9',  '8',  '.',  '1', 0x00, 0x00, 0x00, 0x00, 0x5D, 0xDA});
    const XorRemapObfuscation xorRemap = LabXorRemap();
    PacketReader reader(xorRemap);

    reader.Feed(Join({hello, address, LabServerList()}));
    const Drained drained = Drain(reader);

    EXPECT_EQ(drained.error, std::nullopt);
    ASSERT_EQ(drained.packets.size(), 3u);
    EXPECT_TRUE(SameBytes(drained.packets[0], hello));
    EXPECT_TRUE(SameBytes(drained.packets[1], address));
    EXPECT_TRUE(SameBytes(drained.packets[2], LabServerList()));
}

// A C3 (FIX_C3_SERIAL0) and a C4 (c4-serial2.bin, of 421 bytes, with the size in two bytes) with the 0xAA: the size
// travels masked too.
TEST(XorRemapObfuscationTest, MaskedC3AndC4AreCut)
{
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    const XorRemapObfuscation xorRemap = LabXorRemap();
    PacketReader reader(xorRemap);

    reader.Feed(Join({Masked(NoticeSerial0()), Masked(c4)}));
    const Drained drained = Drain(reader);

    EXPECT_EQ(drained.error, std::nullopt);
    ASSERT_EQ(drained.packets.size(), 2u);
    EXPECT_TRUE(SameBytes(drained.packets[0], NoticeSerial0()));
    EXPECT_TRUE(SameBytes(drained.packets[1], c4));
    EXPECT_EQ(reader.Pending(), 0u);
}

// Q-C18: the original decides the 0xAA by the first one of the read, and undoes the second's as if it were the
// first's. Per packet both come out right, in any order.
TEST(XorRemapObfuscationTest, AnExemptPacketAndAMaskedOneInTheSameReadGiveBoth)
{
    const ByteVector exempt = Bytes({0xC1, 0x04, 0xF4, 0x05});
    const ByteVector chat = Bytes({0xC1, 0x05, 0x42, 0x99, 0x10});
    const XorRemapObfuscation xorRemap = LabXorRemap();

    for (const bool exemptFirst : {true, false})
    {
        PacketReader reader(xorRemap);
        reader.Feed(exemptFirst ? Join({exempt, Masked(chat)}) : Join({Masked(chat), exempt}));
        const Drained drained = Drain(reader);

        SCOPED_TRACE(exemptFirst ? "el exento primero" : "el enmascarado primero");
        EXPECT_EQ(drained.error, std::nullopt);
        ASSERT_EQ(drained.packets.size(), 2u);
        EXPECT_TRUE(SameBytes(drained.packets[exemptFirst ? 0 : 1], exempt));
        EXPECT_TRUE(SameBytes(drained.packets[exemptFirst ? 1 : 0], chat));
    }
}

// A masked packet split between two reads keeps the XOR its first byte chose, and the one behind it in the second
// read, in clear, does not inherit it. A C2 and a C4 cut after their first and their second byte (the header
// halfway), and a C1 in the middle of the body.
TEST(XorRemapObfuscationTest, AMaskedPacketSplitBetweenTwoReadsGivesTheSame)
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
    const XorRemapObfuscation xorRemap = LabXorRemap();

    for (const Split& split : splits)
    {
        SCOPED_TRACE(split.name);
        const ByteVector wire = Masked(split.packet);
        PacketReader reader(xorRemap);

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

// One byte at a time too: the mask is chosen once per packet and is not lost between reads.
TEST(XorRemapObfuscationTest, MaskedPacketsOneByteAtATimeGiveTheSame)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector exempt = Bytes({0xC1, 0x04, 0xF4, 0x05});
    const ByteVector wire = Join({Masked(c2), exempt, Masked(NoticeSerial0())});
    const XorRemapObfuscation xorRemap = LabXorRemap();
    PacketReader reader(xorRemap);
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

// A first byte that is not a type, neither in clear nor with the 0xAA, is still InvalidType.
TEST(XorRemapObfuscationTest, AFirstByteThatIsNoTypeIsStillInvalid)
{
    const XorRemapObfuscation xorRemap = LabXorRemap();
    PacketReader reader(xorRemap);

    reader.Feed(Bytes({0x6A, 0x04, 0x00, 0x01}));
    const Drained drained = Drain(reader);

    EXPECT_EQ(drained.error, PacketError::InvalidType);
}

TEST(XorRemapObfuscationTest, AMovedReaderKeepsItsObfuscation)
{
    const XorRemapObfuscation xorRemap = LabXorRemap();
    PacketReader original(xorRemap);
    PacketReader moved(std::move(original));
    const ByteVector chat = Bytes({0xC1, 0x05, 0x42, 0x99, 0x10});

    moved.Feed(Masked(chat));
    const Drained drained = Drain(moved);

    ASSERT_EQ(drained.packets.size(), 1u);
    EXPECT_TRUE(SameBytes(drained.packets[0], chat));
}

// Q-C03 and Q-C18: a read that ends in the first bytes of a masked packet that comes after a whole one. The next Feed
// compacts the buffer with that packet half unmasked, and must not unmask again what it already unmasked.
TEST(XorRemapObfuscationTest, AMaskedPacketSplitBehindAWholeOneGivesTheSame)
{
    const ByteVector exempt = Bytes({0xC1, 0x04, 0xF4, 0x05});
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector wire = Join({exempt, Masked(c2), Masked(c2)});
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const std::size_t inFirstRead : {std::size_t{1}, std::size_t{2}, std::size_t{4}})
    {
        SCOPED_TRACE(inFirstRead);
        PacketReader reader(xorRemap);
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

// A reader moved in the middle of a masked packet, constructed and assigned, goes on with what it already unmasked,
// with that packet's mask and with the obfuscation for the one after it.
TEST(XorRemapObfuscationTest, AReaderMovedInTheMiddleOfAMaskedPacketGoesOn)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector wire = Join({Masked(c2), Masked(c2)});
    const std::span<const std::byte> view(wire);
    const XorRemapObfuscation xorRemap = LabXorRemap();

    PacketReader original(xorRemap);
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

// The one moved from, constructing or assigning, is left as new and with its obfuscation: a whole masked packet comes
// out right, without what it had unmasked before.
TEST(XorRemapObfuscationTest, AReaderMovedFromInTheMiddleOfAMaskedPacketStartsOver)
{
    const ByteVector c2 = Bytes({0xC2, 0x00, 0x06, 0x20, 0x03, 0x04});
    const ByteVector masked = Masked(c2);
    const std::span<const std::byte> view(masked);
    const XorRemapObfuscation xorRemap = LabXorRemap();

    PacketReader movedByConstruction(xorRemap);
    movedByConstruction.Feed(view.first(4));
    ASSERT_TRUE(Drain(movedByConstruction).packets.empty());
    const PacketReader constructed(std::move(movedByConstruction));
    PacketReader movedByAssignment(xorRemap);
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

// The XOR comes from the tables, not from the layer.
TEST(XorRemapObfuscationTest, UsesTheInjectedInboundXor)
{
    XorRemapTables tables = LabXorRemapTables();
    tables.inboundXor = std::byte{0x55};
    const XorRemapObfuscation xorRemap{tables};

    EXPECT_EQ(xorRemap.InboundMask(std::byte{0xC1} ^ std::byte{0x55}), std::byte{0x55});
    EXPECT_EQ(xorRemap.InboundMask(std::byte{0xC1} ^ LabMask), std::byte{0});
}

// A reader keeps a pointer to its obfuscation, so it does not accept a temporary, which would die before it. An
// IObfuscation is not assigned through the base either; a derived one is copied.
static_assert(!std::is_constructible_v<PacketReader, XorRemapObfuscation>);
static_assert(std::is_constructible_v<PacketReader, const XorRemapObfuscation&>);
static_assert(!std::is_copy_assignable_v<Client::Network::IObfuscation>);
static_assert(std::is_copy_constructible_v<XorRemapObfuscation>);

// --- The remaps, against the minimal FoxAnti ---

// send_forward: what gets encrypted, [serial][opcode][fields with XOR32][padding]. With 0x10, 0x19 and 0x1E the
// opcode changes, and so do the fields and the padding, which chain with it.
TEST(XorRemapObfuscationTest, BeforeEncryptRemapsLikeTheMinimalFoxAnti)
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
        // Like the real session's three A6: the first field is 0x29 (C1 05 1E 29 00 with XOR32).
        {"B.1E.iv", Bytes({0x03, 0x1E, 0xBE, 0x02, 0xA5}), Bytes({0x03, 0xA6, 0x06, 0xBA, 0x1D})},
        {"B.1C.ii.padA5", Bytes({0x00, 0x1C, 0x00, 0xA5}), Bytes({0x00, 0x1C, 0x00, 0xA5})},
        {"B.1C.iii", Bytes({0x07, 0x1C, 0x90, 0x3E, 0x93, 0x20, 0x6A, 0x0F, 0xF4, 0x6A, 0xB1, 0x82, 0x5C}),
         Bytes({0x07, 0x1C, 0x90, 0x3E, 0x93, 0x20, 0x6A, 0x0F, 0xF4, 0x6A, 0xB1, 0x82, 0x5C})},
    };
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const ReferenceCase& reference : cases)
    {
        ByteVector sealed = reference.in;
        xorRemap.BeforeEncrypt(sealed);
        EXPECT_TRUE(SameBytes(sealed, reference.out)) << reference.name;
    }
}

// EncryptPacket with FoxAnti encrypts what BeforeEncrypt left, padding included: B.1E.iv's C1 05 1E, with serial 3
// and padding A5, comes out as a C3 of 13 whose block is [03 A6 06 BA 1D] and three zeros, with 4 useful (the padding
// does not count).
TEST(XorRemapObfuscationTest, EncryptPacketCiphersWhatBeforeEncryptLeft)
{
    const auto identity = IdentityPair();
    PacketSerials serials;
    for (int k = 0; k < 3; ++k)
    {
        [[maybe_unused]] const std::uint8_t serial = serials.TakeSend();
    }
    const XorRemapObfuscation xorRemap = LabXorRemap();

    const auto encrypted = EncryptPacket(PacketOf(Bytes({0xC1, 0x05, 0x1E, 0xBE, 0x02})), identity.encryption, serials,
                                         xorRemap, std::byte{0xA5});

    ByteVector block = Bytes({0x03, 0xA6, 0x06, 0xBA, 0x1D, 0x00, 0x00, 0x00});
    const std::array cipher = EncryptBlock(identity.encryption, std::span(block).first<PlainBlockSize>(), 4);
    ByteVector expected = Bytes({0xC3, 0x0D});
    expected.insert(expected.end(), cipher.begin(), cipher.end());
    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), expected));
}

// encode_outbound: a C1 with XOR32 going out on the wire. With D3 and D7 the opcode changes and so do the fields,
// which chain with it; with 20, nothing. Lengths 3 and 4 are the XOR32 edges: no fields, and only one. The 40 one
// wraps around the 32 key.
TEST(XorRemapObfuscationTest, BeforeSendRemapsLikeTheMinimalFoxAnti)
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
        // The ones from test_net_foxanti.c (mu-client), which go through encode_outbound without the game's XOR32.
        {"A6.net.c1", Bytes({0xC1, 0x04, 0xD3, 0x00}), Bytes({0xC1, 0x04, 0xA3, 0x70})},
        {"A6.net.partial", Bytes({0xC1, 0x0A, 0xD3, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}),
         Bytes({0xC1, 0x0A, 0xA3, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77})},
    };
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const ReferenceCase& reference : cases)
    {
        EXPECT_TRUE(SameBytes(xorRemap.BeforeSend(PacketOf(reference.in)).Bytes(), reference.out)) << reference.name;
    }
}

// What a remap does, in clear: without the XOR32, the packet is the same with the other opcode. Every opcode from 0
// to 255 in a 40-byte C1: only D3 and D7 change.
TEST(XorRemapObfuscationTest, BeforeSendOnlyChangesTheOpcodeOfD3AndD7)
{
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (int opcode = 0; opcode < 256; ++opcode)
    {
        ByteVector plain = BytePattern(40, 7, 3);
        plain[0] = std::byte{0xC1};
        plain[1] = std::byte{40};
        plain[2] = static_cast<std::byte>(opcode);
        ByteVector expected = plain;
        expected[2] = opcode == 0xD3 ? std::byte{0xA3} : opcode == 0xD7 ? std::byte{0xA4} : plain[2];

        const Packet sent = xorRemap.BeforeSend(Xor32Encode(PacketOf(plain)));

        EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(BytesOf(sent))).Bytes(), expected)) << "opcode " << opcode;
    }
}

// The same encrypted, end to end: a C1 with XOR32 goes through EncryptPacket with FoxAnti and the identity key, is
// decrypted, and without the XOR32 it is the same with the other opcode. Every opcode in a C1 of 13 bytes (the
// useful part does not fill the block, and the padding is encrypted) and of 9 (the useful part fills the block, and
// the padding stays out).
TEST(XorRemapObfuscationTest, AnEncryptedPacketOnlyChangesTheOpcodeOf10And19And1E)
{
    const auto keys = IdentityPair();
    const XorRemapObfuscation xorRemap = LabXorRemap();
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
                EncryptPacket(Xor32Encode(PacketOf(plain)), keys.encryption, sending, xorRemap, std::byte{0xA5});
            ASSERT_TRUE(encrypted.HasValue());
            const auto decrypted = DecryptPacket(encrypted.Value(), keys.decryption, receiving);
            ASSERT_TRUE(decrypted.HasValue());

            EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(BytesOf(decrypted.Value().packet))).Bytes(), expected))
                << "tamaño " << size << ", opcode " << opcode;
        }
    }
}

// --- What FoxAnti does not touch ---

// encode_outbound only looks at the C1: a C3 (FIX_C3_SERIAL0) and a C4 (c4-serial2.bin) come out the same. The list
// request goes out in clear and without a remap, the same; and so does a C1 with XOR32 without a remap, the notice.
// The ladder is only for the 6-byte C1 F4 03: one of 8, or one of 6 with another subcode, comes out as it came even
// if it carries a code of 400. And a C3 or a C4 whose first encrypted byte reads as D3 or D7 does not change either.
TEST(XorRemapObfuscationTest, WhatIsNotRemappedGoesOutUntouched)
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
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (std::size_t k = 0; k < untouched.size(); ++k)
    {
        EXPECT_TRUE(SameBytes(xorRemap.BeforeSend(PacketOf(untouched[k])).Bytes(), untouched[k])) << "paquete " << k;
    }
}

// What gets encrypted of the F1 03 notice and of a login (C1 31 F1 01, 49 bytes, with test fields and XOR32), with
// its serial and its padding: the F1 opcode is not remapped, and stays the same byte for byte.
TEST(XorRemapObfuscationTest, TheNoticeAndALoginGoEncryptedUntouched)
{
    ByteVector login = BytePattern(49, 29, 17);
    login[0] = std::byte{0xC1};
    login[1] = std::byte{49};
    login[2] = std::byte{0xF1};
    login[3] = std::byte{0x01};
    const Packet loginWithXor32 = Xor32Encode(PacketOf(login));
    const XorRemapObfuscation xorRemap = LabXorRemap();

    for (const ByteVector& c1 : {NoticeBytes(), BytesOf(loginWithXor32)})
    {
        ByteVector sealed(c1.begin() + 1, c1.end());
        sealed[0] = std::byte{0x05};
        sealed.push_back(std::byte{0xA5});
        const ByteVector before = sealed;

        xorRemap.BeforeEncrypt(sealed);

        EXPECT_TRUE(SameBytes(sealed, before)) << "un C1 de " << c1.size() << " bytes";
    }
}

// What is neither the list nor the F3 11 passes unchanged, when cut and before the dispatcher.
TEST(XorRemapObfuscationTest, OtherIncomingPacketsPassUntouched)
{
    const std::vector<ByteVector> untouched{
        Bytes({0xC1, 0x04, 0x00, 0x01}),             // the ConnectServer's hello
        Bytes({0xC2, 0x00, 0x06, 0xF4, 0x03, 0x07}), // a C2 F4 that is not the list
        Bytes({0xC2, 0x00, 0x04, 0xF4}),             // a C2 F4 without a subcode
        Bytes({0xC1, 0x05, 0xF4, 0x02, 0x01}),       // a C1 F4 02: the list is a C2
        Bytes({0xC1, 0x06, 0xF4, 0x05, 0x02, 0x00}), // a C1 with 02 where a C2 has the subcode
        Bytes({0xC2, 0x00, 0x06, 0xF3, 0x11, 0x00}), // reset_queue's F3 11 is C1 only
        Bytes({0xC1, 0x03, 0xF3}),                   // a C1 F3 without a subcode
        Bytes({0xC1, 0x05, 0xF3, 0x12, 0x00}),       // another F3 subcode
        NoticeSerial0(),                             // a C3: the inside is encrypted
    };
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (std::size_t k = 0; k < untouched.size(); ++k)
    {
        const auto afterCut = xorRemap.AfterCut(PacketOf(untouched[k]));
        ASSERT_TRUE(afterCut.HasValue()) << "paquete " << k;
        EXPECT_TRUE(SameBytes(afterCut.Value().Bytes(), untouched[k])) << "paquete " << k;
        const auto beforeDispatch = xorRemap.BeforeDispatch(PacketOf(untouched[k]));
        ASSERT_TRUE(beforeDispatch.HasValue()) << "paquete " << k;
        EXPECT_TRUE(SameBytes(beforeDispatch.Value().Bytes(), untouched[k])) << "paquete " << k;
    }
}

// --- The server codes (Q-C17) ---

// The encode_outbound ladder: the edges of each of its 8 ranges, and one below and one above (mu-client's
// test_encode_outbound_ladder table, and the A2 cases).
TEST(XorRemapObfuscationTest, TheServerCodeLadderIsTheOriginalOne)
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
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const Step& step : steps)
    {
        EXPECT_TRUE(
            SameBytes(xorRemap.BeforeSend(PacketOf(ServerSelect(step.chosen))).Bytes(), ServerSelect(step.sent)))
            << "código " << step.chosen;
    }
}

// The round trip of code 21, by hand with both tables:
//
// - parse_f4_02: the group is (21 + 1) / 11 = 2, and group 2's delta is 0x154 (340): main.exe's list gets
//   21 + 340 = 361 (0x169).
// - main.exe chooses that server and sends its code, 361.
// - encode_outbound: 361 falls in the range that starts at 0x168 (360 to 379), which subtracts 0x136 (310): the
//   wire gets 361 - 310 = 51.
//
// 51 comes back, not 21: the ladder lacks the 0x154, so each group from 2 to 7 falls in the next group's range, which
// subtracts 30 less. Group 8 (87 to 97) arrives as 247 to 257, below the lowest range (0x104, 260), and goes out
// without subtracting: the code plus 160. Codes 0 to 20 (groups 0 and 1) do come back: 0 to 9 add and subtract 400,
// and 10 to 20 add 370 and fall in the range from 380 to 399, which subtracts 370.
//
// Against the minimal FoxAnti (A3): each code from 0 to 110 as the only entry of the list, what stays in main.exe's
// list, and what goes out on the wire when main.exe chooses it. In each range both go up by one with the code, except
// from 98, which stays at 0.
TEST(XorRemapObfuscationTest, ServerCodesComeBackTheSameOnlyFrom0To20)
{
    struct RoundTrip
    {
        std::uint16_t first;     // the range's first code
        std::uint16_t last;      // and the last
        std::uint16_t firstList; // what arrives in the list with the first
        std::uint16_t firstSent; // and what goes out on the wire
        std::uint16_t step;      // how much both go up with each code
    };
    const std::vector<RoundTrip> ranges{
        {0, 9, 0x190, 0, 1},       {10, 20, 0x17C, 10, 1},     {21, 31, 0x169, 0x33, 1}, {32, 42, 0x156, 0x3E, 1},
        {43, 53, 0x143, 0x49, 1},  {54, 64, 0x130, 0x54, 1},   {65, 75, 0x11D, 0x5F, 1}, {76, 86, 0x10A, 0x6A, 1},
        {87, 97, 0x0F7, 0x0F7, 1}, {98, 110, 0x000, 0x000, 0},
    };
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const RoundTrip& range : ranges)
    {
        for (std::uint16_t code = range.first; code <= range.last; ++code)
        {
            const auto offset = static_cast<std::uint16_t>((code - range.first) * range.step);
            const auto expectedList = static_cast<std::uint16_t>(range.firstList + offset);
            const auto expectedSent = static_cast<std::uint16_t>(range.firstSent + offset);

            const auto list = xorRemap.AfterCut(PacketOf(LabListWithCode(code)));
            ASSERT_TRUE(list.HasValue()) << "código " << code;
            const std::span<const std::byte> entry = list.Value().Bytes().subspan(7);
            const auto listed =
                static_cast<std::uint16_t>(std::to_integer<int>(entry[0]) | (std::to_integer<int>(entry[1]) << 8));
            const Packet sent = xorRemap.BeforeSend(PacketOf(ServerSelect(listed)));

            EXPECT_EQ(listed, expectedList) << "código " << code;
            EXPECT_TRUE(SameBytes(sent.Bytes(), ServerSelect(expectedSent))) << "código " << code;
            EXPECT_EQ(code <= 20, expectedSent == code) << "código " << code;
        }
    }
}

// The lists of more than one entry, with the load copied and the fourth byte at 0 (C2 and C5), the empty one (C3),
// what comes after the entries, which stays at 0 (C4), and mu-client's two test_decode_inbound cases, with the size
// they measure.
TEST(XorRemapObfuscationTest, ServerListsAreTranslatedLikeTheMinimalFoxAnti)
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
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const ReferenceCase& reference : cases)
    {
        const auto list = xorRemap.AfterCut(PacketOf(reference.in));
        ASSERT_TRUE(list.HasValue()) << reference.name;
        EXPECT_TRUE(SameBytes(list.Value().Bytes(), reference.out)) << reference.name;
    }
}

// reset_queue's F3 11: the minimal FoxAnti's two with live samples (D1 and D2), the shortest possible (D3) and one
// of 20 bytes (D4), like the real session's second one.
TEST(XorRemapObfuscationTest, SkillListsGetTheirByteLikeTheMinimalFoxAnti)
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
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const ReferenceCase& reference : cases)
    {
        const auto list = xorRemap.BeforeDispatch(PacketOf(reference.in));
        ASSERT_TRUE(list.HasValue()) << reference.name;
        EXPECT_TRUE(SameBytes(list.Value().Bytes(), reference.out)) << reference.name;
    }
}

// --- The lengths the original does not check (Q-C09) ---

// C1 04 F3 11 crashes the original: reset_queue copies len - 5 bytes, which with 4 wraps around.
TEST(XorRemapObfuscationTest, ASkillListWithoutPkt4IsInvalid)
{
    const auto list = LabXorRemap().BeforeDispatch(PacketOf(Bytes({0xC1, 0x04, 0xF3, 0x11})));

    ASSERT_FALSE(list.HasValue());
    EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket);
}

// One of 255 bytes would become 256, which does not fit in the size byte: the original leaves it at 0.
TEST(XorRemapObfuscationTest, ASkillListOf255BytesIsInvalid)
{
    ByteVector bytes(255, std::byte{0x11});
    bytes[0] = std::byte{0xC1};
    bytes[1] = std::byte{0xFF};
    bytes[2] = std::byte{0xF3};
    bytes[3] = std::byte{0x11};

    const auto list = LabXorRemap().BeforeDispatch(PacketOf(bytes));

    ASSERT_FALSE(list.HasValue());
    EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket);
}

// The 254 one, the longest that fits, becomes 255.
TEST(XorRemapObfuscationTest, ASkillListOf254BytesBecomes255)
{
    ByteVector bytes(254, std::byte{0x11});
    bytes[0] = std::byte{0xC1};
    bytes[1] = std::byte{0xFE};
    bytes[2] = std::byte{0xF3};
    bytes[3] = std::byte{0x11};

    const auto list = LabXorRemap().BeforeDispatch(PacketOf(bytes));

    ASSERT_TRUE(list.HasValue());
    EXPECT_EQ(list.Value().Bytes().size(), 255u);
    EXPECT_EQ(list.Value().Bytes()[1], std::byte{0xFF});
    EXPECT_EQ(list.Value().Bytes()[5], std::byte{0x00});
}

// A count that does not fit in the packet: the original reads up to ~1 KB too much. Also the list without a count,
// and one missing the fourth byte of its last entry, which the original does not read but is a malformed list.
TEST(XorRemapObfuscationTest, AListWhoseCountDoesNotFitIsInvalid)
{
    const std::vector<ByteVector> lists{
        Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x02, 0x00, 0x00, 0x00, 0xCC}),
        Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0xFF, 0x00, 0x00, 0x00, 0xCC}),
        Bytes({0xC2, 0x00, 0x05, 0xF4, 0x02}),
        Bytes({0xC2, 0x00, 0x09, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00}),
    };
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (std::size_t k = 0; k < lists.size(); ++k)
    {
        const auto list = xorRemap.AfterCut(PacketOf(lists[k]));
        ASSERT_FALSE(list.HasValue()) << "lista " << k;
        EXPECT_EQ(list.Error(), ObfuscationError::InvalidPacket) << "lista " << k;
    }
}

// A list of MaxPacketSize bytes would become one more, which the reader does not let through; one of one less ends
// up exactly at MaxPacketSize.
TEST(XorRemapObfuscationTest, AListThatWouldNotFitIsInvalid)
{
    const XorRemapObfuscation xorRemap = LabXorRemap();
    for (const std::size_t size : {MaxPacketSize, MaxPacketSize - 1})
    {
        ByteVector bytes(size);
        bytes[0] = std::byte{0xC2};
        bytes[1] = static_cast<std::byte>(size >> 8);
        bytes[2] = static_cast<std::byte>(size & 0xFF);
        bytes[3] = std::byte{0xF4};
        bytes[4] = std::byte{0x02};
        bytes[5] = std::byte{0x01};

        const auto list = xorRemap.AfterCut(PacketOf(bytes));

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

// With fewer than two bytes there is no opcode.
TEST(XorRemapObfuscationTest, BeforeEncryptWithoutAnOpcodeDoesNothing)
{
    ByteVector onlySerial = Bytes({0x10});

    LabXorRemap().BeforeEncrypt(onlySerial);
    LabXorRemap().BeforeEncrypt(std::span<std::byte>{});

    EXPECT_TRUE(SameBytes(onlySerial, Bytes({0x10})));
}

// With the serial and the opcode alone, the opcode is still remapped, as in send_forward.
TEST(XorRemapObfuscationTest, BeforeEncryptRemapsAnOpcodeWithNothingAfterIt)
{
    ByteVector serialAndOpcode = Bytes({0x10, 0x10});

    LabXorRemap().BeforeEncrypt(serialAndOpcode);

    EXPECT_TRUE(SameBytes(serialAndOpcode, Bytes({0x10, 0x1D})));
}

// Direct fixture of the transformation: AfterCut normally gets a clear list, before the decoder. The true here checks
// that rebuilding does not erase metadata already present.
TEST(XorRemapObfuscationTest, RebuildingTheServerListPreservesDecryptionProvenance)
{
    for (const bool wasDecrypted : {false, true})
    {
        auto packet = Packet::FromBytes(LabServerList(), wasDecrypted);
        ASSERT_TRUE(packet.HasValue());
        const auto translated = LabXorRemap().AfterCut(std::move(packet).Value());
        ASSERT_TRUE(translated.HasValue());
        EXPECT_EQ(translated.Value().WasDecrypted(), wasDecrypted);
        EXPECT_TRUE(SameBytes(translated.Value().Bytes(), MainExeServerList()));
    }
}

TEST(XorRemapObfuscationTest, RebuildingTheSkillListPreservesDecryptionProvenance)
{
    const auto identity = IdentityPair();
    const auto plain = Bytes({0xC1, 0x08, 0xF3, 0x11, 0xFE, 0x04, 0x16, 0x68});
    for (const bool encrypted : {false, true})
    {
        Packet packet = PacketOf(plain);
        if (encrypted)
        {
            PacketSerials sender;
            const auto cipher = EncryptPacket(packet, identity.encryption, sender);
            ASSERT_TRUE(cipher.HasValue());
            PacketSerials receiver;
            auto decoded = DecryptPacket(cipher.Value(), identity.decryption, receiver);
            ASSERT_TRUE(decoded.HasValue());
            packet = std::move(decoded).Value().packet;
        }
        const auto translated = LabXorRemap().BeforeDispatch(std::move(packet));
        ASSERT_TRUE(translated.HasValue());
        EXPECT_EQ(translated.Value().WasDecrypted(), encrypted);
        EXPECT_TRUE(
            SameBytes(translated.Value().Bytes(), Bytes({0xC1, 0x09, 0xF3, 0x11, 0xFE, 0x00, 0x04, 0x16, 0x68})));
    }
}
} // namespace
