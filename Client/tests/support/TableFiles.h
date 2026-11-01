// Ayudas para los tests de las tablas .bmd de registros de largo fijo (Gate, Item, Skill): armar
// un archivo sintético como lo escribe el original, y comparar una tabla cargada con la memoria
// del original.

#pragma once

#include "Data/Tables/RecordTable.h"
#include "Data/Tables/Xor3.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Tests
{

// Lo que habría en el archivo: cada registro cifrado con XOR3 desde su principio, como el escritor
// del original. Los bytes que no llegan a un registro quedan como están.
inline std::vector<std::byte> EncryptRecords(std::vector<std::byte> records, std::size_t recordSize)
{
    const std::span<std::byte> all(records);
    for (std::size_t offset = 0; recordSize <= all.size() - offset; offset += recordSize)
    {
        Data::Tables::Xor3(all.subspan(offset, recordSize));
    }
    return records;
}

// `encrypted` con un checksum detrás, en 4 bytes little-endian, como lo guarda el escritor del
// original después de los registros.
inline std::vector<std::byte> WithStoredChecksum(std::vector<std::byte> encrypted, std::uint32_t checksum)
{
    constexpr unsigned BitsPerByte = 8;
    for (unsigned byte = 0; byte < sizeof(checksum); ++byte)
    {
        encrypted.push_back(static_cast<std::byte>(checksum >> (BitsPerByte * byte)));
    }
    return encrypted;
}

// El primer registro en que `table` y `memory` no coinciden, o std::nullopt si tienen los mismos
// registros. Si a una le faltan registros que la otra tiene, el primero que falta cuenta como
// distinto: una tabla vacía no es igual a una memoria llena.
template <std::size_t RecordSize>
std::optional<std::size_t> FirstRecordThatDiffers(const Data::Tables::RecordTable<RecordSize>& table,
                                                  std::span<const std::byte> memory)
{
    for (std::size_t index = 0; index < table.Count(); ++index)
    {
        const auto record = table.Record(index);
        const std::size_t offset = index * RecordSize;
        if (!record || memory.size() < offset + RecordSize ||
            !std::ranges::equal(*record, memory.subspan(offset, RecordSize)))
        {
            return index;
        }
    }
    if (memory.size() != table.Count() * RecordSize)
    {
        return table.Count();
    }
    return std::nullopt;
}

} // namespace Client::Tests
