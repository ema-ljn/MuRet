#pragma once

#include "Data/Models/ModelFiles.h"
#include "Tables/ModelTextureCalls.h"

#include <span>

// The global effect loads that Castle Siege and Crywolf reach: 0x615700, from step 5 of 0x61ccb0.
namespace Client::Tables
{
// 0x6161ea, 0x616333/349, 0x6163c2/3d8 and 0x61679e..0x616854, with BMD files under Data\\.
[[nodiscard]] std::span<const Data::Models::ModelFile> SiegeModelFiles();
// 0x616370/38d: after SetNext(0x69), Gate02 and the two StoneCoffin. Gate02 is looked up twice.
[[nodiscard]] std::span<const ModelTextureCall> SiegeBeforeSkillTextureCalls();
// The explicit calls 0x616b7e/9c and 0x616c7a..0x616d58, then these models' calls in the loop 0x616e20. The name
// cache of 0x60d550 keeps the first path and slot, even when the loop asks for Skill\\ again.
[[nodiscard]] std::span<const ModelTextureCall> SiegeTextureCalls();
} // namespace Client::Tables
