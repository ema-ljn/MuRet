#pragma once

#include "Data/Tables/ItemTable.h"
#include "Data/Tables/TableChecksum.h"
#include "support/TableFiles.h"

#include <cstddef>
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
} // namespace Client::Tests
