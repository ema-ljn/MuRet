#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>

namespace Client::Actors
{
// Borrowed from lvalues of the exact type: the wrapper rejects temporaries/conversions.
struct CharacterPositionState
{
    glm::vec3& position;
    glm::vec3& angles;
    float& phase;
    std::reference_wrapper<const std::uint16_t> type;
    std::reference_wrapper<const std::uint16_t> item;
    std::reference_wrapper<const std::uint8_t> safeZone;
    std::reference_wrapper<const std::int32_t> map;
};
// 0x508a80. Disjoint, live referents; actor/buffers not relocated during the call. Synchronous callbacks, not empty
// when invoked; do not reassign the callback that is running. Speed gets the live actor by capture; terrain sees the
// provisional Z. Does not copy the actor. Numeric profile and std::sin/FSIN limit: ADR0012.
void UpdateCharacterPosition(CharacterPositionState state, const std::function<double()>& speed,
                             const std::function<double(float, float)>& height);
// Adapter variant: without height it keeps the XYZ/speed prefix and returns false before the height store and the
// phase advance. The speed callback is still required.
[[nodiscard]] bool TryUpdateCharacterPosition(CharacterPositionState state, const std::function<double()>& speed,
                                              const std::function<double(float, float)>& height);
} // namespace Client::Actors
