#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::App
{
struct TeleportHistoryEntry
{
    std::array<std::byte, 256> bytes{};
    std::uint32_t length = 0;
};
// Prefijo ordinario4e2b10/head1C: diez slots, mueve sólo el prefijo length y conserva las colas.
// El caller presta P y N de su Packet lógico; no es una copia del buffer físico de red nativo.
class TeleportHistory
{
public:
    void Append(std::span<const std::byte> packet);
    [[nodiscard]] std::span<const TeleportHistoryEntry> Entries() const;

private:
    std::array<TeleportHistoryEntry, 10> m_entries{};
    std::uint8_t m_count = 0;
};
} // namespace Client::App
