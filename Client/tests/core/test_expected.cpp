#include "Core/Expected.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// Client::Core::Expected: un valor o un error, nunca los dos.
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

Expected<std::string, int> LoadName()
{
    return std::string("Lorencia");
}

// Value() de un temporal devuelve el valor y no una referencia adentro del temporal, que muere al
// terminar la línea. Con una referencia (como std::expected), `name` quedaría colgando, y GCC lo
// marca con -Wdangling-reference.
TEST(ExpectedTest, ValueOfATemporaryCanBeKept)
{
    const std::string& name = LoadName().Value();

    EXPECT_EQ(name, "Lorencia");
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

    EXPECT_DEATH(static_cast<void>(result.Value()), "de un Expected que no tiene valor");
}

TEST(ExpectedDeathTest, ErrorOfAValueStopsTheProgram)
{
    const Expected<int, std::string> result = 42;

    EXPECT_DEATH(static_cast<void>(result.Error()), "de un Expected que no tiene error");
}

// Un valor cuyo move tira una excepción, solo cuando se le pide.
struct MoveThrows
{
    bool armed = false;

    MoveThrows() = default;
    MoveThrows(const MoveThrows&) = default;
    MoveThrows& operator=(const MoveThrows&) = default;
    MoveThrows& operator=(MoveThrows&&) = default;
    MoveThrows(MoveThrows&& other) : armed(other.armed)
    {
        if (armed)
        {
            throw std::runtime_error("move");
        }
    }
};

// Si el move del valor tira mientras un Expected pasa de error a valor, el std::variant de adentro
// queda sin ninguno de los dos (valueless_by_exception). Pedirle el error también es un bug de
// quien llama, y corta con el mismo mensaje.
TEST(ExpectedDeathTest, ErrorOfOneWithNeitherStopsTheProgram)
{
    Expected<MoveThrows, int> result = Unexpected(1);
    Expected<MoveThrows, int> source = MoveThrows{};
    source.Value().armed = true;

    EXPECT_THROW(result = std::move(source), std::runtime_error);

    ASSERT_FALSE(result.HasValue());
    EXPECT_DEATH(static_cast<void>(result.Error()), "de un Expected que no tiene error");
}

// Expected<void, E>: lo que solo puede fallar (subir las texturas de una escena, CL5.1). Un
// Expected construido sin nada es el éxito, así una función hace `return {};`.
Expected<void, std::string> SucceedsUnless(bool fail)
{
    if (fail)
    {
        return Unexpected(std::string("falta la textura"));
    }
    return {};
}

TEST(ExpectedVoidTest, NothingIsASuccess)
{
    const Expected<void, std::string> result = SucceedsUnless(false);

    EXPECT_TRUE(result.HasValue());
}

TEST(ExpectedVoidTest, HoldsAnError)
{
    const Expected<void, std::string> result = SucceedsUnless(true);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), "falta la textura");
}

TEST(ExpectedVoidDeathTest, ErrorOfASuccessStopsTheProgram)
{
    const Expected<void, std::string> result = SucceedsUnless(false);

    EXPECT_DEATH(static_cast<void>(result.Error()), "de un Expected que no tiene error");
}
} // namespace
