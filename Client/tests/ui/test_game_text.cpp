#include "UI/GameText.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

// Client::UI::PlaceArguments: un texto de Text.bmd con sus "%s" y "%d" cambiados por los
// argumentos, sin usarlo como formato (Q-C61, como PlaceVersion de la CL5.4). Lo usa el título de
// la cuenta (los textos 460 y 461, con el grupo y el número; CL5.5).
namespace
{
using Client::UI::PlaceArguments;

TEST(GameTextTest, PlacesEachMarkInOrder)
{
    constexpr std::array<std::string_view, 2> Arguments{"x", "7"};

    EXPECT_EQ(PlaceArguments("A %s b %d c", Arguments), "A x b 7 c");
}

// Una marca sin argumento queda como está, y un argumento de más no va a ningún lado.
TEST(GameTextTest, AMarkWithoutArgumentStays)
{
    constexpr std::array<std::string_view, 1> One{"x"};
    constexpr std::array<std::string_view, 3> Three{"x", "y", "z"};

    EXPECT_EQ(PlaceArguments("%s %d", One), "x %d");
    EXPECT_EQ(PlaceArguments("%s", Three), "x");
}

// Lo que no es "%s" ni "%d" queda: el texto no es un formato, y un "%n" no escribe nada.
TEST(GameTextTest, TheTextIsNotAFormat)
{
    constexpr std::array<std::string_view, 1> One{"y"};

    EXPECT_EQ(PlaceArguments("50% %n%s %x %", One), "50% %ny %x %");
}

// Un argumento con una marca no se vuelve a leer.
TEST(GameTextTest, AnArgumentIsNotReadAgain)
{
    constexpr std::array<std::string_view, 2> Arguments{"%s", "b"};

    EXPECT_EQ(PlaceArguments("%s %s", Arguments), "%s b");
}
} // namespace
