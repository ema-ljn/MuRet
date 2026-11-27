#pragma once

#include <glm/ext/vector_float3.hpp>

// 0x5fd000, a face's normal. The terrain light uses it when a world loads (0x5f8b10, 06 § La luz del terreno en un
// punto), and so do the picking on each triangle (0x5fac90, 06 § El rayo contra el terreno) and the five planes of
// the frustum (0x5fbb00): a single implementation.
namespace Client::Terrain
{
// (p2 − p1) × (p3 − p1), normalized, with the ASM's PC53/nearest profile and no FMA: uy, uz, vz, nz and the length stay
// in the register (53 bits); vy, nx, vx, ux and ny go through a float on the stack, and each component is divided by
// the length in 53 bits before being stored as float. It does not set the original's CW or reproduce x87 flags or
// exceptions. With a length of 0 or NaN the original does not write the normal; this function has no such path and
// requires a finite, non-zero length. On terrain the triangles have finite heights and positive widths in x and y (nz
// is 10,000 in the light). The frustum constructor requires a camera with non-degenerate faces.
[[nodiscard]] glm::vec3 FaceNormal(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3);
} // namespace Client::Terrain
