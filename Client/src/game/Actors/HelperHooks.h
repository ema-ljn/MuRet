#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

// Where a character's pets put their helpers: the helpers' table (0x7d335d0) or, for a character outside the pool,
// the one helper it carries. game/Pets fills it (Scenes/HelperTable.h).
namespace Client::Actors
{
class IHelperHooks
{
public:
    virtual ~IHelperHooks() = default;

    // 0x443d40, for an empty pet.
    virtual void TurnOffTheHelpersOf(std::size_t rider) const = 0;
    // 0x443fa0, the pet's helper.
    [[nodiscard]] virtual Core::Expected<void, std::string>
    CreateTheHelper(std::uint16_t type, const glm::vec3& position, std::size_t rider) const = 0;
};
} // namespace Client::Actors
