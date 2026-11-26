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
// A type and not &std::free: taking the address of a standard library function is not guaranteed ([namespace.std]).
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
    // Owns the copy from here, also on error.
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
