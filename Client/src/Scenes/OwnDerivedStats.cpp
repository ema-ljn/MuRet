#include "Scenes/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnItemOptions.h"
#include "Scenes/OwnItemSets.h"
#include "World/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

// 0x550d10, cotejado con el PE c32bbd5f (1.698 bytes), con 0x54ef00 (31) y 0x40dc50 (66): 06, "Los
// stats del registro propio". 0x40e1b0 y 0x40da20 están en Scenes/OwnItemSets.cpp.
namespace Client::Scenes
{
namespace
{
// El byte del registro de Item.bmd que devuelve 0x40dc50 (0x40dc88).
constexpr std::size_t ItemStatKindOffset = 0x44;
// (D+0x1c >> 2) & 3 (0x40dc58..0x40dc6e).
constexpr int ItemStatValueShift = 2;
constexpr int ItemStatValueMask = 3;
// lea eax, [eax + eax·4] antes de cada suma (0x550d82).
constexpr int ItemStatValueFactor = 5;
// Los tipos de 0x40dc50 que suman, por la tabla de saltos de 0x5513b4, con el WORD de cada uno.
constexpr std::uint8_t KindOfWord2a = 1;
constexpr std::uint8_t KindOfWord2c = 2;
constexpr std::uint8_t KindOfWord30 = 3;
constexpr std::uint8_t KindOfWord2e = 4;

// Los selectores de 0x40da20 para cada WORD (0x550db3..0x550e12): la fuerza, la agilidad, la energía, la
// vitalidad y +0x36.
constexpr std::uint32_t SetOptionSelectorOfWord2a = 0x8c;
constexpr std::uint32_t SetOptionSelectorOfWord2c = 0x8d;
constexpr std::uint32_t SetOptionSelectorOfWord30 = 0x8e;
constexpr std::uint32_t SetOptionSelectorOfWord2e = 0x8f;
constexpr std::uint32_t SetOptionSelectorOfWord36 = 0x90;

// El lugar de las alas, [0x7681eb4] + 0xd64 = +0xb6c + 7 · 0x48, y el tipo que mira (0x550e2a).
constexpr std::size_t WingsSlot = 7;
constexpr std::uint16_t CapeOfLordId = 0x1a1e;
// El selector de 0x54e540 en 0x55104b.
constexpr std::uint32_t Word36OptionSelector = 0x90;

// Los dos topes de la cola (0x55130e) y el módulo de sus dos sorteos.
constexpr std::uint16_t TailCap = 100;
constexpr int TailPercent = 100;

// Un WORD del registro: los 16 bits de abajo, como los `mov word` y `add word` del original.
std::uint16_t Low16(int value)
{
    return static_cast<std::uint16_t>(value);
}

// El WORD que suma el tipo `kind` (0x550d7f..0x550da9), o nullptr si ese tipo no suma.
std::uint16_t* BonusWord(OwnCharacterRecord& own, std::uint8_t kind)
{
    switch (kind)
    {
    case KindOfWord2a:
        return &own.word2a;
    case KindOfWord2c:
        return &own.word2c;
    case KindOfWord30:
        return &own.word30;
    case KindOfWord2e:
        return &own.word2e;
    default:
        return nullptr;
    }
}

std::array<std::byte, 2> LittleEndianWord(std::uint16_t value)
{
    return {static_cast<std::byte>(value & 0xffu), static_cast<std::byte>(value >> 8)};
}

std::uint16_t FromLittleEndian(std::span<const std::byte, 2> bytes)
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[0]) | (std::to_integer<unsigned>(bytes[1]) << 8));
}
} // namespace

Core::Expected<void, std::string> RecalculateOwnStats(OwnCharacterRecord& own, const OwnStatsContext& context)
{
    const auto sets = RecalculateOwnItemSets(own, context);
    if (!sets.HasValue())
    {
        return Core::Unexpected(std::format("0x550d10 (0x550d24): {}", sets.Error()));
    }
    ClearItemSetOptionMarks(context.itemSets->list);
    ClearOwnItemStatWords(own);
    auto bonuses = AddOwnItemStatBonuses(own, context.items);
    if (!bonuses.HasValue())
    {
        return bonuses;
    }
    auto setOptions = AddOwnItemSetOptionValues(own, context.itemSets->list);
    if (!setOptions.HasValue())
    {
        return setOptions;
    }
    auto cape = AddCapeOfLordOptions(own, context.ownRecordAccesses, context.random);
    if (!cape.HasValue())
    {
        return cape;
    }
    return Core::Unexpected(
        std::string("0x550d10: los ocho helpers de 0x551287..0x5512ba (0x54efe0, 0x54f9b0, 0x54fd40, "
                    "0x54fe10, 0x5507e0, 0x550990, 0x550c30 y 0x550c60) están en "
                    "Scenes/OwnStatHelpers.h y RecalculateOwnStats todavía no los llama"));
}

Core::Expected<void, std::string> AddOwnItemSetOptionValues(OwnCharacterRecord& own, ItemSetOptionList& list)
{
    struct WordAndSelector
    {
        std::uint16_t* word;
        std::uint32_t selector;
    };
    const std::array<WordAndSelector, 5> calls = {WordAndSelector{&own.word2a, SetOptionSelectorOfWord2a},
                                                  WordAndSelector{&own.word2c, SetOptionSelectorOfWord2c},
                                                  WordAndSelector{&own.word30, SetOptionSelectorOfWord30},
                                                  WordAndSelector{&own.word2e, SetOptionSelectorOfWord2e},
                                                  WordAndSelector{&own.word36, SetOptionSelectorOfWord36}};
    for (const WordAndSelector& call : calls)
    {
        auto added = AddItemSetOptionValues(list, *call.word, call.selector);
        if (!added.HasValue())
        {
            return Core::Unexpected(std::format("0x550d10 (0x40da20 con {:#x}): {}", call.selector, added.Error()));
        }
    }
    return {};
}

void ClearOwnItemStatWords(OwnCharacterRecord& own)
{
    own.word2a = 0;
    own.word2c = 0;
    own.word2e = 0;
    own.word30 = 0;
    own.word36 = 0;
    own.word32 = 0;
    own.word34 = 0;
}

Core::Expected<ItemStatBonus, std::string> ItemStatBonusOf(const World::ItemState& item,
                                                           const Data::Tables::ItemTable& items)
{
    // El valor se escribe antes de leer la tabla (0x40dc6e); el tipo, con signo (movsx, 0x40dc77).
    const auto value = static_cast<std::uint16_t>((item.flags1c >> ItemStatValueShift) & ItemStatValueMask);
    const auto type = static_cast<std::int16_t>(item.id);
    const std::optional<std::span<const std::byte, Data::Tables::ItemRecordSize>> record =
        type < 0 ? std::nullopt : items.Record(static_cast<std::size_t>(type));
    if (!record)
    {
        return Core::Unexpected(std::format("0x40dc50: el tipo {} lee *[0x7758550] fuera de Item.bmd (Q-C184)", type));
    }
    return ItemStatBonus{.kind = std::to_integer<std::uint8_t>((*record)[ItemStatKindOffset]), .value = value};
}

Core::Expected<void, std::string> AddOwnItemStatBonuses(OwnCharacterRecord& own, const Data::Tables::ItemTable* items)
{
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        const World::ItemState& item = own.equipment[slot];
        if (item.durability == 0)
        {
            continue;
        }
        if (items == nullptr)
        {
            return Core::Unexpected(
                std::format("0x550d10: el lugar {} pide Item.bmd (0x40dc50) y no está cargado", slot));
        }
        const auto bonus = ItemStatBonusOf(item, *items);
        if (!bonus.HasValue())
        {
            return Core::Unexpected(std::format("0x550d10, lugar {}: {}", slot, bonus.Error()));
        }
        if (std::uint16_t* word = BonusWord(own, bonus.Value().kind))
        {
            *word = Low16(*word + bonus.Value().value * ItemStatValueFactor);
        }
    }
    return {};
}

Core::Expected<void, std::string> AddCapeOfLordOptions(OwnCharacterRecord& own, std::uint8_t& ownRecordAccesses,
                                                       Render::Models::CrtRandom& random)
{
    const World::ItemState& wings = own.equipment[WingsSlot];
    if (wings.id != CapeOfLordId)
    {
        return {};
    }
    const Render::Models::LogicalAccess access(ownRecordAccesses, random, Render::Models::OwnRecordCloseRandoms);
    if (static_cast<std::size_t>(wings.optionCount) > World::MaxItemOptions)
    {
        return Core::Unexpected(std::format("0x54e540 (0x551051): Cape of Lord con {} opciones lee más allá de sus "
                                            "nueve, en el lugar de la mascota, que MuRet no proyecta así",
                                            wings.optionCount));
    }
    std::array<std::byte, 2> word36 = LittleEndianWord(own.word36);
    AddOwnItemOptionValues(word36, Word36OptionSelector,
                           {wings.id, wings.durability, wings.optionCount, wings.optionBytes});
    own.word36 = FromLittleEndian(word36);
    return {};
}

Core::Expected<void, std::string> UpdateOwnDerivedStatsTail(OwnCharacterRecord& own, Render::Models::CrtRandom& random)
{
    own.wordef8 = Low16(own.wordef0 - own.wordee6);
    own.wordefa = Low16(own.wordef2 - own.wordee6);
    // 0x5512eb..0x551306: los dos WORD sin signo, el rand() y el IDIV de 32 bits con signo.
    const int minimum = own.wordee2;
    const int draw = random.Next();
    const int divisor = own.wordee4 - minimum + 1;
    if (divisor == 0)
    {
        return Core::Unexpected(std::format("0x550d10: el IDIV de 0x551306 divide por 0 con +0xee2 = {} y +0xee4 = {} "
                                            "(Q-C185)",
                                            own.wordee2, own.wordee4));
    }
    own.wordefc = Low16(draw % divisor + minimum - own.word62);
    own.wordefe = Low16(own.word4e - own.wordeec);
    own.wordf00 = Low16(own.word60 - own.wordeea);
    // cmp y jbe: los topes, sin signo (0x551337 y 0x55134a).
    if (own.wordefe > TailCap)
    {
        own.wordefe = TailCap;
    }
    if (own.wordf00 > TailCap)
    {
        own.wordf00 = TailCap;
    }
    // jge con signo contra el WORD sin signo (0x55136d y 0x551392).
    own.bytef02 = static_cast<std::uint8_t>(random.Next() % TailPercent < own.wordefe);
    own.bytef03 = static_cast<std::uint8_t>(random.Next() % TailPercent < own.wordf00);
    return {};
}
} // namespace Client::Scenes
