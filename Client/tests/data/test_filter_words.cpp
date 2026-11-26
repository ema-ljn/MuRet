#include "Tables/FilterWords.h"

#include "Crypto/Xor3.h"
#include "Data/DataError.h"
#include "Data/Tables/TableChecksum.h"

#include "support/ExpectedError.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// FilterName.bmd and Filter.bmd (0x549fa0 and 0x549c60), with synthetic words.
namespace
{
using Client::Data::DataError;
using Client::Tables::FilterChecksumKey;
using Client::Tables::FilterNameChecksumKey;
using Client::Tables::FilterRecordSize;
using Client::Tables::FilterTableSize;
using Client::Tables::LoadFilterWords;
using Client::Tests::ErrorOf;

std::vector<std::byte> FilterFile(const std::vector<std::string>& words,
                                  std::uint32_t checksumKey = FilterNameChecksumKey)
{
    return Client::Tests::FilterFileBytes(words, checksumKey);
}

// The sum is the tables' (TableChecksum, with Item.bmd's parity) with key << 9 as seed: the lab's matches
// (GoldenFilterWords). One file's is not the other's.
TEST(FilterWordsTest, EachFileHasItsChecksumKey)
{
    EXPECT_EQ(FilterNameChecksumKey << 9, 0x578200u);
    EXPECT_EQ(FilterChecksumKey << 9, 0x7cfa00u);
    EXPECT_TRUE(LoadFilterWords(FilterFile({"Synth"}, FilterChecksumKey), FilterChecksumKey).HasValue());
    EXPECT_FALSE(LoadFilterWords(FilterFile({"Synth"}, FilterChecksumKey), FilterNameChecksumKey).HasValue());
}

// Each 20-byte record carries the XOR3 from its first byte (0x5496c0 per record): since 20 is not a multiple of 3, it
// is not the same as end to end.
TEST(FilterWordsTest, EachRecordIsDecodedFromItsStart)
{
    const auto words = LoadFilterWords(FilterFile({"Synth", "abcdefghijklmnopqrs"}), FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{"Synth", "abcdefghijklmnopqrs"}));
}

TEST(FilterWordsTest, TheWordsEndAtTheFirstEmptyRecord)
{
    std::vector<std::byte> file = FilterFile({"one", "two"});
    // A third one after the empty one, with the sum redone.
    std::span<std::byte> fourth(file.data() + 3 * FilterRecordSize, FilterRecordSize);
    std::ranges::fill(fourth, std::byte{0});
    fourth[0] = std::byte{'x'};
    Client::Crypto::Xor3(fourth);
    const std::uint32_t sum =
        Client::Data::Tables::TableChecksum(std::span(file).first(FilterTableSize), FilterNameChecksumKey,
                                            Client::Data::Tables::ChecksumParity::XorOddDwords);
    for (std::size_t k = 0; k < 4; ++k)
    {
        file[FilterTableSize + k] = static_cast<std::byte>((sum >> (8 * k)) & 0xff);
    }

    const auto words = LoadFilterWords(file, FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{"one", "two"}));
    EXPECT_TRUE(LoadFilterWords(FilterFile({}), FilterNameChecksumKey).Value().empty());
}

TEST(FilterWordsTest, AFullRecordIsAWordOfTwentyBytes)
{
    const auto words = LoadFilterWords(FilterFile({std::string(20, 'w'), "x"}), FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_EQ(words.Value(), (std::vector<std::string>{std::string(20, 'w'), "x"}));
}

// With all 1,000 full, 0x549fa0 exits at the cap (0x54a139) without storing the count, which stays 0: no words.
TEST(FilterWordsTest, ATableWithoutAnEmptyRecordHasNoWords)
{
    const std::vector<std::string> all(1000, std::string(20, 'w'));

    const auto words = LoadFilterWords(FilterFile(all), FilterNameChecksumKey);

    ASSERT_TRUE(words.HasValue());
    EXPECT_TRUE(words.Value().empty());
}

// With another sum, ChecksumMismatch (the original shows "File corrupted" and closes); short, FileTooShort; with the
// other file's key, it does not match either.
TEST(FilterWordsTest, AWrongSumOrAShortFileIsAnError)
{
    std::vector<std::byte> file = FilterFile({"Synth"});
    file[3] ^= std::byte{0x01};
    EXPECT_EQ(ErrorOf(LoadFilterWords(file, FilterNameChecksumKey)), DataError::ChecksumMismatch);
    EXPECT_EQ(ErrorOf(LoadFilterWords(FilterFile({"Synth"}), FilterChecksumKey)), DataError::ChecksumMismatch);
    const std::vector<std::byte> shortFile(FilterTableSize + 3, std::byte{0});
    EXPECT_EQ(ErrorOf(LoadFilterWords(shortFile, FilterNameChecksumKey)), DataError::FileTooShort);
}
} // namespace
