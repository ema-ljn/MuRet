#pragma once

#include <array>
#include <cstddef>
#include <optional>

// A lasting overlay on the shared Player.bmd: 06 § Caminar (0x4f37c0). It does not change the const resource.
namespace Client::Actors
{
class PlayerActionSpeeds
{
public:
    void SetWalking(bool slowed);
    void Set(std::size_t action, float speed);
    [[nodiscard]] float Speed(std::size_t action, float fallback) const;

private:
    static constexpr std::size_t PlayerActionCount = 205;
    std::array<std::optional<float>, PlayerActionCount> m_speeds{};
};
} // namespace Client::Actors
