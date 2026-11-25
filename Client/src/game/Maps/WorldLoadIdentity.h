#pragma once

#include <cstdint>

// Dos observaciones independientes de 0x61a560 (05 § La carpeta de cada mapa).
namespace Client::Data
{
// Devuelve el mapa observado, salvo la guarda argumento=observado=32 que lo pasa a 9.
[[nodiscard]] std::int32_t MapAtWorldLoadStart(std::int32_t argumentMap, std::int32_t observedMap);
// El caller aporta la observación independiente tras modelos; si el mapa permanece estable,
// coincide con el resultado de la guarda, como en la proyección legacy de WorldFolderNumber.
[[nodiscard]] std::int32_t WorldFolderAfterModels(std::int32_t observedMap);
} // namespace Client::Data
