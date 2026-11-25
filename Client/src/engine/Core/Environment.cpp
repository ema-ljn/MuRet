#include "Core/Environment.h"

#include <cstdlib>
#include <optional>
#include <string>

#ifdef _MSC_VER
#include <memory>
#endif

namespace Client::Core
{
namespace
{
// Libera la copia de _dupenv_s. Es un tipo y no &std::free porque tomar la dirección de una función
// de la biblioteca estándar no está garantizado ([namespace.std]).
struct FreeDeleter
{
    void operator()(char* value) const
    {
        std::free(value);
    }
};
} // namespace

std::optional<std::string> ReadEnvironmentVariable(const char* name)
{
#ifdef _MSC_VER
    char* copy = nullptr;
    std::size_t size = 0;
    const errno_t error = _dupenv_s(&copy, &size, name);
    // Dueño de la copia desde acá, también si hubo un error.
    const std::unique_ptr<char, FreeDeleter> value(copy);
    if (error != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value.get());
#else
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
#endif
}
} // namespace Client::Core
