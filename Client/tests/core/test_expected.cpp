#include "Core/Expected.h"

#include "support/SystemCrashReports.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

// Client::Core::Expected: un valor o un error, nunca los dos.
namespace
{
using Client::Core::Expected;
using Client::Core::Result;
using Client::Core::Status;
using Client::Core::Unexpected;

static_assert(std::is_same_v<Result<int>, Expected<int, std::string>>);
static_assert(std::is_same_v<Status, Expected<void, std::string>>);

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

Expected<int, std::string> Half(int value)
{
    if (value % 2 != 0)
    {
        return Unexpected(std::string("odd"));
    }
    return value / 2;
}

TEST(ExpectedAndThenTest, RunsTheNextStepOnAValue)
{
    const Expected<int, std::string> eight = 8;

    const Expected<int, std::string> four = eight.AndThen(Half);

    ASSERT_TRUE(four.HasValue());
    EXPECT_EQ(four.Value(), 4);
}

TEST(ExpectedAndThenTest, ReturnsTheErrorOfTheNextStep)
{
    const Expected<int, std::string> three = 3;

    const Expected<int, std::string> half = three.AndThen(Half);

    ASSERT_FALSE(half.HasValue());
    EXPECT_EQ(half.Error(), "odd");
}

TEST(ExpectedAndThenTest, PassesAnErrorThroughUntouched)
{
    const Expected<int, std::string> failed = Unexpected(std::string("short file"));
    bool ran = false;

    const Expected<std::string, std::string> named = failed.AndThen(
        [&ran](int) -> Expected<std::string, std::string>
        {
            ran = true;
            return std::string("Lorencia");
        });

    EXPECT_FALSE(ran);
    ASSERT_FALSE(named.HasValue());
    EXPECT_EQ(named.Error(), "short file");
}

TEST(ExpectedAndThenTest, MovesAMoveOnlyValueOutOfATemporary)
{
    Expected<std::unique_ptr<int>, int> owned = std::make_unique<int>(5);

    const Expected<int, int> read =
        std::move(owned).AndThen([](std::unique_ptr<int> value) -> Expected<int, int> { return *value; });

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value(), 5);
}

TEST(ExpectedAndThenTest, MovesAMoveOnlyErrorOutOfATemporary)
{
    Expected<int, std::unique_ptr<int>> failed = Unexpected(std::make_unique<int>(7));

    const Expected<long, std::unique_ptr<int>> passed =
        std::move(failed).AndThen([](int value) -> Expected<long, std::unique_ptr<int>> { return long{value}; });

    ASSERT_FALSE(passed.HasValue());
    ASSERT_NE(passed.Error(), nullptr);
    EXPECT_EQ(*passed.Error(), 7);
}

TEST(ExpectedTransformTest, MapsTheValue)
{
    const Expected<int, std::string> six = 6;

    const auto text = six.Transform([](int value) { return std::to_string(value); });

    static_assert(std::is_same_v<decltype(text), const Expected<std::string, std::string>>);
    ASSERT_TRUE(text.HasValue());
    EXPECT_EQ(text.Value(), "6");
}

TEST(ExpectedTransformTest, KeepsTheError)
{
    const Expected<int, std::string> failed = Unexpected(std::string("short file"));
    bool ran = false;

    const Expected<int, std::string> doubled = failed.Transform(
        [&ran](int value)
        {
            ran = true;
            return value * 2;
        });

    EXPECT_FALSE(ran);
    ASSERT_FALSE(doubled.HasValue());
    EXPECT_EQ(doubled.Error(), "short file");
}

TEST(ExpectedTransformTest, AFunctionThatReturnsNothingGivesASuccessWithoutAValue)
{
    const Expected<int, std::string> six = 6;
    int seen = 0;

    const auto done = six.Transform([&seen](int value) { seen = value; });

    static_assert(std::is_same_v<decltype(done), const Expected<void, std::string>>);
    EXPECT_TRUE(done.HasValue());
    EXPECT_EQ(seen, 6);
}

TEST(ExpectedTransformTest, MovesAMoveOnlyValueOutOfATemporary)
{
    Expected<std::unique_ptr<int>, int> owned = std::make_unique<int>(5);

    const Expected<std::unique_ptr<int>, int> kept =
        std::move(owned).Transform([](std::unique_ptr<int> value) { return value; });

    ASSERT_TRUE(kept.HasValue());
    ASSERT_NE(kept.Value(), nullptr);
    EXPECT_EQ(*kept.Value(), 5);
}

TEST(ExpectedOrElseTest, RunsTheRecoveryOnAnError)
{
    const Expected<int, std::string> failed = Unexpected(std::string("short file"));

    const Expected<int, int> recovered =
        failed.OrElse([](const std::string& error) -> Expected<int, int> { return static_cast<int>(error.size()); });

    ASSERT_TRUE(recovered.HasValue());
    EXPECT_EQ(recovered.Value(), 10);
}

TEST(ExpectedOrElseTest, ReturnsTheErrorOfTheRecovery)
{
    const Expected<int, std::string> failed = Unexpected(std::string("short file"));

    const Expected<int, int> coded = failed.OrElse([](const std::string& error) -> Expected<int, int>
                                                   { return Unexpected(static_cast<int>(error.size())); });

    ASSERT_FALSE(coded.HasValue());
    EXPECT_EQ(coded.Error(), 10);
}

TEST(ExpectedOrElseTest, KeepsTheValue)
{
    const Expected<int, std::string> six = 6;
    bool ran = false;

    const Expected<int, int> kept = six.OrElse(
        [&ran](const std::string&) -> Expected<int, int>
        {
            ran = true;
            return 0;
        });

    EXPECT_FALSE(ran);
    ASSERT_TRUE(kept.HasValue());
    EXPECT_EQ(kept.Value(), 6);
}

TEST(ExpectedOrElseTest, MovesAMoveOnlyValueOutOfATemporary)
{
    Expected<std::unique_ptr<int>, int> owned = std::make_unique<int>(5);

    const Expected<std::unique_ptr<int>, std::string> kept = std::move(owned).OrElse(
        [](int) -> Expected<std::unique_ptr<int>, std::string> { return Unexpected(std::string("unused")); });

    ASSERT_TRUE(kept.HasValue());
    ASSERT_NE(kept.Value(), nullptr);
    EXPECT_EQ(*kept.Value(), 5);
}

// Pedir el valor de uno que tiene un error es un bug de quien llama: el programa se corta ahí, con
// un mensaje, en vez de seguir con un valor que no existe. GoogleTest lo prueba en otro proceso.
TEST(ExpectedDeathTest, ValueOfAnErrorStopsTheProgram)
{
    const Expected<int, std::string> result = Unexpected(std::string("archivo corto"));

    EXPECT_DEATH(
        {
            ASSERT_TRUE(Client::Tests::SuppressSystemCrashReports());
            static_cast<void>(result.Value());
        },
        "de un Expected que no tiene valor");
}

TEST(ExpectedDeathTest, ErrorOfAValueStopsTheProgram)
{
    const Expected<int, std::string> result = 42;

    EXPECT_DEATH(
        {
            ASSERT_TRUE(Client::Tests::SuppressSystemCrashReports());
            static_cast<void>(result.Error());
        },
        "de un Expected que no tiene error");
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
    EXPECT_DEATH(
        {
            ASSERT_TRUE(Client::Tests::SuppressSystemCrashReports());
            static_cast<void>(result.Error());
        },
        "de un Expected que no tiene error");
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

TEST(ExpectedVoidAndThenTest, RunsTheNextStepOnASuccess)
{
    const Expected<int, std::string> three =
        SucceedsUnless(false).AndThen([]() -> Expected<int, std::string> { return 3; });

    ASSERT_TRUE(three.HasValue());
    EXPECT_EQ(three.Value(), 3);
}

TEST(ExpectedVoidAndThenTest, PassesAnErrorThroughUntouched)
{
    const Expected<void, std::string> failed = SucceedsUnless(true);
    bool ran = false;

    const Expected<int, std::string> three = failed.AndThen(
        [&ran]() -> Expected<int, std::string>
        {
            ran = true;
            return 3;
        });

    EXPECT_FALSE(ran);
    ASSERT_FALSE(three.HasValue());
    EXPECT_EQ(three.Error(), "falta la textura");
}

TEST(ExpectedVoidTransformTest, MapsASuccess)
{
    const auto three = SucceedsUnless(false).Transform([] { return 3; });

    static_assert(std::is_same_v<decltype(three), const Expected<int, std::string>>);
    ASSERT_TRUE(three.HasValue());
    EXPECT_EQ(three.Value(), 3);
}

TEST(ExpectedVoidTransformTest, KeepsTheError)
{
    const Expected<void, std::string> failed = SucceedsUnless(true);
    bool ran = false;

    const Expected<void, std::string> done = failed.Transform([&ran] { ran = true; });

    EXPECT_FALSE(ran);
    ASSERT_FALSE(done.HasValue());
    EXPECT_EQ(done.Error(), "falta la textura");
}

TEST(ExpectedVoidOrElseTest, RunsTheRecoveryOnAnError)
{
    const Expected<void, std::string> failed = SucceedsUnless(true);

    const Expected<void, int> coded = failed.OrElse([](const std::string& error) -> Expected<void, int>
                                                    { return Unexpected(static_cast<int>(error.size())); });

    ASSERT_FALSE(coded.HasValue());
    EXPECT_EQ(coded.Error(), 16);
}

TEST(ExpectedVoidOrElseTest, KeepsTheSuccess)
{
    bool ran = false;

    const Expected<void, int> done = SucceedsUnless(false).OrElse(
        [&ran](const std::string&) -> Expected<void, int>
        {
            ran = true;
            return {};
        });

    EXPECT_FALSE(ran);
    EXPECT_TRUE(done.HasValue());
}

TEST(ExpectedVoidOrElseTest, MovesAMoveOnlyErrorOutOfATemporary)
{
    Expected<void, std::unique_ptr<int>> failed = Unexpected(std::make_unique<int>(7));

    const Expected<void, int> coded =
        std::move(failed).OrElse([](std::unique_ptr<int> error) -> Expected<void, int> { return Unexpected(*error); });

    ASSERT_FALSE(coded.HasValue());
    EXPECT_EQ(coded.Error(), 7);
}

TEST(ExpectedVoidDeathTest, ErrorOfASuccessStopsTheProgram)
{
    const Expected<void, std::string> result = SucceedsUnless(false);

    EXPECT_DEATH(
        {
            ASSERT_TRUE(Client::Tests::SuppressSystemCrashReports());
            static_cast<void>(result.Error());
        },
        "de un Expected que no tiene error");
}
} // namespace
