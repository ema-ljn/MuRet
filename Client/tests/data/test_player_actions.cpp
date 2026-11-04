#include "Data/Models/PlayerActions.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Client::Data::Models::PlayerActionSpeeds: las velocidades que 0x60dbe0 fija en las acciones de
// Player.bmd (06 § Cuánto avanza la animación).
namespace
{
using Client::Data::Models::ActionSpeedWrite;
using Client::Data::Models::PlayerActionSpeed;
using Client::Data::Models::PlayerActionSpeeds;
using Client::Data::Models::PlayerActionsWithoutRepeat;

// Las acciones de Player.bmd (05).
constexpr std::size_t PlayerActions = 205;

// La de la selección, la 3, va a 0,26: la traza la confirma con el reloj (06).
TEST(PlayerActionsTest, TheSelectionActionIsPointTwoSix)
{
    EXPECT_EQ(PlayerActionSpeed(3), std::optional{0.26f});
    // Los bits del inmediato del original (0x60f35f).
    EXPECT_EQ(std::bit_cast<std::uint32_t>(PlayerActionSpeed(3).value_or(0.0f)), 0x3E851EB8u);
}

// De la 1 a la 12 a 0,28, salvo la 3 (0,26), la 4 y la 5 (0,24) y la 7 y la 8 (0,22).
TEST(PlayerActionsTest, OneToTwelveArePointTwoEightButSix)
{
    for (std::size_t action = 1; action <= 12; ++action)
    {
        float expected = 0.28f;
        if (action == 3)
        {
            expected = 0.26f;
        }
        else if (action == 4 || action == 5)
        {
            expected = 0.24f;
        }
        else if (action == 7 || action == 8)
        {
            expected = 0.22f;
        }
        EXPECT_EQ(PlayerActionSpeed(action), std::optional{expected}) << "acción " << action;
    }
}

// De la 13 a la 33 a 0,3, salvo la 21 y la 29 (0,35).
TEST(PlayerActionsTest, ThirteenToThirtyThreeArePointThree)
{
    for (std::size_t action = 13; action <= 33; ++action)
    {
        const float expected = action == 21 || action == 29 ? 0.35f : 0.3f;
        EXPECT_EQ(PlayerActionSpeed(action), std::optional{expected}) << "acción " << action;
    }
}

// Q-C87: 57 de las 205 no tienen escritura, y quedan con lo que había en el heap.
TEST(PlayerActionsTest, FiftySevenHaveNone)
{
    std::vector<std::size_t> without;
    for (std::size_t action = 0; action < PlayerActions; ++action)
    {
        if (!PlayerActionSpeed(action).has_value())
        {
            without.push_back(action);
        }
    }
    std::vector<std::size_t> expected = {0, 66, 68, 69, 70, 71};
    for (std::size_t action = 34; action <= 63; ++action)
    {
        expected.push_back(action);
    }
    for (std::size_t action = 126; action <= 136; ++action)
    {
        expected.push_back(action);
    }
    for (std::size_t action = 142; action <= 151; ++action)
    {
        expected.push_back(action);
    }
    std::ranges::sort(expected);
    EXPECT_EQ(without.size(), 57u);
    EXPECT_EQ(without, expected);
    EXPECT_FALSE(PlayerActionSpeed(PlayerActions).has_value());
}

// Cuatro se quedan en la última key, en el orden de 0x60f54c..0x60f585.
TEST(PlayerActionsTest, FourDoNotRepeat)
{
    EXPECT_EQ(PlayerActionsWithoutRepeat, (std::array<std::uint8_t, 4>{195, 196, 193, 68}));
}

// 181 escrituras en 47 instrucciones, todas en las acciones de Player.bmd.
TEST(PlayerActionsTest, AHundredAndEightyOneWrites)
{
    std::size_t writes = 0;
    for (const ActionSpeedWrite& write : PlayerActionSpeeds())
    {
        ASSERT_LE(write.first, write.last);
        ASSERT_LT(write.last, PlayerActions);
        writes += static_cast<std::size_t>(write.last - write.first) + 1;
    }
    EXPECT_EQ(PlayerActionSpeeds().size(), 47u);
    EXPECT_EQ(writes, 181u);
}

// Las de después pisan: la 194 va a 0,32 y después a 0,4; de la 106 a la 117, 0,45 y después 0,71;
// la 201 y la 202, 0,4 y después 0,2.
TEST(PlayerActionsTest, LaterWritesOverrideEarlier)
{
    EXPECT_EQ(PlayerActionSpeed(193), std::optional{0.32f});
    EXPECT_EQ(PlayerActionSpeed(194), std::optional{0.4f});
    EXPECT_EQ(PlayerActionSpeed(105), std::optional{0.45f});
    EXPECT_EQ(PlayerActionSpeed(106), std::optional{0.71f});
    EXPECT_EQ(PlayerActionSpeed(117), std::optional{0.71f});
    EXPECT_EQ(PlayerActionSpeed(118), std::optional{0.4f});
    EXPECT_EQ(PlayerActionSpeed(122), std::optional{0.45f});
    EXPECT_EQ(PlayerActionSpeed(200), std::optional{0.4f});
    EXPECT_EQ(PlayerActionSpeed(201), std::optional{0.2f});
    EXPECT_EQ(PlayerActionSpeed(202), std::optional{0.2f});
    EXPECT_EQ(PlayerActionSpeed(84), std::optional{1.0f});
}
} // namespace
