#include "Chat/MacroFile.h"

#include "Chat/ChatInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

// Data\Macro.txt (0x61c1c0 and 0x61c170; CL8 milestone 3). The macros are invented.
namespace
{
using Client::Tables::MacroCount;
using Client::Tables::Macros;
using Client::Tables::MacroSlotBytes;
using Client::Tables::ReadTheMacros;
using Client::Tables::WrittenMacros;

std::span<const std::byte> BytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

TEST(MacroFileTest, EachWordGoesToItsMacroInOrder)
{
    const auto macros = ReadTheMacros(BytesOf("uno\r\ndos\r\n\r\n\ttres\v\fcuatro"));
    ASSERT_TRUE(macros.HasValue());
    EXPECT_EQ(macros.Value()[0], "uno");
    EXPECT_EQ(macros.Value()[1], "dos");
    EXPECT_EQ(macros.Value()[2], "tres");
    EXPECT_EQ(macros.Value()[3], "cuatro");
    for (std::size_t macro = 4; macro < MacroCount; ++macro)
        EXPECT_EQ(macros.Value()[macro], "");
}

// Q-C449: "%s" stops at the space, and the rest shifts the macros that follow. Those past ten are not read.
TEST(MacroFileTest, AMacroWithSpacesComesBackSplit)
{
    const auto macros = ReadTheMacros(BytesOf("a b\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\n"));
    ASSERT_TRUE(macros.HasValue());
    EXPECT_EQ(macros.Value()[0], "a");
    EXPECT_EQ(macros.Value()[1], "b");
    EXPECT_EQ(macros.Value()[2], "c");
    EXPECT_EQ(macros.Value()[9], "j");
}

// Bytes from 0x80 on are not whitespace in the "C" locale; the text mode's Ctrl-Z ends the file.
TEST(MacroFileTest, TheTextModeEndsAtCtrlZ)
{
    const auto macros = ReadTheMacros(BytesOf("\xe1\xa0x\x1ay z"));
    ASSERT_TRUE(macros.HasValue());
    EXPECT_EQ(macros.Value()[0], "\xe1\xa0x");
    EXPECT_EQ(macros.Value()[1], "");
}

TEST(MacroFileTest, AnEmptyFileLeavesThemEmpty)
{
    const auto macros = ReadTheMacros({});
    ASSERT_TRUE(macros.HasValue());
    EXPECT_EQ(macros.Value(), Macros{});
}

// Q-C531: a word of 0x100 bytes or more does not fit in its slot.
TEST(MacroFileTest, AWordThatOverflowsItsSlotIsTheError)
{
    const std::string fits(MacroSlotBytes - 1, 'x');
    ASSERT_TRUE(ReadTheMacros(BytesOf(fits)).HasValue());
    const std::string overflows = "a " + std::string(MacroSlotBytes, 'x');
    const auto macros = ReadTheMacros(BytesOf(overflows));
    ASSERT_FALSE(macros.HasValue());
    EXPECT_NE(macros.Error().find("0x61c1c0"), std::string::npos);
}

// 0x61c170: each one with "%s\n" in text mode, even when empty; with spaces, as is.
TEST(MacroFileTest, TheWrittenFileHasOneLinePerMacro)
{
    Macros macros{};
    macros[0] = "uno";
    macros[2] = "con espacio";
    EXPECT_EQ(WrittenMacros(macros), "uno\r\n\r\ncon espacio\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n");
}
} // namespace
