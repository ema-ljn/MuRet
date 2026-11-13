#include "Scenes/GameMoveWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Text/MoveRequirements.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameChat.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameUi.h"
#include "Scenes/HudText.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatCommands.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

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
// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300): el título (0x7701738), las tres columnas
// (0x7701864, 0x7701990 y 0x7701abc) y el renglón de abajo (0x7706814).
constexpr std::size_t TitleText = 933;
constexpr std::size_t NameColumnText = 934;
constexpr std::size_t LevelColumnText = 935;
constexpr std::size_t ZenColumnText = 936;
constexpr std::size_t CloseText = 1002;
// 0x448fe4..0x449050: el alto de más de la ventana, la y del título y la de las columnas.
constexpr std::int32_t ExtraHeight = 0x3c;
constexpr std::int32_t TitleY = 5;
constexpr std::int32_t ColumnY = 0x19;
// 0x44864e y 0x4491d5: el primer renglón (el alto de uno más 0x1a; el texto, un píxel más abajo).
constexpr std::int32_t FirstRowTop = 0x1a;
constexpr std::int32_t FirstRowText = 0x1b;
// 0x448780 y 0x449550: el PK desde el que no se mueve (+0x375 del héroe, sin signo).
constexpr std::uint8_t OutlawPk = 6;
// 0x6b8c64: 2/3 del nivel para las clases 3 y 4 (+0xb del registro propio, & 7).
constexpr float LevelFactor = 0.6666666865348816f;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t FirstScaledClass = 3;
constexpr std::uint8_t SecondScaledClass = 4;
// Los colores de 0x6c8e5c.
constexpr std::uint32_t TitleColor = 0xff1accffu;
constexpr std::uint32_t ColumnColor = 0xffffb27fu;
constexpr std::uint32_t AllowedColor = 0xffffffffu;
constexpr std::uint32_t DeniedColor = 0xff1127a4u;
constexpr std::uint32_t MissingColor = 0xff1a33ffu;
constexpr Render::Color Shade{0.0f, 0.0f, 0.0f, 0.8f};
constexpr Render::Color CloseBar{0.6f, 0.0f, 0.0f, 1.0f};
constexpr Render::Color Highlight{0.8f, 0.8f, 0.1f, 0.6f};
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// 0x448fbd..0x449050 (y 0x4485a4..0x4485ed, solo el ancho): por el ancho de la pantalla (0x6d5d60); con otro, todo en
// 0.
struct MoveColumns
{
    std::int32_t width = 0;
    std::int32_t name = 0;
    std::int32_t level = 0;
    std::int32_t zen = 0;
};

MoveColumns ColumnsOf(std::int32_t screenWidth)
{
    switch (screenWidth)
    {
    case 640:
        return MoveColumns{.width = 0xbe, .name = 0x2d, .level = 0x69, .zen = 0x9b};
    case 800:
        return MoveColumns{.width = 0xaa, .name = 0x28, .level = 100, .zen = 0x91};
    case 1024:
        return MoveColumns{.width = 0x96, .name = 0x23, .level = 0x5a, .zen = 0x82};
    case 1280:
        return MoveColumns{.width = 0x82, .name = 0x1e, .level = 0x4b, .zen = 0x6e};
    default:
        return MoveColumns{};
    }
}

// 0x4485f0..0x448603 y 0x449062..0x44907a: el paso de la letra (0x7758568) por 640 sobre el ancho, sin signo.
std::int32_t RowOf(App::PixelSize screen)
{
    const auto step = static_cast<std::uint32_t>(Render::Text::FontStepFor(screen.width));
    return static_cast<std::int32_t>(step * 640u / static_cast<std::uint32_t>(screen.width));
}

struct MoveCheck
{
    // El nivel pedido, con los 2/3 de las clases 3 y 4.
    std::int32_t level = 0;
    bool levelOk = false;
    bool zenOk = false;
    // Los dos y el PK del héroe debajo de 6.
    bool allowed = false;
};

// 0x448657..0x448787 y 0x4491df..0x449559: con el registro propio abierto, su zen (+0xecc), su nivel (+0xe) y su
// clase (+0xb); después de cerrarlo, las comparaciones (el nivel con signo, el zen sin signo).
MoveCheck CheckTheMove(WorldState& world, std::uint8_t& ownRecordDepth, Render::Models::CrtRandom& random,
                       const Data::Text::MoveRequirement& move)
{
    std::uint32_t money = 0;
    std::int32_t level = 0;
    MoveCheck check{.level = move.level, .levelOk = false, .zenOk = false, .allowed = false};
    {
        const Render::Models::LogicalAccess own(ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
        const OwnCharacterRecord& record = world.OwnCharacter();
        money = record.zen;
        level = record.level;
        const auto characterClass = static_cast<std::uint8_t>(record.characterClass & ClassMask);
        // 0x448707..0x448715: FILD, FMUL con el float y ftol.
        if (characterClass == FirstScaledClass || characterClass == SecondScaledClass)
            check.level = static_cast<std::int32_t>(static_cast<double>(move.level) * static_cast<double>(LevelFactor));
    }
    const ActorState& hero = world.Actors()[world.Runtime().hero];
    check.levelOk = level >= check.level;
    check.zenOk = money >= move.zen;
    check.allowed = check.levelOk && check.zenOk && hero.pk < OutlawPk;
    return check;
}

// 0x6a2081: _itoa con base 10, con signo.
std::string DecimalOf(std::int32_t value)
{
    return std::to_string(value);
}

// 0x448ef8..0x448f0e y 0x448f70..0x448f81: la ventana cerrada, el botón y el flanco apagados.
void CloseTheWindow(WorldRuntime& runtime)
{
    runtime.panels.overlayMode = 0;
    runtime.movement.leftHeld = false;
    runtime.movement.pendingClick = false;
}

Core::Expected<std::string_view, std::string> TextOf(const Data::Text::TextTable* texts, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: la ventana /move sin el texto {} de Text.bmd", where, index));
    return *text;
}
} // namespace

Core::Expected<void, std::string> StepTheMoveWindow(const GameUiContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const MoveColumns columns = ColumnsOf(context.screen.width);
    const std::int32_t row = RowOf(context.screen);
    const std::int32_t rowHeight = row + 2;
    const auto count = static_cast<std::int32_t>(runtime.moves.size());
    const std::int32_t bottom = count * rowHeight + ExtraHeight;
    const std::int32_t x = context.mouseX;
    const std::int32_t y = context.mouseY;
    // 0x448619..0x448633.
    if (x >= 1 && x < columns.width + 1 && y >= 1 && y < bottom + 1)
        runtime.ui.mouseOnUi = 1;
    std::int32_t rowTop = rowHeight + FirstRowTop;
    for (const Data::Text::MoveRequirement& move : runtime.moves)
    {
        const MoveCheck check = CheckTheMove(world, runtime.ownRecordDepth, context.random, move);
        // 0x44876c..0x4487a9.
        if (check.allowed && x >= 3 && x < columns.width - 2 && y >= rowTop && y < rowTop + rowHeight &&
            runtime.movement.leftHeld)
        {
            // 0x4487d7..0x4488d3: el texto 260, un espacio y el nombre de +4.
            if (context.chat == nullptr)
                return Core::Unexpected(
                    std::string("0x4487d7: el destino de la ventana /move sin la entrada del chat"));
            auto command = TextOf(context.texts, UI::MoveCommandText, "0x4487e7");
            if (!command.HasValue())
                return Core::Unexpected(command.Error());
            const std::string said = std::format("{} {}", command.Value(), move.name);
            if (auto spoken = SayInTheChat(*context.chat, said); !spoken.HasValue())
                return spoken;
            CloseTheWindow(runtime);
            // 0x448f34..0x448f6e: el renglón de abajo ya no ve el botón.
            return {};
        }
        rowTop += rowHeight;
    }
    // 0x448f43..0x448f81: el renglón de abajo.
    if (x >= 3 && x < columns.width - 2 && y >= bottom - rowHeight - 6 && y < bottom - 6 && runtime.movement.leftHeld)
        CloseTheWindow(runtime);
    return {};
}

Core::Expected<void, std::string> DrawTheMoveWindow(GameHudContext& context)
{
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const App::PixelSize screen = sprites.Screen();
    auto title = TextOf(context.texts, TitleText, "0x449142");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    auto nameColumn = TextOf(context.texts, NameColumnText, "0x449180");
    if (!nameColumn.HasValue())
        return Core::Unexpected(nameColumn.Error());
    auto levelColumn = TextOf(context.texts, LevelColumnText, "0x44919a");
    if (!levelColumn.HasValue())
        return Core::Unexpected(levelColumn.Error());
    auto zenColumn = TextOf(context.texts, ZenColumnText, "0x4491aa");
    if (!zenColumn.HasValue())
        return Core::Unexpected(zenColumn.Error());
    auto close = TextOf(context.texts, CloseText, "0x4496f2");
    if (!close.HasValue())
        return Core::Unexpected(close.Error());
    // 0x448fa3.
    sprites.SetBlend(Render::BlendMode::Alpha);
    const MoveColumns columns = ColumnsOf(screen.width);
    const std::int32_t row = RowOf(screen);
    const std::int32_t rowHeight = row + 2;
    const auto count = static_cast<std::int32_t>(runtime.moves.size());
    const std::int32_t height = count * rowHeight + ExtraHeight;
    const std::int32_t closeTop = height - rowHeight;
    const auto barWidth = static_cast<float>(columns.width - 5);
    const auto rowSize = static_cast<float>(rowHeight);
    // 0x44906e..0x449121: el fondo, el renglón rojo de abajo y la mezcla otra vez (con la textura).
    sprites.SetColor(Shade);
    sprites.ColorQuad(1.0f, 1.0f, static_cast<float>(columns.width), static_cast<float>(height));
    sprites.SetColor(CloseBar);
    sprites.ColorQuad(3.0f, static_cast<float>(closeTop - 6), barWidth, rowSize);
    sprites.SetColor(White);
    sprites.SetBlend(Render::BlendMode::Alpha);
    // 0x449126..0x4491bc: el título en negrita y las columnas con la normal, sin fondo.
    const std::int32_t middle = columns.width / 2;
    hud.dcFont = Render::Text::FontKind::Bold;
    hud.textColors = Render::Text::TextColors{.text = TitleColor, .background = 0};
    DrawHudCentered(context.text, screen, hud, middle, TitleY, title.Value());
    hud.dcFont = Render::Text::FontKind::Normal;
    hud.textColors.text = ColumnColor;
    DrawHudCentered(context.text, screen, hud, columns.name, ColumnY, nameColumn.Value());
    DrawHudCentered(context.text, screen, hud, columns.level, ColumnY, levelColumn.Value());
    DrawHudCentered(context.text, screen, hud, columns.zen, ColumnY, zenColumn.Value());
    hud.textColors.background = 0;
    const std::int32_t mouseX = context.mouse.x;
    const std::int32_t mouseY = context.mouse.y;
    std::int32_t y = rowHeight + FirstRowText;
    for (const Data::Text::MoveRequirement& move : runtime.moves)
    {
        const MoveCheck check = CheckTheMove(world, context.ownRecordDepth, context.random, move);
        // 0x44955b..0x4495ef: el resaltado bajo el mouse, si pasa y nadie agarró el mouse (0x5762eb4).
        const bool hovered = mouseX >= 3 && mouseX < columns.width - 2 && mouseY >= y - 1 && mouseY < rowHeight - 1 + y;
        if (hovered && check.allowed && runtime.ui.capture == 0)
        {
            sprites.SetColor(Highlight);
            sprites.ColorQuad(3.0f, static_cast<float>(y - 1), barWidth, rowSize);
            sprites.SetColor(White);
            sprites.SetBlend(Render::BlendMode::Alpha);
        }
        // 0x4495f2..0x4496c6: el nombre, el nivel y el zen, cada uno con su color.
        const std::uint32_t allowedColor = check.allowed ? AllowedColor : DeniedColor;
        hud.textColors.text = allowedColor;
        DrawHudCentered(context.text, screen, hud, columns.name, y, move.name);
        hud.textColors.text = check.levelOk ? allowedColor : MissingColor;
        DrawHudCentered(context.text, screen, hud, columns.level, y, DecimalOf(check.level));
        hud.textColors.text = check.zenOk ? allowedColor : MissingColor;
        DrawHudCentered(context.text, screen, hud, columns.zen, y, DecimalOf(static_cast<std::int32_t>(move.zen)));
        y += rowHeight;
    }
    // 0x4496e9..0x44970b.
    hud.textColors.text = AllowedColor;
    DrawHudCentered(context.text, screen, hud, middle, closeTop - 5, close.Value());
    sprites.SetBlend(Render::BlendMode::Opaque);
    return {};
}
} // namespace Client::Scenes
