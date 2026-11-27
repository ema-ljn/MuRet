#pragma once

#include "Core/Expected.h"
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
struct RibbonState;
} // namespace Client::Scenes

namespace Client::Actors::Drawing
{
struct RibbonCreateRequest
{
    const void* ownerContext = nullptr;
    const std::uint16_t* ownerEquipment = nullptr;
    glm::vec3 first{};
    glm::vec3 second{};
    glm::vec3 light{};
    std::int32_t type = 0;
    bool shortTrail = false;
    std::int32_t key = 0;
};

// 0x52e470 → 0x52e3b0: lookup by owner and key, addition and shift; it does not consume the CRT.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateRibbon(std::span<Scenes::RibbonState> ribbons, const RibbonCreateRequest& request);
// 0x5091d0 → 0x52e500: ages and duplicates the first pair before the possible second subtraction of count.
[[nodiscard]] Core::Expected<void, std::string> StepRibbons(std::span<Scenes::RibbonState> ribbons);
} // namespace Client::Actors::Drawing
