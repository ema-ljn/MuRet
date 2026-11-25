#pragma once
// GoogleTest finds PrintTo by ADL, so include-cleaner cannot see that a test uses this header.
// IWYU pragma: always_keep

#include "Core/Id.h"

#include <ostream>

namespace Client::Core
{
template <class Tag, IdRepresentation Rep> void PrintTo(Id<Tag, Rep> id, std::ostream* out)
{
    *out << +id.Value();
}
} // namespace Client::Core
