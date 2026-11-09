#pragma once

#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/TableChecksum.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

// ItemSetType.bmd e ItemSetOption.bmd sintéticos, como los escribe el original: los registros cifrados
// con XOR3 y su checksum detrás. Registros en 0 son "ningún conjunto". No derivan del port.
namespace Client::Tests
{
inline std::vector<std::byte> ItemSetFileBytes(std::vector<std::byte> plain, std::size_t recordSize,
                                               std::uint32_t checksumKey)
{
    std::vector<std::byte> records = EncryptRecords(std::move(plain), recordSize);
    const std::uint32_t checksum =
        Data::Tables::TableChecksum(std::span(records), checksumKey, Data::Tables::ChecksumParity::XorOddDwords);
    return WithStoredChecksum(std::move(records), checksum);
}

inline std::vector<std::byte> ItemSetTypeFileBytes(std::vector<std::byte> plain)
{
    return ItemSetFileBytes(std::move(plain), Data::Tables::ItemSetTypeRecordSize,
                            Data::Tables::ItemSetTypeChecksumKey);
}

inline std::vector<std::byte> ItemSetOptionFileBytes(std::vector<std::byte> plain)
{
    return ItemSetFileBytes(std::move(plain), Data::Tables::ItemSetOptionRecordSize,
                            Data::Tables::ItemSetOptionChecksumKey);
}

// Los dos archivos sin ningún conjunto en `root` / `local`, donde los busca LoadGameData. `local` es la
// carpeta Local con las mayúsculas que ya tenga en `root`: en Linux, "local" y "Local" serían dos.
inline void WriteEmptyItemSetFiles(const std::filesystem::path& root, const std::filesystem::path& local = "Local")
{
    WriteSceneFile(root, local / "ItemSetType.bmd",
                   ItemSetTypeFileBytes(std::vector<std::byte>(Data::Tables::ItemSetTypeTableSize)));
    WriteSceneFile(root, local / "ItemSetOption.bmd",
                   ItemSetOptionFileBytes(std::vector<std::byte>(Data::Tables::ItemSetOptionTableSize)));
}

// Las dos tablas en memoria, de registros en claro.
inline Data::Tables::ItemSetTables ItemSetTablesFrom(std::vector<std::byte> types, std::vector<std::byte> options)
{
    const std::vector<std::byte> encryptedTypes = EncryptRecords(std::move(types), Data::Tables::ItemSetTypeRecordSize);
    const std::vector<std::byte> encryptedOptions =
        EncryptRecords(std::move(options), Data::Tables::ItemSetOptionRecordSize);
    return Data::Tables::ItemSetTables{.types = Data::Tables::ItemSetTypeTable(encryptedTypes),
                                       .options = Data::Tables::ItemSetOptionTable(encryptedOptions)};
}
} // namespace Client::Tests
