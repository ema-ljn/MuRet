#include "Scenes/Quests.h"

#include "Core/Expected.h"
#include "Data/Tables/QuestTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/QuestState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "World/ItemState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// 0x418e81 y 0x418f19: +0x390 del héroe sigue a la misión 3.
constexpr std::int32_t HeroFlagQuest = 3;
// 0x4194e6: el tipo del ítem es grupo · 0x200 + índice.
constexpr std::int32_t ItemGroupSize = 0x200;
// 0x418fe3 y 0x419011: el enlace de una condición que vale para todos los pedidos.
constexpr std::uint8_t AnyEntry = 0xff;
// 0x4194c0 y 0x419b10: los pedidos de ítems.
constexpr std::uint8_t ItemEntry = 1;
// 0x418f40: los cuatro diálogos de un pedido.
constexpr std::size_t DialogSlots = Data::Tables::QuestDialogCount;
// 0x558445: el nivel que no se mira.
constexpr std::int32_t AnyLevel = -1;

QuestState& QuestOf(WorldState& world)
{
    return world.Runtime().quest;
}

ActorState& HeroOf(WorldState& world)
{
    return world.Actors()[world.Runtime().hero];
}

// +0x390 del héroe: 0 y, con la misión 3 terminada, 1 (0x418e76..0x418e8d y 0x418f0e..0x418f25).
void MarkTheHero(WorldState& world)
{
    ActorState& hero = HeroOf(world);
    hero.byte390 = 0;
    if (QuestStateOf(QuestOf(world), HeroFlagQuest) == QuestDone)
        hero.byte390 = 1;
}

Core::Expected<const Data::Tables::QuestRecord*, std::string> RecordOf(const QuestState& quest, std::size_t record,
                                                                       std::string_view where)
{
    if (record >= quest.records.size())
    {
        return Core::Unexpected(std::format("{}: la misión {} está fuera de los {} registros de +0xc (Q-C842)", where,
                                            record, quest.records.size()));
    }
    return &quest.records[record];
}

// El byte `index` (0 el menos significativo) de un campo como lo dejó 0x418d10.
std::uint8_t ByteOfValue(std::uint32_t value, std::size_t index)
{
    return static_cast<std::uint8_t>((value >> (8 * index)) & 0xffu);
}

std::uint8_t EntryByteOf(const Data::Tables::QuestEntry& entry, std::size_t offset)
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
    if (offset < 7 + Data::Tables::QuestClassFlagCount)
        return entry.classes[offset - 7];
    const std::size_t dialogByte = offset - 7 - Data::Tables::QuestClassFlagCount;
    return ByteOfValue(static_cast<std::uint16_t>(entry.dialogs[dialogByte / 2]), dialogByte % 2);
}

std::uint8_t ConditionByteOf(const Data::Tables::QuestCondition& condition, std::size_t offset)
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

// Los pedidos de `quest` que mira el original: los primeros entryCount (con signo; más de 16 seguiría en las
// condiciones, que MuRet no lee: corta).
// Desde +0x2134c, los 64 bytes que alcanza un índice de un byte (0x419560 con +0x2137e): los 50 de estado y los campos
// que siguen. +0x21383 (la ventana, en los paneles) y +0x21387 (relleno) dan 0 en MuRet.
constexpr std::size_t QuestObjectBytes = 64;

std::uint8_t QuestObjectByteOf(const QuestState& quest, std::size_t index)
{
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
    case 56:
    case 57:
        return ByteOfValue(static_cast<std::uint16_t>(quest.dialog), index - 56);
    case 58:
        return quest.currentState;
    case 60:
    case 61:
    case 62:
    case 63:
        return ByteOfValue(quest.zen, index - 60);
    default:
        return 0;
    }
}

Core::Expected<std::size_t, std::string> EntryCountOf(const Data::Tables::QuestRecord& quest, std::string_view where)
{
    if (quest.entryCount <= 0)
        return std::size_t{0};
    if (static_cast<std::size_t>(quest.entryCount) > Data::Tables::QuestEntryCount)
        return Core::Unexpected(std::format("{}: {} pedidos pasan los 16 del registro", where, quest.entryCount));
    return static_cast<std::size_t>(quest.entryCount);
}

Core::Expected<void, std::string> ShowTheDialog(const QuestContext& context, std::int32_t dialog)
{
    WorldRuntime& runtime = context.world.Runtime();
    return UI::ShowTheQuestDialog(runtime.dialogBox, runtime.dialogs, &runtime.dialogTable, context.texts, dialog);
}
} // namespace

std::uint8_t QuestClassFlagOf(const Data::Tables::QuestRecord& quest, std::size_t entry, std::uint8_t characterClass)
{
    constexpr std::size_t PartSize = 0x14;
    constexpr std::size_t EntriesSize = Data::Tables::QuestEntryCount * PartSize;
    constexpr std::size_t PaddingSize = 2;
    const std::size_t offset = entry * PartSize + 7 + characterClass;
    if (offset < EntriesSize)
        return EntryByteOf(quest.entries[offset / PartSize], offset % PartSize);
    if (offset < EntriesSize + PaddingSize)
        return 0;
    const std::size_t inConditions = offset - EntriesSize - PaddingSize;
    return ConditionByteOf(quest.conditions[inConditions / PartSize], inConditions % PartSize);
}

std::uint8_t QuestStateOf(const QuestState& quest, std::int32_t index)
{
    // 0x41957e..0x419588: CDQ, AND 3 y SAR 2, la división con signo.
    const std::int32_t byteIndex = index / 4;
    if (byteIndex < 0 || static_cast<std::size_t>(byteIndex) >= QuestObjectBytes)
        return 0;
    const std::uint8_t value = QuestObjectByteOf(quest, static_cast<std::size_t>(byteIndex));
    // 0x41958b..0x41959a: SHL CL,1 sobre el byte bajo de la resta; SHR AL,CL toma CL & 0x1f.
    const auto shift = static_cast<std::uint32_t>(((static_cast<std::uint32_t>(index - byteIndex) & 0xfu) << 1));
    const std::uint32_t shifted = shift >= 8 ? 0u : (static_cast<std::uint32_t>(value) >> shift);
    return static_cast<std::uint8_t>(shifted & 3u);
}

void ReadTheCurrentQuestState(QuestState& quest)
{
    // 0x41956f..0x41957c: el índice sin signo y SHR 2.
    quest.currentState = QuestStateOf(quest, quest.current);
}

std::int32_t MissingQuestItems(const OwnInventory& inventory, std::int32_t type, std::int32_t count, std::int32_t level)
{
    std::int32_t found = 0;
    for (const World::ItemState& item : inventory.cells)
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

Core::Expected<void, std::string> ApplyTheQuestList(WorldState& world, std::uint8_t count,
                                                    std::span<const std::uint8_t> bytes)
{
    QuestState& quest = QuestOf(world);
    if (count > quest.states.size())
    {
        return Core::Unexpected(
            std::format("0x418e20: el A0 copia {} bytes y pasa los 50 de +0x2134c (pisaría +0x2137e)", count));
    }
    // 0x418e2e..0x418e47: la clase del héroe, sin signo.
    const std::uint8_t heroClass = HeroOf(world).characterClass;
    quest.refreshClass = 1;
    world.Runtime().byte6eeda8 = 0;
    quest.characterClass = static_cast<std::uint8_t>(heroClass & 7u);
    quest.evolution = static_cast<std::uint8_t>(heroClass >> 3);
    // 0x418e4b..0x418e6f: los 50 en 0 y `count` del paquete.
    quest.states.fill(0);
    for (std::size_t i = 0; i < count; ++i)
        quest.states[i] = i < bytes.size() ? bytes[i] : 0;
    // 0x418e71..0x418e9b: la primera que no está terminada.
    std::int32_t first = 0;
    while (first < count && QuestStateOf(quest, first) == QuestDone)
        ++first;
    quest.current = static_cast<std::uint8_t>(first);
    quest.reached = static_cast<std::uint8_t>(first);
    MarkTheHero(world);
    return {};
}

Core::Expected<void, std::string> SetTheQuestState(WorldState& world, std::uint8_t index, std::uint8_t state)
{
    QuestState& quest = QuestOf(world);
    const std::size_t byteIndex = index >> 2u;
    if (byteIndex >= quest.states.size())
    {
        return Core::Unexpected(
            std::format("0x418ee0: la misión {} escribe el byte {} de +0x2134c, fuera de los 50", index, byteIndex));
    }
    // 0x418ef1..0x418f04: el mayor con signo entre el índice y +0x2137f.
    quest.current = index;
    quest.reached = std::max(quest.reached, index);
    quest.states[byteIndex] = state;
    MarkTheHero(world);
    return {};
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
    const Data::Tables::QuestRecord& current = *read.Value();
    const auto count = EntryCountOf(current, "0x418f40");
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    for (std::size_t e = 0; e < count.Value(); ++e)
    {
        if (QuestClassFlagOf(current, e, quest.characterClass) != 0)
            return current.entries[e].dialogs[slot];
    }
    // 0x418f7a..0x418f93: la anterior (un byte: 0 − 1 es 255) y otra vez 0x419600(0xff).
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
    const Data::Tables::QuestRecord& current = *read.Value();
    const auto count = EntryCountOf(current, "0x4194c0");
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    for (std::size_t e = 0; e < count.Value(); ++e)
    {
        const Data::Tables::QuestEntry& entry = current.entries[e];
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
    WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    const auto read = RecordOf(quest, record, "0x418fb0");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Data::Tables::QuestRecord& current = *read.Value();
    const auto entries = EntryCountOf(current, "0x418fb0");
    if (!entries.HasValue())
        return Core::Unexpected(entries.Error());
    if (current.conditionCount > static_cast<std::int16_t>(Data::Tables::QuestConditionCount))
        return Core::Unexpected(
            std::format("0x418fb0: {} condiciones pasan las 16 del registro", current.conditionCount));
    const auto fail = [&quest](const Data::Tables::QuestCondition& condition)
    {
        quest.dialog = condition.failDialog;
        quest.currentState = QuestConditionFailed;
        return false;
    };
    for (std::size_t e = 0; e < entries.Value(); ++e)
    {
        const Data::Tables::QuestEntry& entry = current.entries[e];
        if (QuestClassFlagOf(current, e, quest.characterClass) == 0)
            continue;
        for (std::int16_t c = 0; c < current.conditionCount; ++c)
        {
            const Data::Tables::QuestCondition& condition = current.conditions[static_cast<std::size_t>(c)];
            if (condition.link != entry.condition && condition.link != AnyEntry)
                continue;
            // 0x41902b..0x419044: la misión pedida, sin signo (0xffff es ninguna).
            if (static_cast<std::uint16_t>(condition.needQuest) != 0xffffu &&
                QuestStateOf(quest, static_cast<std::uint16_t>(condition.needQuest)) != QuestDone)
                return fail(condition);
            // 0x41904a..0x41922a: el nivel con el registro propio abierto; el mínimo se compara sin signo (JA).
            if (condition.minLevel != 0)
            {
                std::uint16_t level = 0;
                {
                    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                            Render::Models::OwnRecordCloseRandoms);
                    level = context.world.OwnCharacter().level;
                }
                if (condition.minLevel > level)
                    return fail(condition);
            }
            // 0x419235..0x419313: el máximo, con otra apertura (JC: el máximo por debajo del nivel).
            if (condition.maxLevel != 0)
            {
                std::uint16_t level = 0;
                {
                    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                            Render::Models::OwnRecordCloseRandoms);
                    level = context.world.OwnCharacter().level;
                }
                if (condition.maxLevel < level)
                    return fail(condition);
            }
            // 0x41931c..0x419407: el zen.
            if (condition.zen != 0)
            {
                if (!checkZen)
                {
                    quest.zen = condition.zen;
                    continue;
                }
                std::uint32_t money = 0;
                {
                    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                            Render::Models::OwnRecordCloseRandoms);
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
    // 0x419606..0x41964b: el MOV AL y el CMP EAX,−1 nunca dan igual (Q-C844).
    if (quest.refreshClass != 0)
    {
        const std::uint8_t heroClass = HeroOf(context.world).characterClass;
        quest.refreshClass = 0;
        quest.characterClass = static_cast<std::uint8_t>(heroClass & 7u);
        quest.evolution = static_cast<std::uint8_t>(heroClass >> 3);
    }
    const std::size_t record = quest.current;
    if (state == 0xff)
        ReadTheCurrentQuestState(quest);
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

Core::Expected<bool, std::string> CanCompleteTheQuest(const WorldState& world)
{
    const QuestState& quest = world.Runtime().quest;
    if (quest.currentState != QuestInProgress)
        return false;
    const auto read = RecordOf(quest, quest.current, "0x419b10");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Data::Tables::QuestRecord& current = *read.Value();
    const auto entries = EntryCountOf(current, "0x419b10");
    if (!entries.HasValue())
        return Core::Unexpected(entries.Error());
    // 0x419b93: la marca se pone antes de contar, con el primer pedido de ítems de la clase.
    bool any = false;
    for (std::size_t e = 0; e < entries.Value(); ++e)
    {
        const Data::Tables::QuestEntry& entry = current.entries[e];
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
    WorldRuntime& runtime = context.world.Runtime();
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
    WorldRuntime& runtime = context.world.Runtime();
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
} // namespace Client::Scenes
