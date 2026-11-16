#include "Data/Tables/QuestTable.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Data::Tables
{
namespace
{
constexpr std::size_t NpcTypeOffset = 4;
constexpr std::size_t NameOffset = 5;
constexpr std::size_t EntriesOffset = 0x26;
constexpr std::size_t ConditionsOffset = 0x168;
constexpr std::size_t PartSize = 0x14;
constexpr std::size_t ClassFlagsOffset = 7;
constexpr std::size_t DialogsOffset = 0xc;

std::uint8_t ByteAt(std::span<const std::byte> bytes, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint16_t WordAt(std::span<const std::byte> bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(ByteAt(bytes, offset) | (ByteAt(bytes, offset + 1) << 8));
}

std::uint32_t DwordAt(std::span<const std::byte> bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(WordAt(bytes, offset)) |
           (static_cast<std::uint32_t>(WordAt(bytes, offset + 2)) << 16);
}

QuestEntry ReadEntry(std::span<const std::byte> bytes)
{
    QuestEntry entry;
    entry.byte0 = ByteAt(bytes, 0);
    entry.kind = ByteAt(bytes, 1);
    entry.itemGroup = ByteAt(bytes, 2);
    entry.itemIndex = ByteAt(bytes, 3);
    entry.itemLevel = ByteAt(bytes, 4);
    entry.itemCount = ByteAt(bytes, 5);
    entry.condition = ByteAt(bytes, 6);
    for (std::size_t i = 0; i < QuestClassFlagCount; ++i)
        entry.classes[i] = ByteAt(bytes, ClassFlagsOffset + i);
    for (std::size_t i = 0; i < QuestDialogCount; ++i)
        entry.dialogs[i] = static_cast<std::int16_t>(WordAt(bytes, DialogsOffset + 2 * i));
    return entry;
}

QuestCondition ReadCondition(std::span<const std::byte> bytes)
{
    QuestCondition condition;
    condition.byte0 = ByteAt(bytes, 0);
    condition.link = ByteAt(bytes, 1);
    condition.needQuest = static_cast<std::int16_t>(WordAt(bytes, 2));
    condition.minLevel = WordAt(bytes, 4);
    condition.maxLevel = WordAt(bytes, 6);
    condition.dword8 = DwordAt(bytes, 8);
    condition.zen = DwordAt(bytes, 0xc);
    condition.failDialog = static_cast<std::int16_t>(WordAt(bytes, 0x10));
    condition.word12 = static_cast<std::int16_t>(WordAt(bytes, 0x12));
    return condition;
}
} // namespace

std::string_view NameOf(const QuestRecord& quest)
{
    const std::string_view name(quest.name.data(), quest.name.size());
    return name.substr(0, name.find('\0'));
}

Core::Expected<QuestTable, DataError> LoadQuestTable(std::span<const std::byte> file)
{
    // Con menos de 0x2a8 bytes, el resto del primer bloque es el de operator new sin iniciar.
    if (file.size() < QuestRecordSize)
        return Core::Unexpected(DataError::FileTooShort);
    QuestTable table(QuestCount);
    // 0x418d61..0x418d70: un solo bloque de operator new para los 200.
    std::array<std::byte, QuestRecordSize> block{};
    for (std::size_t i = 0; i < QuestCount; ++i)
    {
        // 0x418d7c..0x418dba: fread de 0x2a8 en el bloque, 0x418de0 sobre él y la copia a +0xc + 0x2a8 i. El fread de
        // la CRT (0x69fecb) copia lo que queda del archivo al principio del bloque y deja el resto, que es lo del
        // registro anterior ya descifrado.
        const std::size_t start = std::min(i * QuestRecordSize, file.size());
        std::ranges::copy(file.subspan(start, std::min(QuestRecordSize, file.size() - start)), block.begin());
        Core::Xor3(block);
        const std::span<const std::byte> bytes(block);
        QuestRecord& quest = table[i];
        quest.entryCount = static_cast<std::int16_t>(WordAt(bytes, 0));
        quest.conditionCount = static_cast<std::int16_t>(WordAt(bytes, 2));
        quest.npcType = ByteAt(bytes, NpcTypeOffset);
        std::ranges::transform(bytes.subspan(NameOffset, QuestNameSize), quest.name.begin(),
                               [](std::byte letter) { return static_cast<char>(letter); });
        for (std::size_t e = 0; e < QuestEntryCount; ++e)
            quest.entries[e] = ReadEntry(bytes.subspan(EntriesOffset + e * PartSize, PartSize));
        for (std::size_t c = 0; c < QuestConditionCount; ++c)
            quest.conditions[c] = ReadCondition(bytes.subspan(ConditionsOffset + c * PartSize, PartSize));
    }
    return table;
}
} // namespace Client::Data::Tables
