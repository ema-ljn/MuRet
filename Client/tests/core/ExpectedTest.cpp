// Client::Core::Expected: un valor o un error, nunca los dos.

#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace
{

using Client::Core::Expected;
using Client::Core::Unexpected;

TEST(ExpectedTest, HoldsAValue)
{
    const Expected<int, std::string> result = 42;

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 42);
}

TEST(ExpectedTest, HoldsAnError)
{
    const Expected<int, std::string> result = Unexpected(std::string("archivo corto"));

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), "archivo corto");
}

// Con T y E del mismo tipo, lo que dice qué es cada uno es Unexpected, no el tipo.
TEST(ExpectedTest, ValueAndErrorOfTheSameType)
{
    const Expected<int, int> value = 7;
    const Expected<int, int> error = Unexpected(7);

    ASSERT_TRUE(value.HasValue());
    EXPECT_EQ(value.Value(), 7);
    ASSERT_FALSE(error.HasValue());
    EXPECT_EQ(error.Error(), 7);
}

// Una tabla cargada se mueve afuera, no se copia. Con un tipo que no se puede copiar, compila solo
// si Value() mueve.
TEST(ExpectedTest, MovesTheValueOut)
{
    Expected<std::unique_ptr<int>, int> result = std::make_unique<int>(5);

    const std::unique_ptr<int> value = std::move(result).Value();

    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 5);
}

TEST(ExpectedTest, ValueCanBeChangedInPlace)
{
    Expected<std::string, int> result = std::string("Lorencia");

    result.Value() += " 130 130";

    EXPECT_EQ(result.Value(), "Lorencia 130 130");
}

// Pedir el valor de uno que tiene un error es un bug de quien llama: el programa se corta ahí, con
// un mensaje, en vez de seguir con un valor que no existe. GoogleTest lo prueba en otro proceso.
TEST(ExpectedDeathTest, ValueOfAnErrorStopsTheProgram)
{
    const Expected<int, std::string> result = Unexpected(std::string("archivo corto"));

    EXPECT_DEATH(static_cast<void>(result.Value()), "de un Expected que tiene un error");
}

TEST(ExpectedDeathTest, ErrorOfAValueStopsTheProgram)
{
    const Expected<int, std::string> result = 42;

    EXPECT_DEATH(static_cast<void>(result.Error()), "de un Expected que tiene un valor");
}

} // namespace
