#pragma once

#include "Core/Expected.h"

#include <filesystem>
#include <string>
#include <vector>

// A file of a world's folder (World<N>\), looked up as the original asks for it (case-insensitive,
// Data::ResolveDataPath) and read whole, with a cap of 16 MB: more than any world file of the lab. The error names it
// as Data\<relative>.
namespace Client::Terrain
{
[[nodiscard]] Core::Expected<std::vector<char>, std::string> ReadWorldFile(const std::filesystem::path& dataRoot,
                                                                           const std::string& relative);
} // namespace Client::Terrain
