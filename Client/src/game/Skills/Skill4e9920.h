#pragma once

#include <cstdint>
#include <optional>

// 0x4e9920(x, y, targetX, targetY): the byte of the target's cell relative to (x, y), with each coordinate times −0.01
// ([0x6ba110]) and its _ftol: on top (((y) − (targetY)) + 8) << 4 and below ((x) − (targetX)) + 8, joined with OR
// without masking the low one, which outside −8..7 overwrites the high nibble (Q-C1232). The targets arrive as int
// (FILD). Called by the class branches of 0x57cfc0 (0x580a1a, 0x58657c, 0x587b2c, 0x58891c, 0x58dd70 and 0x59409f)
// with the hero's position and the _ftol of its +0x3a8/+0x3ac. Empty if a _ftol has no portable result
// (Math::OriginalFtolLow).
namespace Client::Skills
{
[[nodiscard]] std::optional<std::uint8_t> RelativeTargetCell(float x, float y, std::int32_t targetX,
                                                             std::int32_t targetY);
} // namespace Client::Skills
