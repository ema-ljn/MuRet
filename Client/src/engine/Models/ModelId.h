#pragma once

#include "Core/Id.h"

#include <cstdint>

namespace Client::Models
{
// A slot of the model table, what a model library is asked by. An item code is another space (CODING_RULES §7).
using ModelId = Core::Id<struct ModelIdTag, std::uint16_t>;
} // namespace Client::Models
