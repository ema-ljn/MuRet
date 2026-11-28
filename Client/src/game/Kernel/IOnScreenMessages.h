#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>
#include <string_view>

// A message a module writes on the screen. The chat implements it (Scenes::ChatMessages).
namespace Client::Kernel
{
class IOnScreenMessages
{
public:
    virtual ~IOnScreenMessages() = default;
    // 0x555190(name, text, type).
    virtual Core::Expected<void, std::string> Add(std::string_view name, std::string_view text, std::int32_t type) = 0;
};
} // namespace Client::Kernel
