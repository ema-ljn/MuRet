#pragma once

#include "Data/Models/ModelFiles.h"

#include <span>

// Pedidos con nombre de Object1, en el orden de 0x616ec0 (05 § Modelos .bmd).
// El almacenamiento y las cadenas son estáticos; el span no posee los datos.
namespace Client::Scenes
{
[[nodiscard]] std::span<const Data::Models::ModelFile> LorenciaModelFiles();
} // namespace Client::Scenes
