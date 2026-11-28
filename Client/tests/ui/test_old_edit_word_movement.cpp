#include "Ui/OldEditWordMovement.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

// El EDIT de Windows XP clasifica por bytes y solo reconoce TAB, LF, CR y espacio; ver
// Client/docs/research/11-edit-win32.md, «SetText y atajos». Vectores privados c32, session-02/edit-word-own-01.
namespace
{
using Client::UI::OldEditWordMovement::CursorAfterCtrlLeft;
using Client::UI::OldEditWordMovement::CursorAfterCtrlRight;

TEST(OldEditWordMovementTest, MatchesTheNativeControlOnUsefulByteBoundaries)
{
    struct Case
    {
        std::string_view text;
        std::size_t cursor;
        std::size_t left;
        std::size_t right;
    };
    const std::array cases{
        Case{"", 0, 0, 0},
        Case{"ab cd ef", 0, 0, 3},
        Case{"ab cd ef", 3, 0, 6},
        Case{"ab cd ef", 6, 3, 8},
        Case{"ab cd ef", 8, 6, 8},
        Case{"a,b.c-d", 0, 0, 7},
        Case{"a\tb", 1, 0, 3},
        Case{"a\nb", 1, 0, 2},
        Case{"a\rb", 2, 1, 3},
        Case{"a\xA0"
             "b",
             0, 0, 3},
        Case{"\t ab ", 0, 0, 5},
    };

    for (const Case& test : cases)
    {
        EXPECT_EQ(CursorAfterCtrlLeft(test.text, test.cursor), test.left);
        EXPECT_EQ(CursorAfterCtrlRight(test.text, test.cursor), test.right);
    }
}

TEST(OldEditWordMovementTest, MatchesTheExhaustiveNativePrefixSweepHash)
{
    constexpr std::uint64_t FnvOffset = 14695981039346656037ULL;
    constexpr std::uint64_t FnvPrime = 1099511628211ULL;
    constexpr std::uint64_t ExpectedHash = 0x697d385b07224401ULL;
    const std::array<std::string_view, 9> fixtures{
        "",
        "ab cd ef",
        " ab  cd ",
        "a,b.c-d",
        "a\xA0"
        "b",
        "a\tb",
        "a\nb",
        "a\rb",
        "\t ab ",
    };
    std::uint64_t hash = FnvOffset;

    for (const std::string_view fixture : fixtures)
    {
        for (std::size_t prefixSize = 0; prefixSize <= fixture.size(); ++prefixSize)
        {
            const std::string_view prefix = fixture.substr(0, prefixSize);
            for (std::size_t caret = 0; caret <= prefix.size(); ++caret)
            {
                const std::size_t left = CursorAfterCtrlLeft(prefix, caret);
                const std::size_t right = CursorAfterCtrlRight(prefix, caret);
                for (const std::size_t result : {left, left, right, right})
                {
                    hash ^= static_cast<std::uint8_t>(result);
                    hash *= FnvPrime;
                }
            }
        }
    }

    EXPECT_EQ(hash, ExpectedHash);
}
} // namespace
