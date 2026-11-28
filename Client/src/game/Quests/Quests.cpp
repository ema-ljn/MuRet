#include "Quests/Quests.h"

#include "Core/Expected.h"
#include "Tables/QuestTable.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Quests/QuestState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Client::Quests
{
namespace
{
// 0x418e81 and 0x418f19: the hero's +0x390 follows quest 3.
constexpr std::int32_t HeroFlagQuest = 3;
// 0x4194e6: the item's type is group · 0x200 + index.
constexpr std::int32_t ItemGroupSize = 0x200;
// 0x418fe3 and 0x419011: the link of a condition that applies to every request.
constexpr std::uint8_t AnyEntry = 0xff;
// 0x4194c0 and 0x419b10: the item requests.
constexpr std::uint8_t ItemEntry = 1;
// 0x418f40: a request's four dialogs.
constexpr std::size_t DialogSlots = Tables::QuestDialogCount;
// 0x558445: the level that is not checked.
constexpr std::int32_t AnyLevel = -1;

QuestState& QuestOf(Kernel::WorldState& world)
{
    return world.Runtime().quest;
}

Kernel::ActorState& HeroOf(Kernel::WorldState& world)
{
    return world.Hero();
}

// The hero's +0x390: 0 and, with quest 3 done, 1 (0x418e76..0x418e8d and 0x418f0e..0x418f25).
void MarkTheHero(Kernel::WorldState& world)
{
    Kernel::ActorState& hero = HeroOf(world);
    hero.byte390 = 0;
    if (QuestStateOf(world.Runtime(), HeroFlagQuest) == QuestDone)
        hero.byte390 = 1;
}

// What RecordOf says of a record outside the 200 at +0xc: ReadsAQuestOutsideTheRecords looks for it.
constexpr std::string_view OutsideTheRecords = "fuera de los 200 registros de +0xc (Q-C842)";

Core::Expected<const Tables::QuestRecord*, std::string> RecordOf(const QuestState& quest, std::size_t record,
                                                                 std::string_view where)
{
    if (record >= quest.records.size())
        return Core::Unexpected(std::format("{}: la misión {} está {}", where, record, OutsideTheRecords));
    return &quest.records[record];
}

// The byte `index` (0 the least significant) of a field as 0x418d10 left it.
std::uint8_t ByteOfValue(std::uint32_t value, std::size_t index)
{
    return static_cast<std::uint8_t>((value >> (8 * index)) & 0xffu);
}

std::uint8_t EntryByteOf(const Tables::QuestEntry& entry, std::size_t offset)
{
    switch (offset)
    {
    case 0:
        return entry.byte0;
    case 1:
        return entry.kind;
    case 2:
        return entry.itemGroup;
    case 3:
        return entry.itemIndex;
    case 4:
        return entry.itemLevel;
    case 5:
        return entry.itemCount;
    case 6:
        return entry.condition;
    default:
        break;
    }
    if (offset < 7 + Tables::QuestClassFlagCount)
        return entry.classes[offset - 7];
    const std::size_t dialogByte = offset - 7 - Tables::QuestClassFlagCount;
    return ByteOfValue(static_cast<std::uint16_t>(entry.dialogs[dialogByte / 2]), dialogByte % 2);
}

std::uint8_t ConditionByteOf(const Tables::QuestCondition& condition, std::size_t offset)
{
    switch (offset)
    {
    case 0:
        return condition.byte0;
    case 1:
        return condition.link;
    case 2:
    case 3:
        return ByteOfValue(static_cast<std::uint16_t>(condition.needQuest), offset - 2);
    case 4:
    case 5:
        return ByteOfValue(condition.minLevel, offset - 4);
    case 6:
    case 7:
        return ByteOfValue(condition.maxLevel, offset - 6);
    case 8:
    case 9:
    case 10:
    case 11:
        return ByteOfValue(condition.dword8, offset - 8);
    case 12:
    case 13:
    case 14:
    case 15:
        return ByteOfValue(condition.zen, offset - 12);
    case 16:
    case 17:
        return ByteOfValue(static_cast<std::uint16_t>(condition.failDialog), offset - 16);
    default:
        return ByteOfValue(static_cast<std::uint16_t>(condition.word12), offset - 18);
    }
}

// The requests of `quest` that the original looks at: the first entryCount (signed; more than 16 would go on into the
// conditions, which MuRet does not read: it cuts).
// From +0x2134c, the 64 bytes that a one-byte index reaches (0x419560 with +0x2137e and 0x418ee0 with the A1's and the
// A2's): the 50 state bytes and the fields that follow, with the window of +0x21383 in the panels. Up to the end of
// the object (+0x21398) twelve more follow (the tab, +0x2138d, the padding and the corner), which only the A0's copy
// (0x418e20) reaches.
constexpr std::size_t QuestObjectBytes = 64;
constexpr std::size_t QuestObjectEnd = 76;

std::uint8_t QuestObjectByteOf(const Kernel::WorldRuntime& runtime, std::size_t index)
{
    const QuestState& quest = runtime.quest;
    if (index < quest.states.size())
        return quest.states[index];
    switch (index)
    {
    case 50:
        return quest.current;
    case 51:
        return quest.reached;
    case 52:
        return quest.byte21380;
    case 53:
        return quest.byte21381;
    case 54:
        return quest.byte21382;
    case 55:
        return runtime.panels.delegated.quest;
    case 56:
    case 57:
        return ByteOfValue(static_cast<std::uint16_t>(quest.dialog), index - 56);
    case 58:
        return quest.currentState;
    case 59:
        return quest.byte21387;
    case 60:
    case 61:
    case 62:
    case 63:
        return ByteOfValue(quest.zen, index - 60);
    default:
        return 0;
    }
}

// `value` with its byte `index` (0 the least significant) set to `byte`.
std::uint32_t WithByte(std::uint32_t value, std::size_t index, std::uint8_t byte)
{
    const std::size_t shift = 8 * index;
    return (value & ~(0xffu << shift)) | (static_cast<std::uint32_t>(byte) << shift);
}

// The write of QuestObjectByteOf, up to the end of the object (Q-C1126): 0x418ee0 writes the byte index >> 2 and
// 0x418e20 copies the A0's count from +0x2134c. Nobody reads the padding at +0x2138e and +0x2138f and it is not kept.
void SetQuestObjectByte(Kernel::WorldRuntime& runtime, std::size_t index, std::uint8_t value)
{
    QuestState& quest = runtime.quest;
    if (index < quest.states.size())
    {
        quest.states[index] = value;
        return;
    }
    switch (index)
    {
    case 50:
        quest.current = value;
        break;
    case 51:
        quest.reached = value;
        break;
    case 52:
        quest.byte21380 = value;
        break;
    case 53:
        quest.byte21381 = value;
        break;
    case 54:
        quest.byte21382 = value;
        break;
    case 55:
        runtime.panels.delegated.quest = value;
        break;
    case 56:
    case 57:
        quest.dialog = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(WithByte(static_cast<std::uint16_t>(quest.dialog), index - 56, value)));
        break;
    case 58:
        quest.currentState = value;
        break;
    case 59:
        quest.byte21387 = value;
        break;
    case 60:
    case 61:
    case 62:
    case 63:
        quest.zen = WithByte(quest.zen, index - 60, value);
        break;
    case 64:
        quest.tab = value;
        break;
    case 65:
        quest.refreshClass = value;
        break;
    case 68:
    case 69:
    case 70:
    case 71:
        quest.x = static_cast<std::int32_t>(WithByte(static_cast<std::uint32_t>(quest.x), index - 68, value));
        break;
    case 72:
    case 73:
    case 74:
    case 75:
        quest.y = static_cast<std::int32_t>(WithByte(static_cast<std::uint32_t>(quest.y), index - 72, value));
        break;
    default:
        break;
    }
}

Core::Expected<std::size_t, std::string> EntryCountOf(const Tables::QuestRecord& quest, std::string_view where)
{
    if (quest.entryCount <= 0)
        return std::size_t{0};
    if (static_cast<std::size_t>(quest.entryCount) > Tables::QuestEntryCount)
        return Core::Unexpected(std::format("{}: {} pedidos pasan los 16 del registro", where, quest.entryCount));
    return static_cast<std::size_t>(quest.entryCount);
}

Core::Expected<void, std::string> ShowTheDialog(const QuestContext& context, std::int32_t dialog)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    return Ui::ShowTheQuestDialog(runtime.dialogBox, runtime.dialogs, &runtime.dialogTable, context.texts, dialog);
}
} // namespace

bool ReadsAQuestOutsideTheRecords(std::string_view error)
{
    return error.find(OutsideTheRecords) != std::string_view::npos;
}

std::uint8_t QuestClassFlagOf(const Tables::QuestRecord& quest, std::size_t entry, std::uint8_t characterClass)
{
    constexpr std::size_t PartSize = 0x14;
    constexpr std::size_t EntriesSize = Tables::QuestEntryCount * PartSize;
    constexpr std::size_t PaddingSize = 2;
    const std::size_t offset = entry * PartSize + 7 + characterClass;
    if (offset < EntriesSize)
        return EntryByteOf(quest.entries[offset / PartSize], offset % PartSize);
    if (offset < EntriesSize + PaddingSize)
        return 0;
    const std::size_t inConditions = offset - EntriesSize - PaddingSize;
    return ConditionByteOf(quest.conditions[inConditions / PartSize], inConditions % PartSize);
}

std::uint8_t QuestStateOf(const Kernel::WorldRuntime& runtime, std::int32_t index)
{
    // 0x41957e..0x419588: CDQ, AND 3 and SAR 2, the signed division.
    const std::int32_t byteIndex = index / 4;
    if (byteIndex < 0 || static_cast<std::size_t>(byteIndex) >= QuestObjectBytes)
        return 0;
    const std::uint8_t value = QuestObjectByteOf(runtime, static_cast<std::size_t>(byteIndex));
    // 0x41958b..0x41959a: SHL CL,1 over the low byte of the subtraction; SHR AL,CL takes CL & 0x1f.
    const auto shift = static_cast<std::uint32_t>(((static_cast<std::uint32_t>(index - byteIndex) & 0xfu) << 1));
    const std::uint32_t shifted = shift >= 8 ? 0u : (static_cast<std::uint32_t>(value) >> shift);
    return static_cast<std::uint8_t>(shifted & 3u);
}

void ReadTheCurrentQuestState(Kernel::WorldRuntime& runtime)
{
    // 0x41956f..0x41957c: the index unsigned and SHR 2.
    runtime.quest.currentState = QuestStateOf(runtime, runtime.quest.current);
}

std::int32_t MissingQuestItems(const Kernel::OwnInventory& inventory, std::int32_t type, std::int32_t count,
                               std::int32_t level)
{
    std::int32_t found = 0;
    for (const Kernel::ItemState& item : inventory.cells)
    {
        if (static_cast<std::int32_t>(static_cast<std::int16_t>(item.id)) != type ||
            static_cast<std::int32_t>(item.occupied) <= 0)
            continue;
        if (level != AnyLevel && static_cast<std::int32_t>((item.scalar1Low >> 3) & 0xfu) != level)
            continue;
        ++found;
        if (count <= found)
            return 0;
    }
    return count - found;
}

std::size_t ApplyTheQuestList(Kernel::WorldState& world, std::uint8_t count, std::span<const std::uint8_t> bytes)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    QuestState& quest = runtime.quest;
    // 0x418e2e..0x418e47: the hero's class, unsigned.
    const std::uint8_t heroClass = HeroOf(world).characterClass;
    quest.refreshClass = 1;
    runtime.byte6eeda8 = 0;
    quest.characterClass = static_cast<std::uint8_t>(heroClass & 7u);
    quest.evolution = static_cast<std::uint8_t>(heroClass >> 3);
    // 0x418e4b..0x418e75: the 50 at 0 and the packet's `count` on top, which from the 51st overwrite the fields that
    // follow (also the +0x2138d just set) up to the end of the object; what goes past it is not written (Q-C1126).
    quest.states.fill(0);
    for (std::size_t i = 0; i < std::min<std::size_t>(count, QuestObjectEnd); ++i)
        SetQuestObjectByte(runtime, i, i < bytes.size() ? bytes[i] : 0);
    // 0x418e77..0x418eaa: the first one that is not done, with what the copy left.
    std::int32_t first = 0;
    while (first < count && QuestStateOf(runtime, first) == QuestDone)
        ++first;
    quest.current = static_cast<std::uint8_t>(first);
    quest.reached = static_cast<std::uint8_t>(first);
    MarkTheHero(world);
    return count > QuestObjectEnd ? count - QuestObjectEnd : 0;
}

void SetTheQuestState(Kernel::WorldState& world, std::uint8_t index, std::uint8_t state)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    QuestState& quest = runtime.quest;
    // 0x418ef1..0x418f04: the signed maximum of the index and +0x2137f.
    quest.current = index;
    quest.reached = std::max(quest.reached, index);
    // 0x418f12: the byte index >> 2 (0 to 63, inside the object): from quest 200 on, one of the fields that follow the
    // 50, also the +0x2137e and +0x2137f just set (Q-C1126).
    SetQuestObjectByte(runtime, static_cast<std::size_t>(index >> 2u), state);
    MarkTheHero(world);
}

Core::Expected<std::int16_t, std::string> QuestDialogOf(const QuestContext& context, std::size_t record,
                                                        std::size_t slot)
{
    QuestState& quest = QuestOf(context.world);
    if (slot >= DialogSlots)
        return Core::Unexpected(std::format("0x418f40: el lugar {} pasa los 4 diálogos del pedido", slot));
    const auto read = RecordOf(quest, record, "0x418f40");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Tables::QuestRecord& current = *read.Value();
    const auto count = EntryCountOf(current, "0x418f40");
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    for (std::size_t e = 0; e < count.Value(); ++e)
    {
        if (QuestClassFlagOf(current, e, quest.characterClass) != 0)
            return current.entries[e].dialogs[slot];
    }
    // 0x418f7a..0x418f93: the previous one (a byte: 0 − 1 is 255) and 0x419600(0xff) again.
    --quest.current;
    const auto refreshed = RefreshTheQuest(context, 0xff);
    if (!refreshed.HasValue())
        return Core::Unexpected(refreshed.Error());
    return quest.dialog;
}

Core::Expected<bool, std::string> HasTheQuestItems(const QuestContext& context, std::size_t record)
{
    QuestState& quest = QuestOf(context.world);
    const auto read = RecordOf(quest, record, "0x4194c0");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Tables::QuestRecord& current = *read.Value();
    const auto count = EntryCountOf(current, "0x4194c0");
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    for (std::size_t e = 0; e < count.Value(); ++e)
    {
        const Tables::QuestEntry& entry = current.entries[e];
        if (QuestClassFlagOf(current, e, quest.characterClass) == 0 || entry.kind != ItemEntry)
            continue;
        const std::int32_t type = entry.itemIndex + entry.itemGroup * ItemGroupSize;
        if (MissingQuestItems(context.world.Inventory(), type, entry.itemCount, entry.itemLevel) == 0)
            continue;
        const auto dialog = QuestDialogOf(context, record, 1);
        if (!dialog.HasValue())
            return Core::Unexpected(dialog.Error());
        quest.dialog = dialog.Value();
        return false;
    }
    return true;
}

Core::Expected<bool, std::string> CheckTheQuestConditions(const QuestContext& context, std::size_t record,
                                                          bool checkZen)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    const auto read = RecordOf(quest, record, "0x418fb0");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Tables::QuestRecord& current = *read.Value();
    const auto entries = EntryCountOf(current, "0x418fb0");
    if (!entries.HasValue())
        return Core::Unexpected(entries.Error());
    if (current.conditionCount > static_cast<std::int16_t>(Tables::QuestConditionCount))
        return Core::Unexpected(
            std::format("0x418fb0: {} condiciones pasan las 16 del registro", current.conditionCount));
    const auto fail = [&quest](const Tables::QuestCondition& condition)
    {
        quest.dialog = condition.failDialog;
        quest.currentState = QuestConditionFailed;
        return false;
    };
    for (std::size_t e = 0; e < entries.Value(); ++e)
    {
        const Tables::QuestEntry& entry = current.entries[e];
        if (QuestClassFlagOf(current, e, quest.characterClass) == 0)
            continue;
        for (std::int16_t c = 0; c < current.conditionCount; ++c)
        {
            const Tables::QuestCondition& condition = current.conditions[static_cast<std::size_t>(c)];
            if (condition.link != entry.condition && condition.link != AnyEntry)
                continue;
            // 0x41902b..0x419044: the required quest, unsigned (0xffff is none).
            if (static_cast<std::uint16_t>(condition.needQuest) != 0xffffu &&
                QuestStateOf(runtime, static_cast<std::uint16_t>(condition.needQuest)) != QuestDone)
                return fail(condition);
            // 0x41904a..0x41922a: the level with the own record open; the minimum is compared unsigned (JA).
            if (condition.minLevel != 0)
            {
                std::uint16_t level = 0;
                {
                    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                    Models::OwnRecordCloseRandoms);
                    level = context.world.OwnCharacter().level;
                }
                if (condition.minLevel > level)
                    return fail(condition);
            }
            // 0x419235..0x419313: the maximum, with another opening (JC: the maximum below the level).
            if (condition.maxLevel != 0)
            {
                std::uint16_t level = 0;
                {
                    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                    Models::OwnRecordCloseRandoms);
                    level = context.world.OwnCharacter().level;
                }
                if (condition.maxLevel < level)
                    return fail(condition);
            }
            // 0x41931c..0x419407: the zen.
            if (condition.zen != 0)
            {
                if (!checkZen)
                {
                    quest.zen = condition.zen;
                    continue;
                }
                std::uint32_t money = 0;
                {
                    const Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                    Models::OwnRecordCloseRandoms);
                    money = context.world.OwnCharacter().zen;
                }
                quest.zen = condition.zen;
                if (condition.zen > money)
                    return fail(condition);
            }
        }
    }
    return true;
}

Core::Expected<std::uint8_t, std::string> RefreshTheQuest(const QuestContext& context, std::uint8_t state)
{
    QuestState& quest = QuestOf(context.world);
    // 0x419606..0x41964b: the MOV AL and the CMP EAX,−1 are never equal (Q-C844).
    if (quest.refreshClass != 0)
    {
        const std::uint8_t heroClass = HeroOf(context.world).characterClass;
        quest.refreshClass = 0;
        quest.characterClass = static_cast<std::uint8_t>(heroClass & 7u);
        quest.evolution = static_cast<std::uint8_t>(heroClass >> 3);
    }
    const std::size_t record = quest.current;
    if (state == 0xff)
        ReadTheCurrentQuestState(context.world.Runtime());
    else
        quest.currentState = state;
    switch (quest.currentState)
    {
    case QuestInProgress:
    {
        const auto has = HasTheQuestItems(context, record);
        if (!has.HasValue())
            return Core::Unexpected(has.Error());
        if (has.Value())
        {
            const auto dialog = QuestDialogOf(context, record, 2);
            if (!dialog.HasValue())
                return Core::Unexpected(dialog.Error());
            quest.dialog = dialog.Value();
            quest.currentState = QuestInProgress;
        }
        break;
    }
    case QuestDone:
    {
        const auto dialog = QuestDialogOf(context, record, 3);
        if (!dialog.HasValue())
            return Core::Unexpected(dialog.Error());
        quest.dialog = dialog.Value();
        break;
    }
    case QuestAvailable:
    {
        const auto checked = CheckTheQuestConditions(context, record, false);
        if (!checked.HasValue())
            return Core::Unexpected(checked.Error());
        if (checked.Value())
        {
            const auto dialog = QuestDialogOf(context, record, 0);
            if (!dialog.HasValue())
                return Core::Unexpected(dialog.Error());
            quest.dialog = dialog.Value();
        }
        break;
    }
    default:
        break;
    }
    return quest.currentState;
}

Core::Expected<bool, std::string> CanCompleteTheQuest(const Kernel::WorldState& world)
{
    const QuestState& quest = world.Runtime().quest;
    if (quest.currentState != QuestInProgress)
        return false;
    const auto read = RecordOf(quest, quest.current, "0x419b10");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Tables::QuestRecord& current = *read.Value();
    const auto entries = EntryCountOf(current, "0x419b10");
    if (!entries.HasValue())
        return Core::Unexpected(entries.Error());
    // 0x419b93: the mark is set before counting, with the class's first item request.
    bool any = false;
    for (std::size_t e = 0; e < entries.Value(); ++e)
    {
        const Tables::QuestEntry& entry = current.entries[e];
        if (QuestClassFlagOf(current, e, quest.characterClass) == 0 || entry.kind != ItemEntry)
            continue;
        any = true;
        const std::int32_t type = entry.itemIndex + entry.itemGroup * ItemGroupSize;
        if (MissingQuestItems(world.Inventory(), type, entry.itemCount, entry.itemLevel) != 0)
            return false;
    }
    return any;
}

Core::Expected<void, std::string> OpenTheQuestDialog(const QuestContext& context, std::int32_t quest)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (quest != -1)
        runtime.quest.current = static_cast<std::uint8_t>(quest);
    runtime.panels.field78abb00 = 0;
    runtime.panels.delegated.quest = QuestDialogWindow;
    if (auto refreshed = RefreshTheQuest(context, 0xff); !refreshed.HasValue())
        return Core::Unexpected(refreshed.Error());
    return ShowTheDialog(context, runtime.quest.dialog);
}

Core::Expected<void, std::string> OpenTheQuestList(const QuestContext& context, std::int32_t quest)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    QuestState& state = runtime.quest;
    if (quest != -1)
        state.current = static_cast<std::uint8_t>(quest);
    const std::uint8_t before = state.current;
    state.byte21380 = 0;
    state.byte21381 = 0;
    runtime.panels.delegated.quest = QuestListWindow;
    state.current = state.reached;
    if (auto refreshed = RefreshTheQuest(context, 0xff); !refreshed.HasValue())
        return Core::Unexpected(refreshed.Error());
    if (auto shown = ShowTheDialog(context, state.dialog); !shown.HasValue())
        return shown;
    state.current = before;
    return {};
}

void ResetTheQuests(QuestState& quest, std::uint8_t& window)
{
    quest.characterClass = QuestClassUnknown;
    quest.byte21381 = 0;
    quest.dialog = 0;
    window = 0;
    quest.byte21382 = 0;
    quest.current = 0;
    quest.reached = 0;
    quest.states.fill(0);
    quest.flags[0] = 0;
    quest.flags[1] = 0;
    quest.flags[2] = 0;
}
} // namespace Client::Quests
