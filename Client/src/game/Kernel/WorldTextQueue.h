#pragma once

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <deque>
#include <string_view>

// 0x723c18: the 3D texts' queue, a std::deque of 0x4c-byte entries that 0x4381f0 fills from the characters' body
// (0x4fd603) and the HUD's 0x4383f0 empties (Hud/WorldTexts.h).
namespace Client::Kernel
{
inline constexpr std::size_t WorldTextSize = 64;

struct WorldText
{
    std::array<char, WorldTextSize> text{};
    glm::vec3 position{};
};

using WorldTextQueue = std::deque<WorldText>;

// 0x4381f0(text, position): it copies the text's first 64 bytes (the 0x10 dwords at 0x438209, even when the text is
// shorter: MuRet fills with 0 what the original takes from the stack) and the position to the end of the queue.
void QueueWorldText(WorldTextQueue& queue, std::string_view text, const glm::vec3& position);
} // namespace Client::Kernel
