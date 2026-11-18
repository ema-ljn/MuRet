#pragma once

#include "Data/Tables/ItemTable.h"
#include "Data/Tables/TableChecksum.h"
#include "support/TableFiles.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// Item.bmd sintético: registros vacíos cifrados y checksum válido; no deriva del port de ítems.
namespace Client::Tests
{
inline std::vector<std::byte> EmptyItemTableBytes()
{
    auto records = EncryptRecords(std::vector<std::byte>(Data::Tables::ItemTableSize), Data::Tables::ItemRecordSize);
    const auto checksum = Data::Tables::TableChecksum(std::span(records), Data::Tables::ItemChecksumKey,
                                                      Data::Tables::ChecksumParity::XorOddDwords);
    return WithStoredChecksum(std::move(records), checksum);
}

// El lugar del equipo (+0x22), el ancho (+0x24) y el alto (+0x25) de un tipo de Item.bmd.
struct ItemShape
{
    std::uint16_t type;
    std::uint8_t width;
    std::uint8_t height;
    std::uint8_t equipSlot;
};

// Un WORD de un registro, en little endian (como el nivel que pide, +0x3a).
struct ItemWord
{
    std::uint16_t type;
    std::size_t offset;
    std::uint16_t value;
};

// Item.bmd sintético con esas formas, esos WORD y todo lo demás en 0, ya cargado.
inline Data::Tables::ItemTable ItemTableOf(const std::vector<ItemShape>& shapes,
                                           const std::vector<ItemWord>& words = {})
{
    using Data::Tables::ItemRecordSize;
    constexpr std::size_t EquipSlotOffset = 0x22;
    constexpr std::size_t WidthOffset = 0x24;
    constexpr std::size_t HeightOffset = 0x25;
    std::vector<std::byte> plain(Data::Tables::ItemTableSize);
    for (const ItemShape& shape : shapes)
    {
        const std::size_t base = std::size_t{shape.type} * ItemRecordSize;
        plain[base + EquipSlotOffset] = std::byte{shape.equipSlot};
        plain[base + WidthOffset] = std::byte{shape.width};
        plain[base + HeightOffset] = std::byte{shape.height};
    }
    for (const ItemWord& word : words)
    {
        const std::size_t at = std::size_t{word.type} * ItemRecordSize + word.offset;
        plain[at] = static_cast<std::byte>(word.value & 0xff);
        plain[at + 1] = static_cast<std::byte>(word.value >> 8);
    }
    const std::vector<std::byte> encrypted = EncryptRecords(std::move(plain), ItemRecordSize);
    return Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}
} // namespace Client::Tests
