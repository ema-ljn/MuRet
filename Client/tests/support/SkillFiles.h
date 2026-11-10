#pragma once

#include "Data/Tables/SkillTable.h"
#include "Data/Tables/TableChecksum.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

// Skill.bmd sintético, con el XOR3 por registro y checksum de paridad par (05 § Tablas .bmd).
namespace Client::Tests
{
inline std::vector<std::byte> SkillFileBytes(std::vector<std::byte> plain)
{
    auto records = EncryptRecords(std::move(plain), Data::Tables::SkillRecordSize);
    const auto checksum = Data::Tables::TableChecksum(std::span(records), Data::Tables::SkillChecksumKey,
                                                      Data::Tables::ChecksumParity::XorEvenDwords);
    return WithStoredChecksum(std::move(records), checksum);
}

inline void WriteEmptySkillFile(const std::filesystem::path& root, const std::filesystem::path& local = "Local")
{
    WriteSceneFile(root, local / "Skill.bmd", SkillFileBytes(std::vector<std::byte>(Data::Tables::SkillTableSize)));
}
} // namespace Client::Tests
