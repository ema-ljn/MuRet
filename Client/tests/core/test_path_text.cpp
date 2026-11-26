#include "Core/PathText.h"
#include "Core/Utf8.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>

// On Windows path(std::string) and path::string() use the ANSI page.
namespace
{
using Client::Core::PathFromUtf8;
using Client::Core::PathText;
using Client::Core::ReadUtf8CodePoint;

// Letters not in page 1252: "Ł" (U+0141) and "データ" (U+30C7, U+30FC, U+30BF). With path(std::string), Windows would
// turn them into "?".
constexpr std::string_view NonAnsiUtf8 = "C:/Juegos/\xC5\x81ukasz/\xE3\x83\x87\xE3\x83\xBC\xE3\x82\xBF";

TEST(PathTextTest, PathFromUtf8ReadsTheTextAsUtf8)
{
    const auto path = PathFromUtf8(NonAnsiUtf8);

    ASSERT_TRUE(path.has_value());
    EXPECT_TRUE(path->u8string() == u8"C:/Juegos/\u0141ukasz/\u30C7\u30FC\u30BF");
    EXPECT_EQ(PathText(*path), NonAnsiUtf8);
}

TEST(PathTextTest, AnEmptyTextIsAnEmptyPath)
{
    const auto path = PathFromUtf8("");

    ASSERT_TRUE(path.has_value());
    EXPECT_TRUE(path->empty());
}

// What MultiByteToWideChar rejects with MB_ERR_INVALID_CHARS, which path uses on Windows.
TEST(PathTextTest, PathFromUtf8RejectsTextThatIsNotUtf8)
{
    // A lone 1252 Ú: an ini or a name saved as ANSI.
    EXPECT_FALSE(PathFromUtf8("C:/MU/\xDA").has_value());
    // A continuation byte without its lead byte, and a cut sequence.
    EXPECT_FALSE(PathFromUtf8("a\x80").has_value());
    EXPECT_FALSE(PathFromUtf8("\xE2\x82").has_value());
    // "/" in two and in three bytes, longer than needed.
    EXPECT_FALSE(PathFromUtf8("\xC0\xAF").has_value());
    EXPECT_FALSE(PathFromUtf8("\xE0\x80\xAF").has_value());
    // U+D800, half of a UTF-16 pair, and U+110000, past U+10FFFF.
    EXPECT_FALSE(PathFromUtf8("\xED\xA0\x80").has_value());
    EXPECT_FALSE(PathFromUtf8("\xF4\x90\x80\x80").has_value());
}

// The edges that are UTF-8: the last of each length, the first of the next, and both sides of the UTF-16 halves.
TEST(PathTextTest, PathFromUtf8AcceptsTheLimits)
{
    for (const std::string_view text : {"\x7F", "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xED\x9F\xBF", "\xEE\x80\x80",
                                        "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF"})
    {
        EXPECT_TRUE(PathFromUtf8(text).has_value()) << "largo " << text.size();
    }
}

TEST(Utf8Test, ReadsOneCodePointAndAdvancesByItsByteLength)
{
    constexpr std::string_view text = "A\xe2\x82\xac";
    std::size_t offset = 0;

    EXPECT_EQ(ReadUtf8CodePoint(text, offset), U'A');
    EXPECT_EQ(offset, 1u);
    EXPECT_EQ(ReadUtf8CodePoint(text, offset), U'\u20ac');
    EXPECT_EQ(offset, text.size());
    EXPECT_FALSE(ReadUtf8CodePoint(text, offset).has_value());
}

TEST(Utf8Test, LeavesOffsetAtMalformedCodePoint)
{
    constexpr std::string_view text = "A\xed\xa0\x80";
    std::size_t offset = 1;

    EXPECT_FALSE(ReadUtf8CodePoint(text, offset).has_value());
    EXPECT_EQ(offset, 1u);
}
} // namespace
