#pragma once

#include <iosfwd>

// The camera and rotation fixtures store each float32 in exactly eight hex digits.
namespace Client::Tests
{
// Like operator>>: an invalid token sets failbit; the returned zero must not be used without checking it.
[[nodiscard]] float ReadHexFloat(std::istream& input);
} // namespace Client::Tests
