#pragma once

#include "Crypto/Xor3.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// The fixed-length .bmd tables Gate, Item and Skill (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd"),
// as decrypted bytes with the layout they have in the original's memory.
namespace Client::Data::Tables
{
template <std::size_t RecordSize> class RecordTable
{
public:
    static_assert(RecordSize > 0, "un registro tiene al menos un byte");

    // Decrypts each record with XOR3, as the original: the key restarts on every record. Bytes left over that do not
    // make a whole record are not copied.
    explicit RecordTable(std::span<const std::byte> encrypted) : m_records(CopyWholeRecords(encrypted))
    {
        const std::span<std::byte> all(m_records);
        for (std::size_t index = 0; index < Count(); ++index)
        {
            Crypto::Xor3(all.subspan(index * RecordSize, RecordSize));
        }
    }

    [[nodiscard]] std::size_t Count() const
    {
        return m_records.size() / RecordSize;
    }

    // Points into the table: valid until the table is destroyed, moved or assigned.
    [[nodiscard]] std::optional<std::span<const std::byte, RecordSize>> Record(std::size_t index) const&
    {
        if (index >= Count())
        {
            return std::nullopt;
        }
        const std::span<const std::byte> all(m_records);
        return all.subspan(index * RecordSize).first<RecordSize>();
    }

    // On a temporary table it does not compile: the record would dangle. A temporary reached through a reference or a
    // pointer still dangles, because the compiler no longer sees it: a `const RecordTable&`, the `->` of a temporary
    // `std::optional`, or the `Value()` of a temporary `const` `Expected`.
    std::optional<std::span<const std::byte, RecordSize>> Record(std::size_t index) const&& = delete;

private:
    static std::vector<std::byte> CopyWholeRecords(std::span<const std::byte> bytes)
    {
        const std::span<const std::byte> whole = bytes.first(bytes.size() - bytes.size() % RecordSize);
        return std::vector<std::byte>(whole.begin(), whole.end());
    }

    std::vector<std::byte> m_records;
};
} // namespace Client::Data::Tables
