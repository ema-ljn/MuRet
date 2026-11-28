#pragma once
// What it brings in is used without naming it: GoogleTest finds PrintTo by ADL, and include-cleaner does not see it.
// So every include of this header stays.
// IWYU pragma: always_keep

#include "Kernel/SceneId.h"

#include <ostream>

// How GoogleTest prints a SceneId in a message, like AppPrinters.h: with its name and the number of DAT_006d5dc8, not
// the enum's byte.
namespace Client::Kernel
{
inline void PrintTo(SceneId scene, std::ostream* out)
{
    *out << SceneName(scene) << " (" << static_cast<int>(scene) << ")";
}
} // namespace Client::Kernel
