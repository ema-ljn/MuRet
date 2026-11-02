#pragma once

#include "Core/Expected.h"
#include "Protocol/Connection/Login.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// La cuenta y la clave con las que la sesión entra al GameServer (el login de la CL4.4).
// Contrato: el plan de la CL4, pregunta 7. Hasta la escena de login (CL5.5) salen de dos variables
// de entorno, solo para la prueba a mano: ni en la config (un archivo con una clave) ni en la línea
// de comandos (el historial y `ps`). El README del cliente las carga con `read -rs` o
// `Read-Host -MaskInput`.
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

// Las dos variables de la pregunta 7.
inline constexpr std::string_view AccountVariable = "MURET_CLIENT_ACCOUNT";
inline constexpr std::string_view PasswordVariable = "MURET_CLIENT_PASSWORD";

// Lo que mide como mucho cada una: el campo del login. Con más, WriteLoginRequest daría
// FieldTooLong.
inline constexpr std::size_t MaxCredentialBytes = Protocol::Connection::CredentialSize;

// Lee una variable: std::nullopt si no está. La de main.cpp es SystemEnvironment; la de los tests,
// un mapa.
using EnvironmentLookup = std::function<std::optional<std::string>(std::string_view name)>;

// La variable de entorno `name` del proceso, o std::nullopt si no está. En MSVC con _dupenv_s, como
// tests/support/GoldenDir.cpp: std::getenv da C4996, que con /WX no compila.
[[nodiscard]] std::optional<std::string> SystemEnvironment(std::string_view name);

// La cuenta y la clave de AccountVariable y PasswordVariable. El error nombra cada variable que
// falta, que está vacía o que pasa de MaxCredentialBytes, y nunca trae su valor.
[[nodiscard]] Core::Expected<Credentials, std::string> ReadCredentials(const EnvironmentLookup& lookup);
} // namespace Client::App
