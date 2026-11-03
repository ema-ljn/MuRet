#include "App/Credentials.h"

#include "Core/Environment.h"
#include "Core/Expected.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::App
{
namespace
{
// Lo que tiene mal una variable, sin su valor; vacío si sirve.
std::string ProblemWith(std::string_view name, const std::optional<std::string>& value)
{
    if (!value)
    {
        return std::format("falta {}", name);
    }
    if (value->empty())
    {
        return std::format("{} está vacía", name);
    }
    if (value->size() > MaxCredentialBytes)
    {
        return std::format("{} pasa de {} bytes", name, MaxCredentialBytes);
    }
    return {};
}
} // namespace

std::optional<std::string> SystemEnvironment(std::string_view name)
{
    // Pide un nombre terminado en \0.
    return Core::ReadEnvironmentVariable(std::string(name).c_str());
}

// Las dos se miran antes de decir nada, así el error nombra todas las que hay que arreglar.
Core::Expected<Credentials, std::string> ReadCredentials(const EnvironmentLookup& lookup)
{
    std::optional<std::string> account = lookup(AccountVariable);
    std::optional<std::string> password = lookup(PasswordVariable);
    std::string problems;
    for (const std::string& problem : {ProblemWith(AccountVariable, account), ProblemWith(PasswordVariable, password)})
    {
        if (!problem.empty())
        {
            problems += (problems.empty() ? "" : "; ") + problem;
        }
    }
    if (!problems.empty())
    {
        return Core::Unexpected(std::format("{}: con --auto-login, la cuenta y la clave del login salen del entorno "
                                            "(Client/README.md dice cómo cargarlas)",
                                            problems));
    }
    return Credentials{std::move(account).value_or(std::string()), std::move(password).value_or(std::string())};
}
} // namespace Client::App
