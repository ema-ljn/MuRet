#include "Tables/Credits.h"

#include "Crypto/Xor3.h"
#include "Data/DataError.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// credit.bmd (0x63ff30): 400 records of 33 bytes with the login's XOR3 (0x4c1690) at once over the 13,200. The tables
// are made up.
namespace
{
using Client::Data::DataError;
using Client::Tables::CreditEntry;
using Client::Tables::CreditFileSize;
using Client::Tables::CreditKind;
using Client::Tables::CreditKindAt;
using Client::Tables::CreditRecordSize;
using Client::Tables::LoadCredits;

void PutRecord(std::vector<std::byte>& file, std::size_t record, CreditKind kind, std::string_view text)
{
    const std::size_t first = record * CreditRecordSize;
    file[first] = static_cast<std::byte>(static_cast<std::uint8_t>(kind));
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        file[first + 1 + i] = static_cast<std::byte>(text[i]);
    }
}

// A `size`-byte file with three records and the end in the fourth, encrypted.
std::vector<std::byte> CipheredFile(std::size_t size)
{
    std::vector<std::byte> file(size);
    PutRecord(file, 0, CreditKind::Title, "Una parte");
    PutRecord(file, 1, CreditKind::Role, "Un rol");
    PutRecord(file, 2, CreditKind::Name, "Alguien");
    PutRecord(file, 3, CreditKind::End, "");
    Client::Crypto::Xor3(file);
    return file;
}

TEST(CreditsTest, TheRecordsComeOutDeciphered)
{
    const std::vector<std::byte> file = CipheredFile(CreditFileSize);

    const auto credits = LoadCredits(file);

    ASSERT_TRUE(credits.HasValue());
    ASSERT_EQ(credits.Value().size(), 400U);
    EXPECT_EQ(credits.Value()[0], (CreditEntry{.kind = 1, .text = "Una parte"}));
    EXPECT_EQ(credits.Value()[1], (CreditEntry{.kind = 2, .text = "Un rol"}));
    EXPECT_EQ(credits.Value()[2], (CreditEntry{.kind = 3, .text = "Alguien"}));
    EXPECT_EQ(credits.Value()[3], (CreditEntry{.kind = 0, .text = ""}));
}

// A full text has no zero and reaches the end of the record.
TEST(CreditsTest, AFullTextTakesTheThirtyTwoBytes)
{
    std::vector<std::byte> file(CreditFileSize);
    PutRecord(file, 0, CreditKind::Name, "abcdefghijklmnopqrstuvwxyz012345");
    file[CreditRecordSize] = std::byte{7};
    Client::Crypto::Xor3(file);

    const auto credits = LoadCredits(file);

    ASSERT_TRUE(credits.HasValue());
    EXPECT_EQ(credits.Value()[0].text, "abcdefghijklmnopqrstuvwxyz012345");
    // A kind that is none of the four stays as is.
    EXPECT_EQ(credits.Value()[1].kind, 7);
}

// fread reads what there is: with less, MuRet does not have what was left in the window. The excess is ignored.
TEST(CreditsTest, TheFileHasToBeWhole)
{
    const std::vector<std::byte> shorter = CipheredFile(CreditFileSize - 1);
    const auto refused = LoadCredits(shorter);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Error(), DataError::FileTooShort);

    const std::vector<std::byte> longer = CipheredFile(CreditFileSize + 5);
    const auto loaded = LoadCredits(longer);
    ASSERT_TRUE(loaded.HasValue());
    EXPECT_EQ(loaded.Value().size(), 400U);
}

// 0x63ffe0 reads the kind at any index: past the last, the end.
TEST(CreditsTest, PastTheTableIsTheEnd)
{
    const std::vector<CreditEntry> credits{{.kind = 3, .text = "Alguien"}};

    EXPECT_EQ(CreditKindAt(credits, 0), std::uint8_t{3});
    EXPECT_EQ(CreditKindAt(credits, 1), std::uint8_t{0});
    EXPECT_EQ(CreditKindAt(std::span<const CreditEntry>{}, 0), std::uint8_t{0});
}
} // namespace
