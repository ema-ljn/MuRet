#pragma once

#include <cstdint>

// Two independent observations of 0x61a560 (05 § La carpeta de cada mapa).
namespace Client::Maps
{
// The observed map, except the argument=observed=32 guard, which turns it into 9.
[[nodiscard]] std::int32_t MapAtWorldLoadStart(std::int32_t argumentMap, std::int32_t observedMap);
// The caller provides the independent observation after the models; if the map stays stable it matches the guard's
// result, as in WorldFolderNumber's projection.
[[nodiscard]] std::int32_t WorldFolderAfterModels(std::int32_t observedMap);
} // namespace Client::Maps
