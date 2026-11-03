#pragma once
// Lo que trae se usa sin nombrarlo: GoogleTest busca PrintTo por ADL, e include-cleaner no lo
// ve. Por eso cada include de este header se queda.
// IWYU pragma: always_keep

#include "Scenes/SceneId.h"

#include <ostream>

// Cómo imprime GoogleTest un SceneId en un mensaje, como AppPrinters.h: con su nombre y el número
// de DAT_006d5dc8, y no con el byte del enum.
namespace Client::Scenes
{
inline void PrintTo(SceneId scene, std::ostream* out)
{
    *out << SceneName(scene) << " (" << static_cast<int>(scene) << ")";
}
} // namespace Client::Scenes
