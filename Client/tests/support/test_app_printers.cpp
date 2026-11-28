#include "support/AppPrinters.h"

#include "Session/Credentials.h"
#include "Platform/Resolution.h"
#include "Session/SessionState.h"

#include <gtest/gtest.h>

#include <string>

// support/AppPrinters.h: what GoogleTest prints of Credentials, which cannot be the password, of a resolution, and of
// the session's states and failures.
namespace
{
using Client::App::Credentials;
using Client::Platform::Resolution;
using Client::Session::SessionFailure;
using Client::Session::SessionState;

// The synthetic account and password of mu-client's fixtures (MuClientNetFixtures.h). Without the password, the
// lengths differ: each field says its own.
TEST(AppPrintersTest, CredentialsShowTheLengthsAndNotTheText)
{
    const std::string printed = ::testing::PrintToString(Credentials{"user0001", "pass0002"});

    EXPECT_EQ(printed, "Credentials{cuenta de 8 bytes, clave de 8 bytes}");
    EXPECT_EQ(printed.find("user0001"), std::string::npos);
    EXPECT_EQ(printed.find("pass0002"), std::string::npos);
    EXPECT_EQ(::testing::PrintToString(Credentials{"user0001", ""}),
              "Credentials{cuenta de 8 bytes, clave de 0 bytes}");
}

TEST(AppPrintersTest, TheGameServerStatesAndFailuresShowTheirNames)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingGameHello), "WaitingGameHello");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingLoginResult), "WaitingLoginResult");
    EXPECT_EQ(::testing::PrintToString(SessionState::LoggedIn), "LoggedIn");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::HelloRejected), "HelloRejected");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::VersionMismatch), "VersionMismatch");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::CredentialsTooLong), "CredentialsTooLong");
}

TEST(AppPrintersTest, NoReplyShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionFailure::NoReply), "NoReply");
    EXPECT_EQ(::testing::PrintToString(SessionFailure::ApplicationFailed), "ApplicationFailed");
}

TEST(AppPrintersTest, TheStatesThatWaitForTheUiShowTheirNames)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::ChoosingServer), "ChoosingServer");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingCredentials), "WaitingCredentials");
}

TEST(AppPrintersTest, TheCharacterStatesShowTheirNames)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingCharacterList), "WaitingCharacterList");
    EXPECT_EQ(::testing::PrintToString(SessionState::ChoosingCharacter), "ChoosingCharacter");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingCreateResult), "WaitingCreateResult");
    EXPECT_EQ(::testing::PrintToString(SessionState::WaitingDeleteResult), "WaitingDeleteResult");
    EXPECT_EQ(::testing::PrintToString(SessionState::EnteringWorld), "EnteringWorld");
}

TEST(AppPrintersTest, InWorldShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::InWorld), "InWorld");
}

// The GameServer's order to close the game (Q-C106).
TEST(AppPrintersTest, QuitOrderedShowsItsName)
{
    EXPECT_EQ(::testing::PrintToString(SessionState::QuitOrdered), "QuitOrdered");
}

TEST(AppPrintersTest, AResolutionShowsWidthByHeight)
{
    EXPECT_EQ(::testing::PrintToString(Resolution{1024, 768}), "1024 × 768");
}
} // namespace
