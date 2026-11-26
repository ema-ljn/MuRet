#pragma once

#include "EngineApi.h"

#include <optional>
#include <string>

// The only place in ClientLib, besides the sockets, that branches on the compiler: std::getenv gives C4996 on MSVC,
// which does not compile with /WX.
namespace Client::Core
{
// `name` ends in \0. On MSVC it goes through _dupenv_s, which returns a heap copy freed here.
[[nodiscard]] ENGINE_API std::optional<std::string> ReadEnvironmentVariable(const char* name);
} // namespace Client::Core
