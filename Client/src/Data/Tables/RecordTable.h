// Los registros de una tabla .bmd de largo fijo: Gate, Item y Skill
// (Client/docs/research/05-formatos-de-datos.md, "Tablas .bmd").
//
// Guarda los registros descifrados, como bytes y con el layout que tienen en la memoria del
// original. Leer sus campos (nombres, niveles, requisitos) es de la CL9. Las tres tablas son la
// misma clase con otro largo de registro: RecordTable<9>, RecordTable<0x54> y RecordTable<0x3C>.

#pragma once

#include "Data/Tables/Xor3.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Client::Data::Tables
{

template <std::size_t RecordSize> class RecordTable
{
public:
    static_assert(RecordSize > 0, "un registro tiene al menos un byte");

    // Copia los registros enteros de `encrypted` y descifra cada uno con XOR3, como el original: la
    // clave vuelve a empezar en cada registro. Los bytes que sobran sin llegar a un registro no se
    // copian. Cuántos registros pide el formato lo decide el cargador de cada tabla.
    explicit RecordTable(std::span<const std::byte> encrypted) : records(CopyWholeRecords(encrypted))
    {
        const std::span<std::byte> all(records);
        for (std::size_t index = 0; index < Count(); ++index)
        {
            Xor3(all.subspan(index * RecordSize, RecordSize));
        }
    }

    [[nodiscard]] std::size_t Count() const
    {
        return records.size() / RecordSize;
    }

    // El registro `index`, o std::nullopt si index >= Count(). Apunta adentro de la tabla: vale
    // hasta que la tabla se destruya, se mueva o se le asigne otra.
    [[nodiscard]] std::optional<std::span<const std::byte, RecordSize>> Record(std::size_t index) const&
    {
        if (index >= Count())
        {
            return std::nullopt;
        }
        const std::span<const std::byte> all(records);
        return all.subspan(index * RecordSize).first<RecordSize>();
    }

    // Llamado sobre una tabla temporal no compila: el registro colgaría de una tabla que muere al
    // final de la línea. Si la temporal llega por una referencia o un puntero, el compilador ya no
    // la ve, y el registro cuelga igual: un `const RecordTable&`, el `->` de un `std::optional`
    // temporal, o el `Value()` de un `Expected` temporal y `const`.
    std::optional<std::span<const std::byte, RecordSize>> Record(std::size_t index) const&& = delete;

private:
    static std::vector<std::byte> CopyWholeRecords(std::span<const std::byte> bytes)
    {
        const std::span<const std::byte> whole = bytes.first(bytes.size() - bytes.size() % RecordSize);
        return std::vector<std::byte>(whole.begin(), whole.end());
    }

    std::vector<std::byte> records;
};

} // namespace Client::Data::Tables
