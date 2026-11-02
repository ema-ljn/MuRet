#pragma once

#include <string>

// La cuenta y la clave con las que la sesión entra al GameServer (el login es de la CL4.4).
// Contrato: el plan de la CL4, pregunta 7. Hasta la escena de login (CL5.5) salen de dos variables
// de entorno, que lee la CL4.4.
//
// Sin operator<<: no se imprimen nunca, ni al log. El PrintTo de los tests
// (tests/support/AppPrinters.h) escribe el largo de cada campo y no el texto; sin él, GoogleTest
// vuelca los bytes del struct, y con SSO la clave va ahí.
namespace Client::App
{
struct Credentials
{
    std::string account;
    std::string password;
};
} // namespace Client::App
