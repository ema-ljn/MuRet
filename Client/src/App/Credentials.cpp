#include "App/Credentials.h"

#include <cstdlib>
#include <format>
#include <string>
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
    // Las dos piden un nombre terminado en \0.
    const std::string terminated(name);
#ifdef _MSC_VER
    // _dupenv_s devuelve una copia que hay que liberar.
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, terminated.c_str()) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(terminated.c_str());
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
#endif
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
        return Core::Unexpected(std::format("{}: con un servidor, la cuenta y la clave del login salen del entorno "
                                            "(Client/README.md dice cómo cargarlas)",
                                            problems));
    }
    return Credentials{std::move(account).value_or(std::string()), std::move(password).value_or(std::string())};
}
} // namespace Client::App
