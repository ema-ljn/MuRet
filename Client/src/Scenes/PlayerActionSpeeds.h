#pragma once

#include <array>
#include <cstddef>
#include <optional>

// Overlay durable del Player.bmd compartido: 06, Caminar (0x4f37c0). No altera el recurso const.
namespace Client::Scenes
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
} // namespace Client::Scenes
