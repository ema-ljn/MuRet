#include "support/UiPrinters.h"

#include "FrontEnd/LoginWindow.h"
#include "Ui/TextField.h"

#include <gtest/gtest.h>

#include <string>

// support/UiPrinters.h: what GoogleTest prints of a field and of an account request, which cannot be what was typed
// (CL5.5).
namespace
{
using Client::FrontEnd::LoginRequest;
using Client::Ui::TextField;

// The password field says its length and that it is the password, not what it holds; the account one, the same
// without the mark.
TEST(UiPrintersTest, AFieldShowsItsLengthAndNotItsText)
{
    TextField password(10, true);
    password.Type("pass0002");
    TextField account(10, false);
    account.Type("user0001");

    const std::string printedPassword = ::testing::PrintToString(password);
    const std::string printedAccount = ::testing::PrintToString(account);

    EXPECT_EQ(printedPassword, "TextField{8 bytes, clave}");
    EXPECT_EQ(printedAccount, "TextField{8 bytes}");
    EXPECT_EQ(printedPassword.find("pass0002"), std::string::npos);
    EXPECT_EQ(printedAccount.find("user0001"), std::string::npos);
}

TEST(UiPrintersTest, ARequestShowsItsKindAndTheLengths)
{
    const LoginRequest request{.kind = LoginRequest::Kind::Submit, .credentials = {"user0001", "pass0002"}};

    EXPECT_EQ(::testing::PrintToString(request), "LoginRequest{Submit, cuenta de 8 bytes, clave de 8 bytes}");
    EXPECT_EQ(::testing::PrintToString(LoginRequest::Kind::EmptyPassword), "EmptyPassword");
}
} // namespace
