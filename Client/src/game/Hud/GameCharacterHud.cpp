#include "Hud/GameCharacterHud.h"

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Math/X87.h"
#include "Models/LogicalAccess.h"
#include "Math/RoundedMath.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDamage.h"
#include "Player/OwnStatHelpers.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Ui/HudPrimitives.h"
#include "FrontEnd/ServerListModel.h"
#include "Crt/SprintfFormat.h"
#include "Kernel/CharacterClass.h"
#include "Kernel/ItemState.h"
#include "Movement/GameMovementState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// 0x5ee2e0 (10336 bytes) and the range of 0x5f7440 that follows it, checked against the PE c32bbd5f: 06, "La ventana
// del personaje".
namespace Client::Hud
{
namespace
{
using Kernel::BaseClassOf;
using Kernel::CharacterClass;
using Render::BlendMode;
using Render::Color;
using Render::Text::FontKind;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};

// The corner 0x5f7440 passes (0x5f7494).
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::int32_t WindowY = 0;

// Text.bmd texts: (the address − 0x76bd1dc) / 300.
constexpr std::size_t FirstClassText = 20;
constexpr std::size_t SpeedText = 64;
constexpr std::size_t LevelText = 200;
constexpr std::size_t ExperienceText = 201;
constexpr std::size_t StrengthText = 202;
constexpr std::size_t DamageRateText = 203;
constexpr std::size_t DamageText = 204;
constexpr std::size_t AgilityText = 205;
constexpr std::size_t DefenseRateBonusText = 206;
constexpr std::size_t DefenseBonusText = 207;
constexpr std::size_t DefenseRateText = 208;
constexpr std::size_t DefenseText = 209;
constexpr std::size_t VitalityText = 210;
constexpr std::size_t LifeText = 211;
constexpr std::size_t EnergyText = 212;
constexpr std::size_t ManaText = 213;
constexpr std::size_t MagicBonusText = 215;
constexpr std::size_t MagicText = 216;
constexpr std::size_t PointsText = 217;
constexpr std::size_t CloseHelpText = 222;
constexpr std::size_t SkillDamageText = 582;
constexpr std::size_t CommandText = 1170;
constexpr std::size_t CreateText = 1412;
constexpr std::size_t DecreaseText = 1903;
constexpr std::size_t LettersText = 1907;

// The colors of 0x6c8e5c (the text) and 0x6c8e64 (the background).
// (−(word != 0) & 0x97d685) − 0x984021: orange with something added, light blue with nothing.
constexpr std::uint32_t AddedStatColor = 0xffff9664u;
constexpr std::uint32_t BaseStatColor = 0xff67bfdfu;
constexpr std::uint32_t NameBackground = 0xff141414u;
constexpr std::uint32_t LineBackground = 0x80000000u;
constexpr std::uint32_t LevelColor = 0xffe6e6e6u;
constexpr std::uint32_t LineColor = 0xffffffffu;
constexpr std::uint32_t PointsLineColor = 0xffff9664u;
constexpr std::uint32_t LevelUpBackground = 0xffc86e1eu;
constexpr std::uint32_t LevelUpColor = 0xff000000u;

// The hero's +0x78 bits that add to attack and defense (*inferred*: a buff).
constexpr std::uint32_t AgilityBonusFlag = 0x400;
constexpr std::uint32_t LevelBonusFlag = 0x2000000;

// The four class 3 armors that count as a set without a helm (0x5efae2..0x5efb03).
constexpr std::int32_t GladiatorSetArmors[] = {0x100f, 0x1014, 0x1017, 0x1020};

// 0x5ee2e0's sprintfs on a Text.bmd text (Q-C590).
using Crt::Sprintf;

using Math::F32;
using Math::Truncate;

Models::LogicalAccess OwnAccess(GameHudContext& context)
{
    return Models::LogicalAccess(context.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0): scaled in size and position, without alpha.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float du,
          float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = 0.0f,
                                      .v = 0.0f,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

// The mouse's FILD against the box in float: left ≤ x < left + size, with the sum on the x87.
bool MouseInSquare(const GameHudContext& context, float left, float top, float size)
{
    const auto x = static_cast<double>(static_cast<float>(context.mouse.x));
    const auto y = static_cast<double>(static_cast<float>(context.mouse.y));
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(size) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(size);
}

void SelectFont(GameHudContext& context, FontKind font)
{
    context.hud.dcFont = font;
}

void SetColors(GameHudContext& context, std::uint32_t text, std::uint32_t background)
{
    context.hud.textColors = Render::Text::TextColors{.text = text, .background = background};
}

// 0x554180's width: (W · k) / 640, unsigned.
int BoxPixels(const GameHudContext& context, std::uint32_t designWidth)
{
    const auto screenWidth = static_cast<std::uint32_t>(context.sprites.Screen().width);
    return static_cast<int>(screenWidth * designWidth / 640u);
}

// GetTextExtentPoint32A's cx in 0x467060: an empty text measures "0" (0x4670ba..0x4670d0).
int MeasuredWidth(GameHudContext& context, std::string_view text)
{
    return context.text.Measure(context.hud.dcFont, text.empty() ? std::string_view("0") : text).width;
}

Render::Text::TextStyle CurrentStyle(const GameHudContext& context)
{
    return Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors};
}

// 0x554180(x, y, text, box, 1, p6) → 0x467060 in mode 2 (0x46711b..0x467132, Render::Text::CenterInBox): (box − cx) / 2
// with C division, the box of cx + 2 · that columns from x and the text shifted by that; wider than the box, the
// center shows. Empty and without a box, nothing (0x467077..0x467082).
void DrawCentered(GameHudContext& context, int x, int y, std::string_view text, int box)
{
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.hud.dcFont, text, box);
    context.text.DrawColumns(x, y, text, CurrentStyle(context), -placed.textColumn, placed.width);
}

// 0x554180(x, y, text, width, 0, 0) → 0x467060 in mode 1 (0x467102..0x467119): with the width, the box of that width
// from x; if the text is wider, it shows from column cx − width.
void DrawFromLeft(GameHudContext& context, int x, int y, std::string_view text, int width)
{
    if (width <= 0)
    {
        context.text.Draw(x, y, text, CurrentStyle(context));
        return;
    }
    const int measured = MeasuredWidth(context, text);
    context.text.DrawColumns(x, y, text, CurrentStyle(context), width < measured ? measured - width : 0, width);
}

std::uint32_t StatColor(std::uint16_t added)
{
    return added != 0 ? AddedStatColor : BaseStatColor;
}

// The sum of a stat and what the items add (add cx, ax): 16 bits that wrap, then and 0xffff.
std::uint32_t StatPlus(std::uint16_t stat, std::uint16_t added)
{
    return static_cast<std::uint16_t>(stat + added);
}

// Signed, as the original reads it (movsx).
std::int32_t TypeOf(const Kernel::ItemState& item)
{
    return static_cast<std::int16_t>(item.id);
}

// sprintf("%s", [0x7681eb0]): the name up to its 0.
Core::Expected<std::string, std::string> NameOf(const Kernel::OwnCharacterRecord& own)
{
    const auto end = std::ranges::find(own.name, std::byte{0});
    if (end == own.name.end())
        return Core::Unexpected(std::string("0x5ee47b: el nombre del registro propio no tiene su 0 en sus 11 bytes (el "
                                            "sprintf seguiría por la clase)"));
    std::string name;
    for (auto letter = own.name.begin(); letter != end; ++letter)
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(*letter)));
    return name;
}

struct Window
{
    GameHudContext& context;
    const Kernel::OwnCharacterRecord& own;
    const Kernel::ActorState& hero;
    // local_28
    CharacterClass heroClass = CharacterClass::DarkWizard;
    std::int32_t x = WindowX;
    std::int32_t y = WindowY;
};

// 0x5ee2e0..0x5ee7f0: the background, the name, the class and the server, and the stats' backgrounds.
Core::Expected<void, std::string> DrawTheHeader(Window& window)
{
    GameHudContext& context = window.context;
    Render::SpriteBatch& sprites = context.sprites;
    const std::int32_t x = window.x;
    const std::int32_t y = window.y;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    Quad(sprites, 0x104, left, top, 190.0f, 256.0f, 0.7421875f, 1.0f);
    Quad(sprites, 0x105, left, F32(static_cast<double>(top) + 256.0), 190.0f, 177.0f, 0.7421875f, 0.69140625f);
    sprites.SetBlend(BlendMode::Alpha);
    SelectFont(context, FontKind::Bold);
    // Access 1: the record's name and class.
    std::string name;
    std::string className;
    {
        const auto access = OwnAccess(context);
        auto own = NameOf(window.own);
        if (!own.HasValue())
            return Core::Unexpected(own.Error());
        // sprintf("%s", name) copies it as is.
        name = own.Value();
        // 0x5ee48c..0x5ee4ab: text 20 + (+0xb >> 3) · 5 + (+0xb & 7).
        const std::uint8_t recordClass = window.own.characterClass;
        auto classText = TextOf(context,
                                FirstClassText + static_cast<std::size_t>(recordClass >> 3u) * 5u +
                                    static_cast<std::size_t>(recordClass & 7u),
                                "0x5ee4ab");
        if (!classText.HasValue())
            return Core::Unexpected(classText.Error());
        className = Sprintf("(%s)", classText.Value());
    }
    context.hud.textColors.background = NameBackground;
    context.hud.textColors.text = PkNameColor(window.hero.pk);
    DrawCentered(context, x + 0x23, y + 0xc, name, BoxPixels(context, 120));
    // 0x5ee629..0x5ee69c: [0x6d5ec4] is the chosen group's slot and [0x6d5ecc] its number; the group's name is that of
    // 0x7d36bb0 + 0x21e · slot, which 0x4c1860 set.
    if (!context.chosenServer)
        return Core::Unexpected(std::string("0x5ee629: sin el servidor elegido ([0x6d5ec4] y [0x6d5ecc] en −1)"));
    const std::uint16_t group = FrontEnd::GroupOf(*context.chosenServer);
    const std::optional<int> slot = FrontEnd::GroupSlot(group);
    if (!slot)
        return Core::Unexpected(std::format("0x5ee629: el grupo {} del servidor elegido no tiene lugar", group));
    const int number = FrontEnd::ServerNumber(*context.chosenServer);
    const bool nonPvp = FrontEnd::IsNonPvp(*slot, number);
    // 0x447c70: 460 without PvP and 461 otherwise, like the account title.
    auto serverFormat = TextOf(context, FrontEnd::LoginTitleText(*slot, number), nonPvp ? "0x5ee668" : "0x5ee696");
    if (!serverFormat.HasValue())
        return Core::Unexpected(serverFormat.Error());
    auto groupName = TextOf(context, FrontEnd::GroupNameText(group), "0x5ee660");
    if (!groupName.HasValue())
        return Core::Unexpected(groupName.Error());
    const std::string server = Sprintf(serverFormat.Value(), groupName.Value(), static_cast<std::int32_t>(number));
    // 0x5ee6a1..0x5ee6bf: a = (float)(sin(T · 0.001f) + 1), on the x87.
    const float alpha =
        F32(Math::RoundedSin(static_cast<double>(context.worldClock) * static_cast<double>(0.001f)) + 1.0);
    sprites.SetColor(Color{1.0f, 1.0f, 1.0f, F32(2.0 - static_cast<double>(alpha))});
    DrawCentered(context, x + 0x16, y + 0x16, className, BoxPixels(context, 150));
    sprites.SetColor(Color{1.0f, 1.0f, 1.0f, alpha});
    DrawCentered(context, x + 0x16, y + 0x16, server, BoxPixels(context, 150));
    sprites.SetColor(White);
    // 0x5ee786..0x5ee7f0: [0x6c9330] buttons and one more of backgrounds, every 60.
    const std::int32_t backs = (window.heroClass == CharacterClass::DarkLord ? 5 : 4) + 1;
    const float backLeft = F32(static_cast<double>(left) + 19.0 - 6.0);
    const float backTop = F32(static_cast<double>(top) + 60.0 - 4.0);
    for (std::int32_t i = 0; i < backs; ++i)
        Quad(sprites, 0xf5, backLeft, F32(static_cast<double>(i * 60) + static_cast<double>(backTop)), 75.0f, 21.0f,
             0.5859375f, 0.65625f);
    return {};
}

// 0x5ee7f0..0x5eee60: the level, the experience and the points line.
Core::Expected<void, std::string> DrawTheLevel(Window& window)
{
    GameHudContext& context = window.context;
    const Kernel::OwnCharacterRecord& own = window.own;
    const std::int32_t x = window.x;
    const std::int32_t y = window.y;
    auto levelFormat = TextOf(context, LevelText, "0x5ee8e5");
    auto experienceFormat = TextOf(context, ExperienceText, "0x5eeb4e");
    auto create = TextOf(context, CreateText, "0x5eedfa");
    auto decrease = TextOf(context, DecreaseText, "0x5eedf3");
    if (!levelFormat.HasValue())
        return Core::Unexpected(levelFormat.Error());
    if (!experienceFormat.HasValue())
        return Core::Unexpected(experienceFormat.Error());
    if (!create.HasValue())
        return Core::Unexpected(create.Error());
    if (!decrease.HasValue())
        return Core::Unexpected(decrease.Error());
    SelectFont(context, FontKind::Bold);
    std::string text;
    {
        // Access 2.
        const auto access = OwnAccess(context);
        text = Sprintf(levelFormat.Value(), static_cast<std::uint32_t>(own.level));
    }
    SetColors(context, LevelColor, 0);
    DrawCentered(context, x + 0xe, y + 0x3c, text, BoxPixels(context, 82));
    SelectFont(context, FontKind::Normal);
    {
        // Access 3.
        const auto access = OwnAccess(context);
        text = Sprintf(experienceFormat.Value(), own.experience, own.nextExperience);
    }
    SetColors(context, LineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 0x50, text, BoxPixels(context, 145));
    {
        // Access 4: wsprintfA ([0x6b851c]) with '%s %d/%d | %s %d/%d' from .rdata, which is not the CRT's sprintf.
        // Before level 10, at 0; otherwise, +0x44 and +0x46 signed and −+0x48 and −+0x4a unsigned.
        const auto access = OwnAccess(context);
        constexpr std::uint16_t PointsFromLevel = 10;
        std::int32_t added = 0;
        std::int32_t maxAdded = 0;
        std::int32_t taken = 0;
        std::int32_t maxTaken = 0;
        if (own.level >= PointsFromLevel)
        {
            added = static_cast<std::int16_t>(own.addedPoints);
            maxAdded = static_cast<std::int16_t>(own.maxAddedPoints);
            taken = -static_cast<std::int32_t>(own.word48);
            maxTaken = -static_cast<std::int32_t>(own.word4a);
        }
        text = std::format("{} {}/{} | {} {}/{}", create.Value(), added, maxAdded, decrease.Value(), taken, maxTaken);
    }
    SelectFont(context, FontKind::Bold);
    SetColors(context, PointsLineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 0x5f, text, BoxPixels(context, 130));
    SelectFont(context, FontKind::Bold);
    return {};
}

// 0x5eee60..0x5efa0e: the strength, the damage and whether the equipment is a full set (the result).
Core::Expected<bool, std::string> DrawTheStrength(Window& window)
{
    GameHudContext& context = window.context;
    const Kernel::OwnCharacterRecord& own = window.own;
    const Kernel::ActorState& hero = window.hero;
    const std::int32_t x = window.x;
    const std::int32_t y = window.y;
    auto strengthFormat = TextOf(context, StrengthText, "0x5ef04e");
    auto rateFormat = TextOf(context, DamageRateText, "0x5ef4f5");
    auto damageFormat = TextOf(context, DamageText, "0x5ef554");
    if (!strengthFormat.HasValue())
        return Core::Unexpected(strengthFormat.Error());
    if (!rateFormat.HasValue())
        return Core::Unexpected(rateFormat.Error());
    if (!damageFormat.HasValue())
        return Core::Unexpected(damageFormat.Error());
    std::string text;
    {
        // Access 5: the color before the sprintf.
        const auto access = OwnAccess(context);
        context.hud.textColors.text = StatColor(own.word2a);
        text = Sprintf(strengthFormat.Value(), StatPlus(own.strength, own.word2a));
    }
    context.hud.textColors.background = 0;
    DrawCentered(context, x + 0xe, y + 0x78, text, BoxPixels(context, 70));
    SelectFont(context, FontKind::Normal);
    {
        // Access 6: it only takes the pointer to slot 0 (+0xb6c), which the magic reads.
        const auto access = OwnAccess(context);
    }
    const Player::OwnAttackDamage attack = Player::AttackDamageOf(
        own, window.heroClass, Player::HeldItemOf(context.world.Runtime()), context.ownRecordDepth, context.random);
    {
        // Access 7 (0x5ef450..0x5ef55f).
        const auto access = OwnAccess(context);
        std::int32_t bonus = own.word4e;
        if ((hero.flags78 & AgilityBonusFlag) == AgilityBonusFlag)
        {
            const auto agility = static_cast<std::int32_t>(StatPlus(own.agility, own.word2c));
            bonus += agility;
            const std::int32_t party = context.hud.partyCount;
            if (party >= 3)
                bonus = Truncate(static_cast<double>(party - 2) * static_cast<double>(0.01f) *
                                     static_cast<double>(agility) +
                                 static_cast<double>(bonus));
        }
        std::uint32_t min = attack.min;
        std::uint32_t max = attack.max;
        if ((hero.flags78 & LevelBonusFlag) == LevelBonusFlag)
        {
            const std::uint32_t extra = own.level / 3u + 45u;
            min += extra;
            max += extra;
        }
        text = bonus > 0 ? Sprintf(rateFormat.Value(), min, max, bonus) : Sprintf(damageFormat.Value(), min, max);
    }
    SetColors(context, attack.highlighted ? AddedStatColor : LineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 0x8c, text, BoxPixels(context, 130));
    // 0x5ef62a..0x5ef8ac: slots 2 to 6, one access each, up to the first empty or broken one; class 3 may have no helm.
    bool fullSet = true;
    constexpr std::size_t FirstArmorSlot = 2;
    constexpr std::size_t LastArmorSlot = 6;
    for (std::size_t slot = FirstArmorSlot; slot <= LastArmorSlot; ++slot)
    {
        const auto access = OwnAccess(context);
        const Kernel::ItemState& item = own.equipment[slot];
        const bool empty = TypeOf(item) == -1;
        const bool helmOptional = window.heroClass == CharacterClass::MagicGladiator && slot == FirstArmorSlot;
        if ((empty && !helmOptional) || (!empty && item.durability == 0))
        {
            fullSet = false;
            break;
        }
    }
    if (!fullSet)
        return false;
    {
        // Access 9 (0x5efa0e..0x5efc5f): slots 3 to 6 with the same type % 512 as the helm, or as the armor in class 3,
        // which has to be one of four.
        const auto access = OwnAccess(context);
        std::int32_t reference = 0;
        bool compare = true;
        if (window.heroClass == CharacterClass::MagicGladiator)
        {
            const std::int32_t armor = TypeOf(own.equipment[3]);
            if (std::ranges::find(GladiatorSetArmors, armor) == std::end(GladiatorSetArmors))
                fullSet = false;
            reference = armor % 512;
            compare = fullSet;
        }
        else
            reference = TypeOf(own.equipment[2]) % 512;
        if (compare)
        {
            for (std::size_t slot = 3; slot <= LastArmorSlot; ++slot)
            {
                if (TypeOf(own.equipment[slot]) % 512 != reference)
                {
                    fullSet = false;
                    break;
                }
            }
        }
    }
    return fullSet;
}

// 0x5efc5f..0x5f0236: the agility, the defense and the speed.
Core::Expected<void, std::string> DrawTheAgility(Window& window, bool fullSet)
{
    GameHudContext& context = window.context;
    const Kernel::OwnCharacterRecord& own = window.own;
    const std::int32_t x = window.x;
    const std::int32_t y = window.y;
    auto agilityFormat = TextOf(context, AgilityText, "0x5efd55");
    auto defenseFormat = TextOf(context,
                                fullSet ? (own.word60 != 0 ? DefenseRateBonusText : DefenseBonusText)
                                        : (own.word60 != 0 ? DefenseRateText : DefenseText),
                                "0x5eff86");
    auto speedFormat = TextOf(context, SpeedText, "0x5f008b");
    if (!agilityFormat.HasValue())
        return Core::Unexpected(agilityFormat.Error());
    if (!defenseFormat.HasValue())
        return Core::Unexpected(defenseFormat.Error());
    if (!speedFormat.HasValue())
        return Core::Unexpected(speedFormat.Error());
    SelectFont(context, FontKind::Bold);
    std::string text;
    {
        // Access 10.
        const auto access = OwnAccess(context);
        context.hud.textColors.text = StatColor(own.word2c);
        text = Sprintf(agilityFormat.Value(), StatPlus(own.agility, own.word2c));
    }
    context.hud.textColors.background = 0;
    DrawCentered(context, x + 0xe, y + 0xb4, text, BoxPixels(context, 70));
    SelectFont(context, FontKind::Normal);
    // Access 11, with both lines inside.
    const auto access = OwnAccess(context);
    std::uint32_t defense = own.word62;
    if ((window.hero.flags78 & LevelBonusFlag) == LevelBonusFlag)
        defense += own.level / 5u + 50u;
    const std::uint32_t rate = own.word60;
    if (fullSet)
        text = rate != 0 ? Sprintf(defenseFormat.Value(), defense, rate, defense / 10u)
                         : Sprintf(defenseFormat.Value(), defense, defense / 10u);
    else
        text = rate != 0 ? Sprintf(defenseFormat.Value(), defense, rate) : Sprintf(defenseFormat.Value(), defense);
    SetColors(context, LineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 200, text, BoxPixels(context, 130));
    // 0x5f006f..0x5f007f: the record's class (+0xb), not the hero's; class 0 reads the magic one.
    const std::uint32_t speed =
        BaseClassOf(own.characterClass) == CharacterClass::DarkWizard ? own.magicSpeed : own.attackSpeed;
    text = Sprintf(speedFormat.Value(), speed);
    DrawFromLeft(context, x + 0x18, y + 0xd7, text, BoxPixels(context, 130));
    return {};
}

// 0x5f04b1..0x5f0633, with the record open: the magic of classes 0 and 3, with access 13 inside access 12.
Core::Expected<void, std::string> DrawTheMagic(Window& window)
{
    GameHudContext& context = window.context;
    const Kernel::OwnCharacterRecord& own = window.own;
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const std::optional<std::uint8_t> skill = own.SkillAt(window.hero.skillIndex);
    if (!skill)
        return Core::Unexpected(
            std::format("0x5f04cd: el byte +0x6c + {} del registro propio no está en MuRet", window.hero.skillIndex));
    // An intermediate drawing does not leave the list's marks: it works on a copy.
    Player::ItemSetOptionList projected = world.ItemSets().list;
    Player::ItemSetOptionList& setOptions = context.projecting ? projected : world.ItemSets().list;
    Player::OwnMagicDamage damage;
    {
        // Access 13, inside access 12: it draws nothing when it closes.
        const auto access = OwnAccess(context);
        auto magic = Player::MagicDamageOf(own, *skill, context.skills, runtime.movement.skillDepths[*skill],
                                           context.random, setOptions);
        if (!magic.HasValue())
            return Core::Unexpected(magic.Error());
        damage = magic.Value();
    }
    std::int32_t min = damage.min;
    std::int32_t max = damage.max;
    const std::int32_t maxBefore = max;
    constexpr std::uint8_t ExtraDamageBit = 0x02;
    if ((own.speedAdjustBits & ExtraDamageBit) != 0)
    {
        min += 10;
        max += 10;
    }
    // 0x5f054d..0x5f0571: with a staff (0xa00 to 0xbff) or types 0x1f, 0x17 and 0x15 in the right hand, the addition of
    // its byte +0x16 by what is left of its durability (0x54ee00 on Item.bmd's byte +0x2e, not +0x2d).
    const Kernel::ItemState& right = own.equipment[0];
    const std::int32_t type = TypeOf(right);
    std::string text;
    if ((type >= 0xa00 && type < 0xc00) || type == 0x1f || type == 0x17 || type == 0x15)
    {
        auto format = TextOf(context, MagicBonusText, "0x5f05f1");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        if (context.items == nullptr)
            return Core::Unexpected(std::string("0x5f05a2: sin Item.bmd"));
        const auto record = context.items->Record(static_cast<std::size_t>(type));
        if (!record)
            return Core::Unexpected(std::format("0x5f05a2: Item.bmd no tiene el tipo {}", type));
        constexpr std::size_t MagicDurabilityByte = 0x2e;
        const float share = F32(static_cast<double>(right.byte16) * static_cast<double>(0.01f));
        const float factor = Player::ItemDurabilityFactor(right.durability,
                                                          std::to_integer<std::uint8_t>((*record)[MagicDurabilityByte]),
                                                          right.scalar1Low, right.flags1b, right.flags1c);
        // 0x5f05d1..0x5f05dd: (f − r · f) · max, on the x87.
        const std::int32_t bonus =
            Truncate((static_cast<double>(share) - static_cast<double>(factor) * static_cast<double>(share)) *
                     static_cast<double>(maxBefore));
        text = Sprintf(format.Value(), min, max, bonus);
    }
    else
    {
        auto format = TextOf(context, MagicText, "0x5f057e");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        text = Sprintf(format.Value(), min, max);
    }
    DrawFromLeft(context, window.x + 0x18, window.y + 0x14f, text, BoxPixels(context, 130));
    return {};
}

// 0x5f0236..0x5f0a6c: the vitality, the life, the energy, the mana, the magic, the skill damage, the points to
// distribute and the letters, all with access 12 open.
Core::Expected<void, std::string> DrawTheVitality(Window& window)
{
    GameHudContext& context = window.context;
    const Kernel::OwnCharacterRecord& own = window.own;
    const std::int32_t x = window.x;
    const std::int32_t y = window.y;
    auto vitalityFormat = TextOf(context, VitalityText, "0x5f0243");
    auto lifeFormat = TextOf(context, LifeText, "0x5f02c2");
    auto energyFormat = TextOf(context, EnergyText, "0x5f0356");
    auto manaFormat = TextOf(context, ManaText, "0x5f03d5");
    auto lettersFormat = TextOf(context, LettersText, "0x5f093f");
    if (!vitalityFormat.HasValue())
        return Core::Unexpected(vitalityFormat.Error());
    if (!lifeFormat.HasValue())
        return Core::Unexpected(lifeFormat.Error());
    if (!energyFormat.HasValue())
        return Core::Unexpected(energyFormat.Error());
    if (!manaFormat.HasValue())
        return Core::Unexpected(manaFormat.Error());
    if (!lettersFormat.HasValue())
        return Core::Unexpected(lettersFormat.Error());
    SelectFont(context, FontKind::Bold);
    const auto access = OwnAccess(context);
    context.hud.textColors.text = StatColor(own.word2e);
    std::string text = Sprintf(vitalityFormat.Value(), StatPlus(own.vitality, own.word2e));
    context.hud.textColors.background = 0;
    DrawCentered(context, x + 0xe, y + 0xf0, text, BoxPixels(context, 70));
    SelectFont(context, FontKind::Normal);
    text = Sprintf(lifeFormat.Value(), static_cast<std::uint32_t>(own.life), static_cast<std::uint32_t>(own.maxLife));
    SetColors(context, LineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 0x104, text, BoxPixels(context, 130));
    SelectFont(context, FontKind::Bold);
    const std::uint32_t energy = StatPlus(own.energy, own.word30);
    context.hud.textColors.text = StatColor(own.word30);
    text = Sprintf(energyFormat.Value(), energy);
    context.hud.textColors.background = 0;
    DrawCentered(context, x + 0xe, y + 300, text, BoxPixels(context, 70));
    SelectFont(context, FontKind::Normal);
    text = Sprintf(manaFormat.Value(), static_cast<std::uint32_t>(own.mana), static_cast<std::uint32_t>(own.maxMana));
    SetColors(context, LineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 0x140, text, BoxPixels(context, 130));
    if (window.heroClass == CharacterClass::DarkWizard || window.heroClass == CharacterClass::MagicGladiator)
    {
        if (auto magic = DrawTheMagic(window); !magic.HasValue())
            return magic;
    }
    // 0x5f0633..0x5f06f2: the Dark Knight's and the Dark Lord's skill damage by energy; class 3's, fixed and one line
    // lower.
    if (window.heroClass == CharacterClass::DarkKnight || window.heroClass == CharacterClass::MagicGladiator ||
        window.heroClass == CharacterClass::DarkLord)
    {
        auto format = TextOf(context, SkillDamageText, "0x5f065a");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        std::uint32_t percent = 200;
        int line = y + 0x14f;
        if (window.heroClass == CharacterClass::DarkKnight)
            percent = energy / 10u + 200u;
        else if (window.heroClass == CharacterClass::DarkLord)
            percent = energy / 20u + 200u;
        else
            line = y + 0x15e;
        text = Sprintf(format.Value(), percent);
        DrawFromLeft(context, x + 0x18, line, text, BoxPixels(context, 130));
    }
    if (own.levelUpPoints != 0)
    {
        // 0x5f070b..0x5f0884: the points to distribute and one button per stat, pressed with the button down over it.
        auto format = TextOf(context, PointsText, "0x5f0732");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        SelectFont(context, FontKind::Bold);
        text = Sprintf(format.Value(), static_cast<std::uint32_t>(own.levelUpPoints));
        SetColors(context, LevelUpColor, LevelUpBackground);
        DrawCentered(context, x + 0x5f, y + 0x32, text, BoxPixels(context, 80));
        const std::int32_t buttons = window.heroClass == CharacterClass::DarkLord ? 5 : 4;
        const float left = F32(static_cast<double>(static_cast<float>(x)) + 125.0);
        for (std::int32_t i = 0; i < buttons; ++i)
        {
            const float top = F32(static_cast<double>(i * 60) + static_cast<double>(static_cast<float>(y)) + 115.0);
            const bool pressed = context.leftDown && MouseInSquare(context, left, top, 24.0f);
            Quad(context.sprites, pressed ? 0x121 : 0x120, left, top, 24.0f, 24.0f, 0.75f, 0.75f);
        }
    }
    // 0x5f088a..0x5f0935: the bars' letters. The first, by +0x44 · 100 / +0x46 signed; the second divides +0x48 · 100
    // by 1 (Q-C670), so past 10 it is always 40.
    std::int32_t first = 100;
    const auto added = static_cast<std::int16_t>(own.addedPoints);
    if (added > 10)
    {
        const auto maxAdded = static_cast<std::int16_t>(own.maxAddedPoints);
        if (maxAdded == 0)
            return Core::Unexpected(std::string("0x5f08ac: +0x46 en 0 con +0x44 en más de 10 (idiv por 0)"));
        const std::int32_t ratio = added * 100 / maxAdded;
        first = ratio <= 10 ? 70 : ratio <= 30 ? 60 : ratio > 50 ? 40 : 50;
    }
    std::int32_t second = 100;
    if (own.word48 > 10)
    {
        const std::int32_t ratio = own.word48 * 100 / 1;
        second = ratio <= 10 ? 70 : ratio <= 30 ? 60 : ratio > 50 ? 40 : 50;
    }
    SelectFont(context, FontKind::Normal);
    text = Sprintf(lettersFormat.Value(), first, second);
    SetColors(context, PointsLineColor, LineBackground);
    DrawCentered(context, x + 0x5b, y + 0x41, text, BoxPixels(context, 84));
    return {};
}

// 0x5f0a6c..0x5f0b29: the Dark Lord's command, +0x20 plus +0x36, with access 14 closed before the sprintf.
Core::Expected<void, std::string> DrawTheCommand(Window& window)
{
    GameHudContext& context = window.context;
    const Kernel::OwnCharacterRecord& own = window.own;
    auto format = TextOf(context, CommandText, "0x5f0aef");
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    SelectFont(context, FontKind::Bold);
    context.hud.textColors.background = 0;
    std::uint32_t command = 0;
    {
        const auto access = OwnAccess(context);
        command = StatPlus(own.leadership, own.word36);
        context.hud.textColors.text = StatColor(own.word36);
    }
    const std::string text = Sprintf(format.Value(), command);
    DrawCentered(context, window.x + 0xe, window.y + 0x168, text, BoxPixels(context, 70));
    return {};
}

// 0x5f74a3..0x5f759a: the close button at (x + 25, y + 395) and its help when hovered.
Core::Expected<void, std::string> DrawTheCloseButton(GameHudContext& context)
{
    context.sprites.SetColor(White);
    const float left = F32(static_cast<double>(context.hud.characterX) + 25.0);
    const float top = F32(static_cast<double>(context.hud.characterY) + 395.0);
    Quad(context.sprites, 0x118, left, top, 24.0f, 24.0f, 0.75f, 0.75f);
    if (!MouseInSquare(context, left, top, 24.0f))
        return {};
    SelectFont(context, FontKind::Normal);
    SetColors(context, 0xffffffffu, 0xff000000u);
    auto help = TextOf(context, CloseHelpText, "0x5f757b");
    if (!help.HasValue())
        return Core::Unexpected(help.Error());
    // 0x554340 leaves the text white over a transparent background.
    Ui::DrawHelp(context.sprites, context.text, Truncate(left), Truncate(top) - 13, help.Value());
    SetColors(context, 0xffffffffu, 0);
    return {};
}
} // namespace

std::uint32_t PkNameColor(std::uint8_t pk)
{
    switch (pk)
    {
    case 0:
        return 0xfff0ff96u;
    case 1:
        return 0xffff7864u;
    case 2:
        return 0xffffb48cu;
    case 3:
        return 0xffffdcc8u;
    case 4:
        return 0xff3c96ffu;
    case 5:
        return 0xff1e50ffu;
    default:
        return 0xff0000ffu;
    }
}

Core::Expected<void, std::string> DrawTheCharacterWindow(GameHudContext& context)
{
    Kernel::WorldState& world = context.world;
    const Kernel::ActorState& hero = world.Hero();
    Window window{
        .context = context, .own = world.OwnCharacter(), .hero = hero, .heroClass = BaseClassOf(hero.characterClass)};
    context.hud.characterX = window.x;
    context.hud.characterY = window.y;
    if (auto header = DrawTheHeader(window); !header.HasValue())
        return header;
    if (auto level = DrawTheLevel(window); !level.HasValue())
        return level;
    auto fullSet = DrawTheStrength(window);
    if (!fullSet.HasValue())
        return Core::Unexpected(fullSet.Error());
    if (auto agility = DrawTheAgility(window, fullSet.Value()); !agility.HasValue())
        return agility;
    if (auto vitality = DrawTheVitality(window); !vitality.HasValue())
        return vitality;
    if (window.heroClass == CharacterClass::DarkLord)
    {
        if (auto command = DrawTheCommand(window); !command.HasValue())
            return command;
    }
    return DrawTheCloseButton(context);
}
} // namespace Client::Hud
