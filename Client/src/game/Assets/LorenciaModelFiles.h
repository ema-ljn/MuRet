#pragma once

#include "Data/Models/ModelFiles.h"

#include <span>

// Named Object1 requests, in the order of 0x616ec0 (05 § Modelos .bmd). The storage and the strings are static; the
// span does not own the data.
namespace Client::Assets
{
[[nodiscard]] std::span<const Data::Models::ModelFile> LorenciaModelFiles();
} // namespace Client::Assets
