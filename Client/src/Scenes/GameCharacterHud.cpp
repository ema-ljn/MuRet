#include "Scenes/GameCharacterHud.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDamage.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/ServerListModel.h"
#include "UI/SprintfFormat.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

// 0x5ee2e0 (10.336 bytes) y el tramo de 0x5f7440 que le sigue, cotejados con el PE c32bbd5f: 06, "La ventana del
// personaje".
namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Color;
using Render::Text::FontKind;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};

// La esquina que le pasa 0x5f7440 (0x5f7494).
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::int32_t WindowY = 0;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
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

// Los colores de 0x6c8e5c (el texto) y 0x6c8e64 (el fondo).
// (−(word != 0) & 0x97d685) − 0x984021: naranja con algo sumado, celeste sin nada.
constexpr std::uint32_t AddedStatColor = 0xffff9664u;
constexpr std::uint32_t BaseStatColor = 0xff67bfdfu;
constexpr std::uint32_t NameBackground = 0xff141414u;
constexpr std::uint32_t LineBackground = 0x80000000u;
constexpr std::uint32_t LevelColor = 0xffe6e6e6u;
constexpr std::uint32_t LineColor = 0xffffffffu;
constexpr std::uint32_t PointsLineColor = 0xffff9664u;
constexpr std::uint32_t LevelUpBackground = 0xffc86e1eu;
constexpr std::uint32_t LevelUpColor = 0xff000000u;

// La clase del héroe (+0x1d4 & 7).
constexpr std::uint32_t WizardClass = 0;
constexpr std::uint32_t KnightClass = 1;
constexpr std::uint32_t GladiatorClass = 3;
constexpr std::uint32_t DarkLordClass = 4;

// Los bits de +0x78 del héroe que suman al ataque y a la defensa (*inferido*: un buff).
constexpr std::uint32_t AgilityBonusFlag = 0x400;
constexpr std::uint32_t LevelBonusFlag = 0x2000000;

// Las cuatro armaduras de la clase 3 que cuentan como set sin casco (0x5efae2..0x5efb03).
constexpr std::int32_t GladiatorSetArmors[] = {0x100f, 0x1014, 0x1017, 0x1020};

// Los sprintf de 0x5ee2e0 sobre un texto de Text.bmd (Q-C590).
using UI::Sprintf;

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Render::Models::LogicalAccess OwnAccess(GameHudContext& context)
{
    return Render::Models::LogicalAccess(context.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// 0x621b60(slot, x, y, w, h, 0, 0, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa.
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

// El FILD del mouse contra la caja en float: left ≤ x < left + size, con la suma en la x87.
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

// El ancho de 0x554180: (W · k) / 640, sin signo.
int BoxPixels(const GameHudContext& context, std::uint32_t designWidth)
{
    const auto screenWidth = static_cast<std::uint32_t>(context.sprites.Screen().width);
    return static_cast<int>(screenWidth * designWidth / 640u);
}

// El cx de GetTextExtentPoint32A en 0x467060: un texto vacío mide "0" (0x4670ba..0x4670d0).
int MeasuredWidth(GameHudContext& context, std::string_view text)
{
    return context.text.Measure(context.hud.dcFont, text.empty() ? std::string_view("0") : text).width;
}

Render::Text::TextStyle CurrentStyle(const GameHudContext& context)
{
    return Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors};
}

// 0x554180(x, y, texto, caja, 1, p6) → 0x467060 en el modo 2 (0x46711b..0x467132, Render::Text::CenterInBox):
// (caja − cx) / 2 con la división de C, la caja de cx + 2 · eso columnas desde x y el texto corrido eso; más ancho que
// la caja, se ve el centro. Vacío y sin caja, nada (0x467077..0x467082).
void DrawCentered(GameHudContext& context, int x, int y, std::string_view text, int box)
{
    const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, context.hud.dcFont, text, box);
    context.text.DrawColumns(x, y, text, CurrentStyle(context), -placed.textColumn, placed.width);
}

// 0x554180(x, y, texto, ancho, 0, 0) → 0x467060 en el modo 1 (0x467102..0x467119): con el ancho, la caja de ese
// ancho desde x; si el texto es más ancho, se ve desde la columna cx − ancho.
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

// La suma de un stat y lo que le agregan los ítems (add cx, ax): 16 bits que envuelven, y and 0xffff.
std::uint32_t StatPlus(std::uint16_t stat, std::uint16_t added)
{
    return static_cast<std::uint16_t>(stat + added);
}

// El tipo de un ítem con signo, como lo lee el original (movsx).
std::int32_t TypeOf(const World::ItemState& item)
{
    return static_cast<std::int16_t>(item.id);
}

// sprintf("%s", [0x7681eb0]): el nombre hasta su 0.
Core::Expected<std::string, std::string> NameOf(const OwnCharacterRecord& own)
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

// El byte `index` de las skills del registro propio (+0x6c + index): MuRet tiene los 64 de +0x6c a +0xab.
std::optional<std::uint8_t> OwnSkillAt(const OwnCharacterRecord& own, std::uint32_t index)
{
    if (index >= own.words6c.size() * 4)
        return std::nullopt;
    return static_cast<std::uint8_t>((own.words6c[index / 4] >> ((index % 4) * 8u)) & 0xffu);
}

// Lo que comparten los tramos de la ventana.
struct Window
{
    GameHudContext& context;
    const OwnCharacterRecord& own;
    const ActorState& hero;
    // local_28: la clase del héroe.
    std::uint32_t heroClass = 0;
    std::int32_t x = WindowX;
    std::int32_t y = WindowY;
};

// 0x5ee2e0..0x5ee7f0: el fondo, el nombre, la clase y el servidor, y los fondos de los stats.
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
    // Acceso 1: el nombre y la clase del registro.
    std::string name;
    std::string className;
    {
        const auto access = OwnAccess(context);
        auto own = NameOf(window.own);
        if (!own.HasValue())
            return Core::Unexpected(own.Error());
        // sprintf("%s", nombre) lo copia tal cual.
        name = own.Value();
        // 0x5ee48c..0x5ee4ab: el texto 20 + (+0xb >> 3) · 5 + (+0xb & 7).
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
    // 0x5ee629..0x5ee69c: [0x6d5ec4] es el lugar del grupo elegido y [0x6d5ecc] su número; el nombre del grupo, el de
    // 0x7d36bb0 + 0x21e · lugar, que puso 0x4c1860.
    if (!context.chosenServer)
        return Core::Unexpected(std::string("0x5ee629: sin el servidor elegido ([0x6d5ec4] y [0x6d5ecc] en −1)"));
    const std::uint16_t group = UI::GroupOf(*context.chosenServer);
    const std::optional<int> slot = UI::GroupSlot(group);
    if (!slot)
        return Core::Unexpected(std::format("0x5ee629: el grupo {} del servidor elegido no tiene lugar", group));
    const int number = UI::ServerNumber(*context.chosenServer);
    const bool nonPvp = UI::IsNonPvp(*slot, number);
    // 0x447c70: el 460 sin PvP y el 461 si no, como el título de la cuenta.
    auto serverFormat = TextOf(context, UI::LoginTitleText(*slot, number), nonPvp ? "0x5ee668" : "0x5ee696");
    if (!serverFormat.HasValue())
        return Core::Unexpected(serverFormat.Error());
    auto groupName = TextOf(context, UI::GroupNameText(group), "0x5ee660");
    if (!groupName.HasValue())
        return Core::Unexpected(groupName.Error());
    const std::string server = Sprintf(serverFormat.Value(), groupName.Value(), static_cast<std::int32_t>(number));
    // 0x5ee6a1..0x5ee6bf: a = (float)(sin(T · 0,001f) + 1), en la x87.
    const float alpha = F32(std::sin(static_cast<double>(context.worldClock) * static_cast<double>(0.001f)) + 1.0);
    sprites.SetColor(Color{1.0f, 1.0f, 1.0f, F32(2.0 - static_cast<double>(alpha))});
    DrawCentered(context, x + 0x16, y + 0x16, className, BoxPixels(context, 150));
    sprites.SetColor(Color{1.0f, 1.0f, 1.0f, alpha});
    DrawCentered(context, x + 0x16, y + 0x16, server, BoxPixels(context, 150));
    sprites.SetColor(White);
    // 0x5ee786..0x5ee7f0: [0x6c9330] botones y uno más de fondos, de 60 en 60.
    const std::int32_t backs = (window.heroClass == DarkLordClass ? 5 : 4) + 1;
    const float backLeft = F32(static_cast<double>(left) + 19.0 - 6.0);
    const float backTop = F32(static_cast<double>(top) + 60.0 - 4.0);
    for (std::int32_t i = 0; i < backs; ++i)
        Quad(sprites, 0xf5, backLeft, F32(static_cast<double>(i * 60) + static_cast<double>(backTop)), 75.0f, 21.0f,
             0.5859375f, 0.65625f);
    return {};
}

// 0x5ee7f0..0x5eee60: el nivel, la experiencia y el renglón de los puntos.
Core::Expected<void, std::string> DrawTheLevel(Window& window)
{
    GameHudContext& context = window.context;
    const OwnCharacterRecord& own = window.own;
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
        // Acceso 2.
        const auto access = OwnAccess(context);
        text = Sprintf(levelFormat.Value(), static_cast<std::uint32_t>(own.level));
    }
    SetColors(context, LevelColor, 0);
    DrawCentered(context, x + 0xe, y + 0x3c, text, BoxPixels(context, 82));
    SelectFont(context, FontKind::Normal);
    {
        // Acceso 3.
        const auto access = OwnAccess(context);
        text = Sprintf(experienceFormat.Value(), own.experience, own.nextExperience);
    }
    SetColors(context, LineColor, LineBackground);
    DrawFromLeft(context, x + 0x18, y + 0x50, text, BoxPixels(context, 145));
    {
        // Acceso 4: wsprintfA ([0x6b851c]) con '%s %d/%d | %s %d/%d' de .rdata, que no es el sprintf del CRT. Antes
        // del nivel 10, en 0; si no, +0x44 y +0x46 con signo y −+0x48 y −+0x4a sin signo.
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

// 0x5eee60..0x5efa0e: la fuerza, el daño y si el equipo es un set completo (el resultado).
Core::Expected<bool, std::string> DrawTheStrength(Window& window)
{
    GameHudContext& context = window.context;
    const OwnCharacterRecord& own = window.own;
    const ActorState& hero = window.hero;
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
        // Acceso 5: el color antes del sprintf.
        const auto access = OwnAccess(context);
        context.hud.textColors.text = StatColor(own.word2a);
        text = Sprintf(strengthFormat.Value(), StatPlus(own.strength, own.word2a));
    }
    context.hud.textColors.background = 0;
    DrawCentered(context, x + 0xe, y + 0x78, text, BoxPixels(context, 70));
    SelectFont(context, FontKind::Normal);
    {
        // Acceso 6: solo toma el puntero al lugar 0 (+0xb6c), que lee la magia.
        const auto access = OwnAccess(context);
    }
    const OwnAttackDamage attack = AttackDamageOf(own, window.heroClass, HeldItemOf(context.world.Runtime()),
                                                  context.ownRecordDepth, context.random);
    {
        // Acceso 7 (0x5ef450..0x5ef55f).
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
    // 0x5ef62a..0x5ef8ac: los lugares 2 a 6, un acceso cada uno, hasta el primero vacío o roto; la clase 3 puede no
    // tener casco.
    bool fullSet = true;
    constexpr std::size_t FirstArmorSlot = 2;
    constexpr std::size_t LastArmorSlot = 6;
    for (std::size_t slot = FirstArmorSlot; slot <= LastArmorSlot; ++slot)
    {
        const auto access = OwnAccess(context);
        const World::ItemState& item = own.equipment[slot];
        const bool empty = TypeOf(item) == -1;
        const bool helmOptional = window.heroClass == GladiatorClass && slot == FirstArmorSlot;
        if ((empty && !helmOptional) || (!empty && item.durability == 0))
        {
            fullSet = false;
            break;
        }
    }
    if (!fullSet)
        return false;
    {
        // Acceso 9 (0x5efa0e..0x5efc5f): los lugares 3 a 6 con el mismo tipo % 512 que el casco, o que la armadura
        // en la clase 3, que tiene que ser una de cuatro.
        const auto access = OwnAccess(context);
        std::int32_t reference = 0;
        bool compare = true;
        if (window.heroClass == GladiatorClass)
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

// 0x5efc5f..0x5f0236: la agilidad, la defensa y la velocidad.
Core::Expected<void, std::string> DrawTheAgility(Window& window, bool fullSet)
{
    GameHudContext& context = window.context;
    const OwnCharacterRecord& own = window.own;
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
        // Acceso 10.
        const auto access = OwnAccess(context);
        context.hud.textColors.text = StatColor(own.word2c);
        text = Sprintf(agilityFormat.Value(), StatPlus(own.agility, own.word2c));
    }
    context.hud.textColors.background = 0;
    DrawCentered(context, x + 0xe, y + 0xb4, text, BoxPixels(context, 70));
    SelectFont(context, FontKind::Normal);
    // Acceso 11, con los dos renglones adentro.
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
    // 0x5f006f..0x5f007f: la clase del registro (+0xb), no la del héroe; la 0 lee la de la magia.
    const std::uint32_t speed = (own.characterClass & 7u) == WizardClass ? own.magicSpeed : own.attackSpeed;
    text = Sprintf(speedFormat.Value(), speed);
    DrawFromLeft(context, x + 0x18, y + 0xd7, text, BoxPixels(context, 130));
    return {};
}

// 0x5f04b1..0x5f0633, con el registro abierto: la magia de las clases 0 y 3, con el acceso 13 adentro del 12.
Core::Expected<void, std::string> DrawTheMagic(Window& window)
{
    GameHudContext& context = window.context;
    const OwnCharacterRecord& own = window.own;
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const std::optional<std::uint8_t> skill = OwnSkillAt(own, window.hero.skillIndex);
    if (!skill)
        return Core::Unexpected(
            std::format("0x5f04cd: el byte +0x6c + {} del registro propio no está en MuRet", window.hero.skillIndex));
    // Un dibujo intermedio no deja las marcas de la lista: trabaja sobre una copia.
    ItemSetOptionList projected = world.ItemSets().list;
    ItemSetOptionList& setOptions = context.projecting ? projected : world.ItemSets().list;
    OwnMagicDamage damage;
    {
        // Acceso 13, adentro del 12: no saca nada al cerrarse.
        const auto access = OwnAccess(context);
        auto magic = MagicDamageOf(own, *skill, context.skills, runtime.movement.skillDepths[*skill], context.random,
                                   setOptions);
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
    // 0x5f054d..0x5f0571: con un báculo (0xa00 a 0xbff) o los tipos 0x1f, 0x17 y 0x15 en la derecha, el agregado de
    // su byte +0x16 por lo que queda de su durabilidad (0x54ee00 sobre el byte +0x2e de Item.bmd, no el +0x2d).
    const World::ItemState& right = own.equipment[0];
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
        const float factor =
            ItemDurabilityFactor(right.durability, std::to_integer<std::uint8_t>((*record)[MagicDurabilityByte]),
                                 right.scalar1Low, right.flags1b, right.flags1c);
        // 0x5f05d1..0x5f05dd: (f − r · f) · max, en la x87.
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

// 0x5f0236..0x5f0a6c: la vitalidad, la vida, la energía, el maná, la magia, el daño de la skill, los puntos para
// repartir y las letras, todo con el acceso 12 abierto.
Core::Expected<void, std::string> DrawTheVitality(Window& window)
{
    GameHudContext& context = window.context;
    const OwnCharacterRecord& own = window.own;
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
    if (window.heroClass == WizardClass || window.heroClass == GladiatorClass)
    {
        if (auto magic = DrawTheMagic(window); !magic.HasValue())
            return magic;
    }
    // 0x5f0633..0x5f06f2: el daño de la skill del Dark Knight y el Dark Lord por la energía; el de la clase 3, fijo y
    // un renglón más abajo.
    if (window.heroClass == KnightClass || window.heroClass == GladiatorClass || window.heroClass == DarkLordClass)
    {
        auto format = TextOf(context, SkillDamageText, "0x5f065a");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        std::uint32_t percent = 200;
        int line = y + 0x14f;
        if (window.heroClass == KnightClass)
            percent = energy / 10u + 200u;
        else if (window.heroClass == DarkLordClass)
            percent = energy / 20u + 200u;
        else
            line = y + 0x15e;
        text = Sprintf(format.Value(), percent);
        DrawFromLeft(context, x + 0x18, line, text, BoxPixels(context, 130));
    }
    if (own.levelUpPoints != 0)
    {
        // 0x5f070b..0x5f0884: los puntos para repartir y un botón por stat, apretado con el botón abajo encima.
        auto format = TextOf(context, PointsText, "0x5f0732");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        SelectFont(context, FontKind::Bold);
        text = Sprintf(format.Value(), static_cast<std::uint32_t>(own.levelUpPoints));
        SetColors(context, LevelUpColor, LevelUpBackground);
        DrawCentered(context, x + 0x5f, y + 0x32, text, BoxPixels(context, 80));
        const std::int32_t buttons = window.heroClass == DarkLordClass ? 5 : 4;
        const float left = F32(static_cast<double>(static_cast<float>(x)) + 125.0);
        for (std::int32_t i = 0; i < buttons; ++i)
        {
            const float top = F32(static_cast<double>(i * 60) + static_cast<double>(static_cast<float>(y)) + 115.0);
            const bool pressed = context.leftDown && MouseInSquare(context, left, top, 24.0f);
            Quad(context.sprites, pressed ? 0x121 : 0x120, left, top, 24.0f, 24.0f, 0.75f, 0.75f);
        }
    }
    // 0x5f088a..0x5f0935: las letras de las barras. La primera, por +0x44 · 100 / +0x46 con signo; la segunda divide
    // +0x48 · 100 por 1 (Q-C670), así que pasado 10 es siempre 40.
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

// 0x5f0a6c..0x5f0b29: el mando del Dark Lord, +0x20 más +0x36, con el acceso 14 cerrado antes del sprintf.
Core::Expected<void, std::string> DrawTheCommand(Window& window)
{
    GameHudContext& context = window.context;
    const OwnCharacterRecord& own = window.own;
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

// 0x5f74a3..0x5f759a: el botón de cerrar en (x + 25, y + 395) y su ayuda con el mouse encima.
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
    // 0x554340 deja el texto en blanco sobre un fondo transparente.
    UI::DrawHelp(context.sprites, context.text, Truncate(left), Truncate(top) - 13, help.Value());
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
    WorldState& world = context.world;
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    Window window{.context = context, .own = world.OwnCharacter(), .hero = hero, .heroClass = hero.characterClass & 7u};
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
    if (window.heroClass == DarkLordClass)
    {
        if (auto command = DrawTheCommand(window); !command.HasValue())
            return command;
    }
    return DrawTheCloseButton(context);
}
} // namespace Client::Scenes
