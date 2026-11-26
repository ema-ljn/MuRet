#pragma once

#include "EngineApi.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// PC53/RN value profile + observed C0 preservation + file constants (ADR 0012). C0 includes less than or unordered:
// NaN can return finite values. Does not attest an XP session, x87 status/ABI, NaN payloads or universal FPATAN/libm
// equivalence.
namespace Client::Math
{
// 0x4e93f0: the heading from (x1, y1) to (x2, y2), in degrees, as left on the x87 stack.
[[nodiscard]] ENGINE_API double CreateAngle(float x1, float y1, float x2, float y2);

// 0x4e9560: turns `from` towards `to` along its asymmetric branches. Keeps the sign of `step`; a negative step can move
// away from the target. Distinguishes spilled P from retained Q.
[[nodiscard]] ENGINE_API double TurnAngle(float from, float to, float step);

// 0x4e96f0: one add of 360 and an f32 store if C0 (negative or unordered), no full normalization. Returns to-from, or
// (360-to)+from if from<180 or unordered and from+180 is not ordered-greater than to, or (360-from)+to only if
// from>=180 and from-180>to. A mode byte of exactly 1 negates a result below zero or unordered.
[[nodiscard]] ENGINE_API double AngleDistance(float from, float to, std::uint8_t mode);

// 0x4e9810: turns `angles` towards `target` from `position` by `step` degrees: heading (z) with 0x4e93f0 in the plane
// and 0x4e9560, pitch (x) with 360 minus 0x4e93f0 from (z, flat length) to (target z, 0). Returns the distance on the
// x87, not stored as float: the root of dz² plus the squares of dx and dy that it stored as floats.
[[nodiscard]] ENGINE_API double MoveToward(const glm::vec3& position, glm::vec3& angles, const glm::vec3& target,
                                           float step);
} // namespace Client::Math
