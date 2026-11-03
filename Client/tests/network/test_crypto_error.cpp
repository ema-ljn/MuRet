#include "Network/Crypto/CryptoError.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <set>
#include <string_view>

// Client::Network::Crypto::CryptoErrorText: el error del cifrado en palabras, para el log (el plan
// de la CL4, CL4.4).
namespace
{
using Client::Network::Crypto::CryptoError;
using Client::Network::Crypto::CryptoErrorText;

// Cada error tiene su texto, y dos errores no dicen lo mismo: en el log tienen que distinguirse.
TEST(CryptoErrorTest, EveryErrorHasItsOwnText)
{
    const CryptoError errors[] = {
        CryptoError::InvalidKeyFile,   CryptoError::InvalidKey,   CryptoError::WrongPacketType,
        CryptoError::ChecksumMismatch, CryptoError::InvalidBlock, CryptoError::NotWholeBlocks,
        CryptoError::TooShort,         CryptoError::TooLarge,
    };
    std::set<std::string_view> texts;
    for (const CryptoError error : errors)
    {
        SCOPED_TRACE(::testing::PrintToString(error));
        const std::string_view text = CryptoErrorText(error);
        EXPECT_FALSE(text.empty());
        EXPECT_TRUE(texts.insert(text).second) << "repetido: " << text;
    }
}
} // namespace
