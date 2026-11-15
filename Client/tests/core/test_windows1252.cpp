#include "Core/Windows1252.h"
#include "Core/Windows1252BestFit.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace
{
using Client::Core::Windows1252CodePoint;
using Client::Core::Windows1252XpCodeUnit;

TEST(Windows1252Test, DecodesTheWindowsExtensionRange)
{
    // Valores contrastados con decode1252.csv del probe nativo Windows XP.
    constexpr std::array<char32_t, 32> Expected = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008d, 0x017d, 0x008f, 0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
        0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
    };

    for (std::size_t offset = 0; offset < Expected.size(); ++offset)
    {
        EXPECT_EQ(Windows1252CodePoint(static_cast<std::uint8_t>(0x80U + offset)), Expected[offset]) << offset;
    }
}

TEST(Windows1252Test, KeepsAsciiAndUpperRangeIdentity)
{
    for (unsigned int value = 0; value <= 0x7fU; ++value)
    {
        EXPECT_EQ(Windows1252CodePoint(static_cast<std::uint8_t>(value)), static_cast<char32_t>(value)) << value;
    }
    for (unsigned int value = 0xa0U; value <= 0xffU; ++value)
    {
        EXPECT_EQ(Windows1252CodePoint(static_cast<std::uint8_t>(value)), static_cast<char32_t>(value)) << value;
    }
}

TEST(Windows1252Test, EncodesNativeDefaultFitExamples)
{
    EXPECT_EQ(Windows1252XpCodeUnit(u'\u00e9'), 0xe9);
    EXPECT_EQ(Windows1252XpCodeUnit(u'\u20ac'), 0x80);
    EXPECT_EQ(Windows1252XpCodeUnit(u'\u0391'), static_cast<std::uint8_t>('?'));
    EXPECT_EQ(Windows1252XpCodeUnit(u'\uff21'), static_cast<std::uint8_t>('A'));
}

TEST(Windows1252Test, MatchesTheFullNativeBmpEncodingTableHash)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::uint32_t unit = 0; unit <= 0xffffU; ++unit)
    {
        hash ^= Windows1252XpCodeUnit(static_cast<char16_t>(unit));
        hash *= 1099511628211ULL;
    }

    EXPECT_EQ(hash, 0x5bbef6816f8b8f6dULL);
}

TEST(Windows1252Test, EncodesUtf8WithXpBestFitAndUtf16SurrogateFallback)
{
    const auto combinedAccent = Client::Core::EncodeWindows1252Xp("e\xcc\x81");
    const auto emoji = Client::Core::EncodeWindows1252Xp("\xf0\x9f\x98\x80");

    ASSERT_TRUE(combinedAccent.has_value());
    ASSERT_TRUE(emoji.has_value());
    EXPECT_EQ(*combinedAccent, std::string("e\xb4", 2));
    EXPECT_EQ(*emoji, "??");
}

TEST(Windows1252Test, RejectsMalformedUtf8WithoutReturningAValidPrefix)
{
    EXPECT_FALSE(Client::Core::EncodeWindows1252Xp("A\xc3").has_value());
    EXPECT_FALSE(Client::Core::EncodeWindows1252Xp("A\x80").has_value());
    EXPECT_FALSE(Client::Core::EncodeWindows1252Xp("\xed\xa0\x80").has_value());
}

TEST(Windows1252Test, DecodesAndRoundTripsEveryByte)
{
    std::string bytes;
    for (unsigned int value = 0; value <= 0xffU; ++value)
    {
        bytes.push_back(static_cast<char>(value));
    }

    const std::string decoded = Client::Core::DecodeWindows1252(bytes);
    const auto encoded = Client::Core::EncodeWindows1252Xp(decoded);

    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(*encoded, bytes);
    EXPECT_EQ(Client::Core::DecodeWindows1252("\x80\x81\xff"), "\xe2\x82\xac\xc2\x81\xc3\xbf");
}
} // namespace
