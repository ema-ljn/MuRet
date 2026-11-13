#include "Data/Text/Credits.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// credit.bmd real (Data\Local, 0x63ff30 desde 0x63f020): los 400 registros, cuántos hay de cada clase, el
// primer final y que ningún texto llena los 32 bytes. Sin los textos: los nombres no van al repo.
namespace
{
using Client::Data::Text::CreditEntry;
using Client::Data::Text::LoadCredits;
using Client::Tests::ReadFileBytes;
using GoldenCredits = Client::Tests::GoldenDataTest;

TEST_F(GoldenCredits, TheTableEndsAtRecord244)
{
    const auto path = DataFile("Local\\credit.bmd");
    ASSERT_TRUE(path.has_value());
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    ASSERT_TRUE(file.has_value());

    const auto loaded = LoadCredits(std::as_bytes(std::span(*file)));

    ASSERT_TRUE(loaded.HasValue());
    const std::vector<CreditEntry>& credits = loaded.Value();
    ASSERT_EQ(credits.size(), 400U);
    const auto end = std::ranges::find(credits, std::uint8_t{0}, &CreditEntry::kind);
    EXPECT_EQ(end - credits.begin(), 244);
    const auto count = [&](std::uint8_t kind) { return std::ranges::count(credits, kind, &CreditEntry::kind); };
    EXPECT_EQ(count(1), 9);
    EXPECT_EQ(count(2), 45);
    EXPECT_EQ(count(3), 190);
    EXPECT_EQ(count(0), 156);
    // La primera es un título, seguido por un rol.
    EXPECT_EQ(credits[0].kind, 1);
    EXPECT_EQ(credits[1].kind, 2);
    const std::size_t longest =
        std::ranges::max(credits, {}, [](const CreditEntry& entry) { return entry.text.size(); }).text.size();
    EXPECT_EQ(longest, 25U);
}
} // namespace
