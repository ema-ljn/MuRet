#include "Crypto/CryptoError.h"

#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <set>
#include <string_view>

namespace
{
using Client::Crypto::CryptoError;
using Client::Crypto::CryptoErrorText;

// Two errors never say the same: the log has to tell them apart.
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
