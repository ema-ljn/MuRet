#include "support/UiPrinters.h"

#include "UI/LoginWindow.h"
#include "UI/TextField.h"

#include <gtest/gtest.h>

#include <string>

// support/UiPrinters.h: lo que imprime GoogleTest de un campo y de un pedido de la cuenta, que no
// puede ser lo tecleado (CL5.5).
namespace
{
using Client::UI::LoginRequest;
using Client::UI::TextField;

// El campo de la clave dice su largo y que es la clave, no lo que tiene; el de la cuenta, lo mismo
// sin la marca.
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
