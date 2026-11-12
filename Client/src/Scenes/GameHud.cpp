#include "Scenes/GameHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatBubbleDraw.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/GameCharacterHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/HudItems.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/ChatLogDraw.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "UI/SlideBanner.h"
#include "UI/SprintfFormat.h"
#include "UI/TextField.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cmath>
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
using Render::BlendMode;
using Render::Color;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor3f(1; 0,5; 0,5) de la skill que no se puede usar (0x5a5860) y glColor3f(1; 0,9; 0,8) de su barra
// (0x5a9f4e) y de sus números (0x5a61ea).
constexpr Color SkillTint{1.0f, 0.5f, 0.5f, 1.0f};
constexpr Color SkillColor{1.0f, 0.9f, 0.8f, 1.0f};
// glColor3f(1; 0,95; 0,75): el AG y los números de la vida, el maná y las ranuras (0x5a8d2a, 0x5aa780).
constexpr Color NumberColor{1.0f, 0.95f, 0.75f, 1.0f};

// Los mapas y los textos de Text.bmd (por su índice: la dirección − 0x76bd1dc, sobre 300).
constexpr std::int32_t TarkanMap = 8;
constexpr std::int32_t CastleSiegeMap = 30;
constexpr std::int32_t CrywolfMap = 34;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
constexpr std::size_t ExperienceHelpText = 201;
constexpr std::size_t AgHelpText = 214;
constexpr std::size_t ArrowsText = 351;
constexpr std::size_t BoltsText = 352;
constexpr std::size_t DinorantText = 354;
constexpr std::size_t UniriaText = 355;
constexpr std::size_t SummonText = 356;
constexpr std::size_t LifeHelpText = 358;
constexpr std::size_t ManaHelpText = 359;
constexpr std::size_t PartyHelpText = 361;
constexpr std::size_t CharacterHelpText = 362;
constexpr std::size_t InventoryHelpText = 363;
constexpr std::size_t GuildHelpText = 364;
constexpr std::size_t CommandsHelpText = 939;
constexpr std::size_t DarkHorseText = 1187;
constexpr std::size_t FenrirText = 1916;

// Los modelos de las mascotas (el tipo de Item.bmd + 0x212): el ángel, el sátiro, la Uniria, el Dinorant, el
// caballo oscuro y Fenrir.
constexpr std::int16_t UniriaModel = 0x1c14;
constexpr std::int16_t DinorantModel = 0x1c15;
constexpr std::int16_t DarkHorseModel = 0x1c16;
constexpr std::int16_t FenrirModel = 0x1c37;

// Las flechas y los virotes (0x5ab810).
constexpr std::uint16_t ArrowsType = 0x80f;
constexpr std::uint16_t BoltsType = 0x807;
// El lugar 8 del equipo, el del ayudante.
constexpr std::size_t HelperSlot = 8;

// 1/256, el téxel de los íconos (0x6b8940).
constexpr double Texel = 0.00390625;

// 0x5a736b..0x5a7452: el freno de las macros, su texto (0x6c9310) en y = 0x188 y su barra en y = 404, de 50 × 2.
constexpr std::string_view MacroTimeText = "Macro Time";
constexpr int MacroTimeTextTop = 0x188;
constexpr float MacroTimeBarTop = 404.0f;
constexpr float MacroTimeBarWidth = 50.0f;
constexpr float MacroTimeBarHeight = 2.0f;
// 0x5aa9a0: la caja de la entrada del chat (los slots 0xf8 y 0xf9, dialogue1 y dialogue2), el indicador de F3 (el
// 0xef, dialogue2_1) y los nombres del susurro de los otros lugares, de 15 en 15 desde (0x178, 0x1a6).
constexpr std::uint16_t ChatBoxLeftSlot = 0xf8;
constexpr std::uint16_t ChatBoxRightSlot = 0xf9;
constexpr std::uint16_t WhisperModeSlot = 0xef;
constexpr int WhisperNamesLeft = 0x178;
constexpr int WhisperNamesTop = 0x1a6;
constexpr int WhisperNamesStep = 0xf;
// 0x5aaa77 y 0x5aaa7f: los colores que deja para los nombres.
constexpr Render::Text::TextColors WhisperNamesColors{.text = 0xffc8c8c8u, .background = 0x64000000u};
// 0x4691e0: el color del texto de los EDIT (la vtable + 0x40) y el fondo del del susurro (+ 0x44), con los bytes
// en el orden de TextColors.
constexpr Render::Text::TextColors MessageEditColors{.text = 0xffd2e6ffu, .background = 0};
constexpr Render::Text::TextColors WhisperEditColors{.text = 0xffc8c8c8u, .background = 0x00191919u};
// La esquina de los dos EDIT (0x45a9d0 en 0x4691e0).
constexpr int MessageEditLeft = 0xc1;
constexpr int WhisperEditLeft = 0x178;
constexpr int ChatEditTop = 0x1a6;

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

// _ftol de un float que no es infinito (las x y las y del HUD y el reloj del mundo).
std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

// Lo que comparten los hijos del HUD: el contexto, lo que leen de WorldState y el registro de los paneles, con el
// rand() del frame.
struct Frame
{
    GameHudContext& context;
    WorldRuntime& runtime;
    OwnCharacterRecord& own;
    ActorState& hero;
    UI::PanelRegistry& panels;
};

// Un acceso al registro propio ([0x7681eb4]): abre sin gastar y, si lo deja en 0, cierra con 3.848 rand().
Render::Models::LogicalAccess OwnAccess(const Frame& frame)
{
    return Render::Models::LogicalAccess(frame.context.ownRecordDepth, frame.context.random,
                                         Render::Models::OwnRecordCloseRandoms);
}

// Un acceso a la entrada `skill` de Skill.bmd (0x76bd1d8 + 0x3c · skill): 60 rand() al cerrarla en 0 (0x40f600).
Render::Models::LogicalAccess SkillAccess(const Frame& frame, std::uint32_t skill)
{
    return Render::Models::LogicalAccess(frame.runtime.movement.skillDepths[skill], frame.context.random,
                                         static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
}

Core::Expected<std::string_view, std::string> TextOf(const Frame& frame, std::size_t index, std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(frame.context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<std::span<const std::byte, Data::Tables::SkillRecordSize>, std::string>
SkillRecordOf(const Frame& frame, std::uint32_t skill, std::string_view where)
{
    if (frame.context.skills == nullptr)
        return Core::Unexpected(std::format("{}: sin Skill.bmd", where));
    const auto record = frame.context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    return *record;
}

Core::Expected<std::span<const std::byte, Data::Tables::ItemRecordSize>, std::string>
ItemRecordOf(const Frame& frame, std::int32_t type, std::string_view where)
{
    if (frame.context.items == nullptr)
        return Core::Unexpected(std::format("{}: sin Item.bmd", where));
    const auto record = type < 0 ? std::nullopt : frame.context.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("{}: Item.bmd no tiene el tipo {}", where, type));
    return *record;
}

std::uint8_t ByteAt(std::span<const std::byte, Data::Tables::SkillRecordSize> record, std::size_t at)
{
    return std::to_integer<std::uint8_t>(record[at]);
}

std::uint32_t LittleEndianAt(std::span<const std::byte, Data::Tables::SkillRecordSize> record, std::size_t at,
                             std::size_t bytes)
{
    std::uint32_t value = 0;
    for (std::size_t i = bytes; i-- > 0;)
        value = (value << 8u) | ByteAt(record, at + i);
    return value;
}

// El byte `index` de las skills del registro propio (+0x6c + index): MuRet tiene las 64 de +0x6c a +0xab.
std::optional<std::uint8_t> OwnSkillAt(const OwnCharacterRecord& own, std::uint32_t index)
{
    if (index >= own.words6c.size() * 4)
        return std::nullopt;
    return static_cast<std::uint8_t>((own.words6c[index / 4] >> ((index % 4) * 8u)) & 0xffu);
}

// 0x465950 con la fuente normal (0x5763554) y los colores de 0x6c8e5c y 0x6c8e64: el modo 1, que empieza en x, o
// el 4, que termina en x (0x467060, Render::Text::EndingAt).
void DrawHudText(Frame& frame, int x, int y, std::string_view text, bool endingAtX)
{
    const Render::Text::TextStyle style{.font = Render::Text::FontKind::Normal, .colors = frame.context.hud.textColors};
    int at = x;
    if (endingAtX)
        at = Render::Text::EndingAt(x, frame.context.text.Measure(Render::Text::FontKind::Normal, text),
                                    frame.context.sprites.Screen());
    frame.context.text.Draw(at, y, text, style);
}

// 0x554340 deja el texto en blanco sobre un fondo transparente (0x6c8e5c en −1 y 0x6c8e64 en 0).
void DrawHelp(Frame& frame, int x, int y, std::string_view help)
{
    UI::DrawHelp(frame.context.sprites, frame.context.text, x, y, help);
    frame.context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
}

bool MouseIn(const Frame& frame, int left, int top, int right, int bottom)
{
    const UI::Point mouse = frame.context.mouse;
    return left <= mouse.x && mouse.x < right && top <= mouse.y && mouse.y < bottom;
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa.
void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float u,
          float v, float du, float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = u,
                                      .v = v,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

// El índice de 0x5763338, la resolución del registro (0 a 4: 640, 800, 1024, 1280 y 1600 de ancho). MuRet no lo
// lee del registro: toma la mayor de las cinco que entra en el ancho de la pantalla (08, decisión de la CL8).
int ResolutionIndex(App::PixelSize screen)
{
    constexpr std::array<int, 4> Widths{800, 1024, 1280, 1600};
    int index = 0;
    for (const int width : Widths)
    {
        if (screen.width >= width)
            ++index;
    }
    return index;
}

// ---------------------------------------------------------------------------------------------------------------
// 1. Las arenas (0x5a7520).

// 0x622000(slot, 0, 0, 640, 435, ((int) T % 100000) · velocidad, 0, du, dv): el fild del resto por la velocidad en
// float, guardado en float.
void DrawSand(Frame& frame, std::uint16_t slot, float speed, float du, float dv)
{
    const std::int32_t remainder = Truncate(frame.context.worldClock) % 100000;
    frame.context.sprites.SkewedQuad(
        Render::OriginalSkewedQuad{.slot = Render::TextureSlot{slot},
                                   .x = 0.0f,
                                   .y = 0.0f,
                                   .width = 640.0f,
                                   .height = 435.0f,
                                   .u = F32(static_cast<double>(remainder) * static_cast<double>(speed)),
                                   .v = 0.0f,
                                   .du = du,
                                   .dv = dv});
}

constexpr std::uint16_t SlowSand = 0x5c2;
constexpr std::uint16_t FastSand = 0x5c3;

// 0x435930 y 0x42b100: el humo de Crywolf y del asedio, con su color: primero la rápida con la mezcla normal y
// después la lenta, aditiva.
void DrawSmoke(Frame& frame, Color color)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(color);
    DrawSand(frame, FastSand, 0.0005f, 3.0f, 2.0f);
    sprites.SetBlend(BlendMode::Additive);
    DrawSand(frame, SlowSand, 0.0002f, 0.3f, 0.3f);
}

void DrawTheSands(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const std::int32_t map = frame.runtime.map;
    if (map == TarkanMap)
    {
        // 0x5a7539..0x5a7604: el blanco con alfa 0,5 de la mezcla normal no dibuja nada; la aditiva, las dos.
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(Color{1.0f, 1.0f, 1.0f, 0.5f});
        sprites.SetBlend(BlendMode::Additive);
        sprites.SetColor(Color{0.3f, 0.3f, 0.25f, 1.0f});
        DrawSand(frame, SlowSand, 0.0002f, 0.3f, 0.3f);
        DrawSand(frame, FastSand, 0.001f, 3.0f, 2.0f);
    }
    else if (map == CrywolfMap)
    {
        // 0x5a760e: una sola vez por 0x576353c, que lo apaga.
        if (frame.context.crywolfSandOverlay == 1)
        {
            DrawSmoke(frame, Color{0.4f, 0.4f, 0.45f, 1.0f});
            frame.context.crywolfSandOverlay = 0;
        }
    }
    else if (map == CastleSiegeMap && frame.runtime.siegeActive)
    {
        // 0x42b100, con 0x4294b0.
        DrawSmoke(frame, Color{0.3f, 0.3f, 0.25f, 1.0f});
    }
    sprites.SetColor(White);
}

// ---------------------------------------------------------------------------------------------------------------
// 6. Los íconos de estado (0x42a660 y 0x5a63d0).

// 0x5a63d0(ícono, x, y, w, h): el ícono en su atlas de 8 columnas de 32 × 36 téxeles: los de más de 0xca en
// StatusIcon (0x68e), los de 0x78 a 0x7c en command (300), los de más de 0x38 en Skill2 (299) y los demás en Skill
// (0x12a), desde 1.
void DrawStatusIcon(Frame& frame, std::int32_t icon, int x, int y, int width, int height)
{
    std::uint16_t slot = 0x12a;
    std::int32_t index = icon - 1;
    if (icon > 0xca)
    {
        slot = 0x68e;
        index = icon - 0xcb;
    }
    else if (icon >= 0x78 && icon <= 0x7c)
    {
        slot = 300;
        index = icon - 0x78;
    }
    else if (icon > 0x38)
    {
        slot = 299;
        index = icon - 0x39;
    }
    Quad(frame.context.sprites, slot, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
         static_cast<float>(height), F32(static_cast<double>((index % 8) * 32) * Texel),
         F32(static_cast<double>((index / 8) * 36) * Texel), 0.125f, 0.13671875f);
}

// El nombre de un registro de Skill.bmd como lo copia strcpy (0x551e0c): hasta su '\0', dentro del registro.
std::string SkillRecordName(std::span<const std::byte, Data::Tables::SkillRecordSize> record)
{
    std::string name;
    for (const std::byte value : record)
    {
        if (value == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return name;
}

// 0x5a6600(x, y, ícono), desde 0x42a735 con el mouse sobre el ícono: su ayuda en la caja de los tooltips (0x5af090,
// con el modo 2 y el marco). Con el registro propio abierto alrededor, el nombre de la entrada `icon` de Skill.bmd
// (0x551c70, con el acceso de la entrada: 60 rand() al cerrarla en 0) va en azul y negrita en el renglón 1, entre dos
// "\n", y el texto del ícono (la tabla de 0x5a69fc), en blanco en el 3; el 0xcc lleva tres, del 3 al 5. No limpia
// los renglones: un ícono sin texto deja el 3 que había, y el 0xcc, los colores y las negritas del 3 y del 4
// (Q-C597).
Core::Expected<void, std::string> DrawTheStatusHelp(Frame& frame, std::int32_t x, std::int32_t y, std::int32_t icon)
{
    struct HelpText
    {
        std::int32_t icon = 0;
        std::size_t text = 0;
    };
    constexpr std::array<HelpText, 6> Texts{
        {{0x40, 1466}, {0x43, 1468}, {0x45, 1469}, {0x46, 1470}, {0x4b, 1467}, {0xcb, 1471}}};
    // 0x5a6855..0x5a6888: los tres del 0xcc.
    constexpr std::int32_t ThreeLinesIcon = 0xcc;
    constexpr std::array<std::size_t, 3> ThreeLines{1638, 1639, 1472};
    TooltipLines& lines = frame.context.hud.tooltip;
    std::size_t last = 3;
    {
        const auto own = OwnAccess(frame);
        const auto skill = static_cast<std::uint32_t>(icon);
        auto record = SkillRecordOf(frame, skill, "0x551c70");
        if (!record.HasValue())
            return Core::Unexpected(record.Error());
        std::string name;
        {
            const auto entry = SkillAccess(frame, skill);
            name = SkillRecordName(record.Value());
        }
        lines.text[0] = "\n";
        lines.text[1] = UI::Sprintf("%s", std::string_view(name));
        lines.colors[1] = TooltipBlue;
        lines.bold[1] = 1;
        lines.text[2] = "\n";
        const auto write = [&](std::size_t line, std::size_t index) -> Core::Expected<void, std::string>
        {
            const auto text = TextOf(frame, index, "0x5a6888");
            if (!text.HasValue())
                return Core::Unexpected(text.Error());
            lines.text[line] = UI::Sprintf(text.Value());
            return {};
        };
        if (icon == ThreeLinesIcon)
        {
            for (std::size_t i = 0; i < ThreeLines.size(); ++i)
            {
                if (auto written = write(3 + i, ThreeLines[i]); !written.HasValue())
                    return written;
            }
            last = 5;
        }
        for (const HelpText& help : Texts)
        {
            if (help.icon != icon)
                continue;
            if (auto written = write(3, help.text); !written.HasValue())
                return written;
        }
        lines.colors[last] = TooltipWhite;
        lines.bold[last] = 0;
        lines.text[last + 1] = "\n";
    }
    return DrawTheTooltipBox(frame.context, x, y, static_cast<std::int32_t>(last + 2), 0, 2, 1);
}

Core::Expected<void, std::string> DrawTheStatusIcons(Frame& frame)
{
    struct StatusIcon
    {
        std::uint32_t bit = 0;
        std::int32_t icon = 0;
    };
    constexpr std::array<StatusIcon, 8> Icons{{{0x200, 0x4b},
                                               {0x400, 0x35},
                                               {0x800, 0x40},
                                               {0x1000, 0x43},
                                               {0x2000, 0x45},
                                               {0x4000, 0x46},
                                               {0x8000, 0xcb},
                                               {0x10000, 0xcc}}};
    std::uint8_t shown = 0;
    for (const StatusIcon& icon : Icons)
    {
        if ((frame.hero.flags78 & icon.bit) != icon.bit)
            continue;
        const int x = shown * 25 + 180;
        DrawStatusIcon(frame, icon.icon, x, 1, 20, 24);
        const UI::Point mouse = frame.context.mouse;
        if (x < mouse.x && mouse.x < x + 20 && 1 < mouse.y && mouse.y < 25)
        {
            if (auto help = DrawTheStatusHelp(frame, x + 10, 0x1d, icon.icon); !help.HasValue())
                return help;
        }
        shown = static_cast<std::uint8_t>(shown + 1);
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 7. Las flechas y los virotes (0x5ab810).

// Lo que deja la party a la derecha: con alguien en la party, 10 con su ventana abierta o 60.
float PartyOffset(Frame& frame)
{
    if (frame.context.hud.partyCount <= 0)
        return 0.0f;
    return frame.panels.IsOpen(UI::PartyPanel) ? 10.0f : 60.0f;
}

// Un texto de las flechas: con la mezcla normal, en (x, 4) con el modo 1, y su tamaño en 0x77f31b8 y 0x77f31bc
// (GetTextExtentPoint pasado a 640 × 480 con _ftol). Un texto vacío no se dibuja ni se mide.
void DrawAmmoText(Frame& frame, float x, std::string_view text)
{
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    if (text.empty())
        return;
    DrawHudText(frame, Truncate(x), 4, text, false);
    const Render::Text::TextExtent extent = frame.context.text.Measure(Render::Text::FontKind::Normal, text);
    frame.context.hud.arrowsTextWidth = Render::Text::DesignX(extent.width, frame.context.sprites.Screen());
    frame.context.hud.arrowsTextHeight = Render::Text::DesignY(extent.height, frame.context.sprites.Screen());
}

// 10. 0x5a736b..0x5a7452: con el freno de las macros (0x77f3b74), "Macro Time" centrado en el ancho del mundo
// (0x5bb8d0, con sus consultas) y la barra (0x5a6af0) con el freno / 2, en verde y dentro de la pantalla. El texto
// deja su tamaño en 0x77f31b8 y 0x77f31bc.
void DrawTheMacroTime(Frame& frame)
{
    HudState& hud = frame.context.hud;
    const std::int32_t time = hud.macroTime;
    if (time <= 0)
        return;
    const std::int32_t worldWidth = frame.panels.WorldWidth();
    const float x = F32((static_cast<double>(worldWidth) - 50.0) * 0.5);
    const std::int32_t fill = time * 50 / 100;
    frame.context.sprites.SetBlend(BlendMode::Alpha);
    DrawHudText(frame, Truncate(x), MacroTimeTextTop, MacroTimeText, false);
    const Render::Text::TextExtent extent = frame.context.text.Measure(Render::Text::FontKind::Normal, MacroTimeText);
    hud.arrowsTextWidth = Render::Text::DesignX(extent.width, frame.context.sprites.Screen());
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, frame.context.sprites.Screen());
    UI::DrawGauge(frame.context.sprites, x, MacroTimeBarTop, MacroTimeBarWidth, MacroTimeBarHeight,
                  static_cast<float>(fill), false, true);
}

// 0x468320 de un EDIT del chat a la vista: lo que tiene, con la fuente normal y sus colores, en su esquina. El
// original compone la ventana del EDIT en una textura (con el cursor); MuRet dibuja el texto, como el campo del login
// (sin el cursor).
void DrawTheChatEdit(Frame& frame, const UI::TextField& field, int left, Render::Text::TextColors colors)
{
    const std::string_view value = field.Value();
    if (value.empty())
        return;
    frame.context.text.Draw(left, ChatEditTop, value,
                            Render::Text::TextStyle{.font = Render::Text::FontKind::Normal, .colors = colors});
}

// 22. 0x5aa9a0: con el campo del chat abierto (o 0x77f3b69), la caja sin mezcla, los dos EDIT (0x469350), con el
// campo del susurro elegido los nombres de los otros lugares (cada uno deja su tamaño en 0x77f31b8 y 0x77f31bc,
// también los vacíos), y con el modo del susurro (F3) su indicador.
void DrawTheChatInput(Frame& frame)
{
    GameHudContext& context = frame.context;
    HudState& hud = context.hud;
    const UI::ChatInputState& input = frame.runtime.chat.input;
    context.sprites.SetColor(White);
    hud.dcFont = Render::Text::FontKind::Normal;
    if (!frame.runtime.ui.chatOpen && !input.otherInput)
        return;
    context.sprites.SetBlend(BlendMode::Opaque);
    Quad(context.sprites, ChatBoxLeftSlot, 186.0f, 415.0f, 134.0f, 29.0f, 0.0f, 0.0f, 0.51953125f, 0.90625f);
    Quad(context.sprites, ChatBoxRightSlot, 320.0f, 415.0f, 134.0f, 29.0f, 0.0f, 0.0f, 0.51953125f, 0.90625f);
    context.sprites.SetBlend(BlendMode::Alpha);
    if (input.shown)
    {
        DrawTheChatEdit(frame, input.message, MessageEditLeft, MessageEditColors);
        DrawTheChatEdit(frame, input.whisper, WhisperEditLeft, WhisperEditColors);
    }
    hud.textColors = WhisperNamesColors;
    if (input.field % 2 == UI::WhisperField)
    {
        for (std::int32_t slot = 0; slot < static_cast<std::int32_t>(UI::WhisperNameSlots); ++slot)
        {
            if (slot == input.whisperNameAt)
                continue;
            const std::string_view name = UI::CStringOf(input.whisperNames[static_cast<std::size_t>(slot)]);
            if (!name.empty())
                DrawHudText(frame, WhisperNamesLeft, (slot - input.whisperNameAt) * WhisperNamesStep + WhisperNamesTop,
                            name, false);
            const Render::Text::TextExtent extent = context.text.Measure(Render::Text::FontKind::Normal, name);
            hud.arrowsTextWidth = Render::Text::DesignX(extent.width, context.sprites.Screen());
            hud.arrowsTextHeight = Render::Text::DesignY(extent.height, context.sprites.Screen());
        }
    }
    if (input.whisperMode)
        Quad(context.sprites, WhisperModeSlot, 432.0f, 420.0f, 15.0f, 16.0f, 0.0f, 0.0f, 0.9375f, 1.0f);
}

Core::Expected<bool, std::string> DrawTheArrows(Frame& frame)
{
    const float partyOffset = PartyOffset(frame);
    constexpr std::array<int, 5> ResolutionOffsets{0x5a, 0x4b, 0x41, 0x3a, 10};
    const int resolutionOffset =
        ResolutionOffsets[static_cast<std::size_t>(ResolutionIndex(frame.context.sprites.Screen()))];
    bool shown = false;
    const auto own = OwnAccess(frame);
    const std::span<const World::ItemState, InventoryCellCount> cells(frame.context.world.Inventory().cells);
    struct Ammo
    {
        std::size_t slot = 0;
        std::uint16_t type = 0;
        std::size_t text = 0;
        bool countsTheInventory = false;
    };
    // 0x5ab96c: las flechas en la mano derecha muestran también las del inventario; 0x5abab3: los virotes en la
    // izquierda, solo con los suyos (la cuenta del inventario corre igual).
    constexpr std::array<Ammo, 2> Kinds{{{0, ArrowsType, ArrowsText, true}, {1, BoltsType, BoltsText, false}}};
    for (const Ammo& ammo : Kinds)
    {
        const World::ItemState& item = frame.own.equipment[ammo.slot];
        if (item.id != ammo.type)
            continue;
        const std::int32_t worldWidth = frame.panels.WorldWidth();
        const std::int32_t count = item.durability;
        const float x = F32(static_cast<double>(worldWidth) - static_cast<double>(partyOffset) -
                            static_cast<double>(resolutionOffset));
        // 0x557d80, con su propio acceso al registro, anidado.
        std::int32_t total = 0;
        {
            const auto nested = OwnAccess(frame);
            total = CountTheAmmo(frame.own.characterClass, frame.own.equipment[0].id, frame.own.equipment[1].id, cells);
        }
        if (count <= 0 && (!ammo.countsTheInventory || total <= 0))
            continue;
        auto format = TextOf(frame, ammo.text, "0x5ab9be");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        const std::string first = std::to_string(count);
        const std::string second = std::to_string(total);
        const std::array<std::string_view, 2> arguments{first, second};
        DrawAmmoText(frame, x, UI::PlaceArguments(format.Value(), arguments));
        shown = true;
    }
    return shown;
}

// ---------------------------------------------------------------------------------------------------------------
// 8. El ayudante y el invocado (0x5ab210).

Core::Expected<std::int32_t, std::string> DrawTheHelper(Frame& frame, bool arrowsShown)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    float partyOffset = 0.0f;
    std::int32_t top = 15;
    if (frame.context.hud.partyCount > 0 && !frame.panels.IsOpen(UI::PartyPanel))
        partyOffset = 50.0f;
    const auto pet = static_cast<std::int16_t>(frame.hero.pet.model);
    if ((pet > 0x1c11 && pet < 0x1c17) || pet == FenrirModel)
    {
        const std::int32_t worldWidth = frame.panels.WorldWidth();
        const float x = F32(static_cast<double>(worldWidth - 50) - static_cast<double>(partyOffset) - 15.0);
        const float y = arrowsShown ? 24.0f : 4.0f;
        std::uint32_t fill = 0;
        {
            const auto own = OwnAccess(frame);
            fill = (static_cast<std::uint32_t>(frame.own.equipment[HelperSlot].durability) * 50u) / 255u;
        }
        sprites.SetBlend(BlendMode::Alpha);
        std::string name;
        std::optional<std::size_t> text;
        if (pet == UniriaModel)
            text = UniriaText;
        else if (pet == DinorantModel)
            text = DinorantText;
        else if (pet == DarkHorseModel)
            text = DarkHorseText;
        else if (pet == FenrirModel)
            text = FenrirText;
        if (text)
        {
            auto found = TextOf(frame, *text, "0x5ab2f0");
            if (!found.HasValue())
                return Core::Unexpected(found.Error());
            name = std::string(found.Value());
        }
        else
        {
            auto record =
                ItemRecordOf(frame, pet - static_cast<std::int32_t>(Data::Models::FirstItemModel), "0x5ab2f0");
            if (!record.HasValue())
                return Core::Unexpected(record.Error());
            name = ItemRecordName(record.Value());
        }
        if (!name.empty())
            DrawHudText(frame, Truncate(x) + 50, Truncate(y), name, true);
        UI::DrawGauge(sprites, x, F32(static_cast<double>(y) + 12.0), 50.0f, 2.0f, static_cast<float>(fill), false,
                      true);
        top = Truncate(y) + 0x17;
    }
    const std::int32_t summonLife = frame.context.hud.summonLife;
    if (summonLife != 0)
    {
        const std::int32_t worldWidth = frame.panels.WorldWidth();
        const std::int32_t fill = summonLife * 50 / 100;
        const float x = F32(static_cast<double>(worldWidth - 50) - 150.0);
        sprites.SetBlend(BlendMode::Alpha);
        auto text = TextOf(frame, SummonText, "0x5ab5a6");
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        if (!text.Value().empty())
            DrawHudText(frame, Truncate(x) + 50, 4, text.Value(), true);
        UI::DrawGauge(sprites, x, 16.0f, 50.0f, 2.0f, static_cast<float>(fill), false, true);
    }
    return top;
}

// ---------------------------------------------------------------------------------------------------------------
// 9. Los avisos de durabilidad (0x5aac20).

bool ShowsNoDurability(std::int16_t type)
{
    return type == static_cast<std::int16_t>(BoltsType) || type == static_cast<std::int16_t>(ArrowsType) ||
           (type >= 0x1a00 && type <= 0x1a03) || type == 0x1a25;
}

Core::Expected<void, std::string> DrawTheDurability(Frame& frame, std::int32_t top)
{
    if (frame.panels.IsOpen(UI::TradePanel))
        return {};
    HudState& hud = frame.context.hud;
    Render::SpriteBatch& sprites = frame.context.sprites;
    const float partyOffset = PartyOffset(frame);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    const std::int32_t worldWidth = frame.panels.WorldWidth();
    const float x = F32(static_cast<double>(worldWidth) - static_cast<double>(partyOffset));
    const auto y = static_cast<float>(top);
    float offset = 0.0f;
    for (std::size_t slot = 0; slot < OwnEquipmentSlotCount; ++slot)
    {
        hud.textColors.background = 0x80000000u;
        const auto own = OwnAccess(frame);
        World::ItemState& item = frame.own.equipment[slot];
        if (item.id == EmptyOwnItemId)
            continue;
        const auto type = static_cast<std::int16_t>(item.id);
        std::optional<std::uint8_t> warning;
        if (!ShowsNoDurability(type))
        {
            auto record = ItemRecordOf(frame, type, "0x5aaf2a");
            if (!record.HasValue())
                return Core::Unexpected(record.Error());
            std::uint16_t maximum =
                MaxDurability(item, record.Value(), static_cast<int>((item.scalar1Low >> 3) & 0xfu));
            if (item.id == 0x1a0a)
                maximum = 200;
            const std::uint8_t durability = item.durability;
            bool shown = true;
            if (durability == 0)
            {
                warning = std::uint8_t{8};
                hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0x800a0affu};
            }
            else
            {
                // 0x5aaf6d..0x5aafce: fild del máximo, guardado en float, por 0,2, 0,3 y 0,5 en float, contra la
                // durabilidad en la x87.
                const auto current = static_cast<double>(durability);
                const auto limit = static_cast<double>(static_cast<float>(maximum));
                if (current <= limit * static_cast<double>(0.2f))
                {
                    warning = std::uint8_t{7};
                    hud.textColors.text = 0xff0a0affu;
                }
                else if (current <= limit * static_cast<double>(0.3f))
                {
                    warning = std::uint8_t{6};
                    hud.textColors.text = 0xff009cffu;
                }
                else if (current <= limit * static_cast<double>(0.5f))
                {
                    warning = std::uint8_t{5};
                    hud.textColors.text = 0xff00e4ffu;
                }
                else
                    shown = false;
            }
            if (shown)
            {
                // 0x6c9304: "%s (%d/%d)", el nombre de Item.bmd, la durabilidad y el máximo.
                const std::string text = std::format("{} ({}/{})", ItemRecordName(record.Value()), durability, maximum);
                hud.durabilityWarning = 1;
                DrawHudText(frame, Truncate(x), Truncate(y) + Truncate(offset), text, true);
                offset = static_cast<float>(Truncate(offset) + 12);
            }
        }
        // 0x5ab04e: sin los requisitos, el aviso 3, también en las flechas y los ayudantes.
        if (static_cast<std::uint16_t>(frame.own.agility * 2) < item.word20 ||
            static_cast<std::uint16_t>(frame.own.energy * 2) < item.word22 ||
            static_cast<std::uint16_t>(frame.own.word2a + frame.own.strength) < item.word1e)
            warning = std::uint8_t{3};
        if (warning && !frame.context.projecting)
            item.warning = *warning;
    }
    hud.textColors.background = 0x80000000u;
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 11. El objeto del evento ([0x723c60]): 0x40bab0 y su método +4.

Core::Expected<void, std::string> DrawTheEventObject(Frame& frame)
{
    if (!frame.runtime.eventObject)
        return {};
    const EventMapObject& event = *frame.runtime.eventObject;
    const std::int32_t map = frame.runtime.map;
    // 0x40bab0: la cuenta de 30 segundos con +8 de 1 a 14 (GetTickCount contra +0xc y los textos de la entrada).
    if (event.field08 >= 1 && event.field08 <= 14)
        return Core::Unexpected(std::format("0x40bab0: la cuenta del evento ({}) es de la CL9.5", event.field08));
    // El método +4: el de Blood Castle (0x40c2d0) con +0x10 en los mapas 11 a 17, el de Chaos Castle (0x405090) con
    // +0x10 en los 18 a 23 y el de Devil Square (0x40bdb0), un ret. Los dos campos los escriben los paquetes de los
    // eventos: hasta la CL9.5 quedan en 0 y no dibujan nada.
    const bool bloodCastle = event.kind == EventMapKind::BloodCastle && map > 10 && map < 0x12;
    const bool chaosCastle = event.kind == EventMapKind::ChaosCastle && map > 0x11 && map < 0x18;
    if (event.field10 != 0 && (bloodCastle || chaosCastle))
        return Core::Unexpected(std::format("{}: el tiempo y los puntos del evento son de la CL9.5",
                                            bloodCastle ? "0x40c2d0" : "0x405090"));
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 18. Las ventanas (0x5f7440).

Core::Expected<void, std::string> DrawTheWindows(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    UI::PanelRegistry& panels = frame.panels;
    const UI::PanelState& state = frame.runtime.panels;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Opaque);
    frame.context.hud.textColors.text = 0xffff8080u;
    const auto cut = [](std::string_view what) { return Core::Unexpected(std::string(what)); };
    if (panels.IsOpen(UI::PartyPanel) || frame.context.hud.partyCount != 0)
        return cut("0x5f0be0: la ventana de la party es de la CL9.4");
    // 0x5f7487: 0x5ee2e0 y el botón de cerrar, con la ventana del personaje abierta.
    if (panels.IsOpen(UI::CharacterPanel))
    {
        if (auto character = DrawTheCharacterWindow(frame.context); !character.HasValue())
            return character;
    }
    // 0x5f2340, que empieza con su IsOpen.
    if (auto inventory = DrawTheInventory(frame.context, panels); !inventory.HasValue())
        return inventory;
    if (panels.IsOpen(UI::TradePanel))
        return cut("0x5f3b40: el comercio es de la CL9.4");
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return cut("0x5f3030: las tiendas personales son de la CL9.3");
    if (panels.IsOpen(UI::NpcShopPanel))
        return cut("0x5f4720: la tienda del NPC es de la CL9.3");
    if (panels.IsOpen(UI::ChaosBoxPanel))
        return cut("0x5febf0: la caja del caos es de la CL9.3");
    if (state.delegated.trainer != 0 && state.delegated.trainer != 4)
        return cut("0x44b510: la ventana del entrenador es de la CL9.3");
    if (state.delegated.siegeWeapon != 0)
        return cut("0x44ea60: el arma del asedio es de la CL9.5");
    if (state.delegated.siegeGate != 0)
        return cut("0x44ff00: la puerta del asedio es de la CL9.5");
    if (panels.IsOpen(UI::WarehousePanel))
        return cut("0x5f4e30: el almacén es de la CL9.3");
    if (panels.IsOpen(UI::EventNpcPanel))
        return cut("0x5f58f0: la ventana del NPC de evento es de la CL9.5");
    if (state.field78abb00 != 0)
        return cut("0x5f6550: la ventana de 0x78abb00 es de la CL9.5");
    if (state.delegated.quest == 1 || state.delegated.quest == 2)
        return cut("0x41ca40: la ventana de las misiones es de la CL9.1");
    if (state.delegated.pet == 2)
        return cut("0x424160: la ventana de la mascota es de la CL9.2");
    if (panels.IsOpen(UI::Panel2000))
        return cut("0x5f7180: la ventana 0x2000 es del hito 4");
    if (state.delegated.commands != 0)
        return cut("0x40a650: la ventana de los comandos es de la CL9.4");
    if (panels.IsOpen(UI::SecondGuildPanel) || panels.IsOpen(UI::GuildPanel) || panels.IsOpen(UI::CastleSiegePanel) ||
        panels.IsOpen(UI::CastleSiegeListPanel) || panels.IsOpen(UI::SecondCastleSiegePanel))
        return cut("0x6579e0: las ventanas del gremio y del asedio son de la CL9.4 y la CL9.5");
    if (state.delegated.panelDialog != 0)
        return cut("0x660830: el diálogo de los paneles es del hito 6");
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 20. La barra de abajo (0x5a9290).

void DrawTheBottomBar(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    frame.context.hud.durabilityWarning = 0;
    frame.context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    Quad(sprites, 0xe9, 0.0f, 387.0f, 108.0f, 45.0f, 0.0f, 0.0f, 0.84375f, 0.703125f);
    Quad(sprites, 0xe9, 532.0f, 387.0f, 108.0f, 45.0f, 0.84375f, 0.0f, -0.84375f, 0.703125f);
    sprites.SetBlend(BlendMode::Opaque);
    Quad(sprites, 0xe6, 0.0f, 432.0f, 256.0f, 48.0f, 0.0f, 0.0f, 1.0f, 0.75f);
    {
        // 0x5a93b5..0x5a9455: abre y cierra el registro propio sin leer nada que cambie el dibujo.
        const auto own = OwnAccess(frame);
    }
    Quad(sprites, 0xe7, 256.0f, 432.0f, 128.0f, 48.0f, 0.0f, 0.0f, 1.0f, 0.75f);
    Quad(sprites, 0xe8, 384.0f, 432.0f, 256.0f, 48.0f, 0.0f, 0.0f, 1.0f, 0.75f);
    sprites.SetBlend(BlendMode::Opaque);
}

// ---------------------------------------------------------------------------------------------------------------
// 23. La experiencia (0x5abcd0).

Core::Expected<void, std::string> DrawTheExperience(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto own = OwnAccess(frame);
    const OwnCharacterRecord& record = frame.own;
    if (record.level != 0)
    {
        // 0x5abe3b..0x5abe74: la experiencia del nivel, en 32 bits.
        const std::uint32_t steps = static_cast<std::uint32_t>(record.level) - 1u;
        std::uint32_t base = 0;
        if (static_cast<std::int32_t>(steps) > 0)
        {
            base = (steps + 9u) * steps * steps * 10u;
            if (steps > 0xffu)
            {
                const std::uint32_t more = steps - 0xffu;
                base += (more + 9u) * more * more * 1000u;
            }
        }
        // 0x5abe77..0x5abebd: fild qword de los dos restos sin signo; lo hecho, 0 si la experiencia no llega a la base.
        const auto needed = static_cast<double>(record.nextExperience - base);
        const double done = record.experience < base ? 0.0 : static_cast<double>(record.experience - base);
        float fill = 0.0f;
        if (done > 0.0)
            fill = F32(done / needed * 10.0);
        // 0x5abef0..0x5abf1d: le resta 1 en la x87 mientras pase de 1. Infinito (sin experiencia para el nivel que
        // sigue) no termina.
        double fraction = fill;
        if (fill > 1.0f)
        {
            if (std::isinf(fill))
                return Core::Unexpected(std::string(
                    "0x5abf0a: sin experiencia entre el nivel y el que sigue, la barra del original no termina"));
            fraction = static_cast<double>(fill) - std::ceil(static_cast<double>(fill) - 1.0);
        }
        const float barFraction = F32(fraction);
        sprites.SetColor(Color{0.92f, 0.8f, 0.34f, 1.0f});
        sprites.ColorQuad(221.0f, 439.0f, F32(static_cast<double>(barFraction) * 198.0), 4.0f);
        sprites.SetBlend(BlendMode::Alpha);
        sprites.SetColor(Color{0.91f, 0.81f, 0.6f, 1.0f});
        UI::DrawNumber(sprites, 425.0f, 434.0f, Truncate(fill), 9.0f, 10.0f);
        sprites.SetBlend(BlendMode::Opaque);
        sprites.SetColor(White);
    }
    if (MouseIn(frame, 219, 439, 421, 445))
    {
        auto format = TextOf(frame, ExperienceHelpText, "0x5ac007");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        const std::string experience = std::to_string(record.experience);
        const std::string next = std::to_string(record.nextExperience);
        const std::array<std::string_view, 2> arguments{experience, next};
        DrawHelp(frame, 223, 424, UI::PlaceArguments(format.Value(), arguments));
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 24. El AG (0x5a8cc0).

Core::Expected<void, std::string> DrawTheAg(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    sprites.SetBlend(BlendMode::Alpha);
    std::uint16_t maximum = 1;
    std::uint16_t current = 0;
    {
        const auto own = OwnAccess(frame);
        if (frame.own.maxBp != 0)
            maximum = frame.own.maxBp;
        current = frame.own.bp < maximum ? frame.own.bp : maximum;
    }
    const std::int32_t height = static_cast<std::int32_t>(current) * 36 / static_cast<std::int32_t>(maximum);
    const auto fill = static_cast<float>(height);
    sprites.SetColor(NumberColor);
    Quad(sprites, 0x101, 551.0f, F32(473.0 - static_cast<double>(fill)), 15.0f, fill, 0.0f, 0.0f, 0.9375f,
         F32(static_cast<double>(fill) * 0.015625));
    UI::DrawNumber(sprites, 571.0f, 467.0f, current, 9.0f, 10.0f);
    if (MouseIn(frame, 551, 437, 566, 473))
    {
        auto format = TextOf(frame, AgHelpText, "0x5a8e7e");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        const std::string first = std::to_string(current);
        const std::string second = std::to_string(maximum);
        const std::array<std::string_view, 2> arguments{first, second};
        DrawHelp(frame, 546, 419, UI::PlaceArguments(format.Value(), arguments));
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 27. El 3D de la UI (0x5ac120) y 28. las grillas (0x5f7840).

Core::Expected<void, std::string> DrawTheUi3D(Frame& frame)
{
    const UI::PanelState& state = frame.runtime.panels;
    // 0x5ac120 cierra el 2D y arma su proyección de 1° con la cámara de 0x7d366f0 y la profundidad. MuRet no dibuja
    // los modelos de los ítems de la UI (08, P-C70): saca sus rand() y no corta el lote del 2D.
    // 0x41b9e0: el ítem de la ventana de las misiones.
    if (state.delegated.quest == 1 || state.delegated.quest == 2)
        return Core::Unexpected(std::string("0x41b9e0: el ítem de la ventana de las misiones es de la CL9.1"));
    if (state.field78abb00 != 0 && state.field78abb00 != 3)
        return Core::Unexpected(std::string("0x5f70a0: los ítems de la ventana de 0x78abb00 son de la CL9.5"));
    // 0x5f7610: los ítems de las grillas abiertas, en su orden, y la caja del entrenador (0x44ad40(0)).
    if (auto grids = DrawTheGridItems(frame.context, frame.panels); !grids.HasValue())
        return grids;
    // 0x5ac1f8: el ítem del cursor (con 0x78abb14 en 0 y 0x78aba60 distinto de 2, que MuRet no tiene).
    if (auto cursor = DrawTheCursorItem(frame.context); !cursor.HasValue())
        return cursor;
    // 0x5ac22a..0x5ac26d: las tres ranuras rápidas, cada una con 0x5e1480(208 + 31i, 454, 20, 20, tipo, +4, 0, 0, 0).
    const std::span<const World::ItemState, InventoryCellCount> cells(frame.context.world.Inventory().cells);
    for (int slot = 0; slot < 3; ++slot)
    {
        auto found = FindQuickSlotItem(frame.context.hud.quickSlots, cells, slot,
                                       frame.runtime.movement.gateRequestBlocked != 0);
        if (!found.HasValue())
            return Core::Unexpected(found.Error());
        if (!found.Value())
            continue;
        const World::ItemState& item = cells[*found.Value()];
        const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
        if (auto drawn = DrawUiItem(frame.context, type, item.scalar1Low, 0, 0); !drawn.HasValue())
            return drawn;
    }
    return {};
}

// 0x5f7840.
Core::Expected<void, std::string> DrawTheGrids(Frame& frame)
{
    return DrawTheGridNumbers(frame.context, frame.panels);
}

// ---------------------------------------------------------------------------------------------------------------
// La skill de la barra (0x5a5860, 0x40ca20 y 0x5531d0).

bool IsRiding(std::int16_t pet)
{
    return pet == UniriaModel || pet == DinorantModel || pet == FenrirModel;
}

// 0x40ca20(skill, energía, liderazgo): si el personaje no llega a la skill. La energía que pide es la del registro
// de Skill.bmd (+0x2c · +0x20 · 4 / 100 + 20), salvo las de la tabla, y el liderazgo (+0x2e) solo se mira con uno
// pedido mayor que 0.
Core::Expected<bool, std::string> CannotUseTheSkill(Frame& frame, std::uint32_t skill, std::int32_t energy,
                                                    std::int32_t leadership)
{
    auto record = SkillRecordOf(frame, skill, "0x40ca20");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const auto entry = SkillAccess(frame, skill);
    std::uint32_t required = ByteAt(record.Value(), 0x2c) * ByteAt(record.Value(), 0x20) * 4u / 100u + 0x14u;
    switch (skill)
    {
    case 0x11:
    case 0x3e:
    case 0x4c:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0x7b:
        required = 0;
        break;
    case 0x1e:
        required = 30;
        break;
    case 0x1f:
        required = 60;
        break;
    case 0x20:
        required = 90;
        break;
    case 0x21:
        required = 130;
        break;
    case 0x22:
        required = 170;
        break;
    case 0x23:
        required = 210;
        break;
    case 0x24:
        required = 300;
        break;
    case 0x3c:
        required = 15;
        break;
    default:
        break;
    }
    const bool withoutEnergy = (skill >= 0x43 && skill <= 0x48) || (skill > 0x11 && skill < 0x17) ||
                               (skill > 0x28 && skill < 0x2c) || (skill > 0x2e && skill < 0x32) || skill == 0x18 ||
                               skill == 0x33 || skill == 0x34 || skill == 0x37 || skill == 0x38 || skill == 0x2c ||
                               skill == 0x2d || skill == 0x2e || skill == 0x39 || skill == 0x49 || skill == 0x4a;
    if (withoutEnergy)
        required = 0;
    const auto leadershipRequired = static_cast<std::int32_t>(LittleEndianAt(record.Value(), 0x2e, 2));
    if (0 < leadership && leadership < leadershipRequired)
        return true;
    return energy < static_cast<std::int32_t>(required);
}

// 0x5531d0(índice, x, y, w, h): la espera de la skill (+0x76c del registro propio), que tapa el ícono de abajo
// hacia arriba con rojo a medias. Nadie la escribe hasta la CL9.2.
Core::Expected<void, std::string> DrawTheSkillDelay(Frame& frame, std::uint32_t index, int x, int y, int width,
                                                    int height)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto own = OwnAccess(frame);
    if (index >= frame.own.skillDelays.size())
        return Core::Unexpected(std::format("0x5531f9: la espera {} pasa del equipo del registro propio", index));
    const std::int32_t delay = frame.own.skillDelays[index];
    if (delay <= 0)
        return {};
    const std::optional<std::uint8_t> skill = OwnSkillAt(frame.own, index);
    if (!skill)
        return Core::Unexpected(
            std::format("0x5532a3: la skill {} del registro propio (+0x6c + {}) no está en MuRet", index, index));
    auto record = SkillRecordOf(frame, *skill, "0x5531d0");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    std::int32_t total = 0;
    {
        const auto entry = SkillAccess(frame, *skill);
        total = static_cast<std::int32_t>(LittleEndianAt(record.Value(), 0x28, 4));
    }
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(Color{1.0f, 0.5f, 0.5f, 0.5f});
    const float fill =
        F32(static_cast<double>(delay) / static_cast<double>(total) * static_cast<double>(static_cast<float>(height)));
    sprites.ColorQuad(static_cast<float>(x),
                      F32(static_cast<double>(static_cast<float>(y)) + static_cast<double>(static_cast<float>(height)) -
                          static_cast<double>(fill)),
                      static_cast<float>(width), fill);
    sprites.SetBlend(BlendMode::Opaque);
    return {};
}

// El ícono `index` de un atlas de 8 columnas de w × h téxeles (0x5a6140..0x5a61b4).
void DrawSkillQuad(Frame& frame, std::uint16_t slot, std::int32_t index, int x, int y, int width, int height)
{
    Quad(frame.context.sprites, slot, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
         static_cast<float>(height), F32(static_cast<double>((index % 8) * width) * Texel),
         F32(static_cast<double>((index / 8) * height) * Texel), F32(static_cast<double>(width) * Texel),
         F32((static_cast<double>(height) - 1.0) * Texel));
}

// 0x5a5860(índice, x, y, w, h): la skill `index` del registro propio (+0x6c + índice; de 0x78 en adelante, el
// índice es la skill), teñida de rojo si no se puede usar, con su espera y su número.
Core::Expected<void, std::string> DrawTheSkillIcon(Frame& frame, std::uint32_t index, int x, int y, int width,
                                                   int height)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    const auto own = OwnAccess(frame);
    std::uint32_t skill = index;
    if (index <= 0x77)
    {
        const std::optional<std::uint8_t> fromRecord = OwnSkillAt(frame.own, index);
        if (!fromRecord)
            return Core::Unexpected(
                std::format("0x5a58c4: la skill {} del registro propio (+0x6c + {}) no está en MuRet", index, index));
        skill = *fromRecord;
    }
    if (skill == 0)
        return {};
    std::uint8_t kind = 0;
    {
        auto record = SkillRecordOf(frame, skill, "0x5a5926");
        if (!record.HasValue())
            return Core::Unexpected(record.Error());
        const auto entry = SkillAccess(frame, skill);
        kind = ByteAt(record.Value(), 0x31);
    }
    const auto pet = static_cast<std::int16_t>(frame.hero.pet.model);
    const std::int32_t map = frame.runtime.map;
    bool tinted = false;
    if ((skill == 0x2c || skill == 0x2d || skill == 0x2e || skill == 0x39 || skill == 0x49 || skill == 0x4a) &&
        (map != CastleSiegeMap || !frame.runtime.siegeActive))
        tinted = true;
    if ((frame.hero.flags78 & 0x200u) != 0x200u && kind == 2)
        tinted = true;
    if (skill == 0x2f)
    {
        if (!IsRiding(pet))
            tinted = true;
    }
    else if (((skill > 0x11 && skill < 0x18) || skill == 0x38) && IsRiding(pet))
        tinted = true;
    const std::int32_t energy = frame.own.word30 + frame.own.energy;
    auto cannot = CannotUseTheSkill(frame, skill, energy, 0);
    if (!cannot.HasValue())
        return Core::Unexpected(cannot.Error());
    if (cannot.Value())
        tinted = true;
    // 0x5a5c43 y 0x5a5de2: el ayudante sin durabilidad, leído con un acceso anidado al registro.
    const auto helperWornOut = [&frame]
    {
        const auto nested = OwnAccess(frame);
        return frame.own.equipment[HelperSlot].durability == 0;
    };
    if (skill == 0x3f)
    {
        if (frame.context.hud.partyCount < 1)
            tinted = true;
    }
    else if (skill == 0x3e && helperWornOut())
        tinted = true;
    if (map < FirstChaosCastleMap || map > LastChaosCastleMap)
    {
        if (skill == 0x3e && helperWornOut())
            tinted = true;
    }
    else if (skill == 0x3e || skill == 0x31 || (skill > 0x77 && skill < 0x7c))
        tinted = true;
    cannot = CannotUseTheSkill(frame, skill, energy, frame.own.word36 + frame.own.leadership);
    if (!cannot.HasValue())
        return Core::Unexpected(cannot.Error());
    if (cannot.Value())
        tinted = true;
    if (tinted)
        sprites.SetColor(SkillTint);

    bool numbered = false;
    if (skill >= 0x78 && skill <= 0x7c)
    {
        DrawSkillQuad(frame, 300, static_cast<std::int32_t>(skill) - 0x78, x, y, width, height);
        numbered = true;
    }
    else if (skill == 0x4c)
    {
        Quad(sprites, 300, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
             static_cast<float>(height), F32(static_cast<double>(width * 4) * Texel), 0.0f,
             F32(static_cast<double>(width) * Texel), F32((static_cast<double>(height) - 1.0) * Texel));
    }
    else
    {
        const bool first = skill < 0x39;
        DrawSkillQuad(frame, first ? 0x12a : 299, static_cast<std::int32_t>(skill) - (first ? 1 : 0x39), x, y, width,
                      height);
        numbered = skill > 0x71;
    }
    if (numbered)
    {
        // 0x5a61e7: el número de la skill de comando, desde 1.
        sprites.SetColor(SkillColor);
        UI::DrawNumber(sprites, F32(static_cast<double>(static_cast<float>(x)) + 4.0),
                       F32(static_cast<double>(static_cast<float>(y)) + 26.0), static_cast<int>(skill) - 0x77, 9.0f,
                       10.0f);
        return {};
    }
    if (auto delay = DrawTheSkillDelay(frame, index, x, y, width, height); !delay.HasValue())
        return delay;
    sprites.SetBlend(BlendMode::Alpha);
    // 0x5a61f9: la tecla de la skill, en +0x26c + 0x100 · [0x6d5edc] + índice del registro propio, que escribe el
    // F3 30 (hito 3): sin ella, 0xff como en la traza, y ningún número. 0x6c8e4c, que guarda ese byte, no lo lee
    // nadie.
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 29. La barra principal (0x5a9630).

// Un orbe: el slot en (x, 432 + falta · 48, 53, 48 − falta · 48 / 64), con la v en falta · 48 / 64. El alto le resta
// la v y no los píxeles, como el original (0x5a9840).
void DrawOrb(Frame& frame, std::uint16_t slot, float x, float missing)
{
    const double pixels = static_cast<double>(missing) * 48.0;
    const float v = F32(pixels * 0.015625);
    Quad(frame.context.sprites, slot, x, F32(pixels + 432.0), 53.0f, F32(48.0 - static_cast<double>(v)), 0.0f, v,
         0.828125f, 0.75f);
}

Core::Expected<void, std::string> HelpWithText(Frame& frame, int x, int y, std::size_t index, std::string_view where)
{
    auto text = TextOf(frame, index, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    DrawHelp(frame, x, y, text.Value());
    return {};
}

Core::Expected<void, std::string> HelpWithValues(Frame& frame, int x, int y, std::size_t index, std::uint32_t first,
                                                 std::uint32_t second, std::string_view where)
{
    auto format = TextOf(frame, index, where);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    const std::string one = std::to_string(first);
    const std::string two = std::to_string(second);
    const std::array<std::string_view, 2> arguments{one, two};
    DrawHelp(frame, x, y, UI::PlaceArguments(format.Value(), arguments));
    return {};
}

Core::Expected<void, std::string> DrawTheMainBar(Frame& frame)
{
    Render::SpriteBatch& sprites = frame.context.sprites;
    UI::PanelRegistry& panels = frame.panels;
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    std::uint16_t maxLife = 0;
    std::uint16_t life = 0;
    std::uint16_t maxMana = 0;
    std::uint16_t mana = 0;
    {
        const auto own = OwnAccess(frame);
        maxLife = frame.own.maxLife;
        life = frame.own.life < maxLife ? frame.own.life : maxLife;
        maxMana = frame.own.maxMana;
        mana = frame.own.mana < maxMana ? frame.own.mana : maxMana;
    }
    // 0x5a9796..0x5a97b8: lo que falta, sobre el máximo (fidiv: con un máximo en 0, NaN).
    const float missingLife = F32(static_cast<double>(maxLife - life) / static_cast<double>(maxLife));
    const float missingMana = F32(static_cast<double>(maxMana - mana) / static_cast<double>(maxMana));
    if (life != 0 && static_cast<double>(life) / static_cast<double>(maxLife) < static_cast<double>(0.2f))
        frame.context.sound = 0x22;
    // 0x5a97fb: el orbe verde con el veneno (+0x78 & 1).
    DrawOrb(frame, (frame.hero.flags78 & 1u) == 1u ? 0xf7 : 0xea, 97.0f, missingLife);
    if (MouseIn(frame, 97, 432, 150, 480))
    {
        if (auto help = HelpWithValues(frame, 103, 419, LifeHelpText, life, maxLife, "0x5a9881"); !help.HasValue())
            return help;
    }
    DrawOrb(frame, 0xeb, 489.0f, missingMana);
    if (MouseIn(frame, 489, 432, 542, 480))
    {
        if (auto help = HelpWithValues(frame, 495, 419, ManaHelpText, mana, maxMana, "0x5a9934"); !help.HasValue())
            return help;
    }
    // Los botones de la party, el personaje y el inventario, prendidos con su ventana abierta.
    struct Button
    {
        int left = 0;
        std::size_t help = 0;
        UI::PanelId panel = 0;
        std::uint16_t slot = 0;
    };
    constexpr std::array<Button, 3> Buttons{{{348, PartyHelpText, UI::PartyPanel, 0xec},
                                             {379, CharacterHelpText, UI::CharacterPanel, 0xed},
                                             {410, InventoryHelpText, UI::InventoryPanel, 0xee}}};
    for (const Button& button : Buttons)
    {
        if (MouseIn(frame, button.left, 452, button.left + 24, 476))
        {
            if (auto help = HelpWithText(frame, button.left, 437, button.help, "0x5a99a0"); !help.HasValue())
                return help;
        }
        if (panels.IsOpen(button.panel))
            Quad(sprites, button.slot, static_cast<float>(button.left), 452.0f, 24.0f, 24.0f, 0.0f, 0.0f, 0.75f, 0.75f);
    }
    // 0x5a9b3e..0x5a9c4c: con el mouse sobre el botón de los amigos, la ayuda de tres renglones (los textos 1043 a
    // 1045) que arma 0x78793c8 y dibuja 0x5af090, la de los tooltips del hito 4; solo dibuja.
    // 0x5a9c51: 0x4a5120, los íconos de los amigos y las cartas de la UI vieja (hito 7); solo dibuja.
    // 0x5a9c56: 0x40a5b0, el botón de los comandos.
    if (MouseIn(frame, 6, 435, 59, 454))
    {
        if (auto help = HelpWithText(frame, 6, 420, CommandsHelpText, "0x40a5f6"); !help.HasValue())
            return help;
    }
    if (frame.runtime.panels.delegated.commands != 0)
        Quad(sprites, 0x65c, 6.0f, 435.0f, 53.0f, 19.0f, 0.0f, 0.0f, 0.828125f, 0.59375f);
    if (MouseIn(frame, 582, 459, 634, 477))
    {
        if (auto help = HelpWithText(frame, 582, 444, GuildHelpText, "0x5a9cb2"); !help.HasValue())
            return help;
    }
    if (panels.IsOpen(UI::GuildPanel))
        Quad(sprites, 0xfa, 582.0f, 459.0f, 52.0f, 18.0f, 0.0f, 0.0f, 0.8125f, 0.5625f);
    {
        const auto own = OwnAccess(frame);
        if (frame.own.byte6a != 0)
        {
            sprites.SetColor(SkillColor);
            if (auto icon = DrawTheSkillIcon(frame, frame.hero.skillIndex, 304, 444, 32, 36); !icon.HasValue())
                return icon;
            if (frame.runtime.ui.skillListOpen != 0)
                return Core::Unexpected(std::string("0x5a9f9a: la lista de skills es de la CL9.2"));
        }
    }
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(Color{0.6f, 0.6f, 0.6f, 1.0f});
    std::uint8_t cell = 0;
    {
        const Render::Models::LogicalAccess access(frame.context.cellXDepth, frame.context.random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cell = frame.hero.cellX;
    }
    UI::DrawNumber(sprites, 24.0f, 462.0f, cell, 8.0f, 9.0f);
    {
        const Render::Models::LogicalAccess access(frame.context.cellYDepth, frame.context.random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cell = frame.hero.cellY;
    }
    UI::DrawNumber(sprites, 48.0f, 462.0f, cell, 8.0f, 9.0f);
    sprites.SetColor(NumberColor);
    {
        const auto own = OwnAccess(frame);
        UI::DrawNumber(sprites, 100.0f, 467.0f, frame.own.life, 9.0f, 10.0f);
        UI::DrawNumber(sprites, 540.0f, 467.0f, frame.own.mana, 9.0f, 10.0f);
    }
    const std::span<const World::ItemState, InventoryCellCount> cells(frame.context.world.Inventory().cells);
    for (int slot = 0; slot < 3; ++slot)
    {
        auto count = CountQuickSlotItems(frame.context.hud.quickSlots, cells, slot);
        if (!count.HasValue())
            return Core::Unexpected(count.Error());
        if (count.Value() > 0)
            UI::DrawNumber(sprites, F32(static_cast<double>(slot) * 31.0 + 226.0), 447.0f, count.Value(), 9.0f, 10.0f);
    }
    // 0x5aa980: 0x621a90, los pops de las matrices del 2D, que MuRet arma por pase.
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 31. El panel del asedio (0x4125b0).

Core::Expected<void, std::string> DrawTheSiegePanel(Frame& frame)
{
    if (frame.panels.IsOpen(UI::AnyPanel))
        return {};
    const std::optional<std::uint8_t> kind = frame.runtime.siegePanelKind;
    if (!kind)
        return {};
    if (*kind == 0 && frame.runtime.siegePanel.visible == 0)
        return {};
    return Core::Unexpected(std::format("0x4125b0: el panel del asedio de la clase {} (0x40f850 para la 0, el "
                                        "minimapa con los personajes) es de la CL9.5",
                                        *kind));
}
} // namespace

Core::Expected<void, std::string> DrawTheHud(GameHudContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    GamePanels panels(world, context.random);
    Frame frame{.context = context,
                .runtime = runtime,
                .own = world.OwnCharacter(),
                .hero = world.Actors()[runtime.hero],
                .panels = panels.Registry()};
    HudState& hud = context.hud;
    hud.textColors.text = 0xffffffffu;
    // 1. 0x5a7520.
    DrawTheSands(frame);
    // 2. 0x5a7640: la guerra de gremios, con 0x576796c (CL9.4): nada.
    // 3. 0x5a89b0: las barras de la party.
    if (hud.partyCount > 0)
        return Core::Unexpected(std::string("0x5a89b0: las barras de vida de la party son de la CL9.4"));
    // 4. 0x5a6fc0: la info de Castle Siege, con el objeto de 0x5767924 (CL9.5): nada.
    // 5. 0x42a4d0: las barras de la cola de 0x7239ac, vacía hasta la CL9.5: nada.
    // 6. 0x42a660.
    if (auto icons = DrawTheStatusIcons(frame); !icons.HasValue())
        return icons;
    // 7..9. 0x5ab810, 0x5ab210 y 0x5aac20.
    auto arrows = DrawTheArrows(frame);
    if (!arrows.HasValue())
        return Core::Unexpected(arrows.Error());
    auto top = DrawTheHelper(frame, arrows.Value());
    if (!top.HasValue())
        return Core::Unexpected(top.Error());
    if (auto durability = DrawTheDurability(frame, top.Value()); !durability.HasValue())
        return durability;
    // 10. La macro.
    DrawTheMacroTime(frame);
    // 11.
    context.sprites.SetColor(White);
    if (auto event = DrawTheEventObject(frame); !event.HasValue())
        return event;
    // 12. 0x5a9050: los globos y los nombres sobre las cabezas (0x77e1be0, Scenes/ChatBubbleDraw). No saca rand() y
    // escribe en cada entrada dónde quedó (+0x278..+0x284), que lee el clic derecho de 0x557650.
    ChatBubbleDrawContext bubbles{.world = world,
                                  .projection = context.projection,
                                  .sprites = context.sprites,
                                  .text = context.text,
                                  .dcFont = hud.dcFont,
                                  .textColors = hud.textColors,
                                  .clampBoxes = hud.durabilityWarning,
                                  .boxWidth = hud.arrowsTextWidth,
                                  .boxHeight = hud.arrowsTextHeight,
                                  .mouse = context.mouse,
                                  .blink = hud.messageFrame.areaBlink,
                                  .projecting = context.projecting};
    if (auto drawn = DrawTheChatBubbles(bubbles); !drawn.HasValue())
        return drawn;
    // 13. 0x451d10 y 0x451c20: los carteles del singleton de 0x7245c8 (CL9.5).
    // 14. 0x5a6cc0: el marcador del duelo, con 0x78abad8 (hito 4).
    // 15. 0x46b090: el cartel de Slide.bmd ([0x5763528]).
    if (auto slides = UI::DrawTheSlides(hud.slides, UI::SlideDrawContext{.sprites = context.sprites,
                                                                         .text = context.text,
                                                                         .dcFont = hud.dcFont,
                                                                         .textColors = hud.textColors,
                                                                         .mouseX = context.mouse.x,
                                                                         .mouseY = context.mouse.y,
                                                                         .slidesOn = runtime.chat.slidesOn});
        !slides.HasValue())
        return slides;
    // 16. 0x413ca0.
    if (runtime.map == CastleSiegeMap && runtime.siegeActive)
        return Core::Unexpected(std::string("0x413ca0: la UI de Castle Siege es de la CL9.5"));
    // 17. [[0x576350c]] + 0x14: el registro del chat (0x479080).
    UI::DrawTheChatLog(
        runtime.chat.log, hud.chatFrame, runtime.ui.chatModes,
        UI::ChatLogDrawContext{
            .sprites = context.sprites, .text = context.text, .dcFont = hud.dcFont, .textColors = hud.textColors},
        UI::ChatLogDrawView{.mouseX = context.mouse.x,
                            .mouseY = context.mouse.y,
                            .leftDown = context.leftDown,
                            .chatOpen = runtime.ui.chatOpen,
                            .crywolfStage = runtime.crywolf.stage,
                            .texts = context.texts});
    // 18. 0x5f7440.
    if (auto windows = DrawTheWindows(frame); !windows.HasValue())
        return windows;
    // 19. 0x65dd70: la imagen que se desvanece de [0x5763534], con su +0x18 en 0 (hito 6).
    // 20. 0x5a9290.
    DrawTheBottomBar(frame);
    // 21. 0x4365c0 empieza siempre con 0x620a00(1), y la traza lo muestra antes de la experiencia. La UI de los
    // eventos, con 0x723bf9 y 0x6c73d1 (.data −1, sale sin dibujar), es de la CL9.5.
    context.sprites.SetBlend(BlendMode::Alpha);
    // 22. 0x5aa9a0.
    DrawTheChatInput(frame);
    // 23. y 24. 0x5abcd0 y 0x5a8cc0.
    if (auto experience = DrawTheExperience(frame); !experience.HasValue())
        return experience;
    if (auto ag = DrawTheAg(frame); !ag.HasValue())
        return ag;
    // 25. 0x652c90.
    if (runtime.siegeActive)
        return Core::Unexpected(std::string("0x652c90: las skills de la batalla del asedio son de la CL9.5"));
    // 26. 0x436240: la confirmación de los eventos (CL9.5).
    // 27. y 28. 0x5ac120 y 0x5f7840.
    if (auto ui3D = DrawTheUi3D(frame); !ui3D.HasValue())
        return ui3D;
    if (auto grids = DrawTheGrids(frame); !grids.HasValue())
        return grids;
    // 29. 0x5a9630.
    if (auto bar = DrawTheMainBar(frame); !bar.HasValue())
        return bar;
    // 30. 0x5e9b10.
    if (runtime.ui.field78abb20 != 0)
        return Core::Unexpected(std::string("0x5e9b10: la caja de la contraseña es del hito 4"));
    // 31. Sin ninguna ventana, 0x4125b0.
    return DrawTheSiegePanel(frame);
}

Core::Expected<void, std::string> DrawTheOverlays(GameHudContext& context)
{
    WorldState& world = context.world;
    const std::int32_t mode = world.Runtime().panels.overlayMode;
    if (mode == 1 || mode == 2)
        return Core::Unexpected(std::format("0x5b0550: las ayudas de F1 (el modo {}) son del hito 6", mode));
    if (mode == 3)
        return Core::Unexpected(std::string("0x5b0550: la ayuda del ítem de 0x77f3b0c es del hito 6"));
    GamePanels panels(world, context.random);
    if (panels.Registry().IsOpen(UI::MovePanel))
        return Core::Unexpected(std::string("0x449860: la ventana /move es del hito 6"));
    return {};
}

Core::Expected<void, std::string> DrawTheGameCursor(GameHudContext& context)
{
    Render::SpriteBatch& sprites = context.sprites;
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    // 0x5ac2ee..0x5ac33a: el cuadro del de hablar, ftol(T · 0,01) % 6, en cuartos de la textura.
    const std::int32_t frameIndex = Truncate(static_cast<double>(context.worldClock) * static_cast<double>(0.01f)) % 6;
    const float u = frameIndex == 1 || frameIndex == 3 || frameIndex == 5 ? 0.5f : 0.0f;
    const float v = frameIndex == 2 || frameIndex == 3 || frameIndex == 4 ? 0.5f : 0.0f;
    const float x = F32(static_cast<double>(context.mouse.x) - 2.0);
    const float y = F32(static_cast<double>(context.mouse.y) - 2.0);
    const auto cursor = [&](std::uint16_t slot) { Quad(sprites, slot, x, y, 24.0f, 24.0f, 0.0f, 0.0f, 1.0f, 1.0f); };
    if (runtime.ui.field78abb20 != 0 || runtime.dialogs.current != 0)
    {
        cursor(2);
        return {};
    }
    const std::array<std::int32_t, 5>& targets = runtime.selectedTargets;
    if (targets[0] != -1)
    {
        cursor(5);
        return {};
    }
    if (targets[1] != -1)
    {
        Quad(sprites, 6, x, y, 24.0f, 24.0f, u, v, 0.5f, 0.5f);
        return {};
    }
    if (targets[3] != -1)
        return Core::Unexpected(
            std::string("0x5ac3d8: el cursor de los objetos lee su tabla (0x7d34820), de la CL9.1"));
    GamePanels panels(world, context.random);
    const ActorState& hero = world.Actors()[runtime.hero];
    if (hero.safeZone == 0 && targets[2] != -1)
        return Core::Unexpected(std::string("0x558bb0: si el personaje elegido se puede atacar es de la CL9.2"));
    if (panels.Registry().IsOpen(UI::CommandsPanel))
    {
        cursor(0x65a);
        return {};
    }
    const std::int32_t repairMode = runtime.panels.repairMode;
    if (repairMode == 1)
    {
        cursor(7);
        return {};
    }
    if (repairMode == 2)
    {
        // 0x5ac6d4: fsin(T · 0,02) contra 0; el signo es el mismo en cualquier CPU.
        const double turn = std::sin(static_cast<double>(context.worldClock) * static_cast<double>(0.02f));
        if (turn <= 0.0)
            sprites.RotatedQuad(Render::TextureSlot{7}, F32(static_cast<double>(context.mouse.x) + 5.0),
                                F32(static_cast<double>(context.mouse.y) + 18.0), 24.0f, 24.0f, 45.0f);
        else
            sprites.RotatedQuad(Render::TextureSlot{7}, F32(static_cast<double>(context.mouse.x) + 10.0),
                                F32(static_cast<double>(context.mouse.y) + 10.0), 24.0f, 24.0f, 0.0f);
        return {};
    }
    if (context.leftDown)
    {
        cursor(context.hud.pushCursor == 0 ? 3 : 0xa);
        return {};
    }
    cursor(2);
    return {};
}
} // namespace Client::Scenes
