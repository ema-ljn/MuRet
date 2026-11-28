#include "Items/FloorItemNames.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/DrawTailState.h"
#include "Hud/GameHud.h"
#include "Items/HudItems.h"
#include "Hud/HudText.h"
#include "Render/Frame/ITextDrawer.h" // IWYU pragma: keep (ITextDrawer as ITextMeasure)
#include "Items/ItemNames.h"
#include "Tooltips/ItemTooltipParts.h"
#include "Monsters/NpcNames.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Crt/SprintfFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Items
{
namespace
{
using Render::Color;
using Render::Text::FontKind;
using Render::Text::TextColors;

// 0x5ba790's glColor3f: gold for almost every case, the archangel weapons (0x5bb51f), the excellent ones (0x5bb489),
// grey for level 0 and for 1 and 2, orange for 3 and 4, light blue for 5 and 6 or with options (0x5bb50e) and white
// for the set (0x5bb55e).
constexpr Color Gold{1.0f, 0.8f, 0.1f, 1.0f};
constexpr Color Archangel{1.0f, 0.1f, 1.0f, 1.0f};
constexpr Color Excellent{0.1f, 1.0f, 0.5f, 1.0f};
constexpr Color LevelZero{0.7f, 0.7f, 0.7f, 1.0f};
constexpr Color LowLevel{0.9f, 0.9f, 0.9f, 1.0f};
constexpr Color MiddleLevel{1.0f, 0.5f, 0.2f, 1.0f};
constexpr Color HighLevel{0.4f, 0.7f, 1.0f, 1.0f};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// 0x5ba7bd..0x5ba7c7, 0x5bb585..0x5bb58f and 0x5bb89c..0x5bb8a6: 0x6c8e5c (text) and 0x6c8e64 (background).
constexpr TextColors StartColors{.text = 0xffffffffu, .background = 0xff000000u};
constexpr TextColors SetColors{.text = 0xff00ff00u, .background = 0xffc83c3cu};
constexpr TextColors EndColors{.text = 0xffc8e6ffu, .background = 0xff000000u};
// A type's model (0x212, as in 0x608350) and the text's y, 15 higher (0x5bb850 and 0x5bb873).
constexpr std::int32_t ModelBase = 0x212;
constexpr std::int32_t NameRise = 15;
constexpr std::uint32_t DesignWidth = 640;
// The entry's +8: the level (>> 3, & 0xf), the skill (0x80), the luck (4) and the option (3). +0x1f: the excellent
// options (0x3f) and one more (0x40).
constexpr std::int32_t LevelShift = 3;
constexpr std::int32_t LevelMask = 0xf;
constexpr std::int32_t SkillBit = 0x80;
constexpr std::int32_t LuckBit = 4;
constexpr std::int32_t OptionBits = 3;
constexpr std::int32_t PlusLevelBits = 0x78;
constexpr std::int32_t OptionColorBits = 0x87;
constexpr std::uint8_t ExcellentBits = 0x3f;
constexpr std::uint8_t ExcellentOptionBit = 0x40;
// 0x5bb4bb, 0x5bb4e7 and 0x5bb4fd.
constexpr std::int32_t GoldLevel = 7;
constexpr std::int32_t MiddleFirstLevel = 3;
constexpr std::int32_t HighFirstLevel = 5;
// 0x5bae3e..0x5bae4f: 0x1a1d's skill, level + 30 (0x76bd1d8 + 0x3c · (level + 30)), and text 102.
constexpr std::uint32_t FirstScrollSkill = 30;
// 0x5bb5e7.., 0x5bb66f and 0x5bb696: the texts of the skill (176, and " +" and 179 with 0x1c15), the option (177) and
// the luck (178).
constexpr std::size_t SkillText = 176;
constexpr std::size_t DinorantSkillText = 179;
constexpr std::size_t OptionText = 177;
constexpr std::size_t LuckText = 178;
constexpr std::int16_t DinorantModel = 0x1c15;

using Name = Core::Expected<std::string, std::string>;

struct NameFrame
{
    Hud::GameHudContext& context;
    const Kernel::FloorItemState& item;
    std::int32_t level = 0;
};

Core::Expected<std::string_view, std::string> TextOf(const NameFrame& frame, std::size_t index)
{
    const std::optional<std::string_view> text = Ui::TextAt(frame.context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("0x5ba790: falta el texto {} de Text.bmd", index));
    return *text;
}

// The type's Item.bmd record (0x7758550 + 0x54 · type), up to its '\0'.
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

Name TextAsFormat(const NameFrame& frame, std::size_t index)
{
    const auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return Crt::Sprintf(text.Value());
}

Name TextAndModelName(const NameFrame& frame, std::size_t index)
{
    const auto text = TextOf(frame, index);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    const auto name = ModelName(frame);
    if (!name.HasValue())
        return name;
    return Crt::Sprintf("%s %s", text.Value(), std::string_view(name.Value()));
}

// The cases that leave the buffer unwritten with another level ([ebp-0x58]): the original draws what was left on its
// stack.
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

// 0x5bb449..0x5bb52e.
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

// 0x5bb52e..0x5bb72e: the color, the set (0x40cf30), the name with " +level" and the skill, option and luck texts.
Name GenericName(const NameFrame& frame)
{
    Paint(frame, GenericColor(frame));
    const auto set = Tooltips::ItemSetNameOf(frame.context, static_cast<std::uint16_t>(frame.item.type - ModelBase),
                                             frame.item.ancient);
    if (!set.HasValue())
        return Core::Unexpected(set.Error());
    const auto record = ModelName(frame);
    if (!record.HasValue())
        return record;
    std::string name = record.Value();
    if (set.Value())
    {
        // 0x5bb55e..0x5bb5dc: white, bold, 0xff00ff00 on 0xffc83c3c, and the name after the set's.
        Paint(frame, White);
        Bold(frame);
        frame.context.hud.textColors = SetColors;
        name = *set.Value() + name;
    }
    std::string text = frame.level == 0 ? Crt::Sprintf("%s", std::string_view(name))
                                        : Crt::Sprintf("%s +%d", std::string_view(name), frame.level);
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

// 0x5bae2e..0x5bb2e2: the summoning orb (type 0x180b), with its Skill.bmd skill's access open while it writes.
Name ScrollName(const NameFrame& frame)
{
    const auto text = TextOf(frame, 102);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    const auto skill = Tooltips::ReadTheSkillEntry(
        frame.context, FirstScrollSkill + static_cast<std::uint32_t>(frame.level), "0x5bae38");
    if (!skill.HasValue())
        return Core::Unexpected(skill.Error());
    return Crt::Sprintf("%s %s", std::string_view(skill.Value().name), text.Value());
}

// A text picked by the level, from 0 on; with any other level the buffer stays unwritten.
template <std::size_t Count>
Name ByLevel(const NameFrame& frame, const std::array<std::size_t, Count>& texts, std::uint32_t unwritten)
{
    if (frame.level < 0 || static_cast<std::size_t>(frame.level) >= Count)
        return Unwritten(frame, unwritten);
    return TextAsFormat(frame, texts[static_cast<std::size_t>(frame.level)]);
}

// 0x5bb2ed..0x5bb338: the transformation ring (model 0x1c1c): the first record of 0x7681eb8 whose type (the word,
// unsigned) is the level's monster (0x6c9338), and sprintf(buffer, "%s %s" (0x6c9cb4), the record's +2, text 103).
// With none, it jumps to 0x5bb811 without writing the buffer (0x5bb30f).
Name TransformationRingName(const NameFrame& frame)
{
    const std::optional<std::uint32_t> monster = TransformationMonsterOf(frame.level);
    if (!monster)
        return Core::Unexpected(std::format("0x5bb2ed: el nivel {} del anillo de transformación lee 0x6c9338 fuera de "
                                            "lo que MuRet tiene de la .data",
                                            frame.level));
    const std::optional<std::string> name =
        Monsters::NpcNameOf(frame.context.world.Runtime().npcNames, static_cast<std::int32_t>(*monster));
    if (!name)
        return Unwritten(frame, 0x5bb30f);
    constexpr std::size_t TransformationRingText = 103;
    const auto text = TextOf(frame, TransformationRingText);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    return Crt::Sprintf("%s %s", std::string_view(*name), text.Value());
}

// 0x5ba7d5..0x5bb811: the text and its color by the model.
Name ComposeTheName(const NameFrame& frame)
{
    const std::int16_t model = frame.item.type;
    const std::int32_t level = frame.level;
    switch (model)
    {
    case 0x1e21:
    {
        // 0x5ba7db: zen, "%s %d" with the integer value (+8).
        Paint(frame, Gold);
        const auto name = ModelName(frame);
        if (!name.HasValue())
            return name;
        return Crt::Sprintf("%s %d", std::string_view(name.Value()), frame.item.value);
    }
    case 0x1e1f:
    case 0x1e20:
    case 0x1e22:
    case 0x1a21:
    case 0x1e28:
    case 0x1e31:
    case 0x1c20:
    {
        // 0x5bb7bc: the jewels, in bold.
        Bold(frame);
        Paint(frame, Gold);
        const auto name = ModelName(frame);
        if (!name.HasValue())
            return name;
        return Crt::Sprintf("%s", std::string_view(name.Value()));
    }
    case 0x1a30:
    case 0x1a31:
    {
        // 0x5ba875 and 0x5ba8b9: in bold, with the name of 0x1c0d (0x7758550 + 0x93444) or of 0x1c0e (+ 0x93498).
        Bold(frame);
        Paint(frame, Gold);
        const auto name = RecordNameOf(frame, model == 0x1a30 ? 0x1c0d : 0x1c0e);
        if (!name.HasValue())
            return name;
        return Crt::Sprintf("%s", std::string_view(name.Value()));
    }
    case 0x1e23:
    case 0x1e24:
    case 0x1e25:
    {
        // 0x5bb733: in bold; with a level (+8 & 0x78), " +level".
        Bold(frame);
        Paint(frame, Gold);
        const auto name = ModelName(frame);
        if (!name.HasValue())
            return name;
        if ((frame.item.value & PlusLevelBits) != 0)
            return Crt::Sprintf("%s +%d", std::string_view(name.Value()), level);
        return Crt::Sprintf("%s", std::string_view(name.Value()));
    }
    case 0x1e1e:
        // 0x5ba94c: no glColor3f.
        return ByLevel(frame, std::array<std::size_t, 3>{100, 101, 104}, 0x5ba95b);
    case 0x1c21:
    {
        // 0x5ba9a3, the table at 0x5bb8b4: "%s %s" with the level's text and the name.
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
        // 0x5bac43: with level 13, gold and "%s" with text 117 (0x5bb809).
        if (level != 13)
            return TextAsFormat(frame, 107);
        Paint(frame, Gold);
        const auto text = TextOf(frame, 117);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        return Crt::Sprintf("%s", text.Value());
    }
    case 0x2241:
        return TextAsFormat(frame, 108);
    case 0x2242:
        return TextAsFormat(frame, 109);
    case 0x2243:
        return TextAsFormat(frame, 110);
    case 0x2244:
    {
        // 0x5bace0: "%s +%d" with text 115 and the level − 7.
        const auto text = TextOf(frame, 115);
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        return Crt::Sprintf("%s +%d", text.Value(), level - 7);
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
        return TransformationRingName(frame);
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
    // 0x5ba916 and 0x5bb33d: the luck box of level 7 and the 0x1e27 of level 3; with another level, like the others.
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

Core::Expected<void, std::string> DrawTheFloorItemName(Hud::GameHudContext& context, const Kernel::FloorItemState& item,
                                                       bool atTheItem)
{
    // 0x5ba7a7..0x5ba7c7: SelectObject with the normal font (0x5763554) and the colors.
    Kernel::HudState& hud = context.hud;
    hud.dcFont = FontKind::Normal;
    hud.textColors = StartColors;
    const NameFrame frame{.context = context, .item = item, .level = (item.value >> LevelShift) & LevelMask};
    const Name name = ComposeTheName(frame);
    if (!name.HasValue())
        return Core::Unexpected(name.Error());
    // 0x5bb811..0x5bb894: GetTextExtentPointA with the DC's font; half of cx, signed (CDQ, SUB and SAR), times 640 and
    // by W unsigned; 0x554180(x − that, y − 15, text, 0, 0, 3).
    const Core::PixelSize screen = context.sprites.Screen();
    const Render::Text::TextExtent extent = context.text.Measure(hud.dcFont, name.Value());
    const std::int32_t x = atTheItem ? item.screenX : context.mouse.x;
    const std::int32_t y = atTheItem ? item.screenY : context.mouse.y;
    std::uint32_t half = 0;
    if (screen.width > 0)
        half = static_cast<std::uint32_t>(extent.width / 2) * DesignWidth / static_cast<std::uint32_t>(screen.width);
    Hud::DrawHudLine(context.text, screen, hud, static_cast<std::int32_t>(static_cast<std::uint32_t>(x) - half),
                     y - NameRise, name.Value());
    hud.textColors = EndColors;
    return {};
}
} // namespace Client::Items
