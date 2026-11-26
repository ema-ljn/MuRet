#include "Diagnostics/Crc32.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string_view>

// The CRC-32 0x683bd0 signs the executable with in MuError.dmp.
namespace
{
using Client::Diagnostics::Crc32;

std::span<const std::byte> Bytes(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}
} // namespace

// The usual CRC-32 check value (zlib's and PNG's).
TEST(Crc32Test, MatchesTheStandardCheckValue)
{
    Crc32 crc;
    crc.Update(Bytes("123456789"));

    EXPECT_EQ(crc.Value(), 0xCBF43926u);
}

TEST(Crc32Test, IsZeroWithoutBytes)
{
    EXPECT_EQ(Crc32{}.Value(), 0u);
}

// Like the original over the mapped file.
TEST(Crc32Test, GivesTheSameValueInPieces)
{
    Crc32 whole;
    whole.Update(Bytes("The quick brown fox jumps over the lazy dog"));
    Crc32 pieces;
    pieces.Update(Bytes("The quick brown "));
    pieces.Update(Bytes(""));
    pieces.Update(Bytes("fox jumps over the lazy dog"));

    EXPECT_EQ(whole.Value(), 0x414FA339u);
    EXPECT_EQ(pieces.Value(), whole.Value());
}
