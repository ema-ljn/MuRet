#include "Scenes/ItemNames.h"

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Scenes/HudItems.h"
#include "Scenes/NpcNames.h"
#include "UI/GameText.h"
#include "Crt/SprintfFormat.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
// Cómo escribe un caso de 0x5b1c70: el nombre del registro ("%s"), un texto de Text.bmd como "%s", un texto con el
// nombre detrás ("%s %s"), un texto como formato sin argumentos (sprintf(buffer, texto)) o nada.
enum class NameKind : std::uint8_t
{
    None,
    Record,
    Text,
    TextAndRecord,
    TextAsFormat,
};

struct NameCase
{
    NameKind kind = NameKind::None;
    std::size_t text = 0;
};

constexpr NameCase Nothing{};
constexpr NameCase Record{.kind = NameKind::Record};
constexpr NameCase Text(std::size_t text)
{
    return NameCase{.kind = NameKind::Text, .text = text};
}
constexpr NameCase TextAndRecord(std::size_t text)
{
    return NameCase{.kind = NameKind::TextAndRecord, .text = text};
}
constexpr NameCase TextAsFormat(std::size_t text)
{
    return NameCase{.kind = NameKind::TextAsFormat, .text = text};
}

// Los tipos con un nombre por nivel, del nivel 0 en adelante; más allá, nada.
struct LeveledName
{
    std::uint16_t type = 0;
    std::array<NameCase, 5> levels{};
};

constexpr std::array<LeveledName, 9> LeveledNames{{
    // 0x5b1ca1..0x5b1d24.
    {.type = 0x1c17, .levels = {Record, Text(906), Nothing, Nothing, Nothing}},
    {.type = 0x1c18, .levels = {Record, Text(907), Nothing, Nothing, Nothing}},
    // 0x5b1ebf.. (0x5b22e8): las cajas de la fruta de stats.
    {.type = 0x1a0f,
     .levels = {TextAndRecord(168), TextAndRecord(169), TextAndRecord(167), TextAndRecord(166), TextAndRecord(1900)}},
    // 0x5b1f5a.
    {.type = 0x1a0e, .levels = {Record, Text(1235), Nothing, Nothing, Nothing}},
    // 0x5b1f9c.
    {.type = 0x1a1f, .levels = {TextAndRecord(1187), TextAndRecord(1214), Nothing, Nothing, Nothing}},
    // 0x5b1fe4.. (0x5b22fc).
    {.type = 0x1c15, .levels = {Record, Text(810), Text(1098), Text(1290), Nothing}},
    // 0x5b2079.. (0x5b230c).
    {.type = 0x1a14, .levels = {Record, Text(922), Text(928), Text(929), Nothing}},
    // 0x5b20ef.
    {.type = 0x1c09, .levels = {Record, Text(108), Nothing, Nothing, Nothing}},
    // 0x5b1d39..0x5b1da7: el nivel 2 corta (abajo).
    {.type = 0x1c0c, .levels = {Text(100), Text(101), Nothing, Nothing, Nothing}},
}};

// 0x1c0b, la caja de la suerte, del nivel 0 al 15 (0x5b1da8.., la tabla 0x5b22a8): del 8 al 12, el 115 con nivel − 7.
constexpr std::array<NameCase, 16> BoxOfLuckNames{
    Record,  Text(105), Text(106), Text(107), Nothing, Text(109),         Text(110),          Text(111),
    Nothing, Nothing,   Nothing,   Nothing,   Nothing, TextAsFormat(117), TextAsFormat(1650), TextAsFormat(1651)};
constexpr std::int32_t FirstPlusBox = 8;
constexpr std::int32_t LastPlusBox = 12;
constexpr std::int32_t PlusBoxBase = 7;
constexpr std::size_t PlusBoxText = 115;

// 0x5b2131: el 0x180b, "%s %s" con los textos 738 y 102.
constexpr std::uint16_t Type180b = 0x180b;
constexpr std::size_t Type180bFirst = 738;
constexpr std::size_t Type180bSecond = 102;

std::optional<NameCase> FixedCase(std::uint16_t type, std::int32_t level)
{
    for (const LeveledName& named : LeveledNames)
    {
        if (named.type != type)
            continue;
        if (level < 0 || static_cast<std::size_t>(level) >= named.levels.size())
            return Nothing;
        return named.levels[static_cast<std::size_t>(level)];
    }
    // 0x5b1c96..0x5b1d38 y 0x5b205a, 0x5b2155..0x5b21a5: solo el nombre, con cualquier nivel.
    if (type == 0x1c19 || type == 0x1c1a || type == 0x1c20 || type == 0x1c21 || type == 0x1c22)
        return Record;
    if (type == 0x1a13)
        return Text(809);
    if (type == 0x1c0b)
    {
        if (level < 0 || level > 15)
            return Nothing;
        return BoxOfLuckNames[static_cast<std::size_t>(level)];
    }
    return std::nullopt;
}

Core::Expected<std::string_view, std::string> TextOf(const Tables::TextTable* texts, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x5b1c70: falta el texto {} de Text.bmd", index));
    return *text;
}
// 0x6c9338..0x6c9377 del c32bbd5f: del 0x18 en adelante, los bytes de 0x6c9350.
constexpr std::array<std::uint32_t, 16> TransformationMonsters{
    0x2, 0x7, 0xe, 0x8, 0x9, 0x29, 0xf9b5f7c8, 0xb020a9c5, 0x20e2c7ed, 0xe0c7a9bf, 0xc7b1, 0x0, 0x0, 0x0, 0x0, 0x0};
constexpr std::uint16_t TransformationRing = 0x1a0a;
constexpr std::size_t TransformationRingText = 103;

// 0x6c9350..0x6c9737 del c32bbd5f: los diez premios del boleto, de a 100 bytes, cada uno hasta su '\0' (EUC-KR). Del 0
// al 9: 히딩크 고향 여행권, 펜티엄4 컴퓨터, 디지탈카메라, 로지텍 무선 마우스+키보드 세트, 256M 램, 6개월 잡지 구독권,
// 문화상품권(만원), 뮤 머그컵, 뮤 T셔츠 y 뮤 10시간 무료이용권. Ningún código los escribe: solo los leen 0x5b1d60 y
// 0x5b3835.
constexpr std::array<std::string_view, 10> EventTicketPrizes{
    "\xc8\xf7\xb5\xf9\xc5\xa9 \xb0\xed\xc7\xe2 \xbf\xa9\xc7\xe0\xb1\xc7",
    "\xc6\xe6\xc6\xbc\xbe\xf6"
    "4 \xc4\xc4\xc7\xbb\xc5\xcd",
    "\xb5\xf0\xc1\xf6\xc5\xbb\xc4\xab\xb8\xde\xb6\xf3",
    "\xb7\xce\xc1\xf6\xc5\xd8 \xb9\xab\xbc\xb1 \xb8\xb6\xbf\xec\xbd\xba+\xc5\xb0\xba\xb8\xb5\xe5 \xbc\xbc\xc6\xae",
    "256M \xb7\xa5",
    "6\xb0\xb3\xbf\xf9 \xc0\xe2\xc1\xf6 \xb1\xb8\xb5\xb6\xb1\xc7",
    "\xb9\xae\xc8\xad\xbb\xf3\xc7\xb0\xb1\xc7(\xb8\xb8\xbf\xf8)",
    "\xb9\xc2 \xb8\xd3\xb1\xd7\xc4\xc5",
    "\xb9\xc2 T\xbc\xc5\xc3\xf7",
    "\xb9\xc2 10\xbd\xc3\xb0\xa3 \xb9\xab\xb7\xe1\xc0\xcc\xbf\xeb\xb1\xc7",
};
constexpr std::uint16_t EventTicket = 0x1c0c;
constexpr std::int32_t EventTicketTableLevel = 2;
// Item.bmd+0x2d: el byte que 0x5b1d54 lee del registro.
constexpr std::size_t EventTicketPrizeOffset = 0x2d;

// 0x5b21b6..0x5b21e9: los registros de 0x7681eb8 con el tipo del monstruo, sin cortar en el primero.
bool HasTheMonster(const NpcNameTable& table, std::uint32_t monster)
{
    return std::ranges::any_of(table, [monster](const NpcName& entry) { return std::uint32_t{entry.type} == monster; });
}

Core::Expected<std::string, std::string> TransformationRingName(const Tables::TextTable* texts,
                                                                const NpcNameTable* npcNames, std::int32_t level)
{
    if (npcNames == nullptr)
        return Core::Unexpected(std::string("0x5b21a6: el nombre del anillo de transformación lee los monstruos de "
                                            "0x7681eb8 y no están"));
    const std::optional<std::uint32_t> monster = TransformationMonsterOf(level);
    if (!monster)
        return Core::Unexpected(std::format("0x5b21bb: el nivel {} del anillo de transformación lee 0x6c9338 fuera "
                                            "de lo que MuRet tiene de la .data",
                                            level));
    // Sin ninguno, el buffer queda sin escribir.
    std::string name;
    for (std::size_t index = 0; index < npcNames->size(); ++index)
    {
        // 0x5b21c2..0x5b21ca: el word del tipo, sin signo, contra el dword del nivel, que se vuelve a leer.
        if (std::uint32_t{(*npcNames)[index].type} != *monster)
            continue;
        // 0x5b21cc..0x5b21d8: sprintf(buffer, "%s %s" (0x6c9998), +2 del registro, el 103 (0x76c4a90)).
        const auto text = TextOf(texts, TransformationRingText);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        name = Crt::Sprintf("%s %s", std::string_view(NpcNameAt(*npcNames, index)), text.Value());
    }
    return name;
}
} // namespace

std::optional<std::string_view> EventTicketPrizeOf(std::uint32_t index)
{
    if (index >= EventTicketPrizes.size())
        return std::nullopt;
    return EventTicketPrizes[index];
}

std::optional<std::uint32_t> TransformationMonsterOf(std::int32_t level)
{
    if (level < 0 || static_cast<std::size_t>(level) >= TransformationMonsters.size())
        return std::nullopt;
    return TransformationMonsters[static_cast<std::size_t>(level)];
}

Core::Expected<std::string, std::string> ItemNameOf(const Tables::ItemTable* items, const Tables::TextTable* texts,
                                                    const NpcNameTable* npcNames, std::uint16_t type,
                                                    std::int32_t level)
{
    if (type == TransformationRing)
        return TransformationRingName(texts, npcNames, level);
    // 0x5b1c76..0x5b1c8d: el registro de Item.bmd (ECX), que leen los casos con el nombre y el boleto.
    const auto recordOf = [&]() -> Core::Expected<std::span<const std::byte, Tables::ItemRecordSize>, std::string>
    {
        if (items == nullptr)
            return Core::Unexpected(std::string("0x5b1c76: lee *[0x7758550] (Item.bmd) y no está cargado"));
        const auto record = items->Record(type);
        if (!record)
            return Core::Unexpected(std::format("0x5b1c76: el tipo {:#x} lee fuera de Item.bmd", type));
        return *record;
    };
    const auto recordName = [&]() -> Core::Expected<std::string, std::string>
    {
        const auto record = recordOf();
        if (!record.HasValue())
            return Core::Unexpected(record.Error());
        return ItemRecordName(record.Value());
    };
    // 0x5b1d52..0x5b1d6e: el boleto de nivel 2, sprintf(buffer, "%s" (0x6c98f0), 0x6c9350 + byte [ECX + 0x2d] · 100).
    if (type == EventTicket && level == EventTicketTableLevel)
    {
        const auto record = recordOf();
        if (!record.HasValue())
            return Core::Unexpected(record.Error());
        const auto index = std::to_integer<std::uint32_t>(record.Value()[EventTicketPrizeOffset]);
        const std::optional<std::string_view> prize = EventTicketPrizeOf(index);
        if (!prize)
            return Core::Unexpected(std::format("0x5b1d60: el premio {} del boleto (Item.bmd+0x2d) lee 0x6c9350 + {} "
                                                "· 100, en la .data que sigue a los diez premios, que MuRet no tiene",
                                                index, index));
        return Crt::Sprintf("%s", *prize);
    }
    if (type == Type180b)
    {
        const auto first = TextOf(texts, Type180bFirst);
        const auto second = TextOf(texts, Type180bSecond);
        if (!first.HasValue())
            return Core::Unexpected(first.Error());
        if (!second.HasValue())
            return Core::Unexpected(second.Error());
        return Crt::Sprintf("%s %s", first.Value(), second.Value());
    }
    if (type == 0x1c0b && level >= FirstPlusBox && level <= LastPlusBox)
    {
        const auto text = TextOf(texts, PlusBoxText);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        return Crt::Sprintf("%s +%d", text.Value(), level - PlusBoxBase);
    }
    if (const std::optional<NameCase> fixed = FixedCase(type, level))
    {
        switch (fixed->kind)
        {
        case NameKind::None:
            return std::string{};
        case NameKind::Record:
            return recordName();
        case NameKind::Text:
        case NameKind::TextAsFormat:
        {
            const auto text = TextOf(texts, fixed->text);
            if (!text.HasValue())
                return Core::Unexpected(text.Error());
            return fixed->kind == NameKind::Text ? std::string(text.Value()) : Crt::Sprintf(text.Value());
        }
        case NameKind::TextAndRecord:
        {
            const auto text = TextOf(texts, fixed->text);
            if (!text.HasValue())
                return Core::Unexpected(text.Error());
            const auto name = recordName();
            if (!name.HasValue())
                return name;
            return Crt::Sprintf("%s %s", text.Value(), std::string_view(name.Value()));
        }
        }
    }
    // 0x5b21f0..0x5b22a3: los demás (también 0x1803..0x1806 y 0x13, 0x812, 0xa0a y 0x40d, que hacen lo mismo): el
    // nombre y, con nivel, " +nivel".
    auto name = recordName();
    if (!name.HasValue() || level == 0)
        return name;
    return Crt::Sprintf("%s +%d", std::string_view(name.Value()), level);
}

bool ItemNameIsWritten(std::uint16_t type, std::int32_t level, const NpcNameTable* npcNames)
{
    if (type == TransformationRing && npcNames != nullptr)
    {
        const std::optional<std::uint32_t> monster = TransformationMonsterOf(level);
        return !monster || HasTheMonster(*npcNames, *monster);
    }
    // El 0x180b, las cajas de 0x1c0b del 8 al 12 y el 0x1c0c de nivel 2 (0x5b1d52) escriben antes de la tabla.
    if (type == Type180b || (type == 0x1c0b && level >= FirstPlusBox && level <= LastPlusBox) ||
        (type == 0x1c0c && level == 2))
        return true;
    const std::optional<NameCase> fixed = FixedCase(type, level);
    return !fixed || fixed->kind != NameKind::None;
}
} // namespace Client::Scenes
