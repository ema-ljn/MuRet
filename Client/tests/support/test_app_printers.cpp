#include "support/AppPrinters.h"

#include "App/Credentials.h"
#include "App/Resolution.h"
#include "App/SessionState.h"

#include <gtest/gtest.h>

#include <string>

// support/AppPrinters.h: lo que imprime GoogleTest de unas Credentials, que no puede ser la clave,
// de una resolución, y de los estados y las fallas de la sesión.
namespace
{
using Client::App::Credentials;
using Client::App::Resolution;
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
    EXPECT_EQ(::testing::PrintToString(SessionFailure::CredentialsTooLong), "CredentialsTooLong");
}

// La falla del tope de los Waiting* (CL4.5).
TEST(AppPrintersTest, NoReplyShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionFailure::NoReply), "NoReply");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::ApplicationFailed), "ApplicationFailed");
}

// Los estados que esperan a la UI (CL5.5).
TEST(AppPrintersTest, TheStatesThatWaitForTheUiShowTheirNames)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::ChoosingServer), "ChoosingServer");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingCredentials), "WaitingCredentials");
}

// Los de la selección de personaje (CL6.3).
TEST(AppPrintersTest, TheCharacterStatesShowTheirNames)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingCharacterList), "WaitingCharacterList");
    EXPECT_EQ(::testing::PrintToString(SessionState::ChoosingCharacter), "ChoosingCharacter");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingCreateResult), "WaitingCreateResult");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingDeleteResult), "WaitingDeleteResult");
    EXPECT_EQ(::testing::PrintToString(SessionState::EnteringWorld), "EnteringWorld");
}

// El de la orden del GameServer de cerrar el juego (Q-C106).
TEST(AppPrintersTest, QuitOrderedShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::QuitOrdered), "QuitOrdered");
}

TEST(AppPrintersTest, AResolutionShowsWidthByHeight)
{
    EXPECT_EQ(::testing::PrintToString(Resolution{1024, 768}), "1024 × 768");
}
} // namespace
