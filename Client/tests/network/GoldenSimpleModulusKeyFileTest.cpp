// Las claves reales: Enc1.dat y Dec2.dat del cliente, de la carpeta golden, y Dec1.dat y Enc2.dat
// del servidor, de la carpeta del cable. Ningún test muestra un valor de una clave, ni cuando
// falla: solo el carril.

#include "Network/Crypto/SimpleModulus.h"
#include "Network/Crypto/SimpleModulusKeyFile.h"

#include "support/FixedRandom.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

namespace
{

using Client::Network::Crypto::DecryptBlock;
using Client::Network::Crypto::DecryptedBlock;
using Client::Network::Crypto::DecryptionKey;
using Client::Network::Crypto::EncryptBlock;
using Client::Network::Crypto::EncryptionKey;
using Client::Network::Crypto::LoadDecryptionKey;
using Client::Network::Crypto::LoadEncryptionKey;
using Client::Network::Crypto::PlainBlockSize;
using Client::Tests::Below;
using Client::Tests::FixedSeed;
using Client::Tests::LoadKeyFile;
using Client::Tests::SameBytesWithoutValues;

// El valor más grande de un carril en claro, y el de un producto antes de dar la vuelta.
constexpr std::uint64_t MaxPlainLane = 0xFFFF;
constexpr std::uint64_t MaxProduct = std::numeric_limits<std::uint32_t>::max();

// Un par: la clave que cifra de un lado y la que descifra del otro.
struct KeyPair
{
    const char* name;
    EncryptionKey encryption;
    DecryptionKey decryption;
};

class GoldenSimpleModulusKeyFile : public Client::Tests::GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        // Lo que manda el cliente: lo cifra con Enc1.dat y el servidor lo descifra con Dec1.dat.
        // Lo que contesta el servidor: lo cifra con Enc2.dat y el cliente lo descifra con Dec2.dat.
        const std::optional<EncryptionKey> enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        const std::optional<DecryptionKey> dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        const std::optional<EncryptionKey> enc2 = LoadKeyFile(WireFile("Enc2.dat"), LoadEncryptionKey);
        const std::optional<DecryptionKey> dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        ASSERT_TRUE(enc1 && dec1 && enc2 && dec2);
        pairs = {{"Enc1.dat y Dec1.dat", *enc1, *dec1}, {"Enc2.dat y Dec2.dat", *enc2, *dec2}};
    }

    std::vector<KeyPair> pairs;
};

// En cada par, el mismo módulo y el mismo XOR, y en cada carril la clave de descifrar es la
// inversa de la de cifrar: su producto da 1 módulo m.
TEST_F(GoldenSimpleModulusKeyFile, EachPairHasInverseKeys)
{
    for (const KeyPair& pair : pairs)
    {
        for (std::size_t lane = 0; lane < pair.encryption.Modulus().size(); ++lane)
        {
            const std::uint64_t modulus = pair.encryption.Modulus()[lane];
            const std::uint64_t product =
                std::uint64_t{pair.encryption.Key()[lane]} * std::uint64_t{pair.decryption.Key()[lane]};

            EXPECT_TRUE(pair.decryption.Modulus()[lane] == modulus) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.decryption.XorKey()[lane] == pair.encryption.XorKey()[lane])
                << pair.name << ", carril " << lane;
            EXPECT_TRUE(product % modulus == 1) << pair.name << ", carril " << lane;
        }
    }
}

// Lo que supone c3c4_reference.py, y lo que hace falta para que descifrar deshaga cifrar sin que
// un producto dé la vuelta: el módulo pasa de 16 bits, así que un carril en claro entra entero; el
// XOR tiene 16 bits; la clave de cifrar por 0xFFFF entra en 32 bits; y la de descifrar por el
// carril cifrado más grande, m - 1, también.
TEST_F(GoldenSimpleModulusKeyFile, TheRealKeysFitTheLaneArithmetic)
{
    for (const KeyPair& pair : pairs)
    {
        for (std::size_t lane = 0; lane < pair.encryption.Modulus().size(); ++lane)
        {
            const std::uint64_t modulus = pair.encryption.Modulus()[lane];

            EXPECT_TRUE(modulus > MaxPlainLane) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.encryption.XorKey()[lane] <= MaxPlainLane) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.encryption.Key()[lane] * MaxPlainLane <= MaxProduct) << pair.name << ", carril " << lane;
            EXPECT_TRUE(pair.decryption.Key()[lane] * (modulus - 1) <= MaxProduct) << pair.name << ", carril " << lane;
        }
    }
}

// Lo que cifra un lado lo descifra el otro: bloques al azar, con todos los largos útiles.
TEST_F(GoldenSimpleModulusKeyFile, WhatOneSideEncryptsTheOtherDecrypts)
{
    constexpr std::uint32_t BlocksPerPair = 2000; // 250 por cada largo útil, de 1 a 8
    constexpr std::uint32_t ByteValues = 256;     // los valores de un byte
    std::mt19937 engine(FixedSeed);

    for (const KeyPair& pair : pairs)
    {
        for (std::uint32_t round = 0; round < BlocksPerPair; ++round)
        {
            std::array<std::byte, PlainBlockSize> plain{};
            for (std::byte& value : plain)
            {
                value = static_cast<std::byte>(Below(engine, ByteValues));
            }
            const auto useful = static_cast<std::uint8_t>(1 + round % PlainBlockSize);

            const std::optional<DecryptedBlock> decrypted =
                DecryptBlock(pair.decryption, EncryptBlock(pair.encryption, plain, useful));

            ASSERT_TRUE(decrypted.has_value()) << pair.name << ", vuelta " << round;
            EXPECT_TRUE(SameBytesWithoutValues(decrypted->bytes, plain)) << pair.name << ", vuelta " << round;
            EXPECT_EQ(decrypted->usefulSize, useful) << pair.name << ", vuelta " << round;
        }
    }
}

} // namespace
