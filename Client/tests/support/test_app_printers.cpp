#include "support/AppPrinters.h"

#include "App/Credentials.h"
#include "App/SessionState.h"

#include <gtest/gtest.h>

#include <string>

// support/AppPrinters.h: lo que imprime GoogleTest de unas Credentials, que no puede ser la clave,
// y de los estados y las fallas de la sesión.
namespace
{
using Client::App::Credentials;
using Client::App::SessionFailure;
using Client::App::SessionState;

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

// Los estados y las fallas del GameServer (CL4.4) salen con su nombre, como los de antes.
TEST(AppPrintersTest, TheGameServerStatesAndFailuresShowTheirNames)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingGameHello), "WaitingGameHello");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingLoginResult), "WaitingLoginResult");
    EXPECT_EQ(::testing::PrintToString(SessionState::LoggedIn), "LoggedIn");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::HelloRejected), "HelloRejected");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::VersionMismatch), "VersionMismatch");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::LoginRejected), "LoginRejected");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::CredentialsTooLong), "CredentialsTooLong");
}

// La falla del tope de los Waiting* (CL4.5).
TEST(AppPrintersTest, NoReplyShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionFailure::NoReply), "NoReply");
}

// La falla del servidor ocupado, F4 05 (Q-C71).
TEST(AppPrintersTest, ServerBusyShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionFailure::ServerBusy), "ServerBusy");
}
} // namespace
