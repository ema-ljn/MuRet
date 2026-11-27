#pragma once

#include "Core/Id.h"

#include <cstdint>

namespace Client::Audio
{
// An id of the sound catalog, what a sound request names. A model slot or an opcode is another space (CODING_RULES §7).
using SoundId = Core::Id<struct SoundIdTag, std::int32_t>;
} // namespace Client::Audio
