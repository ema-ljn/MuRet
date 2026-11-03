#include "App/Credentials.h"

#include "support/AppPrinters.h"

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// Client::App::ReadCredentials: la cuenta y la clave de las dos variables de la pregunta 7 del
// plan de la CL4 (CL4.4, PR B), con un mapa en vez del entorno. Solo con la cuenta y la clave
// sintéticas de MuClientNetFixtures.h; los mensajes de error se imprimen enteros porque nunca
// traen un valor.
namespace
{
using Client::App::AccountVariable;
using Client::App::EnvironmentLookup;
using Client::App::PasswordVariable;
using Client::App::ReadCredentials;
using Client::App::SystemEnvironment;

using Environment = std::map<std::string, std::string, std::less<>>;

// La cuenta y la clave sintéticas de los fixtures del login (MuClientNetFixtures.h).
constexpr std::string_view Account = "user0001";
constexpr std::string_view Password = "pass0002";

// Un entorno de mentira: las variables de `values`, y ninguna más.
EnvironmentLookup LookupIn(const Environment& values)
{
    return [values](std::string_view name) -> std::optional<std::string>
    {
        const auto found = values.find(name);
        if (found == values.end())
        {
            return std::nullopt;
        }
        return found->second;
    };
}

bool Says(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

TEST(CredentialsTest, ReadsBoth)
{
    const auto read = ReadCredentials(LookupIn({{std::string(AccountVariable), std::string(Account)},
                                                {std::string(PasswordVariable), std::string(Password)}}));

    ASSERT_TRUE(read.HasValue()) << read.Error();
    EXPECT_TRUE(read.Value().account == Account);
    EXPECT_TRUE(read.Value().password == Password);
}

// Sin las dos, el error nombra las dos: así se arreglan de una vez.
TEST(CredentialsTest, MissingNamesBoth)
{
    const auto read = ReadCredentials(LookupIn({}));

    ASSERT_FALSE(read.HasValue());
    EXPECT_TRUE(Says(read.Error(), AccountVariable)) << read.Error();
    EXPECT_TRUE(Says(read.Error(), PasswordVariable)) << read.Error();
}

// Con una sola, el error nombra la que falta y no la otra.
TEST(CredentialsTest, MissingOneNamesOnlyIt)
{
    const auto read = ReadCredentials(LookupIn({{std::string(AccountVariable), std::string(Account)}}));

    ASSERT_FALSE(read.HasValue());
    EXPECT_TRUE(Says(read.Error(), PasswordVariable)) << read.Error();
    EXPECT_FALSE(Says(read.Error(), AccountVariable)) << read.Error();
}

// Una clave vacía y una cuenta de 11 bytes, que WriteLoginRequest no podría escribir: el error
// nombra las dos variables y no trae la cuenta, ni entera ni su principio.
TEST(CredentialsTest, EmptyOrTooLongNamesItWithoutItsValue)
{
    const std::string tooLong = std::string(Account) + "xyz";
    ASSERT_EQ(tooLong.size(), 11u);

    const auto read = ReadCredentials(
        LookupIn({{std::string(AccountVariable), tooLong}, {std::string(PasswordVariable), std::string()}}));

    ASSERT_FALSE(read.HasValue());
    EXPECT_TRUE(Says(read.Error(), AccountVariable)) << read.Error();
    EXPECT_TRUE(Says(read.Error(), PasswordVariable)) << read.Error();
    EXPECT_FALSE(Says(read.Error(), Account)) << read.Error();
}

// Diez bytes entran: es el campo del login.
TEST(CredentialsTest, TenBytesFit)
{
    const std::string tenBytes = std::string(Account) + "12";
    ASSERT_EQ(tenBytes.size(), Client::App::MaxCredentialBytes);

    const auto read = ReadCredentials(
        LookupIn({{std::string(AccountVariable), tenBytes}, {std::string(PasswordVariable), std::string(Password)}}));

    ASSERT_TRUE(read.HasValue()) << read.Error();
    EXPECT_TRUE(read.Value().account == tenBytes);
}

// La del proceso lee el entorno de verdad: PATH está siempre, y un nombre inventado no. No se
// imprime lo que vale PATH.
TEST(CredentialsTest, SystemEnvironmentReadsTheProcess)
{
    EXPECT_TRUE(SystemEnvironment("PATH").has_value());
    EXPECT_FALSE(SystemEnvironment("MURET_CLIENT_NO_EXISTE_ESTA_VARIABLE").has_value());
}
} // namespace
