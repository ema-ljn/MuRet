#include "UI/MacroFile.h"

#include "UI/ChatInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

// Data\Macro.txt (0x61c1c0 y 0x61c170; CL8 hito 3). Las macros son inventadas.
namespace
{
using Client::UI::MacroCount;
using Client::UI::Macros;
using Client::UI::MacroSlotBytes;
using Client::UI::ReadTheMacros;
using Client::UI::WrittenMacros;

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

// Q-C449: "%s" corta en el espacio, y lo que queda corre las macros que siguen. Las de más de diez no se leen.
TEST(MacroFileTest, AMacroWithSpacesComesBackSplit)
{
    const auto macros = ReadTheMacros(BytesOf("a b\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\n"));
    ASSERT_TRUE(macros.HasValue());
    EXPECT_EQ(macros.Value()[0], "a");
    EXPECT_EQ(macros.Value()[1], "b");
    EXPECT_EQ(macros.Value()[2], "c");
    EXPECT_EQ(macros.Value()[9], "j");
}

// Los bytes de 0x80 en adelante no son blancos en el locale "C"; el Ctrl-Z del modo texto termina el archivo.
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

// Q-C531: una palabra de 0x100 bytes o más no entra en su lugar.
TEST(MacroFileTest, AWordThatOverflowsItsSlotIsTheError)
{
    const std::string fits(MacroSlotBytes - 1, 'x');
    ASSERT_TRUE(ReadTheMacros(BytesOf(fits)).HasValue());
    const std::string overflows = "a " + std::string(MacroSlotBytes, 'x');
    const auto macros = ReadTheMacros(BytesOf(overflows));
    ASSERT_FALSE(macros.HasValue());
    EXPECT_NE(macros.Error().find("0x61c1c0"), std::string::npos);
}

// 0x61c170: cada una con "%s\n" en modo texto, aun vacía; con espacios, tal cual.
TEST(MacroFileTest, TheWrittenFileHasOneLinePerMacro)
{
    Macros macros{};
    macros[0] = "uno";
    macros[2] = "con espacio";
    EXPECT_EQ(WrittenMacros(macros), "uno\r\n\r\ncon espacio\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n");
}
} // namespace
