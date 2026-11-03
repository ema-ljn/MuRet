#pragma once

#include "UI/LoginWindow.h"
#include "UI/TextField.h"

#include <ostream>

// Cómo imprime GoogleTest los tipos de Client::UI (CL5.5), como AppPrinters.h. Un campo sale con
// su largo y si es la clave, nunca con lo que tiene, y un pedido de la cuenta, con su tipo y el
// largo de cada campo: sin estos PrintTo, GoogleTest vuelca los bytes del objeto, y con SSO lo
// tecleado va ahí.
namespace Client::UI
{
inline void PrintTo(const TextField& field, std::ostream* out)
{
    *out << "TextField{" << field.Size() << " bytes" << (field.Masked() ? ", clave" : "") << "}";
}

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
} // namespace Client::UI
