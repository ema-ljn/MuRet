#pragma once

#include "Core/Expected.h"

#include <filesystem>
#include <string>
#include <vector>

// Un archivo de la carpeta de un mundo (World<N>\), buscado como lo pide el original (sin mayúsculas,
// Data::ResolveDataPath) y leído entero, con un tope de 16 MB: más que cualquier archivo de un mundo
// del laboratorio. El error lo nombra como Data\<relative>.
namespace Client::World
{
[[nodiscard]] Core::Expected<std::vector<char>, std::string> ReadWorldFile(const std::filesystem::path& dataRoot,
                                                                           const std::string& relative);
} // namespace Client::World
