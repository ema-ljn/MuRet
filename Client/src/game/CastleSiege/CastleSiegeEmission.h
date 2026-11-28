#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::CastleSiege
{
struct CastleSiegePrelude;

// 0x42b5fe..0x42c4e5: rand() draws and protected accesses before and between the battle's emissions.
[[nodiscard]] Core::Expected<void, std::string> EmitTheCastleBattle(const CastleSiegePrelude& prelude);
} // namespace Client::CastleSiege
