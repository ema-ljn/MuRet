#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Weather
{
struct Leaf;
}

// External creators of 0x530490: 0x443990 (31) and 0x435600 (34/state 2).
namespace Client::Weather
{
struct GameLeafContext;
[[nodiscard]] Core::Expected<void, std::string> SpawnGameLeafCloud(Weather::Leaf& leaf, GameLeafContext& context,
                                                                   bool crywolf);
} // namespace Client::Weather
