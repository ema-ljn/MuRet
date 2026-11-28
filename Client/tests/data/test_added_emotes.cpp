#include "Tables/AddedEmotes.h"

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Tables::ReadAddedEmotes: Data\MuRet\Emotes.txt, MuRet's own file, which `mutools emote add` writes.
namespace
{
using Client::Tables::AddedEmote;
using Client::Tables::FindAddedEmote;
using Client::Tables::ReadAddedEmotes;

Client::Core::Expected<std::vector<AddedEmote>, std::string> Read(std::string_view text)
{
    return ReadAddedEmotes(std::as_bytes(std::span(text)));
}

TEST(AddedEmotesTest, ReadsEachRow)
{
    const auto emotes = Read("MuRet emotes 1\r\n205\t206\t0.32\tloop\twave\t/wave\r\n\n230\t230\t1\tonce\tsit\n");

    ASSERT_TRUE(emotes.HasValue()) << emotes.Error();
    const std::vector<AddedEmote> expected{
        {.action = 205, .elfAction = 206, .keysPerStep = 0.32f, .repeats = true, .triggers = {"wave", "/wave"}},
        {.action = 230, .elfAction = 230, .keysPerStep = 1.0f, .repeats = false, .triggers = {"sit"}},
    };
    EXPECT_EQ(emotes.Value(), expected);
}

// The nearest float to the text, as the original's emotes have it: 0.32f is 0x3EA3D70A.
TEST(AddedEmotesTest, TheKeysPerStepAreTheNearestFloat)
{
    const auto emotes = Read("MuRet emotes 1\n205\t205\t0.32\tloop\twave\n");

    ASSERT_TRUE(emotes.HasValue()) << emotes.Error();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(emotes.Value().front().keysPerStep), 0x3ea3d70au);
}

TEST(AddedEmotesTest, AFileWithOnlyTheHeaderHasNone)
{
    const auto emotes = Read("MuRet emotes 1\n");

    ASSERT_TRUE(emotes.HasValue()) << emotes.Error();
    EXPECT_TRUE(emotes.Value().empty());
}

TEST(AddedEmotesTest, AnotherFirstLineIsNotTheFile)
{
    for (const std::string_view text : {"", "205\t205\t0.32\tloop\twave\n", "MuRet emotes 2\n"})
    {
        SCOPED_TRACE(text);
        const auto emotes = Read(text);

        ASSERT_FALSE(emotes.HasValue());
        EXPECT_NE(emotes.Error().find("MuRet emotes 1"), std::string::npos) << emotes.Error();
    }
}

// Each rule a row breaks is an error that names its line.
TEST(AddedEmotesTest, ARowThatBreaksARuleIsNamedByItsLine)
{
    const std::vector<std::string_view> rows{
        "205\t205\t0.32\tloop",         "204\t204\t0.32\tloop\tw", "256\t205\t0.32\tloop\tw",
        "x\t205\t0.32\tloop\tw",        "205\t256\t0.32\tloop\tw", "205\t205\t0\tloop\tw",
        "205\t205\t-1\tloop\tw",        "205\t205\t10.5\tloop\tw", "205\t205\tfast\tloop\tw",
        "205\t205\t0.32\tsometimes\tw", "205\t205\t0.32\tloop\t",  "205\t205\t0.32\tloop\tw\t",
    };
    for (const std::string_view row : rows)
    {
        SCOPED_TRACE(row);
        const auto emotes = Read(std::string("MuRet emotes 1\n") + std::string(row) + "\n");

        ASSERT_FALSE(emotes.HasValue());
        EXPECT_TRUE(emotes.Error().starts_with("line 2: ")) << emotes.Error();
    }
}

TEST(AddedEmotesTest, AnActionComesOnce)
{
    const auto emotes = Read("MuRet emotes 1\n205\t205\t0.32\tloop\twave\n205\t205\t0.4\tonce\tbow\n");

    ASSERT_FALSE(emotes.HasValue());
    EXPECT_TRUE(emotes.Error().starts_with("line 3: ")) << emotes.Error();
}

TEST(AddedEmotesTest, FindsTheRowOfAnAction)
{
    const std::vector<AddedEmote> emotes{
        {.action = 205, .elfAction = 205, .keysPerStep = 0.32f, .repeats = true, .triggers = {"a"}},
        {.action = 207, .elfAction = 208, .keysPerStep = 0.32f, .repeats = true, .triggers = {"b"}},
    };

    ASSERT_NE(FindAddedEmote(emotes, 207), nullptr);
    EXPECT_EQ(FindAddedEmote(emotes, 207)->elfAction, 208);
    EXPECT_EQ(FindAddedEmote(emotes, 206), nullptr);
}
} // namespace
