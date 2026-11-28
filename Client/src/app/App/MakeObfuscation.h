#pragma once

#include "Network/IObfuscation.h"

#include <memory>

namespace Client::App
{
// The layer between the wire and the client that this build has: the CMake option MU_XOR_REMAP picks the source file
// that defines it.
[[nodiscard]] std::unique_ptr<const Network::IObfuscation> MakeObfuscation();
} // namespace Client::App
