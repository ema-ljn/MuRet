#pragma once
// What it brings is used without naming it: GoogleTest finds PrintTo by ADL, and include-cleaner does not see it.
// That is why every include of this header stays.
// IWYU pragma: always_keep

#include "FrontEnd/LoginWindow.h"
#include "Ui/TextField.h"

#include <ostream>

// How GoogleTest prints the Client::Ui and Client::FrontEnd types (CL5.5), like AppPrinters.h. A field comes out with
// its length and whether it is the password, never with what it holds, and an account request, with its type and the
// length of each field: without these PrintTo, GoogleTest dumps the object's bytes, and with SSO the typed text is
// there.
namespace Client::Ui
{
inline void PrintTo(const TextField& field, std::ostream* out)
{
    *out << "TextField{" << field.Size() << " bytes" << (field.Masked() ? ", clave" : "") << "}";
}
} // namespace Client::Ui

namespace Client::FrontEnd
{
inline void PrintTo(LoginRequest::Kind kind, std::ostream* out)
{
    switch (kind)
    {
    case LoginRequest::Kind::Submit:
        *out << "Submit";
        return;
    case LoginRequest::Kind::EmptyAccount:
        *out << "EmptyAccount";
        return;
    case LoginRequest::Kind::EmptyPassword:
        *out << "EmptyPassword";
        return;
    case LoginRequest::Kind::Cancel:
        *out << "Cancel";
        return;
    }
    *out << "LoginRequest::Kind fuera del enum";
}

inline void PrintTo(const LoginRequest& request, std::ostream* out)
{
    *out << "LoginRequest{";
    PrintTo(request.kind, out);
    *out << ", cuenta de " << request.credentials.account.size() << " bytes, clave de "
         << request.credentials.password.size() << " bytes}";
}

inline void PrintTo(LoginFocus focus, std::ostream* out)
{
    *out << (focus == LoginFocus::Account ? "Account" : "Password");
}
} // namespace Client::FrontEnd
