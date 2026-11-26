#pragma once

#include "EngineApi.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>

// main.exe asks for its files with Windows paths ("Data\\Player\\Player.bmd") and Windows ignores case. Linux does
// not, and the lab's Data\ copy has lowercase folders (player\, skill\, sound\)
// (Client/docs/research/09-datos-disponibles.md, "Cómo se verificó").
namespace Client::Data
{
// Each part is compared ignoring ASCII case, as on Windows: an exact match wins, else the first in byte order, with no
// backtracking if the next part is not in it (only on Linux, with two folders that differ in case). Bytes >= 0x80 are
// compared as they are. `originalPath` ends at its first '\0', like the .bmd fixed-length names; empty parts and "."
// are skipped. On Windows the path found may keep the requested case.
// nullopt with an empty `error`: it does not exist (a broken symlink does not) or is not inside Data\ (empty, only
// ".", absolute, ending in a separator or with ".."). nullopt with `error`: a folder could not be read, or `dataRoot`
// is empty (std::errc::invalid_argument).
ENGINE_API std::optional<std::filesystem::path> ResolveDataPath(const std::filesystem::path& dataRoot,
                                                                std::string_view originalPath, std::error_code& error);
} // namespace Client::Data
