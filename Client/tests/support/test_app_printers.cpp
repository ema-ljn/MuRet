#include "support/AppPrinters.h"

#include "App/Credentials.h"

#include <gtest/gtest.h>

#include <string>

// support/AppPrinters.h: lo que imprime GoogleTest de unas Credentials, que no puede ser la clave.
namespace
{
using Client::App::Credentials;

// La cuenta y la clave sintéticas de los fixtures de mu-client (MuClientNetFixtures.h). Sin la
// clave, los largos difieren: cada campo dice el suyo.
TEST(AppPrintersTest, CredentialsShowTheLengthsAndNotTheText)
{
    const std::string printed = ::testing::PrintToString(Credentials{"user0001", "pass0002"});

    EXPECT_EQ(printed, "Credentials{cuenta de 8 bytes, clave de 8 bytes}");
    EXPECT_EQ(printed.find("user0001"), std::string::npos);
    EXPECT_EQ(printed.find("pass0002"), std::string::npos);
    EXPECT_EQ(::testing::PrintToString(Credentials{"user0001", ""}),
              "Credentials{cuenta de 8 bytes, clave de 0 bytes}");
}
} // namespace
