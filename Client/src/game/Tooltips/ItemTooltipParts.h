#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Kernel/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The parts of an item tooltip's body, 0x5b2aa0 (06 § Los tooltips, "El cuerpo del ítem"), in the order it writes
// them: the name, each type's lines, the damage and the stats, the durability, the requirements, the classes, the
// options and the set. Each one writes into TooltipWriter and returns the error of what MuRet does not port.
namespace Client::Tooltips
{
class TooltipWriter;

// What the parts share: the item ([EBP+0x10]), its Item.bmd record ([EBP−0x18]), its level ((D+4 >> 3) & 0xf,
// [EBP−0x14]) and the name's color ([EBP−0x1c]).
struct ItemTooltipFrame
{
    Ui::GameHudContext& context;
    TooltipWriter& lines;
    const Kernel::ItemState& item;
    std::span<const std::byte, Tables::ItemRecordSize> record;
    std::int32_t level = 0;
    std::int32_t nameColor = 0;
};

[[nodiscard]] std::uint8_t RecordByte(const ItemTooltipFrame& frame, std::size_t offset);
// D+0x1b & 0x3f
[[nodiscard]] bool IsExcellent(const Kernel::ItemState& item);
// The hero's class, byte +0x1d4 of [0x743a170] (the class in bits 0 to 2 and the evolution above).
[[nodiscard]] std::uint8_t HeroClassOf(const Ui::GameHudContext& context);

// What is read from a Skill.bmd entry with its access open: the name (+0, up to its '\0' or the end of the entry),
// byte +0x21, word +0x22 (the mana), word +0x24 (text 360, "Stamina usada") and byte +0x26 (text 174, "Range"), what
// 0x551c70 leaves in its pointers.
struct SkillEntryFields
{
    std::string name;
    std::uint8_t byte21 = 0;
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
    std::uint8_t range = 0;
};

// The access of Skill.bmd's entry `skill` (0x551c70 or the same code inline): it opens, its fields are read and it
// closes, with its 60 rand() if its count returns to 0.
[[nodiscard]] Core::Expected<SkillEntryFields, std::string>
ReadTheSkillEntry(Ui::GameHudContext& context, std::uint32_t skill, std::string_view where);
// The same from a part: with an error already in the writer, that error, without opening the access.
[[nodiscard]] Core::Expected<SkillEntryFields, std::string>
ReadTheSkillEntry(ItemTooltipFrame& frame, std::uint32_t skill, std::string_view where);

// 0x40cf30(buffer, type, D+0x1c), a thiscall on [0x6eed3c]: with D+0x1c & 3 from 1 to 3, the set of byte (& 3) − 1 of
// ItemSetType.bmd; if it is not 0 or 0xff, the 32 bytes of the name of its ItemSetOption.bmd record and a space.
// Without a set, nothing. A set outside the 64 or a name without '\0' reads outside: error.
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string> ItemSetNameOf(const Ui::GameHudContext& context,
                                                                                    const Kernel::ItemState& item);
// The same with the type and the piece (& 3) given, as the item help calls it with 1 and 2 (0x5b0ccd and 0x5b0d45).
[[nodiscard]] Core::Expected<std::optional<std::string>, std::string>
ItemSetNameOf(const Ui::GameHudContext& context, std::uint16_t type, std::uint8_t piece);

// 0x5b371b..0x5b45f4: the name line, in the name's color and in bold, and a "\n".
[[nodiscard]] Core::Expected<void, std::string> WriteTheItemName(ItemTooltipFrame& frame);

// 0x5b45f5..0x5b4f92: the lines of 0x1a13, of the quests (0x1c17 to 0x1c1a), of 0x1c0c, of 0x1a14 and of 0x1c13.
[[nodiscard]] Core::Expected<void, std::string> WriteTheKindLines(ItemTooltipFrame& frame);

// 0x5b4f93..0x5b58e9: the damage, the defense, the speeds, the defense rate and what goes with 0x1c20 to 0x1c22 and
// 0x1c0b.
[[nodiscard]] Core::Expected<void, std::string> WriteTheStats(ItemTooltipFrame& frame);

// 0x5b58ea..0x5b6a3b: the jewel bundles, each type's lines and the wings.
[[nodiscard]] Core::Expected<void, std::string> WriteTheNotes(ItemTooltipFrame& frame);

// 0x5b6a3c..0x5b79bc: the durability, the arrows' damage, the resistances and the requirements.
[[nodiscard]] Core::Expected<void, std::string> WriteTheRequirements(ItemTooltipFrame& frame);

// 0x5af590(record): the classes that use the item of `record` (Item.bmd+0x45..+0x49, the evolution + 1 of each), from
// the next line of `lines`. It sets the colors of N + 2 and N + 3 to 0 and, with some class, writes a "\n" (with its
// half) and: MG (the hero's class 3), 61 or 60 with 23 depending on its byte +0x48 and its evolution; with one or two
// classes, 61 with each one's name (15 + 5 · byte + class); with three or four, 60 with the name of each class that
// does not use it (20 + class); with five, nothing more. What the hero does not meet goes in red if their class uses it
// and in 5 if not. Called by the item body (0x5b79d2) and the item help (0x5b0da8).
void WriteTheItemClasses(const Ui::GameHudContext& context, TooltipWriter& lines,
                         std::span<const std::byte, Tables::ItemRecordSize> record);

// 0x5b2f7f..0x5b3716 (CL9 milestone 7), with the own personal shop or another's open: with its grid's item under the
// mouse (0x78abaa4), its price, from the mode's map (0x6c931c) by the place (column + row · 8 + 0x4c). With a price:
// 63 with the price unsigned and with commas (0x5b1130), in red from ten million, gold from one million and green from
// a hundred thousand (signed), in bold, and a "\n"; then the own record's zen (+0xecc) with its access opened and
// closed and, viewing another's with less zen (unsigned), 423 in red and bold and another "\n". Without a price, with
// the own one, 1101 in red and bold and a "\n". No rand() except the access's.
[[nodiscard]] Core::Expected<void, std::string>
WriteThePersonalShopPrice(Ui::GameHudContext& context, TooltipWriter& lines, const Kernel::ItemState& item);

// 0x5b2320(type, line, option, value, mana): the text of option `tag` on the next line of `lines`, without its color
// or its bold. The ones that read the mana open the entry of the option's skill (0x551c70, which overwrites the
// argument's mana: 60 rand() if its count returns to 0). An option not in the table of 0x5b2a00 leaves the line as it
// was. Called by the item's options (0x5b7c62), the blue box (0x5b7e49) and the pets' tooltip (0x423b1d).
[[nodiscard]] Core::Expected<void, std::string> WriteAnOption(Ui::GameHudContext& context, TooltipWriter& lines,
                                                              std::int32_t type, std::uint8_t tag, std::uint8_t value);

// 0x5b79bd..0x5b801a: the classes (0x5af590), what each type adds, the set's stat (0x40dd80), the options (0x5b2320),
// the boxes 0x1a20 to 0x1a25 and the set's options (0x40eee0).
[[nodiscard]] Core::Expected<void, std::string> WriteTheOptions(ItemTooltipFrame& frame);
} // namespace Client::Tooltips
