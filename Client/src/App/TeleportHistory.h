#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

// 04 § Los cambios de mapa, 0x4e3e98..0x4e3f1d: diez copias del wire anteriores al handler 1C.
namespace Client::App
{
inline constexpr std::size_t TeleportHistoryCapacity = 10;
inline constexpr std::size_t MaxTeleportHistoryPacketSize = 256;

class TeleportHistory
{
public:
    void Record(std::span<const std::byte> packet);
    [[nodiscard]] std::span<const std::vector<std::byte>> Entries() const;

private:
    std::array<std::vector<std::byte>, TeleportHistoryCapacity> m_entries;
    std::size_t m_size = 0;
};
} // namespace Client::App
