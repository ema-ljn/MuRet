#pragma once

#include "Network/Packet.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Los paquetes de los otros jugadores en el viewport armados con los structs que leen 0x4c8860 (0x12), 0x4ca780
// (0x14), 0x4c92a0 (0x45), 0x4c8380 (0x25) y 0x4c6b40 (0xDF): los offsets del ASM, no los del GS 0.97 (08, P-C160).
// Los nombres son inventados.
namespace Client::Tests
{
// El equipo de la lista sin nada: las manos 0xfff, las cinco partes 0x1ff, sin alas (b[4] & 0xc en 3 y b[8] & 7 en
// 0) ni mascota (b[4] & 3 en 3 y b[9] & 1 en 0).
inline constexpr std::array<std::uint8_t, 17> NakedEquipment = {0xff, 0xff, 0xff, 0xff, 0xff, 0,    0, 0, 0xf8,
                                                                0,    0,    0xf0, 0xff, 0xff, 0xff, 0, 0};

// Un registro del 0x12 (0x2c bytes).
struct ViewportPlayerRecord
{
    std::uint16_t key = 0;
    bool isNew = false;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t classAndState = 0;
    std::array<std::uint8_t, 17> equipment = NakedEquipment;
    std::uint32_t effects = 0;
    std::string_view name;
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    std::uint8_t directionAndPk = 0;
};

// Un registro del 0x45 (0x1c bytes).
struct TransformedRecordBytes
{
    std::uint16_t key = 0;
    bool isNew = false;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint16_t type = 0;
    std::uint32_t effects = 0;
    std::string_view name;
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    std::uint8_t directionAndPk = 0;
};

namespace ViewportDetail
{
inline void PutKey(std::span<std::byte> at, std::uint16_t key, bool isNew)
{
    const auto word = static_cast<std::uint16_t>(key | (isNew ? 0x8000u : 0u));
    at[0] = std::byte{static_cast<std::uint8_t>(word >> 8)};
    at[1] = std::byte{static_cast<std::uint8_t>(word & 0xff)};
}

inline void PutLe32(std::span<std::byte> at, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        at[i] = std::byte{static_cast<std::uint8_t>((value >> (8 * i)) & 0xff)};
}

inline void PutName(std::span<std::byte> at, std::string_view name)
{
    for (std::size_t i = 0; i < name.size() && i < 10; ++i)
        at[i] = std::byte{static_cast<std::uint8_t>(name[i])};
}

inline std::vector<std::byte> C2Header(std::uint8_t opcode, std::size_t count, std::size_t recordSize)
{
    const std::size_t size = 5 + count * recordSize;
    std::vector<std::byte> bytes(size);
    bytes[0] = std::byte{0xc2};
    bytes[1] = std::byte{static_cast<std::uint8_t>(size >> 8)};
    bytes[2] = std::byte{static_cast<std::uint8_t>(size & 0xff)};
    bytes[3] = std::byte{opcode};
    bytes[4] = std::byte{static_cast<std::uint8_t>(count)};
    return bytes;
}

inline Network::Packet Built(std::vector<std::byte> bytes)
{
    auto built = Network::Packet::FromBytes(std::move(bytes));
    EXPECT_TRUE(built.HasValue());
    return std::move(built).Value();
}
} // namespace ViewportDetail

// El C2 12: pkt[4] la cuenta y los registros desde pkt[5].
inline Network::Packet ViewportPlayersPacket(std::span<const ViewportPlayerRecord> records)
{
    using namespace ViewportDetail;
    constexpr std::size_t Size = 0x2c;
    std::vector<std::byte> bytes = C2Header(0x12, records.size(), Size);
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        const ViewportPlayerRecord& record = records[index];
        const std::span<std::byte> at = std::span(bytes).subspan(5 + index * Size, Size);
        PutKey(at, record.key, record.isNew);
        at[2] = std::byte{record.x};
        at[3] = std::byte{record.y};
        at[4] = std::byte{record.classAndState};
        for (std::size_t i = 0; i < record.equipment.size(); ++i)
            at[5 + i] = std::byte{record.equipment[i]};
        PutLe32(at.subspan(0x18), record.effects);
        PutName(at.subspan(0x1c), record.name);
        at[0x26] = std::byte{record.targetX};
        at[0x27] = std::byte{record.targetY};
        at[0x28] = std::byte{record.directionAndPk};
    }
    return Built(std::move(bytes));
}

// El C2 45.
inline Network::Packet TransformedPacket(std::span<const TransformedRecordBytes> records)
{
    using namespace ViewportDetail;
    constexpr std::size_t Size = 0x1c;
    std::vector<std::byte> bytes = C2Header(0x45, records.size(), Size);
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        const TransformedRecordBytes& record = records[index];
        const std::span<std::byte> at = std::span(bytes).subspan(5 + index * Size, Size);
        PutKey(at, record.key, record.isNew);
        at[2] = std::byte{record.x};
        at[3] = std::byte{record.y};
        at[4] = std::byte{static_cast<std::uint8_t>(record.type >> 8)};
        at[5] = std::byte{static_cast<std::uint8_t>(record.type & 0xff)};
        PutLe32(at.subspan(8), record.effects);
        PutName(at.subspan(0xc), record.name);
        at[0x16] = std::byte{record.targetX};
        at[0x17] = std::byte{record.targetY};
        at[0x18] = std::byte{record.directionAndPk};
    }
    return Built(std::move(bytes));
}

// El C1 14: pkt[3] la cuenta y las claves enteras, big-endian.
inline Network::Packet LeavePacket(std::span<const std::uint16_t> keys)
{
    std::vector<std::byte> bytes(4 + keys.size() * 2);
    bytes[0] = std::byte{0xc1};
    bytes[1] = std::byte{static_cast<std::uint8_t>(bytes.size())};
    bytes[2] = std::byte{0x14};
    bytes[3] = std::byte{static_cast<std::uint8_t>(keys.size())};
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        bytes[4 + index * 2] = std::byte{static_cast<std::uint8_t>(keys[index] >> 8)};
        bytes[5 + index * 2] = std::byte{static_cast<std::uint8_t>(keys[index] & 0xff)};
    }
    return ViewportDetail::Built(std::move(bytes));
}

// El C1 25: la clave entera en pkt[3..4] y los siete bytes del ítem desde pkt[5].
inline Network::Packet EquipmentShownPacket(std::uint16_t key, const std::array<std::uint8_t, 7>& item)
{
    std::vector<std::byte> bytes(12);
    bytes[0] = std::byte{0xc1};
    bytes[1] = std::byte{12};
    bytes[2] = std::byte{0x25};
    bytes[3] = std::byte{static_cast<std::uint8_t>(key >> 8)};
    bytes[4] = std::byte{static_cast<std::uint8_t>(key & 0xff)};
    for (std::size_t i = 0; i < item.size(); ++i)
        bytes[5 + i] = std::byte{item[i]};
    return ViewportDetail::Built(std::move(bytes));
}

// El C1 DF: la clave entera y la celda.
inline Network::Packet PositionPacket(std::uint16_t key, std::uint8_t x, std::uint8_t y)
{
    std::vector<std::byte> bytes{std::byte{0xc1},
                                 std::byte{7},
                                 std::byte{0xdf},
                                 std::byte{static_cast<std::uint8_t>(key >> 8)},
                                 std::byte{static_cast<std::uint8_t>(key & 0xff)},
                                 std::byte{x},
                                 std::byte{y}};
    return ViewportDetail::Built(std::move(bytes));
}
} // namespace Client::Tests
