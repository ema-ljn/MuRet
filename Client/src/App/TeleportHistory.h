#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

// 04 § Los cambios de mapa, 0x4e3e98..0x4e3f1d: diez copias del wire anteriores al handler 1C. El 0B (0x4e3594..
// 0x4e361f, CL9 hito 11) guarda en la misma cola de 0x77f27b4, con el mismo código.
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
