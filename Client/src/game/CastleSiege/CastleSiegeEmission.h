#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct CastleSiegePrelude;

// 0x42b5fe..0x42c4e5: sorteos y accesos protegidos antes y entre las emisiones de la batalla.
[[nodiscard]] Core::Expected<void, std::string> EmitTheCastleBattle(const CastleSiegePrelude& prelude);
} // namespace Client::World
