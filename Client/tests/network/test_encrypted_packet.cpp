#include "Network/EncryptedPacket.h"

#include "Core/Expected.h"
#include "Network/Crypto/CryptoError.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/IObfuscation.h"
#include "Network/ObfuscationError.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/BytePattern.h"
#include "support/Bytes.h"
#include "support/FixedRandom.h"
#include "support/MuClientNetFixtures.h"
#include "support/NetVectors.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/SameBytes.h"
#include "support/SimpleModulusKeys.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Client::Network::EncryptPacket y DecryptPacket: un C1/C2 como C3/C4 y de vuelta, con los
// seriales, Q-C05 y Q-C10, y lo que EncryptPacket le da a la ofuscación (CL2.4). Los bloques solos
// son SimpleModulusTest, y los seriales solos, PacketSerialsTest.
namespace
{
using Client::Core::Expected;
using Client::Network::DecryptPacket;
using Client::Network::EncryptPacket;
using Client::Network::HeaderSize;
using Client::Network::IObfuscation;
using Client::Network::MaxPacketSize;
using Client::Network::MinPacketSize;
using Client::Network::ObfuscationError;
using Client::Network::Packet;
using Client::Network::PacketSerials;
using Client::Network::PacketType;
using Client::Network::SerialCheck;
using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::DecryptBlock;
using Client::Network::Crypto::DecryptedBlock;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::EncryptionKey;
using Client::Network::Crypto::PlainBlockSize;
using Client::Tests::Below;
using Client::Tests::BytePattern;
using Client::Tests::Bytes;
using Client::Tests::FixedSeed;
using Client::Tests::IdentityPair;
using Client::Tests::KeyPair;
using Client::Tests::NoticeBytes;
using Client::Tests::NoticeRandA5;
using Client::Tests::NoticeSerial0;
using Client::Tests::NoticeSerialFF;
using Client::Tests::PacketOf;
using Client::Tests::Pair17;
using Client::Tests::Pair18;
using Client::Tests::ReadNetVector;
using Client::Tests::SameBytes;

using ByteVector = std::vector<std::byte>;

// El opcode de los paquetes de prueba: el de los ejemplos de la CL2.0 y de c2-long.bin.
constexpr std::byte TestOpcode{0x42};

// El serial de los paquetes que no se pueden descifrar, que no es el que se espera, el 0: si un
// error resincronizara con el serial que trajo en vez de gastar el que se esperaba, el siguiente
// sería el 0x38 y no el 1.
constexpr int OtherSerial = 0x37;

// Escribe en la cabecera de `bytes` el tipo y el tamaño que mide: el byte 1 en un C1/C3, y los
// bytes 1 y 2, big-endian, en un C2/C4.
void PutHeader(ByteVector& bytes, PacketType type)
{
    const std::size_t size = bytes.size();
    bytes[0] = static_cast<std::byte>(type);
    if (HeaderSize(type) == 2)
    {
        bytes[1] = static_cast<std::byte>(size);
        return;
    }
    bytes[1] = static_cast<std::byte>(size >> 8);
    bytes[2] = static_cast<std::byte>(size & 0xFF);
}

// Un C1 o un C2 en claro de `size` bytes: la cabecera, TestOpcode y el resto con
// BytePattern(…, 37, 11), el patrón de c2-long.bin.
ByteVector PlainOfSize(PacketType type, std::size_t size)
{
    ByteVector bytes = BytePattern(size, 37, 11);
    PutHeader(bytes, type);
    bytes[HeaderSize(type)] = TestOpcode;
    return bytes;
}

// Lo que sigue a la cabecera: desde el opcode en un C1/C2.
std::span<const std::byte> AfterHeader(const Packet& packet)
{
    return packet.Bytes().subspan(HeaderSize(packet.Type()));
}

// Unos seriales que ya mandaron `sent` paquetes: el próximo sale con ese serial.
PacketSerials HavingSent(int sent)
{
    PacketSerials serials;
    for (int k = 0; k < sent; ++k)
    {
        [[maybe_unused]] const std::uint8_t serial = serials.TakeSend();
    }
    return serials;
}

// Unos seriales que esperan `expected` en el próximo paquete que llega.
PacketSerials Expecting(int expected)
{
    PacketSerials serials;
    for (int k = 0; k < expected; ++k)
    {
        serials.SkipReceive();
    }
    return serials;
}

// Un bloque en claro y cuántos de sus bytes dice que valen.
struct PlainBlock
{
    std::array<std::byte, PlainBlockSize> bytes{};
    std::uint8_t useful = 0;
};

// Un bloque con estos bytes, escritos como en Bytes(), y los que faltan hasta 8 en 0.
PlainBlock BlockOf(std::initializer_list<int> values, std::uint8_t useful)
{
    EXPECT_LE(values.size(), PlainBlockSize);
    const ByteVector given = Bytes(values);
    PlainBlock block;
    std::copy_n(given.begin(), std::min(given.size(), PlainBlockSize), block.bytes.begin());
    block.useful = useful;
    return block;
}

// Un C3 o un C4 armado bloque a bloque con EncryptBlock, sin EncryptPacket: así un test pone los
// bytes útiles que quiera, también los que EncryptPacket nunca escribe.
Packet EncryptedOf(PacketType type, const EncryptionKey& key, const std::vector<PlainBlock>& blocks)
{
    ByteVector bytes(HeaderSize(type));
    for (const PlainBlock& block : blocks)
    {
        const auto cipher = EncryptBlock(key, block.bytes, block.useful);
        bytes.insert(bytes.end(), cipher.begin(), cipher.end());
    }
    PutHeader(bytes, type);
    return PacketOf(std::move(bytes));
}

// FIX_C3_SERIAL0: el serial pisa el tamaño del C1, y se cifra desde ahí.
TEST(EncryptedPacketTest, EncryptsTheNoticeWithSerial0)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials);

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Value().Type(), PacketType::C3);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), NoticeSerial0()));
    EXPECT_EQ(serials.TakeSend(), 1);
}

// FIX_C3_RANDA5: el relleno es el byte que sigue a lo útil.
TEST(EncryptedPacketTest, EncryptsTheNoticeWithItsPadding)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials, std::byte{0xA5});

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), NoticeRandA5()));
}

// FIX_C3_SERIALFF: el paquete 256 sale con el serial 255, y el siguiente, con el 0.
TEST(EncryptedPacketTest, EncryptsTheNoticeWithSerial255AndWraps)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials = HavingSent(255);

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials);

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), NoticeSerialFF()));
    EXPECT_EQ(serials.TakeSend(), 0);
}

// Un C3 de mu-client y el serial con que salió.
struct NoticeFixture
{
    std::string_view name;
    ByteVector encrypted;
    int serial = 0;
};

// Los tres se descifran en el aviso, con su serial en orden.
TEST(EncryptedPacketTest, DecryptsTheMuClientFixturesIntoTheNotice)
{
    const KeyPair identity = IdentityPair();
    const std::vector<NoticeFixture> fixtures{
        {"FIX_C3_SERIAL0", NoticeSerial0(), 0},
        {"FIX_C3_RANDA5", NoticeRandA5(), 0},
        {"FIX_C3_SERIALFF", NoticeSerialFF(), 255},
    };

    for (const NoticeFixture& fixture : fixtures)
    {
        PacketSerials serials = Expecting(fixture.serial);

        const auto decrypted = DecryptPacket(PacketOf(fixture.encrypted), identity.decryption, serials);

        ASSERT_TRUE(decrypted.HasValue()) << fixture.name;
        EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << fixture.name;
        EXPECT_TRUE(decrypted.Value().packet.WasDecrypted()) << fixture.name;
        EXPECT_EQ(decrypted.Value().packet.Type(), PacketType::C1);
        EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), NoticeBytes())) << fixture.name;
    }
}

// El C2 largo de la CL2.2 (c2-long.bin) con el serial 2 da c4-serial2.bin, el FIX_C4_SERIAL2 de
// mu-client armado de nuevo: 298 útiles son 38 bloques, 418 bytes cifrados y 421 con la cabecera.
TEST(EncryptedPacketTest, EncryptsTheLongC2AsC4WithSerial2)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c2 = ReadNetVector("c2-long.bin");
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c2.empty());
    ASSERT_FALSE(c4.empty());
    PacketSerials serials = HavingSent(2);

    const auto encrypted = EncryptPacket(PacketOf(c2), identity.encryption, serials);

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Value().Type(), PacketType::C4);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), c4));
}

// Y el C4 se rearma en el C2: C4 da C2, con el tamaño en dos bytes.
TEST(EncryptedPacketTest, DecryptsTheC4IntoTheLongC2)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c2 = ReadNetVector("c2-long.bin");
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c2.empty());
    ASSERT_FALSE(c4.empty());
    PacketSerials serials = Expecting(2);

    const auto decrypted = DecryptPacket(PacketOf(c4), identity.decryption, serials);

    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder);
    EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), c2));
    EXPECT_TRUE(decrypted.Value().packet.WasDecrypted());
    EXPECT_EQ(decrypted.Value().packet.Type(), PacketType::C2);
}

// Con un par sintético y tres bloques, lo que da `wrap` de make_vectors.py, que usa el
// encrypt_block de c3c4_reference.py: un C1 de 20 bytes son 19 útiles (8, 8 y 3), con el serial 7
// y el relleno 0x5C en el byte 3 del tercer bloque.
TEST(EncryptedPacketTest, EncryptsThreeBlocksLikeTheReference)
{
    const KeyPair pair = Pair17();
    const ByteVector plain = Bytes({0xC1, 0x14, 0x42, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10});
    const ByteVector expected = Bytes({0xC3, 0x23, 0xF2, 0x99, 0x16, 0x68, 0xC2, 0x59, 0x64, 0x6B, 0x44, 0x89,
                                       0xBC, 0x36, 0x59, 0x55, 0x3A, 0x0D, 0x77, 0x51, 0x68, 0x38, 0xCD, 0xF8,
                                       0xBC, 0x71, 0x45, 0x43, 0xC1, 0x8D, 0x33, 0x48, 0x84, 0x8B, 0xB5});
    PacketSerials serials = HavingSent(7);

    const auto encrypted = EncryptPacket(PacketOf(plain), pair.encryption, serials, std::byte{0x5C});

    ASSERT_TRUE(encrypted.HasValue());
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes(), expected));
}

// Detrás de lo útil va el relleno, y el resto del último bloque en 0: el aviso tiene 5 útiles, así
// que su bloque en claro es [serial F1 7B C1 73], el relleno y dos ceros.
TEST(EncryptedPacketTest, PutsThePaddingRightAfterTheUsefulBytes)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials, std::byte{0xA5});

    ASSERT_TRUE(encrypted.HasValue());
    const std::span<const std::byte> cipher = AfterHeader(encrypted.Value());
    ASSERT_EQ(cipher.size(), Client::Network::Crypto::CipherBlockSize);
    const std::optional<DecryptedBlock> block =
        DecryptBlock(identity.decryption, cipher.first<Client::Network::Crypto::CipherBlockSize>());
    ASSERT_TRUE(block.has_value());
    EXPECT_TRUE(SameBytes(block->bytes, Bytes({0x00, 0xF1, 0x7B, 0xC1, 0x73, 0xA5, 0x00, 0x00})));
    EXPECT_EQ(block->usefulSize, 5);
}

// Un bloque lleno no tiene lugar para el relleno: un C1 de 9 bytes son 8 útiles, y el relleno no
// cambia nada.
TEST(EncryptedPacketTest, AFullBlockHasNoPadding)
{
    const KeyPair identity = IdentityPair();
    const Packet plain = PacketOf(PlainOfSize(PacketType::C1, 9));
    PacketSerials withZero;
    PacketSerials withA5;

    const auto zero = EncryptPacket(plain, identity.encryption, withZero);
    const auto a5 = EncryptPacket(plain, identity.encryption, withA5, std::byte{0xA5});

    ASSERT_TRUE(zero.HasValue());
    ASSERT_TRUE(a5.HasValue());
    EXPECT_EQ(zero.Value().Bytes().size(), 13u);
    EXPECT_TRUE(SameBytes(a5.Value().Bytes(), zero.Value().Bytes()));
}

// El borde entre C3 y C4 de un C1, por lo que mide cifrado (cmp eax,0x100 en 0x4e22a8): uno de
// 185 bytes son 184 útiles, 23 bloques y 253 cifrados, menos de 0x100, y sale como C3 de 255; uno
// de 186 son 24 bloques, 264 cifrados, y sale como C4 de 267.
TEST(EncryptedPacketTest, AC1GoesOutAsC3UpTo23Blocks)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto c3 = EncryptPacket(PacketOf(PlainOfSize(PacketType::C1, 185)), identity.encryption, serials);
    const auto c4 = EncryptPacket(PacketOf(PlainOfSize(PacketType::C1, 186)), identity.encryption, serials);

    ASSERT_TRUE(c3.HasValue());
    ASSERT_TRUE(c4.HasValue());
    EXPECT_EQ(c3.Value().Type(), PacketType::C3);
    EXPECT_EQ(c3.Value().Bytes().size(), 255u);
    EXPECT_TRUE(SameBytes(c3.Value().Bytes().first(2), Bytes({0xC3, 0xFF})));
    EXPECT_EQ(c4.Value().Type(), PacketType::C4);
    EXPECT_EQ(c4.Value().Bytes().size(), 267u);
    EXPECT_TRUE(SameBytes(c4.Value().Bytes().first(3), Bytes({0xC4, 0x01, 0x0B})));
}

// Un C1 que sale como C4 vuelve como C2, con un byte más de cabecera: el rearmado de un C4 es un C2
// (0x4e1d50), y el original hace lo mismo. Lo que sigue a la cabecera es igual.
TEST(EncryptedPacketTest, AC1ThatGoesOutAsC4ComesBackAsC2)
{
    const KeyPair identity = IdentityPair();
    const Packet plain = PacketOf(PlainOfSize(PacketType::C1, 186));
    PacketSerials serials;

    const auto encrypted = EncryptPacket(plain, identity.encryption, serials);
    ASSERT_TRUE(encrypted.HasValue());
    const auto decrypted = DecryptPacket(encrypted.Value(), identity.decryption, serials);

    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_EQ(decrypted.Value().packet.Type(), PacketType::C2);
    EXPECT_EQ(decrypted.Value().packet.Bytes().size(), 187u);
    EXPECT_TRUE(SameBytes(AfterHeader(decrypted.Value().packet), AfterHeader(plain)));
}

// Un C2 sale siempre como C4, aunque lo cifrado mida menos de 0x100: así lo arma la ventana de
// cartas (0x498820, el mov 0xc4 de 0x49941e, sin el cmp), el único emisor de un C2. Uno de 6 bytes
// son 4 útiles, un bloque, y sale como C4 de 14; vuelve como el mismo C2.
TEST(EncryptedPacketTest, AC2AlwaysGoesOutAsC4)
{
    const KeyPair identity = IdentityPair();
    const ByteVector plain = PlainOfSize(PacketType::C2, 6);
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(plain), identity.encryption, serials);
    ASSERT_TRUE(encrypted.HasValue());
    const auto decrypted = DecryptPacket(encrypted.Value(), identity.decryption, serials);

    EXPECT_EQ(encrypted.Value().Type(), PacketType::C4);
    EXPECT_TRUE(SameBytes(encrypted.Value().Bytes().first(3), Bytes({0xC4, 0x00, 0x0E})));
    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain));
}

// Q-C05: el C2 más grande que se cifra, 5.954 bytes: 5.952 útiles, 744 bloques y un C4 de 8.187, el
// más grande que entra en MaxPacketSize. Se descifra entero, en más bytes que los ~2.046 de la pila
// donde descifra el original.
TEST(EncryptedPacketTest, TheLargestC4DecryptsWhole)
{
    constexpr std::size_t LargestC2 = 5954; // el C2 más grande cuyo C4 entra en MaxPacketSize
    constexpr std::size_t LargestC4 = 8187; // ese C4: 744 bloques de 11 y la cabecera
    const KeyPair identity = IdentityPair();
    const ByteVector plain = PlainOfSize(PacketType::C2, LargestC2);
    PacketSerials serials;

    const auto encrypted = EncryptPacket(PacketOf(plain), identity.encryption, serials);
    ASSERT_TRUE(encrypted.HasValue());
    const auto decrypted = DecryptPacket(encrypted.Value(), identity.decryption, serials);

    static_assert(LargestC4 <= MaxPacketSize);
    EXPECT_EQ(encrypted.Value().Bytes().size(), LargestC4);
    ASSERT_TRUE(decrypted.HasValue());
    EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain));
}

// Uno de 5.955 daría un C4 de 8.198, más que MaxPacketSize: TooLarge, y no gasta el serial.
TEST(EncryptedPacketTest, AC2WhoseC4WouldNotFitIsTooLarge)
{
    constexpr std::size_t TooLargeC2 = 5955; // uno más que el más grande que se cifra
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto encrypted =
        EncryptPacket(PacketOf(PlainOfSize(PacketType::C2, TooLargeC2)), identity.encryption, serials);

    ASSERT_FALSE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Error(), CryptoError::TooLarge);
    EXPECT_EQ(serials.TakeSend(), 0);
}

// Q-C10: el C3 de [05] F1 02 01 se rearma como C1 05 F1 02 01, que declara lo que mide. El
// original declara C1 04, uno menos: cuenta el serial, que no copia (0x4e1d50). El C4 de lo mismo
// da un C2 de 6, y el original declara 4.
TEST(EncryptedPacketTest, TheRebuiltPacketDeclaresWhatItMeasures)
{
    const KeyPair identity = IdentityPair();
    const std::vector<PlainBlock> blocks{BlockOf({0x05, 0xF1, 0x02, 0x01}, 4)};
    PacketSerials fromC3 = Expecting(5);
    PacketSerials fromC4 = Expecting(5);

    const auto c1 =
        DecryptPacket(EncryptedOf(PacketType::C3, identity.encryption, blocks), identity.decryption, fromC3);
    const auto c2 =
        DecryptPacket(EncryptedOf(PacketType::C4, identity.encryption, blocks), identity.decryption, fromC4);

    ASSERT_TRUE(c1.HasValue());
    EXPECT_EQ(c1.Value().serial, SerialCheck::InOrder);
    EXPECT_TRUE(SameBytes(c1.Value().packet.Bytes(), Bytes({0xC1, 0x05, 0xF1, 0x02, 0x01})));
    ASSERT_TRUE(c2.HasValue());
    EXPECT_TRUE(SameBytes(c2.Value().packet.Bytes(), Bytes({0xC2, 0x00, 0x06, 0xF1, 0x02, 0x01})));
}

// Q-C04: un paquete con otro serial se descifra y se despacha igual, y dice Resynced. Esperando el
// 0 llega el 255; el que le sigue, el 0, ya está en orden.
TEST(EncryptedPacketTest, AnotherSerialStillDecryptsAndResyncs)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto first = DecryptPacket(PacketOf(NoticeSerialFF()), identity.decryption, serials);
    const auto second = DecryptPacket(PacketOf(NoticeSerial0()), identity.decryption, serials);

    ASSERT_TRUE(first.HasValue());
    EXPECT_EQ(first.Value().serial, SerialCheck::Resynced);
    EXPECT_TRUE(first.Value().packet.WasDecrypted());
    EXPECT_TRUE(SameBytes(first.Value().packet.Bytes(), NoticeBytes()));
    ASSERT_TRUE(second.HasValue());
    EXPECT_EQ(second.Value().serial, SerialCheck::InOrder);
    EXPECT_TRUE(second.Value().packet.WasDecrypted());
}

// Que descifrar falló con `error` y gastó el serial que se esperaba, el 0: el siguiente es el 1,
// aunque el paquete traiga OtherSerial.
void ExpectFailedAndSpentTheSerial(const Packet& encrypted, CryptoError error, std::string_view name)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto decrypted = DecryptPacket(encrypted, identity.decryption, serials);

    ASSERT_FALSE(decrypted.HasValue()) << name;
    EXPECT_EQ(decrypted.Error(), error) << name;
    EXPECT_EQ(serials.Receive(1), SerialCheck::InOrder) << name;
}

// Un bit cambiado en el checksum del aviso (su último byte, como el caso 6 de red_reference.py):
// ChecksumMismatch. El original no despacha, suma 1 al serial y manda el aviso (03, "C3/C4").
TEST(EncryptedPacketTest, ABadChecksumSpendsTheSerial)
{
    ByteVector bad = NoticeSerial0();
    bad.back() ^= std::byte{0x01};

    ExpectFailedAndSpentTheSerial(PacketOf(std::move(bad)), CryptoError::ChecksumMismatch, "C3");
}

// Lo cifrado que no mide un múltiplo de 11 (10 o 12 bytes, en un C3 y en un C4): NotWholeBlocks.
// El original lee de a 11 sin mirar si alcanzan, y el checksum casi seguro falla.
TEST(EncryptedPacketTest, CipherThatIsNotWholeBlocksSpendsTheSerial)
{
    for (const PacketType type : {PacketType::C3, PacketType::C4})
    {
        for (const std::size_t cipherSize : {std::size_t{10}, std::size_t{12}})
        {
            ByteVector bytes(HeaderSize(type) + cipherSize, std::byte{0x5A});
            PutHeader(bytes, type);

            ExpectFailedAndSpentTheSerial(PacketOf(std::move(bytes)), CryptoError::NotWholeBlocks,
                                          type == PacketType::C3 ? "C3" : "C4");
        }
    }
}

// Un caso de bloques mal armados.
struct BlocksCase
{
    std::string_view name;
    std::vector<PlainBlock> blocks;
};

// Un primer bloque de 8 bytes: OtherSerial, el aviso y tres más. `useful` dice cuántos valen.
PlainBlock FirstBlock(std::uint8_t useful)
{
    return BlockOf({OtherSerial, 0xF1, 0x7B, 0xC1, 0x73, 0x01, 0x02, 0x03}, useful);
}

// Bloques con un largo útil que EncryptPacket nunca escribe: InvalidBlock. Pasan el checksum, que
// no cubre el largo útil. El original suma lo que dicen sin mirar, despacha y compara el serial.
// Con 255, copiar lo útil del bloque pasaría de sus 8 bytes: ASan lo ve si el largo no se mira
// antes, en el último bloque y en uno del medio. Con el último que dice 0, el primer bloque pasó y
// trae su serial, y el error igual gasta el que se esperaba.
TEST(EncryptedPacketTest, ABlockWithAnImpossibleUsefulSizeSpendsTheSerial)
{
    constexpr std::uint8_t LargestUsefulSize = 0xFF; // lo más que dice el byte del largo útil
    const KeyPair identity = IdentityPair();
    const PlainBlock tail = BlockOf({0x11, 0x22}, 2);
    const std::vector<BlocksCase> cases{
        {"uno que dice 9", {FirstBlock(9)}},
        {"el último que dice 255", {FirstBlock(8), BlockOf({0x11, 0x22}, LargestUsefulSize)}},
        {"uno del medio que dice 7", {FirstBlock(7), tail}},
        {"uno del medio que dice 9", {FirstBlock(9), tail}},
        {"uno del medio que dice 255", {FirstBlock(LargestUsefulSize), tail}},
        {"el último que dice 0", {FirstBlock(8), BlockOf({}, 0)}},
    };

    for (const BlocksCase& blocksCase : cases)
    {
        ExpectFailedAndSpentTheSerial(EncryptedOf(PacketType::C3, identity.encryption, blocksCase.blocks),
                                      CryptoError::InvalidBlock, blocksCase.name);
    }
}

// Con un bloque que dice 9 y otro detrás con el checksum mal, gana ChecksumMismatch: el original
// corta en el primer checksum malo sin mirar los largos, y es el error que lleva el aviso.
TEST(EncryptedPacketTest, ABadChecksumWinsOverAnInvalidBlock)
{
    const KeyPair identity = IdentityPair();
    const Packet blocks = EncryptedOf(PacketType::C3, identity.encryption, {FirstBlock(9), BlockOf({0x11, 0x22}, 2)});
    ByteVector bad(blocks.Bytes().begin(), blocks.Bytes().end());
    bad.back() ^= std::byte{0x01};

    ExpectFailedAndSpentTheSerial(PacketOf(std::move(bad)), CryptoError::ChecksumMismatch, "C3");
}

// Un solo bloque con 1 útil trae el serial y no el opcode: TooShort, en un C3 y en un C4. El bloque
// pasó su checksum y su serial, OtherSerial, se sabe: igual se gasta el que se esperaba.
TEST(EncryptedPacketTest, OnlyTheSerialIsTooShort)
{
    const KeyPair identity = IdentityPair();
    const std::vector<PlainBlock> blocks{BlockOf({OtherSerial}, 1)};

    ExpectFailedAndSpentTheSerial(EncryptedOf(PacketType::C3, identity.encryption, blocks), CryptoError::TooShort,
                                  "C3");
    ExpectFailedAndSpentTheSerial(EncryptedOf(PacketType::C4, identity.encryption, blocks), CryptoError::TooShort,
                                  "C4");
}

// Cifrar un C3 o un C4 es WrongPacketType, y no gasta el serial de envío.
TEST(EncryptedPacketTest, EncryptingAC3OrC4IsTheWrongType)
{
    const KeyPair identity = IdentityPair();
    const ByteVector c4 = ReadNetVector("c4-serial2.bin");
    ASSERT_FALSE(c4.empty());
    PacketSerials serials;

    const auto fromC3 = EncryptPacket(PacketOf(NoticeSerial0()), identity.encryption, serials);
    const auto fromC4 = EncryptPacket(PacketOf(c4), identity.encryption, serials);

    ASSERT_FALSE(fromC3.HasValue());
    EXPECT_EQ(fromC3.Error(), CryptoError::WrongPacketType);
    ASSERT_FALSE(fromC4.HasValue());
    EXPECT_EQ(fromC4.Error(), CryptoError::WrongPacketType);
    EXPECT_EQ(serials.TakeSend(), 0);
}

// Descifrar un C1 o un C2 también, y no toca el serial de recepción.
TEST(EncryptedPacketTest, DecryptingAC1OrC2IsTheWrongType)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials;

    const auto fromC1 = DecryptPacket(PacketOf(NoticeBytes()), identity.decryption, serials);
    const auto fromC2 = DecryptPacket(PacketOf(PlainOfSize(PacketType::C2, 6)), identity.decryption, serials);

    ASSERT_FALSE(fromC1.HasValue());
    EXPECT_EQ(fromC1.Error(), CryptoError::WrongPacketType);
    ASSERT_FALSE(fromC2.HasValue());
    EXPECT_EQ(fromC2.Error(), CryptoError::WrongPacketType);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

// Un Packet al que ya se le sacaron los bytes (TakeBytes) conserva el tipo y queda vacío: cifrar el
// C1 o descifrar el C3 es WrongPacketType, y los dos seriales quedan como estaban.
TEST(EncryptedPacketTest, AnEmptiedPacketIsTheWrongType)
{
    const KeyPair identity = IdentityPair();
    Packet c1 = PacketOf(NoticeBytes());
    Packet c3 = PacketOf(NoticeSerial0());
    [[maybe_unused]] const ByteVector takenFromC1 = std::move(c1).TakeBytes();
    [[maybe_unused]] const ByteVector takenFromC3 = std::move(c3).TakeBytes();
    PacketSerials serials;

    const auto encrypted = EncryptPacket(c1, identity.encryption, serials);
    const auto decrypted = DecryptPacket(c3, identity.decryption, serials);

    ASSERT_FALSE(encrypted.HasValue());
    EXPECT_EQ(encrypted.Error(), CryptoError::WrongPacketType);
    ASSERT_FALSE(decrypted.HasValue());
    EXPECT_EQ(decrypted.Error(), CryptoError::WrongPacketType);
    EXPECT_EQ(serials.TakeSend(), 0);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

// Los paquetes más chicos, la cabecera y el opcode: un C1 de 3 bytes y un C2 de 4 son 2 útiles, el
// serial y el opcode, lo mínimo que DecryptPacket acepta. Van y vuelven iguales.
TEST(EncryptedPacketTest, TheSmallestPacketsGoAndComeBack)
{
    const KeyPair pair = Pair17();
    for (const PacketType type : {PacketType::C1, PacketType::C2})
    {
        const std::string_view name = type == PacketType::C1 ? "C1" : "C2";
        const ByteVector plain = PlainOfSize(type, MinPacketSize(type));
        PacketSerials serials;

        const auto encrypted = EncryptPacket(PacketOf(plain), pair.encryption, serials);
        ASSERT_TRUE(encrypted.HasValue()) << name;
        const auto decrypted = DecryptPacket(encrypted.Value(), pair.decryption, serials);

        ASSERT_TRUE(decrypted.HasValue()) << name;
        EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << name;
        EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain)) << name;
    }
}

// Cuántos valores tiene un byte: Below(engine, ByteValues) da uno al azar.
constexpr std::uint32_t ByteValues = 256;

// Un C1 o un C2 al azar, con el tamaño entre el mínimo y `largest`, y bytes al azar detrás de la
// cabecera.
ByteVector RandomPlain(std::mt19937& engine, PacketType type, std::size_t largest)
{
    const std::size_t smallest = MinPacketSize(type);
    const std::size_t size = smallest + Below(engine, static_cast<std::uint32_t>(largest - smallest + 1));
    ByteVector bytes(size);
    for (std::byte& value : bytes)
    {
        value = static_cast<std::byte>(Below(engine, ByteValues));
    }
    PutHeader(bytes, type);
    return bytes;
}

// Con los dos pares sintéticos, C1 y C2 al azar con relleno al azar: descifrar deshace cifrar, con
// los seriales en orden, también cuando dan la vuelta.
TEST(EncryptedPacketTest, DecryptUndoesEncryptWithSyntheticKeys)
{
    constexpr std::uint32_t Rounds = 600;             // más de 256: los seriales dan la vuelta
    constexpr std::size_t LargestC1ThatStaysC1 = 185; // uno más grande sale como C4, vuelve como C2
    constexpr std::size_t LargestRandomC2 = 700;      // pasa de 0x100: el byte alto no es 0
    std::mt19937 engine(FixedSeed);

    for (const KeyPair& pair : {Pair17(), Pair18()})
    {
        PacketSerials sender;
        PacketSerials receiver;
        for (std::uint32_t round = 0; round < Rounds; ++round)
        {
            const bool isC1 = Below(engine, 2) == 0;
            const ByteVector plain = isC1 ? RandomPlain(engine, PacketType::C1, LargestC1ThatStaysC1)
                                          : RandomPlain(engine, PacketType::C2, LargestRandomC2);
            const auto padding = static_cast<std::byte>(Below(engine, ByteValues));

            const auto encrypted = EncryptPacket(PacketOf(plain), pair.encryption, sender, padding);
            ASSERT_TRUE(encrypted.HasValue()) << "vuelta " << round;
            const auto decrypted = DecryptPacket(encrypted.Value(), pair.decryption, receiver);

            ASSERT_TRUE(decrypted.HasValue()) << "vuelta " << round;
            EXPECT_EQ(decrypted.Value().serial, SerialCheck::InOrder) << "vuelta " << round;
            EXPECT_TRUE(SameBytes(decrypted.Value().packet.Bytes(), plain)) << "vuelta " << round;
        }
    }
}

// --- Lo que EncryptPacket le da a la ofuscación (CL2.4) ---

// Una ofuscación que guarda lo que EncryptPacket le da para cifrar y le cambia el opcode: así se ve
// qué recibe BeforeEncrypt y que se cifra lo que deja. Lo demás pasa como vino.
class RecordingObfuscation final : public IObfuscation
{
public:
    // El opcode que deja en lo que se va a cifrar.
    static constexpr std::byte WrittenOpcode{0x5A};

    explicit RecordingObfuscation(ByteVector& seenBytes) : m_seen(&seenBytes) {}

    [[nodiscard]] std::byte InboundMask(std::byte /*firstWireByte*/) const override
    {
        return std::byte{0};
    }

    [[nodiscard]] Expected<Packet, ObfuscationError> AfterCut(Packet packet) const override
    {
        return packet;
    }

    [[nodiscard]] Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const override
    {
        return packet;
    }

    void BeforeEncrypt(std::span<std::byte> sealed) const override
    {
        m_seen->assign(sealed.begin(), sealed.end());
        if (sealed.size() > 1)
        {
            sealed[1] = WrittenOpcode;
        }
    }

    [[nodiscard]] Packet BeforeSend(Packet packet) const override
    {
        return packet;
    }

private:
    ByteVector* m_seen; // dónde guarda lo que recibió BeforeEncrypt la última vez
};

// BeforeEncrypt recibe [serial][lo útil desde el opcode][relleno]: el aviso, con el serial 4 y el
// relleno A5, son los 5 útiles y el relleno. Y lo que deja es lo que se cifra.
TEST(EncryptedPacketTest, BeforeEncryptGetsTheSerialTheUsefulBytesAndThePadding)
{
    const KeyPair identity = IdentityPair();
    PacketSerials serials = HavingSent(4);
    ByteVector seen;
    const RecordingObfuscation recording(seen);

    const auto encrypted =
        EncryptPacket(PacketOf(NoticeBytes()), identity.encryption, serials, recording, std::byte{0xA5});

    EXPECT_TRUE(SameBytes(seen, Bytes({0x04, 0xF1, 0x7B, 0xC1, 0x73, 0xA5})));
    ASSERT_TRUE(encrypted.HasValue());
    const std::span<const std::byte> cipher = AfterHeader(encrypted.Value());
    ASSERT_EQ(cipher.size(), Client::Network::Crypto::CipherBlockSize);
    const std::optional<DecryptedBlock> block =
        DecryptBlock(identity.decryption, cipher.first<Client::Network::Crypto::CipherBlockSize>());
    ASSERT_TRUE(block.has_value());
    EXPECT_TRUE(SameBytes(block->bytes, Bytes({0x04, 0x5A, 0x7B, 0xC1, 0x73, 0xA5, 0x00, 0x00})));
    EXPECT_EQ(block->usefulSize, 5);
}

// Con el bloque lleno, el relleno igual llega al final de lo que recibe BeforeEncrypt, aunque no se
// cifre: un C1 de 9 bytes y un C2 de 10 son 8 útiles, y BeforeEncrypt recibe 9. En el C2, el serial
// pisa el byte bajo del tamaño.
TEST(EncryptedPacketTest, BeforeEncryptGetsThePaddingAlsoWhenTheBlockIsFull)
{
    const KeyPair identity = IdentityPair();
    for (const auto& [type, size] :
         {std::pair{PacketType::C1, std::size_t{9}}, std::pair{PacketType::C2, std::size_t{10}}})
    {
        PacketSerials serials = HavingSent(7);
        ByteVector seen;
        const RecordingObfuscation recording(seen);
        const ByteVector plain = PlainOfSize(type, size);

        const auto encrypted = EncryptPacket(PacketOf(plain), identity.encryption, serials, recording, std::byte{0xA5});

        ASSERT_TRUE(encrypted.HasValue()) << "un C" << (type == PacketType::C1 ? 1 : 2);
        ByteVector expected(plain.begin() + static_cast<std::ptrdiff_t>(HeaderSize(type) - 1), plain.end());
        expected[0] = std::byte{0x07};
        expected.push_back(std::byte{0xA5});
        EXPECT_TRUE(SameBytes(seen, expected)) << "un C" << (type == PacketType::C1 ? 1 : 2);
        EXPECT_EQ(AfterHeader(encrypted.Value()).size(), Client::Network::Crypto::CipherBlockSize);
    }
}

// Recifrar un paquete recibido crea otro contenedor: no hereda la acreditación del decoder.
TEST(EncryptedPacketTest, EncryptionDoesNotClaimDecryptionEvenWhenTheInputWasDecrypted)
{
    const KeyPair identity = IdentityPair();
    for (const auto& bytes : {NoticeSerial0(), ReadNetVector("c4-serial2.bin")})
    {
        PacketSerials receiver;
        const auto decrypted = DecryptPacket(PacketOf(bytes), identity.decryption, receiver);
        ASSERT_TRUE(decrypted.HasValue());
        ASSERT_TRUE(decrypted.Value().packet.WasDecrypted());
        for (const bool wasDecrypted : {false, true})
        {
            const auto view = decrypted.Value().packet.Bytes();
            const Packet clear = PacketOf(ByteVector(view.begin(), view.end()));
            const Packet& plain = wasDecrypted ? decrypted.Value().packet : clear;
            PacketSerials sender;
            const auto encrypted = EncryptPacket(plain, identity.encryption, sender);
            ASSERT_TRUE(encrypted.HasValue());
            EXPECT_FALSE(encrypted.Value().WasDecrypted());
            EXPECT_EQ(encrypted.Value().Type(),
                      decrypted.Value().packet.Type() == PacketType::C1 ? PacketType::C3 : PacketType::C4);
            PacketSerials roundtripSerials;
            const auto roundtrip = DecryptPacket(encrypted.Value(), identity.decryption, roundtripSerials);
            ASSERT_TRUE(roundtrip.HasValue());
            EXPECT_TRUE(SameBytes(roundtrip.Value().packet.Bytes(), view));
        }
    }
}
} // namespace
