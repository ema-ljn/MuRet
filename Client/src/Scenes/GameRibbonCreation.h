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

// 52e470→52e3b0: búsqueda por dueño/key, alta y desplazamiento; no consume CRT.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string> CreateRibbon(std::span<RibbonState> ribbons,
                                                                                   const RibbonCreateRequest& request);
// 5091d0→52e500: envejece y duplica el primer par antes de la posible segunda resta de count.
[[nodiscard]] Core::Expected<void, std::string> StepRibbons(std::span<RibbonState> ribbons);
} // namespace Client::Scenes
