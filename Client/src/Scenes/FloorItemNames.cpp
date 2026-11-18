#include "Scenes/FloorItemNames.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameHud.h"
#include "Scenes/HudItems.h"
#include "Scenes/HudText.h"
#include "Scenes/ITextDrawer.h" // IWYU pragma: keep (ITextDrawer como ITextMeasure)
#include "Scenes/ItemTooltipParts.h"
#include "Scenes/WorldRuntime.h"
#include "UI/GameText.h"
#include "UI/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using Render::Color;
using Render::Text::FontKind;
using Render::Text::TextColors;

// Los glColor3f de 0x5ba790: el dorado (1; 0,8; 0,1) de casi todos los casos, el violeta de las armas del arcángel
// (0x5bb51f), el de los excelentes (0x5bb489), los grises del nivel 0 y del 1 y 2, el naranja del 3 y 4, el celeste del
// 5 y 6 o con opciones (0x5bb50e) y el blanco del conjunto (0x5bb55e).
constexpr Color Gold{1.0f, 0.8f, 0.1f, 1.0f};
constexpr Color Archangel{1.0f, 0.1f, 1.0f, 1.0f};
constexpr Color Excellent{0.1f, 1.0f, 0.5f, 1.0f};
constexpr Color LevelZero{0.7f, 0.7f, 0.7f, 1.0f};
constexpr Color LowLevel{0.9f, 0.9f, 0.9f, 1.0f};
constexpr Color MiddleLevel{1.0f, 0.5f, 0.2f, 1.0f};
constexpr Color HighLevel{0.4f, 0.7f, 1.0f, 1.0f};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// 0x5ba7bd..0x5ba7c7, 0x5bb585..0x5bb58f y 0x5bb89c..0x5bb8a6: 0x6c8e5c (el texto) y 0x6c8e64 (el fondo).
constexpr TextColors StartColors{.text = 0xffffffffu, .background = 0xff000000u};
constexpr TextColors SetColors{.text = 0xff00ff00u, .background = 0xffc83c3cu};
constexpr TextColors EndColors{.text = 0xffc8e6ffu, .background = 0xff000000u};
// El modelo de un tipo (0x212, el de 0x608350) y la y del texto, 15 más arriba (0x5bb850 y 0x5bb873).
constexpr std::int32_t ModelBase = 0x212;
constexpr std::int32_t NameRise = 15;
constexpr std::uint32_t DesignWidth = 640;
// +8 de la entrada: el nivel (>> 3, & 0xf), la skill (0x80), la suerte (4) y la opción (3). +0x1f: las opciones
// excelentes (0x3f) y una más (0x40).
constexpr std::int32_t LevelShift = 3;
constexpr std::int32_t LevelMask = 0xf;
constexpr std::int32_t SkillBit = 0x80;
constexpr std::int32_t LuckBit = 4;
constexpr std::int32_t OptionBits = 3;
constexpr std::int32_t PlusLevelBits = 0x78;
constexpr std::int32_t OptionColorBits = 0x87;
constexpr std::uint8_t ExcellentBits = 0x3f;
constexpr std::uint8_t ExcellentOptionBit = 0x40;
// 0x5bb4bb, 0x5bb4e7 y 0x5bb4fd: los cortes del color por el nivel.
constexpr std::int32_t GoldLevel = 7;
constexpr std::int32_t MiddleFirstLevel = 3;
constexpr std::int32_t HighFirstLevel = 5;
// 0x5bae3e..0x5bae4f: la skill del 0x1a1d, nivel + 30 (0x76bd1d8 + 0x3c · (nivel + 30)), y el texto 102.
constexpr std::uint32_t FirstScrollSkill = 30;
// 0x5bb5e7.., 0x5bb66f y 0x5bb696: los textos de la skill (176, y " +" y 179 con el 0x1c15), de la opción (177) y de
// la suerte (178).
constexpr std::size_t SkillText = 176;
constexpr std::size_t DinorantSkillText = 179;
constexpr std::size_t OptionText = 177;
constexpr std::size_t LuckText = 178;
constexpr std::int16_t DinorantModel = 0x1c15;

// Lo que deja cada caso: el texto, o un corte.
using Name = Core::Expected<std::string, std::string>;

struct NameFrame
{
    GameHudContext& context;
    const FloorItemState& item;
    std::int32_t level = 0;
};

Core::Expected<std::string_view, std::string> TextOf(const NameFrame& frame, std::size_t index)
{
    const std::optional<std::string_view> text = UI::TextAt(frame.context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x5ba790: falta el texto {} de Text.bmd", index));
    return *text;
}

// El registro de Item.bmd del tipo (0x7758550 + 0x54 · tipo), hasta su '\0'.
Name RecordNameOf(const NameFrame& frame, std::int32_t type)
{
    if (frame.context.items == nullptr)
        return Core::Unexpected(std::string("0x5ba790: lee *[0x7758550] (Item.bmd) y no está cargado"));
    if (type < 0)
        return Core::Unexpected(std::format("0x5ba790: el modelo {:#x} lee antes de Item.bmd", type + ModelBase));
    const auto record = frame.context.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("0x5ba790: el tipo {:#x} lee fuera de Item.bmd", type));
    return ItemRecordName(*record);
}

Name ModelName(const NameFrame& frame)
{
    return RecordNameOf(frame, frame.item.type - ModelBase);
}

// sprintf(buffer, texto): el texto como formato, sin argumentos.
Name TextAsFormat(const NameFrame& frame, std::size_t index)
{
    const auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return UI::Sprintf(text.Value());
}

// sprintf(buffer, "%s %s", texto, nombre del modelo).
Name TextAndModelName(const NameFrame& frame, std::size_t index)
{
    const auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    const auto name = ModelName(frame);
    if (!name.HasValue())
        return name;
    return UI::Sprintf("%s %s", text.Value(), std::string_view(name.Value()));
}

// Los casos que con otro nivel no escriben el buffer ([ebp-0x58]): el original dibuja lo que quedó en su pila.
Name Unwritten(const NameFrame& frame, std::uint32_t address)
{
    return Core::Unexpected(
        std::format("{:#x}: 0x5ba790 no escribe el nombre del modelo {:#x} con el nivel {} y dibuja "
                    "lo que quedó en su pila (07 Q-C1242)",
                    address, frame.item.type, frame.level));
}

void Bold(const NameFrame& frame)
{
    frame.context.hud.dcFont = FontKind::Bold;
}

void Paint(const NameFrame& frame, Color color)
{
    frame.context.sprites.SetColor(color);
}

// 0x5bb449..0x5bb52e: el color de los demás.
Color GenericColor(const NameFrame& frame)
{
    const std::int16_t model = frame.item.type;
    if (model == 0xc1c || model == 0x225 || model == 0xa24 || model == 0x61f)
        return Archangel;
    if ((frame.item.excellent & ExcellentBits) != 0 && (model < 0x1a15 || model > 0x1a18) && model != 0x1c30)
        return Excellent;
    if (model == 0x1a2b || model == 0x1e2e || model == 0x1e2f || frame.level >= GoldLevel)
        return Gold;
    if ((frame.item.value & OptionColorBits) != 0)
        return HighLevel;
    if (frame.level == 0)
        return LevelZero;
    if (frame.level < MiddleFirstLevel)
        return LowLevel;
    return frame.level < HighFirstLevel ? MiddleLevel : HighLevel;
}

// 0x5bb52e..0x5bb72e: el color, el conjunto (0x40cf30), el nombre con " +nivel" y los textos de la skill, la opción y
// la suerte.
Name GenericName(const NameFrame& frame)
{
    Paint(frame, GenericColor(frame));
    const auto set =
        ItemSetNameOf(frame.context, static_cast<std::uint16_t>(frame.item.type - ModelBase), frame.item.ancient);
    if (!set.HasValue())
        return Core::Unexpected(set.Error());
    const auto record = ModelName(frame);
    if (!record.HasValue())
        return record;
    std::string name = record.Value();
    if (set.Value())
    {
        // 0x5bb55e..0x5bb5dc: blanco, negrita, 0xff00ff00 sobre 0xffc83c3c, y el nombre detrás del conjunto.
        Paint(frame, White);
        Bold(frame);
        frame.context.hud.textColors = SetColors;
        name = *set.Value() + name;
    }
    std::string text = frame.level == 0 ? UI::Sprintf("%s", std::string_view(name))
                                        : UI::Sprintf("%s +%d", std::string_view(name), frame.level);
    const auto append = [&](std::size_t index) -> Core::Expected<void, std::string>
    {
        const auto added = TextOf(frame, index);
        if (!added.HasValue())
            return Core::Unexpected(added.Error());
        text += added.Value();
        return {};
    };
    if ((frame.item.value & SkillBit) != 0)
    {
        const bool dinorant = frame.item.type == DinorantModel;
        if (dinorant)
            text += " +";
        if (auto added = append(dinorant ? DinorantSkillText : SkillText); !added.HasValue())
            return Core::Unexpected(added.Error());
    }
    if ((frame.item.value & OptionBits) != 0 || (frame.item.excellent & ExcellentOptionBit) != 0)
    {
        if (auto added = append(OptionText); !added.HasValue())
            return Core::Unexpected(added.Error());
    }
    if ((frame.item.value & LuckBit) != 0)
    {
        if (auto added = append(LuckText); !added.HasValue())
            return Core::Unexpected(added.Error());
    }
    return text;
}

// 0x5bae2e..0x5bb2e2: el orbe de invocación (el tipo 0x180b), con el acceso de su skill de Skill.bmd abierto mientras
// escribe.
Name ScrollName(const NameFrame& frame)
{
    const auto text = TextOf(frame, 102);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    const auto skill =
        ReadTheSkillEntry(frame.context, FirstScrollSkill + static_cast<std::uint32_t>(frame.level), "0x5bae38");
    if (!skill.HasValue())
        return Core::Unexpected(skill.Error());
    return UI::Sprintf("%s %s", std::string_view(skill.Value().name), text.Value());
}

// Un texto elegido por el nivel, del 0 en adelante; con otro, el buffer queda sin escribir.
template <std::size_t Count>
Name ByLevel(const NameFrame& frame, const std::array<std::size_t, Count>& texts, std::uint32_t unwritten)
{
    if (frame.level < 0 || static_cast<std::size_t>(frame.level) >= Count)
        return Unwritten(frame, unwritten);
    return TextAsFormat(frame, texts[static_cast<std::size_t>(frame.level)]);
}

// 0x5ba7d5..0x5bb811: el texto y su color según el modelo.
Name ComposeTheName(const NameFrame& frame)
{
    const std::int16_t model = frame.item.type;
    const std::int32_t level = frame.level;
    switch (model)
    {
    case 0x1e21:
    {
        // 0x5ba7db: el zen, "%s %d" con el valor entero (+8).
        Paint(frame, Gold);
        const auto name = ModelName(frame);
        if (!name.HasValue())
            return name;
        return UI::Sprintf("%s %d", std::string_view(name.Value()), frame.item.value);
    }
    case 0x1e1f:
    case 0x1e20:
    case 0x1e22:
    case 0x1a21:
    case 0x1e28:
    case 0x1e31:
    case 0x1c20:
    {
        // 0x5bb7bc: las joyas, en negrita.
        Bold(frame);
        Paint(frame, Gold);
        const auto name = ModelName(frame);
        if (!name.HasValue())
            return name;
        return UI::Sprintf("%s", std::string_view(name.Value()));
    }
    case 0x1a30:
    case 0x1a31:
    {
        // 0x5ba875 y 0x5ba8b9: en negrita, con el nombre de 0x1c0d (0x7758550 + 0x93444) o de 0x1c0e (+ 0x93498).
        Bold(frame);
        Paint(frame, Gold);
        const auto name = RecordNameOf(frame, model == 0x1a30 ? 0x1c0d : 0x1c0e);
        if (!name.HasValue())
            return name;
        return UI::Sprintf("%s", std::string_view(name.Value()));
    }
    case 0x1e23:
    case 0x1e24:
    case 0x1e25:
    {
        // 0x5bb733: en negrita; con nivel (+8 & 0x78), " +nivel".
        Bold(frame);
        Paint(frame, Gold);
        const auto name = ModelName(frame);
        if (!name.HasValue())
            return name;
        if ((frame.item.value & PlusLevelBits) != 0)
            return UI::Sprintf("%s +%d", std::string_view(name.Value()), level);
        return UI::Sprintf("%s", std::string_view(name.Value()));
    }
    case 0x1e1e:
        // 0x5ba94c: sin glColor3f.
        return ByLevel(frame, std::array<std::size_t, 3>{100, 101, 104}, 0x5ba95b);
    case 0x1c21:
    {
        // 0x5ba9a3, la tabla de 0x5bb8b4: "%s %s" con el texto del nivel y el nombre.
        Paint(frame, Gold);
        constexpr std::array<std::size_t, 5> Fruits{168, 169, 167, 166, 1900};
        if (level < 0 || static_cast<std::size_t>(level) >= Fruits.size())
            return Unwritten(frame, 0x5ba9c5);
        return TextAndModelName(frame, Fruits[static_cast<std::size_t>(level)]);
    }
    case 0x1c31:
    {
        // 0x5baaf2.
        Paint(frame, Gold);
        if (level == 0)
            return TextAndModelName(frame, 1187);
        if (level == 1)
            return TextAndModelName(frame, 1214);
        return Unwritten(frame, 0x5bab17);
    }
    case 0x224a:
        Paint(frame, Gold);
        return TextAsFormat(frame, 1235);
    case 0x223e:
        return TextAsFormat(frame, 105);
    case 0x223f:
        // 0x5babde.
        Paint(frame, Gold);
        if (level == 14)
            return TextAsFormat(frame, 1650);
        return TextAsFormat(frame, level == 15 ? 1651 : 106);
    case 0x2240:
    {
        // 0x5bac43: con el nivel 13, dorado y "%s" con el texto 117 (0x5bb809).
        if (level != 13)
            return TextAsFormat(frame, 107);
        Paint(frame, Gold);
        const auto text = TextOf(frame, 117);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        return UI::Sprintf("%s", text.Value());
    }
    case 0x2241:
        return TextAsFormat(frame, 108);
    case 0x2242:
        return TextAsFormat(frame, 109);
    case 0x2243:
        return TextAsFormat(frame, 110);
    case 0x2244:
    {
        // 0x5bace0: "%s +%d" con el texto 115 y el nivel − 7.
        const auto text = TextOf(frame, 115);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        return UI::Sprintf("%s +%d", text.Value(), level - 7);
    }
    case 0x2245:
        Paint(frame, Gold);
        return TextAsFormat(frame, 810);
    case 0x2246:
        Paint(frame, Gold);
        return TextAsFormat(frame, 906);
    case 0x2247:
        Paint(frame, Gold);
        return TextAsFormat(frame, 907);
    case 0x2248:
        // 0x5bad98.
        Paint(frame, Gold);
        if (level == 2)
            return TextAsFormat(frame, 928);
        return TextAsFormat(frame, level == 3 ? 929 : 922);
    case 0x2249:
        Paint(frame, Gold);
        return TextAsFormat(frame, 925);
    case 0x1a1d:
        return ScrollName(frame);
    case 0x1c1c:
        // 0x5bb2eb..0x5bb30d: busca en los monstruos (0x7681eb8, de 0x36 bytes) el de 0x6c9338[nivel].
        return Core::Unexpected(std::string("0x5bb2f1: el nombre del anillo de transformación en el piso lee los "
                                            "monstruos de 0x7681eb8, que MuRet no carga (CL9 hito 2)"));
    case 0x1e19:
        return ByLevel(frame, std::array<std::size_t, 2>{1413, 1414}, 0x5bb37f);
    case 0x1c19:
        return ByLevel(frame, std::array<std::size_t, 2>{1460, 1461}, 0x5bb3bd);
    case 0x1c1d:
        return ByLevel(frame, std::array<std::size_t, 2>{1416, 1462}, 0x5bb3fb);
    case 0x224c:
        return TextAsFormat(frame, 1462);
    default:
        break;
    }
    // 0x5ba916 y 0x5bb33d: la caja de la suerte de nivel 7 y el 0x1e27 de nivel 3; con otro nivel, como los demás.
    if (model == 0x1e1d && level == 7)
    {
        Paint(frame, Gold);
        return TextAsFormat(frame, 111);
    }
    if (model == 0x1e27 && level == 3)
    {
        Paint(frame, Gold);
        return TextAsFormat(frame, 1290);
    }
    return GenericName(frame);
}
} // namespace

Core::Expected<void, std::string> DrawTheFloorItemName(GameHudContext& context, const FloorItemState& item,
                                                       bool atTheItem)
{
    // 0x5ba7a7..0x5ba7c7: SelectObject con la fuente normal (0x5763554) y los colores.
    HudState& hud = context.hud;
    hud.dcFont = FontKind::Normal;
    hud.textColors = StartColors;
    const NameFrame frame{.context = context, .item = item, .level = (item.value >> LevelShift) & LevelMask};
    const Name name = ComposeTheName(frame);
    if (!name.HasValue())
        return Core::Unexpected(name.Error());
    // 0x5bb811..0x5bb894: GetTextExtentPointA con la fuente del DC; la mitad de cx con signo (CDQ, SUB y SAR), por
    // 640 y por W sin signo; 0x554180(x − eso, y − 15, texto, 0, 0, 3).
    const App::PixelSize screen = context.sprites.Screen();
    const Render::Text::TextExtent extent = context.text.Measure(hud.dcFont, name.Value());
    const std::int32_t x = atTheItem ? item.screenX : context.mouse.x;
    const std::int32_t y = atTheItem ? item.screenY : context.mouse.y;
    std::uint32_t half = 0;
    if (screen.width > 0)
        half = static_cast<std::uint32_t>(extent.width / 2) * DesignWidth / static_cast<std::uint32_t>(screen.width);
    DrawHudLine(context.text, screen, hud, static_cast<std::int32_t>(static_cast<std::uint32_t>(x) - half),
                y - NameRise, name.Value());
    hud.textColors = EndColors;
    return {};
}
} // namespace Client::Scenes
