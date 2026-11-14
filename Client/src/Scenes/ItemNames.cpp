#include "Scenes/ItemNames.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/HudItems.h"
#include "UI/GameText.h"
#include "UI/SprintfFormat.h"

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

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x5b1c70: falta el texto {} de Text.bmd", index));
    return *text;
}
} // namespace

Core::Expected<std::string, std::string> ItemNameOf(const Data::Tables::ItemTable* items,
                                                    const Data::Text::TextTable* texts, std::uint16_t type,
                                                    std::int32_t level)
{
    if (type == 0x1c0c && level == 2)
        return Core::Unexpected(std::string("0x5b1d52: el nombre del 0x1c0c de nivel 2 lee la tabla de .data de "
                                            "0x6c9350 por +0x2d del registro, como el cuerpo del ítem (0x5b3824, "
                                            "08 P-C125 b)"));
    if (type == 0x1a0a)
        return Core::Unexpected(std::string("0x5b21a6: el nombre del anillo de transformación lee los monstruos de "
                                            "0x7681eb8, que MuRet no carga (CL9 hito 2)"));
    // 0x5b1c76..0x5b1c8d: el registro de Item.bmd, que todos los casos con el nombre leen.
    const auto recordName = [&]() -> Core::Expected<std::string, std::string>
    {
        if (items == nullptr)
            return Core::Unexpected(std::string("0x5b1c76: lee *[0x7758550] (Item.bmd) y no está cargado"));
        const auto record = items->Record(type);
        if (!record)
            return Core::Unexpected(std::format("0x5b1c76: el tipo {:#x} lee fuera de Item.bmd", type));
        return ItemRecordName(*record);
    };
    if (type == Type180b)
    {
        const auto first = TextOf(texts, Type180bFirst);
        const auto second = TextOf(texts, Type180bSecond);
        if (!first.HasValue())
            return Core::Unexpected(first.Error());
        if (!second.HasValue())
            return Core::Unexpected(second.Error());
        return UI::Sprintf("%s %s", first.Value(), second.Value());
    }
    if (type == 0x1c0b && level >= FirstPlusBox && level <= LastPlusBox)
    {
        const auto text = TextOf(texts, PlusBoxText);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        return UI::Sprintf("%s +%d", text.Value(), level - PlusBoxBase);
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
            return fixed->kind == NameKind::Text ? std::string(text.Value()) : UI::Sprintf(text.Value());
        }
        case NameKind::TextAndRecord:
        {
            const auto text = TextOf(texts, fixed->text);
            if (!text.HasValue())
                return Core::Unexpected(text.Error());
            const auto name = recordName();
            if (!name.HasValue())
                return name;
            return UI::Sprintf("%s %s", text.Value(), std::string_view(name.Value()));
        }
        }
    }
    // 0x5b21f0..0x5b22a3: los demás (también 0x1803..0x1806 y 0x13, 0x812, 0xa0a y 0x40d, que hacen lo mismo): el
    // nombre y, con nivel, " +nivel".
    auto name = recordName();
    if (!name.HasValue() || level == 0)
        return name;
    return UI::Sprintf("%s +%d", std::string_view(name.Value()), level);
}
} // namespace Client::Scenes
