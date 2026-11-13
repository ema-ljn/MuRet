#include "Scenes/GameInventoryHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/PassColors.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameProjection.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SocialState.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Color;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor3f(1; 0,9; 0,7) de las cifras de 0x5e3760.
constexpr Color NumberColor{1.0f, 0.9f, 0.7f, 1.0f};

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
constexpr std::size_t InventoryTitleText = 223;
constexpr std::size_t ZenText = 224;
constexpr std::size_t CloseHelpText = 225;
constexpr std::size_t RepairHelpText = 233;
constexpr std::size_t OpenShopHelpText = 1125;
constexpr std::size_t ShopOpenHelpText = 1127;
constexpr std::size_t SetText = 989;

// El comercio (0x5f3b40, 0x5f7610 y 0x5f7840): las grillas en la esquina más 15 (0x6b87bc) y 70 (0x6ba1b0) o 270
// (0x6ba10c), de 8 × 4.
constexpr double TradeGridLeft = 15.0;
constexpr double OtherGridTop = 70.0;
constexpr double OwnGridTop = 270.0;
constexpr std::size_t TradeColumns = 8;
constexpr std::size_t TradeRows = 4;

// 0x449c00: la caja del entrenador ([0x724580]) en 3.
constexpr std::int32_t TrainerWithBox = 3;
// [0x7d536fc] + 0x18 en 3: la lista de Castle Siege va junto con el inventario.
constexpr std::int32_t SiegeListWithInventory = 3;

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

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0): escalado en tamaño y en posición, sin alfa.
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

// (float) del mouse contra la caja en float: left ≤ x < left + size, con la suma en la x87.
bool MouseInSquare(const GameHudContext& context, float left, float top, float size)
{
    const auto x = static_cast<double>(static_cast<float>(context.mouse.x));
    const auto y = static_cast<double>(static_cast<float>(context.mouse.y));
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(size) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(size);
}

// SelectObject sobre el DC de los textos: la fuente con la que miden y dibujan los que siguen.
void SelectFont(GameHudContext& context, Render::Text::FontKind font)
{
    context.hud.dcFont = font;
}

// 0x554180(x, y, texto, 0, 0, 0): el modo 1 sin tope, desde x, con la fuente y los colores que haya.
void DrawText(GameHudContext& context, int x, int y, std::string_view text)
{
    context.text.Draw(x, y, text,
                      Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors});
}

// 0x554180(x, y, texto, ancho, 1, p6) → 0x467060 con el modo 2: una caja de `boxPixels` píxeles que empieza en
// _ftol(x · rx), con el texto en la columna (ancho − cx) / 2 de la división de C y el fondo en toda la caja, con la
// fuente y los colores que haya (Render::Text::CenterInBox).
void DrawTextCenteredInBox(GameHudContext& context, int x, int y, std::string_view text, int boxPixels)
{
    const Render::Text::CenteredInBox placed =
        Render::Text::CenterInBox(context.text, context.hud.dcFont, text, boxPixels);
    context.text.DrawColumns(x, y, text,
                             Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                             -placed.textColumn, placed.width);
}

// 0x623690(x, y, texto): centrado en x, con la fuente y los colores que haya.
void DrawTextCenteredAt(GameHudContext& context, int x, int y, std::string_view text)
{
    const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, text);
    DrawText(context, Render::Text::CenteredAt(x, extent, context.sprites.Screen()), y, text);
}

// 0x554340 con la fuente normal y los colores blanco y negro que le ponen antes: la ayuda deja el texto en blanco
// sobre un fondo transparente (0x6c8e5c en −1 y 0x6c8e64 en 0).
void DrawHelp(GameHudContext& context, int x, int y, std::string_view help)
{
    UI::DrawHelp(context.sprites, context.text, x, y, help);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
}

// 0x5e22d0(ítem): el color del aviso del ítem (+0x44, que escribe 0x5aac20). Con otro valor, el que haya.
void SetWarningColor(Render::SpriteBatch& sprites, const World::ItemState& item)
{
    switch (item.warning)
    {
    case 0:
        sprites.SetColor(White);
        return;
    case 1:
        sprites.SetColor(Color{0.8f, 0.8f, 0.8f, 1.0f});
        return;
    case 2:
        sprites.SetColor(Color{0.6f, 0.7f, 1.0f, 1.0f});
        return;
    case 3:
    case 99:
        sprites.SetColor(Color{1.0f, 0.2f, 0.1f, 1.0f});
        return;
    case 4:
        sprites.SetColor(Color{0.5f, 1.0f, 0.6f, 1.0f});
        return;
    case 5:
        sprites.SetColor(Color{0.8f, 0.7f, 0.0f, 1.0f});
        return;
    case 6:
        sprites.SetColor(Color{0.8f, 0.5f, 0.0f, 1.0f});
        return;
    case 7:
        sprites.SetColor(Color{0.8f, 0.3f, 0.3f, 1.0f});
        return;
    case 8:
        sprites.SetColor(Color{1.0f, 0.0f, 0.0f, 1.0f});
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// El ítem 3D de la UI (0x5e1480 → 0x5e0720, Scenes/UiItemDrawing).

// Sin la biblioteca de los modelos, los rand() del dibujo en su orden: los de 0x607b60 (1 con 0x220 y 0xc19, 2 con
// 0xc17 y 0xe20), los de 0x609640 (2 con 0xe21 y 0xe22) y el del pase del set (0x60cc29: con el set en 1 o 2, sin
// excelente y con el pulso apagado, rand() % 100 en el +0xc0 del modelo; como DrawItemFromMeshes, solo en un frame
// del original). El 0x1e1e cambia de modelo sin cambiar de cuenta.
void SpendUiItemRandoms(GameHudContext& context, std::int32_t model, std::uint8_t excellent, std::uint8_t set)
{
    context.random.Discard(static_cast<std::uint32_t>(HangingRandomCount(static_cast<std::uint16_t>(model))));
    constexpr std::int32_t ShieldWithUv = 0xe21;
    constexpr std::int32_t SecondShieldWithUv = 0xe22;
    if (model == ShieldWithUv || model == SecondShieldWithUv)
        context.random.Discard(2);
    const bool showsExcellent = (excellent & 0x3fu) != 0 && (model < 0x1a12 || model > 0x1a18) && model != 0x1c30;
    if (showsExcellent || context.projecting)
        return;
    const std::uint32_t setKind = set & 3u;
    // El pase del set de 0x60b3d0 (0x60cbff..0x60cc29), con el pulso de la x87 sin pasar por un float.
    if ((setKind == 1 || setKind == 2) &&
        Render::Models::SetPulseIsOff(Render::Models::SetPulseAt(static_cast<double>(context.worldClock))))
        context.world.Runtime().itemRender.selections[static_cast<std::uint16_t>(model)] = context.random.Next() % 100;
}

// Item.bmd +0x24 y +0x25: las casillas de ancho y de alto, cada una por 20 (0x6b86d0) en la x87 con el fstp.
Core::Expected<UiItemBox, std::string> ItemSizeOf(const GameHudContext& context, std::int32_t type,
                                                  std::string_view where)
{
    if (context.items == nullptr)
        return Core::Unexpected(std::format("{}: sin Item.bmd", where));
    const auto record = type < 0 ? std::nullopt : context.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("{}: Item.bmd no tiene el tipo {:#x}", where, type));
    constexpr std::size_t Width = 0x24;
    constexpr std::size_t Height = 0x25;
    constexpr float CellPixels = 20.0f;
    UiItemBox box;
    box.width = F32(static_cast<double>(std::to_integer<std::uint8_t>((*record)[Width])) * CellPixels);
    box.height = F32(static_cast<double>(std::to_integer<std::uint8_t>((*record)[Height])) * CellPixels);
    return box;
}
} // namespace

Core::Expected<void, std::string> DrawUiItem(GameHudContext& context, const UiItemBox& box, std::int32_t type,
                                             std::uint32_t level, std::uint8_t excellent, std::uint8_t set, bool always)
{
    // 0x5e1486..0x5e1d69: bajo el mouse, el punto de la caja y 0x620480 con él (sin efectos fuera de 0x7d36834).
    const bool hovered = UiItemHovered(box, context.mouse, context.world.Runtime().cursorItemCount, always);
    const auto anchor = UiItemAnchor(type, level, box);
    if (!anchor.HasValue())
        return Core::Unexpected(anchor.Error());
    const std::optional<UiItemCall> call = UiItemCallOf(type, level);
    if (!call)
        return {};
    // 0x60c3b6: 0x60b3d0 cuelga del 0x1a18 las uniones 0x614 y 0x613 (0x530860) con el objeto local de 0x5e0720 de
    // dueño, en la pila, que el paso de las uniones lee después (P-C70).
    constexpr std::int32_t WingsWithJoints = 0x1a18;
    if (call->model == WingsWithJoints)
        return Core::Unexpected(std::string("0x60c3b6: las uniones 0x614 y 0x613 (0x530860) del 0x1a18 en la UI "
                                            "tienen de dueño el objeto local de 0x5e0720 (P-C70)"));
    if (context.uiItems == nullptr)
    {
        SpendUiItemRandoms(context, call->model, excellent, set);
        return {};
    }
    UiItemResources& resources = *context.uiItems;
    const UnprojectedPoint point = UnprojectGamePoint(resources.pass.projection, anchor.Value().x, anchor.Value().y);
    auto placement =
        PlaceUiItem(*call, point, hovered, always, context.worldClock, context.items, context.hud.uiItemAngles);
    if (!placement.HasValue())
        return Core::Unexpected(placement.Error());
    return DrawUiItemModel(resources, context.random, placement.Value(), excellent, set);
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// La ventana (0x5f2340).

// 0x5ee220(x, y, línea): el fondo de 190 × 433 en dos texturas, y con `line` la línea del comercio en y + 225.
void DrawWindowBack(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y, bool line)
{
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    Quad(sprites, 0x104, left, top, 190.0f, 256.0f, 0.7421875f, 1.0f);
    Quad(sprites, 0x105, left, F32(static_cast<double>(top) + 256.0), 190.0f, 177.0f, 0.7421875f, 0.69140625f);
    if (line)
        Quad(sprites, 0x117, left, F32(static_cast<double>(top) + 225.0), 190.0f, 10.0f, 0.7421875f, 0.625f);
}

// 0x5e2460: los fondos de los doce lugares del equipo, cada uno con el color del aviso de su ítem, con el registro
// propio abierto alrededor (3.848 rand() al cerrarlo sin otros accesos) y otro acceso adentro para la clase: el
// casco no tiene fondo con la clase 3.
void DrawTheEquipmentSlots(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    const auto& equipment = own.equipment;
    sprites.SetBlend(BlendMode::Alpha);
    const auto outer = OwnAccess(context);
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    const float column15 = F32(static_cast<double>(left) + 15.0);
    const float row46 = F32(static_cast<double>(top) + 46.0);
    SetWarningColor(sprites, equipment[8]);
    Quad(sprites, 0x113, column15, row46, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[7]);
    const float column115 = F32(static_cast<double>(left) + 115.0);
    Quad(sprites, 0x112, column115, row46, 60.0f, 40.0f, 0.9375f, 0.625f);
    {
        const auto inner = OwnAccess(context);
        if ((own.characterClass & 7u) != 3u)
        {
            SetWarningColor(sprites, equipment[2]);
            Quad(sprites, 0x107, F32(static_cast<double>(left) + 75.0), row46, 40.0f, 40.0f, 0.625f, 0.625f);
        }
    }
    SetWarningColor(sprites, equipment[3]);
    const float row89 = F32(static_cast<double>(top) + 89.0);
    const float column75 = F32(static_cast<double>(left) + 75.0);
    Quad(sprites, 0x108, column75, row89, 40.0f, 60.0f, 0.625f, 0.9375f);
    SetWarningColor(sprites, equipment[4]);
    const float row152 = F32(static_cast<double>(top) + 152.0);
    Quad(sprites, 0x109, column75, row152, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[0]);
    Quad(sprites, 0x10a, column15, row89, 40.0f, 60.0f, 0.625f, 0.9375f);
    SetWarningColor(sprites, equipment[1]);
    const float column134 = F32(static_cast<double>(left) + 134.0);
    Quad(sprites, 0x114, column134, row89, 40.0f, 60.0f, 0.625f, 0.9375f);
    SetWarningColor(sprites, equipment[5]);
    Quad(sprites, 0x10b, column15, row152, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[6]);
    Quad(sprites, 0x10c, column134, row152, 40.0f, 40.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[9]);
    const float column55 = F32(static_cast<double>(left) + 55.0);
    Quad(sprites, 0x10d, column55, row89, 20.0f, 20.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[10]);
    Quad(sprites, 0x10e, column55, row152, 20.0f, 20.0f, 0.625f, 0.625f);
    SetWarningColor(sprites, equipment[11]);
    Quad(sprites, 0x10e, column115, row152, 20.0f, 20.0f, 0.625f, 0.625f);
}

// 0x5e3660(x, y, grilla, columnas, filas): una casilla de 20 × 20 por lugar, con el color del aviso de lo que haya
// (también de las vacías): Item_box (0x115) vacía, InventoryBox2 (0x116) con un tipo.
void DrawTheGridBoxes(Render::SpriteBatch& sprites, float x, float y, std::span<const World::ItemState> cells,
                      std::size_t columns, std::size_t rows)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        const float top = F32(static_cast<double>(static_cast<float>(row * 20)) + static_cast<double>(y));
        for (std::size_t column = 0; column < columns; ++column)
        {
            const float left = F32(static_cast<double>(static_cast<float>(column * 20)) + static_cast<double>(x));
            const World::ItemState& item = cells[row * columns + column];
            SetWarningColor(sprites, item);
            if (item.id == EmptyOwnItemId)
                Quad(sprites, 0x115, left, top, 20.0f, 20.0f, 1.0f, 1.0f);
            else
                Quad(sprites, 0x116, left, top, 20.0f, 20.0f, 0.625f, 0.6666667f);
        }
    }
}

// 0x5f26a7..0x5f2751: el zen con comas cada tres cifras.
std::string FormatZen(std::uint32_t zen)
{
    const std::uint32_t units = zen % 1000u;
    const std::uint32_t thousands = zen % 1000000u / 1000u;
    const std::uint32_t millions = zen % 1000000000u / 1000000u;
    if (zen >= 1000000000u)
        return std::format("{},{:03},{:03},{:03}", zen / 1000000000u, millions, thousands, units);
    if (zen >= 1000000u)
        return std::format("{},{:03},{:03}", millions, thousands, units);
    if (zen >= 1000u)
        return std::format("{},{:03}", thousands, units);
    return std::format("{}", units);
}

// 0x5f2771..0x5f27ef: el color del zen por cuánto es.
std::uint32_t ZenColor(std::uint32_t zen)
{
    if (zen >= 10000000u)
        return 0xff0000ffu;
    if (zen >= 1000000u)
        return 0xff0096ffu;
    if (zen >= 100000u)
        return 0xff18c900u;
    return 0xff96dcffu;
}

// Un botón de 24 × 24 al pie de la ventana y su ayuda con el mouse encima, en (_ftol(x), _ftol(y) − arriba).
Core::Expected<void, std::string> DrawFootButton(GameHudContext& context, std::uint16_t slot, float x, float y,
                                                 std::size_t help, int above, std::string_view where)
{
    Quad(context.sprites, slot, x, y, 24.0f, 24.0f, 0.75f, 0.75f);
    if (!MouseInSquare(context, x, y, 24.0f))
        return {};
    SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    auto text = TextOf(context, help, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    DrawHelp(context, Truncate(x), Truncate(y) - above, text.Value());
    return {};
}

// 0x40e720(x, y), el método del objeto de los sets ([0x6eed3c]): el cartel de setitem y "[texto 989]" dos veces,
// en negro y corrido un punto, y encima en dorado con alguna opción de set (+0x9348 o +0x9349) o en gris.
Core::Expected<void, std::string> DrawTheSetLabel(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    const float top = F32(static_cast<double>(y) + 22.0);
    Quad(sprites, 0x652, F32(static_cast<double>(x) + 14.0), top, 162.0f, 20.0f, 0.6328125f, 0.625f);
    SelectFont(context, Render::Text::FontKind::Bold);
    context.hud.textColors = Render::Text::TextColors{.text = 0xff000000u, .background = 0x64u};
    auto set = TextOf(context, SetText, "0x40e786");
    if (!set.HasValue())
        return Core::Unexpected(set.Error());
    const std::string label = std::format("[{}]", set.Value());
    DrawTextCenteredAt(context, x + 0x60, Truncate(static_cast<double>(top) + 3.0), label);
    context.hud.textColors.text = 0xffffffffu;
    const ItemSetOptionList& list = context.world.ItemSets().list;
    if (list.count9348 == 0 && list.count9349 == 0)
        sprites.SetColor(Color{0.5f, 0.5f, 0.5f, 1.0f});
    else
        sprites.SetColor(Color{1.0f, 0.8f, 0.1f, 1.0f});
    DrawTextCenteredAt(context, x + 0x5f, Truncate(static_cast<double>(top) + 2.0), label);
    sprites.SetColor(White);
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheInventory(GameHudContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::InventoryPanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    HudState& hud = context.hud;
    std::int32_t x = 0x1c2;
    if (panels.IsOpen(UI::CharacterPanel) || panels.IsOpen(UI::PartyPanel))
        x = 0x104;
    constexpr std::int32_t y = 0;
    hud.inventoryX = x;
    hud.inventoryY = y;
    DrawWindowBack(sprites, x, y, false);
    DrawTheEquipmentSlots(context, x, y);
    DrawTheGridBoxes(sprites, F32(static_cast<double>(x) + 15.0), F32(static_cast<double>(y) + 200.0),
                     world.Inventory().cells, InventoryColumns, InventoryColumns);
    sprites.SetColor(White);
    // 0x5f23e1..0x5f2441: el título con la fuente gruesa, en blanco sobre gris oscuro, centrado en 120 de 640.
    SelectFont(context, Render::Text::FontKind::Bold);
    hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff141414u};
    auto title = TextOf(context, InventoryTitleText, "0x5f241c");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    const auto screenWidth = static_cast<std::uint32_t>(sprites.Screen().width);
    DrawTextCenteredInBox(context, x + 0x23, y + 0xc, title.Value(), static_cast<int>(screenWidth * 120u / 640u));
    // 0x5f2441..0x5f2484: el fondo del zen.
    const float zenX = F32(static_cast<double>(x) + 50.0);
    const float zenY = F32(static_cast<double>(y) + static_cast<double>(367.7f));
    Quad(sprites, 0x10f, zenX, zenY, 113.0f, 18.0f, 0.8828125f, 0.5625f);
    SelectFont(context, Render::Text::FontKind::Bold);
    std::uint32_t zen = 0;
    {
        // 0x5f249d..0x5f26a2: el zen de +0xecc, leído con el registro abierto.
        const auto own = OwnAccess(context);
        zen = world.OwnCharacter().zen;
    }
    const std::string amount = FormatZen(zen);
    hud.textColors = Render::Text::TextColors{.text = 0xff96dcffu, .background = 0xff282828u};
    const std::int32_t textY = Truncate(zenY) + 3;
    const std::int32_t textX = Truncate(zenX);
    auto zenLabel = TextOf(context, ZenText, "0x5f2795");
    if (!zenLabel.HasValue())
        return Core::Unexpected(zenLabel.Error());
    DrawText(context, textX - 30, textY, zenLabel.Value());
    hud.textColors.text = ZenColor(zen);
    DrawText(context, textX + 10, textY, amount);
    // 0x5f27ff..0x5f28f5: cerrar, sin el comercio y sin la caja del entrenador.
    const UI::DelegatedPanels& delegated = runtime.panels.delegated;
    const float footY = F32(static_cast<double>(y) + 395.0);
    if (!panels.IsOpen(UI::TradePanel) && delegated.trainer != TrainerWithBox)
    {
        if (auto close = DrawFootButton(context, 0x118, F32(static_cast<double>(x) + 20.0), footY, CloseHelpText, 13,
                                        "0x5f28d6");
            !close.HasValue())
            return close;
    }
    std::uint16_t level = 0;
    {
        // 0x5f28f8..0x5f2bdc: el nivel de [0x7681eb0] + 0xe, leído con el registro abierto.
        const auto own = OwnAccess(context);
        level = world.OwnCharacter().level;
    }
    // 0x5f2be1..0x5f2da9: reparar, sin ninguna otra ventana, sin 0x78abb00 y desde el nivel 50.
    if (!panels.IsOpen(UI::TradePanel) && !panels.IsOpen(UI::NpcShopPanel) && !panels.IsOpen(UI::WarehousePanel) &&
        !panels.IsOpen(UI::ChaosBoxPanel) && !panels.IsOpen(UI::CastleSiegeListPanel) &&
        !panels.IsOpen(UI::EventNpcPanel) && !panels.IsOpen(UI::SecondEventNpcPanel) &&
        delegated.trainer != TrainerWithBox && runtime.panels.field78abb00 == 0 && level > 0x31)
    {
        const std::uint16_t slot = runtime.panels.repairMode == 0 ? 0x11e : 0x11f;
        if (auto repair = DrawFootButton(context, slot, F32(static_cast<double>(x) + 48.0), footY, RepairHelpText, 12,
                                         "0x5f2d8a");
            !repair.HasValue())
            return repair;
    }
    // 0x5f2dac..0x5f3008: la tienda personal, con las mismas ventanas en otro orden y desde el nivel 6.
    const float shopX = F32(static_cast<double>(x) + 76.0);
    if (!panels.IsOpen(UI::TradePanel) && !panels.IsOpen(UI::NpcShopPanel) && delegated.trainer != TrainerWithBox &&
        !panels.IsOpen(UI::WarehousePanel) && !panels.IsOpen(UI::ChaosBoxPanel) &&
        !panels.IsOpen(UI::CastleSiegeListPanel) && !panels.IsOpen(UI::EventNpcPanel) &&
        !panels.IsOpen(UI::SecondEventNpcPanel) && runtime.panels.field78abb00 == 0 && level > 5)
    {
        const bool shopOpen = panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel);
        Quad(sprites, shopOpen ? 0x658 : 0x657, shopX, footY, 24.0f, 24.0f, 0.75f, 0.75f);
        if (MouseInSquare(context, shopX, footY, 24.0f))
        {
            SelectFont(context, Render::Text::FontKind::Normal);
            hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0xff000000u};
            const bool open = panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel);
            auto help = TextOf(context, open ? ShopOpenHelpText : OpenShopHelpText, "0x5f2fc4");
            if (!help.HasValue())
                return Core::Unexpected(help.Error());
            DrawHelp(context, Truncate(shopX), Truncate(footY) - 12, help.Value());
        }
    }
    return DrawTheSetLabel(context, x, y);
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// Los ítems 3D y las cifras de las grillas (0x5e3760).

// 0x5e2fc0: los lugares del equipo en su orden y sus cajas: la esquina de la ventana (0x78a5a14 y 0x78a5a10) más los
// floats de la tabla (fild, fadd y fstp; el 3, además, fsub de 10, 0x6b87a8). El 6, el 9, el 10 y el 11 van por
// 0x5e2d00 con la esquina guardada en float antes de sumar: con estos enteros, lo mismo.
struct EquipmentBox
{
    std::size_t slot = 0;
    float x = 0.0f;
    float y = 0.0f;
    float lift = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

constexpr std::array<EquipmentBox, 12> EquipmentBoxes{{
    {.slot = 8, .x = 15.0f, .y = 46.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 7, .x = 115.0f, .y = 46.0f, .width = 60.0f, .height = 40.0f},
    {.slot = 2, .x = 75.0f, .y = 46.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 3, .x = 75.0f, .y = 89.0f, .lift = 10.0f, .width = 40.0f, .height = 60.0f},
    {.slot = 4, .x = 75.0f, .y = 152.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 0, .x = 15.0f, .y = 89.0f, .width = 40.0f, .height = 60.0f},
    {.slot = 1, .x = 134.0f, .y = 89.0f, .width = 40.0f, .height = 60.0f},
    {.slot = 5, .x = 15.0f, .y = 152.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 6, .x = 134.0f, .y = 152.0f, .width = 40.0f, .height = 40.0f},
    {.slot = 9, .x = 55.0f, .y = 89.0f, .width = 20.0f, .height = 20.0f},
    {.slot = 10, .x = 55.0f, .y = 152.0f, .width = 20.0f, .height = 20.0f},
    {.slot = 11, .x = 115.0f, .y = 152.0f, .width = 20.0f, .height = 20.0f},
}};

// 0x5e3760(x, y, grilla, columnas, filas, 0): el ítem de cada casilla de arriba a la izquierda (+0x3c > 0), en la
// casilla ((float)(20 · columna) + x, (float)(20 · fila) + y) con el fstp y del tamaño de Item.bmd.
Core::Expected<void, std::string> DrawTheGridModels(GameHudContext& context, float x, float y,
                                                    std::span<const World::ItemState> cells, std::size_t columns,
                                                    std::size_t rows)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        const float top = F32(static_cast<double>(row * 20) + static_cast<double>(y));
        for (std::size_t column = 0; column < columns; ++column)
        {
            const World::ItemState& item = cells[row * columns + column];
            if (item.id == EmptyOwnItemId || static_cast<std::int32_t>(item.occupied) <= 0)
                continue;
            const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
            auto box = ItemSizeOf(context, type, "0x5e37fd");
            if (!box.HasValue())
                return Core::Unexpected(box.Error());
            box.Value().x = F32(static_cast<double>(column * 20) + static_cast<double>(x));
            box.Value().y = top;
            if (auto drawn = DrawUiItem(context, box.Value(), type, item.scalar1Low, item.flags1b, item.flags1c, false);
                !drawn.HasValue())
                return drawn;
        }
    }
    return {};
}

// 0x5e3760(x, y, grilla, columnas, filas, 1): las pociones de 0x1c00 a 0x1c08 con la durabilidad en 2 o más la
// muestran (9 × 10, 13 a la derecha); los demás, si 0x402b30 da 2 o más (los paquetes 0x181e y 0x181f), la cuenta
// ((nivel + 1) · 5) · 2 (7 × 8, 14 a la derecha). En (1; 0,9; 0,7).
void DrawTheGridCounts(Render::SpriteBatch& sprites, float x, float y, std::span<const World::ItemState> cells,
                       std::size_t columns, std::size_t rows)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        const float top = F32(static_cast<double>(static_cast<float>(row * 20)) + static_cast<double>(y));
        for (std::size_t column = 0; column < columns; ++column)
        {
            const World::ItemState& item = cells[row * columns + column];
            if (item.id == EmptyOwnItemId || static_cast<std::int32_t>(item.occupied) <= 0)
                continue;
            const auto type = static_cast<std::int16_t>(item.id);
            const float left = F32(static_cast<double>(static_cast<float>(column * 20)) + static_cast<double>(x));
            if (type < 0x1c00 || type > 0x1c08 || item.durability < 2)
            {
                // 0x402b30: 2 con 0x181e y 3 con 0x181f; con los demás, menos de 2.
                if (type != 0x181e && type != 0x181f)
                    continue;
                sprites.SetColor(NumberColor);
                const auto steps = static_cast<std::int32_t>((item.scalar1Low >> 3u) & 0xfu);
                UI::DrawNumber(sprites, F32(static_cast<double>(left) + 14.0), top, (steps * 5 + 5) * 2, 7.0f, 8.0f);
                continue;
            }
            sprites.SetColor(NumberColor);
            UI::DrawNumber(sprites, F32(static_cast<double>(left) + 13.0), top, item.durability, 9.0f, 10.0f);
        }
    }
}
} // namespace

Core::Expected<void, std::string> DrawTheGridItems(GameHudContext& context, UI::PanelRegistry& panels)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const UI::DelegatedPanels& delegated = runtime.panels.delegated;
    if (panels.IsOpen(UI::InventoryPanel))
    {
        // 0x5e2fc0: el equipo, con el registro propio abierto alrededor (los accesos de cada lugar, 0x4e79a0 y
        // 0x40f370 o los de 0x5e2d00, quedan adentro y no sacan nada; el color de 0x5e22d0 de cada lugar lo pisa el
        // dibujo del modelo). Cada uno con su tipo y +0x3c > 0, en su caja.
        {
            const auto own = OwnAccess(context);
            const auto& equipment = world.OwnCharacter().equipment;
            for (const EquipmentBox& place : EquipmentBoxes)
            {
                const World::ItemState& item = equipment[place.slot];
                if (item.id == EmptyOwnItemId || static_cast<std::int32_t>(item.occupied) <= 0)
                    continue;
                const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
                const UiItemBox box{.x =
                                        F32(static_cast<double>(context.hud.inventoryX) + static_cast<double>(place.x)),
                                    .y = F32(static_cast<double>(context.hud.inventoryY) +
                                             static_cast<double>(place.y) - static_cast<double>(place.lift)),
                                    .width = place.width,
                                    .height = place.height};
                if (auto drawn = DrawUiItem(context, box, type, item.scalar1Low, item.flags1b, item.flags1c, false);
                    !drawn.HasValue())
                    return drawn;
            }
        }
        // 0x5f7624: la grilla, en la esquina más 15 (0x6b87bc) y 200 (0x6b8928).
        if (auto grid = DrawTheGridModels(context, F32(static_cast<double>(context.hud.inventoryX) + 15.0),
                                          F32(static_cast<double>(context.hud.inventoryY) + 200.0),
                                          world.Inventory().cells, InventoryColumns, InventoryColumns);
            !grid.HasValue())
            return grid;
    }
    if (panels.IsOpen(UI::NpcShopPanel))
        return Core::Unexpected(std::string("0x5f769b: los ítems de la tienda (0x78a7070) son de la CL9.3"));
    if (panels.IsOpen(UI::TradePanel))
    {
        // 0x5f76b4..0x5f7717: la grilla del otro (0x78a5a20) en la esquina más 15 y 70, y la propia (0x78a92a8) en 270.
        const TradeState& trade = runtime.social.trade;
        const float left = F32(static_cast<double>(context.hud.tradeX) + TradeGridLeft);
        if (auto other = DrawTheGridModels(context, left, F32(static_cast<double>(context.hud.tradeY) + OtherGridTop),
                                           trade.otherGrid, TradeColumns, TradeRows);
            !other.HasValue())
            return other;
        if (auto own = DrawTheGridModels(context, left, F32(static_cast<double>(context.hud.tradeY) + OwnGridTop),
                                         trade.ownGrid, TradeColumns, TradeRows);
            !own.HasValue())
            return own;
    }
    if (panels.IsOpen(UI::WarehousePanel))
        return Core::Unexpected(std::string("0x5f7756: los ítems del almacén (0x78a7070) son de la CL9.3"));
    if (panels.IsOpen(UI::ChaosBoxPanel))
        return Core::Unexpected(std::string("0x5f77c6: los ítems de la caja del caos (0x7d2d380) son de la CL9.3"));
    if (panels.IsOpen(UI::CastleSiegeListPanel) && delegated.castleSiegeListMode == SiegeListWithInventory)
        return Core::Unexpected(std::string("0x5f7792: 0x6689d0, la marca del asedio del héroe, es de la CL9.5"));
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return Core::Unexpected(std::string("0x5f7821: los ítems de la tienda personal (0x78ab0b8) son de la CL9.4"));
    // 0x44ad40(0).
    if (delegated.trainer == TrainerWithBox)
        return Core::Unexpected(
            std::string("0x5f782b: 0x44ad40, los ítems de la caja del entrenador (0x723c80) son de la CL9.3"));
    return {};
}

Core::Expected<void, std::string> DrawTheGridNumbers(GameHudContext& context, UI::PanelRegistry& panels)
{
    WorldState& world = context.world;
    const UI::DelegatedPanels& delegated = world.Runtime().panels.delegated;
    // 0x5f7849..0x5f78e4: sale sin ninguna grilla abierta, en este orden.
    bool any = panels.IsOpen(UI::InventoryPanel) || panels.IsOpen(UI::NpcShopPanel) ||
               panels.IsOpen(UI::WarehousePanel) || panels.IsOpen(UI::ChaosBoxPanel) || panels.IsOpen(UI::TradePanel);
    if (!any && panels.IsOpen(UI::CastleSiegeListPanel) && delegated.castleSiegeListMode == SiegeListWithInventory)
        return Core::Unexpected(std::string("0x5f78c2: 0x6689d0, la marca del asedio del héroe, es de la CL9.5"));
    if (!any)
        any = panels.IsOpen(UI::TrainerPanel);
    if (!any)
        return {};
    context.sprites.SetBlend(BlendMode::Alpha);
    if (panels.IsOpen(UI::InventoryPanel))
        DrawTheGridCounts(context.sprites, F32(static_cast<double>(context.hud.inventoryX) + 15.0),
                          F32(static_cast<double>(context.hud.inventoryY) + 200.0), world.Inventory().cells,
                          InventoryColumns, InventoryColumns);
    if (panels.IsOpen(UI::NpcShopPanel))
        return Core::Unexpected(std::string("0x5f7973: las cifras de la tienda (0x78a7070) son de la CL9.3"));
    if (panels.IsOpen(UI::TradePanel))
    {
        // 0x5f798c..0x5f79f0: las dos grillas del comercio, como sus ítems.
        const TradeState& trade = world.Runtime().social.trade;
        const float left = F32(static_cast<double>(context.hud.tradeX) + TradeGridLeft);
        DrawTheGridCounts(context.sprites, left, F32(static_cast<double>(context.hud.tradeY) + OtherGridTop),
                          trade.otherGrid, TradeColumns, TradeRows);
        DrawTheGridCounts(context.sprites, left, F32(static_cast<double>(context.hud.tradeY) + OwnGridTop),
                          trade.ownGrid, TradeColumns, TradeRows);
    }
    if (panels.IsOpen(UI::WarehousePanel))
        return Core::Unexpected(std::string("0x5f7a2e: las cifras del almacén (0x78a7070) son de la CL9.3"));
    if (panels.IsOpen(UI::ChaosBoxPanel))
        return Core::Unexpected(std::string("0x5f7a9e: las cifras de la caja del caos (0x7d2d380) son de la CL9.3"));
    if (panels.IsOpen(UI::CastleSiegeListPanel) && delegated.castleSiegeListMode == SiegeListWithInventory)
        return Core::Unexpected(std::string("0x5f7a6a: 0x6689d0, la marca del asedio del héroe, es de la CL9.5"));
    if (panels.IsOpen(UI::PersonalShopPanel) || panels.IsOpen(UI::OtherShopPanel))
        return Core::Unexpected(std::string("0x5f7af9: las cifras de la tienda personal (0x78ab0b8) son de la CL9.4"));
    // 0x44ad40(1).
    if (delegated.trainer == TrainerWithBox)
        return Core::Unexpected(
            std::string("0x5f7b03: 0x44ad40, las cifras de la caja del entrenador (0x723c80) son de la CL9.3"));
    return {};
}

Core::Expected<void, std::string> DrawTheCursorItem(GameHudContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    if (runtime.cursorItemCount <= 0)
        return {};
    const World::ItemState& item = runtime.cursorItem;
    const auto type = static_cast<std::int32_t>(static_cast<std::int16_t>(item.id));
    // El tamaño de Item.bmd, centrado en el mouse: fild del mouse menos la mitad (0x6b882c), con el fstp.
    auto box = ItemSizeOf(context, type, "0x5f7b34");
    if (!box.HasValue())
        return Core::Unexpected(box.Error());
    constexpr double Half = 0.5;
    box.Value().x = F32(static_cast<double>(context.mouse.x) - static_cast<double>(box.Value().width) * Half);
    box.Value().y = F32(static_cast<double>(context.mouse.y) - static_cast<double>(box.Value().height) * Half);
    return DrawUiItem(context, box.Value(), type, item.scalar1Low, item.flags1b, item.flags1c, true);
}

namespace
{
// ---------------------------------------------------------------------------------------------------------------
// La ventana del comercio (0x5f3b40).

// Los textos: 0x76cdab4 (el título), 0x76cdbe0, 0x76cdd0c y 0x76cde38 (las ayudas del zen, del OK y de cerrar),
// 0x76d811c y 0x76d8248 (el nivel y su formato) y 0x76d8374, 0x76d7d98, 0x76d7ec4 y 0x76d7ff0 (los renglones).
constexpr std::size_t TradeTitleText = 226;
constexpr std::size_t TradeMoneyHelpText = 227;
constexpr std::size_t TradeOkHelpText = 228;
constexpr std::size_t TradeCloseHelpText = 229;
constexpr std::size_t TradeLevelText = 368;
constexpr std::size_t TradeLevelFormatText = 369;
constexpr std::size_t TradeWarningText = 370;
constexpr std::size_t TradeFirstLineText = 365;
constexpr std::size_t TradeSecondLineText = 366;
constexpr std::size_t TradeThirdLineText = 367;
// 0x5f3b68: la esquina, que el dibujo pone.
constexpr std::int32_t TradeWindowX = 0x104;
constexpr std::int32_t TradeWindowY = 0;
// 0x5ee220 con 1: Interface\Trading_line.jpg (0x117) de 190 × 10 en y + 225 (0x6ba640).
constexpr double TradingLineTop = 225.0;
// Los botones de 24 × 24 (0x6b8800) con du = dv = 0,75: el OK del otro en (140, 185), el zen en (26, 390), el OK
// propio en (97, 390) y cerrar en (137, 390). La ayuda va 13 arriba.
constexpr float ButtonSize = 24.0f;
constexpr float ButtonUv = 0.75f;
constexpr double OtherOkLeft = 140.0;
constexpr double OtherOkTop = 185.0;
constexpr double MoneyButtonLeft = 26.0;
constexpr double OwnOkLeft = 97.0;
constexpr double CloseLeft = 137.0;
constexpr double FootTop = 390.0;
constexpr int HelpAbove = 13;
// Interface\accept_box01.jpg y accept_box02.jpg (0x61ae60): el OK sin y con la marca.
constexpr std::uint16_t OkOffSlot = 0x122;
constexpr std::uint16_t OkOnSlot = 0x123;
// Money_01 y Money_02, Item_Money y exit_01.
constexpr std::uint16_t MoneySlot = 0x110;
constexpr std::uint16_t MoneyPressedSlot = 0x111;
constexpr std::uint16_t MoneyBoxSlot = 0x10f;
constexpr std::uint16_t CloseSlot = 0x118;
constexpr std::uint16_t TradingLineSlot = 0x117;
// Las cajas del zen: 113 × 18 con du = 0,8828125 y dv = 0,5625 en (50, 160) y (50, 360); el texto 30 a la izquierda
// y la cifra 10 a la derecha, 2 abajo.
constexpr double MoneyBoxLeft = 50.0;
constexpr double OtherMoneyTop = 160.0;
constexpr double OwnMoneyTop = 360.0;
// La flecha (CursorSitDown, 9) de 15 × 24 desde v = 0,4, en (x + 140, y + 28 + sen(T · 0,01)), en (1; 0,86; 0,58).
constexpr std::uint16_t ArrowSlot = 9;
constexpr std::int32_t ArrowTop = 0x1c;
constexpr float ArrowBob = 0.01f;
constexpr Color ArrowColor{1.0f, 0.86f, 0.58f, 1.0f};
// El aviso que late: el alfa de min(sen(T · 0,005) · 200 + 275, 255) sobre 0xd2.
constexpr float WarningPulse = 0.005f;
constexpr double WarningSwing = 200.0;
constexpr double WarningBase = 275.0;
constexpr double WarningMost = 255.0;
constexpr std::uint32_t WarningRed = 0xd2;

using Render::Text::FontKind;
using Render::Text::TextColors;

void TradeQuad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y)
{
    Quad(sprites, slot, x, y, ButtonSize, ButtonSize, ButtonUv, ButtonUv);
}

float TradePlace(std::int32_t corner, double offset)
{
    return F32(static_cast<double>(corner) + offset);
}

// 0x5f3e0b..0x5f3ecf y sus copias: con el mouse sobre el botón, la fuente normal y la ayuda en (_ftol(x), _ftol(y) −
// 13), con los colores que haya.
Core::Expected<void, std::string> TradeHelp(GameHudContext& context, float x, float y, std::size_t help,
                                            std::string_view where)
{
    SelectFont(context, FontKind::Normal);
    auto text = TextOf(context, help, where);
    if (!text.HasValue())
        return Core::Unexpected(text.Error());
    DrawText(context, Truncate(static_cast<double>(x)), Truncate(static_cast<double>(y)) - HelpAbove, text.Value());
    return {};
}

// 0x5f40cd..0x5f428b: la caja del zen con "Zen" y la cifra en miles, del color de cuánto es.
Core::Expected<void, std::string> DrawTradeMoney(GameHudContext& context, float x, float y, std::uint32_t zen)
{
    Quad(context.sprites, MoneyBoxSlot, x, y, 113.0f, 18.0f, 0.8828125f, 0.5625f);
    const std::int32_t textY = Truncate(static_cast<double>(y)) + 2;
    const std::int32_t textX = Truncate(static_cast<double>(x));
    auto label = TextOf(context, ZenText, "0x5f415d");
    if (!label.HasValue())
        return Core::Unexpected(label.Error());
    DrawText(context, textX - 30, textY, label.Value());
    context.hud.textColors.text = ZenColor(zen);
    DrawText(context, textX + 10, textY, FormatZen(zen));
    return {};
}

// 0x5f4507..0x5f4573: el piso del nivel del otro y su color.
std::pair<std::int32_t, std::uint32_t> LevelBracket(std::int32_t level)
{
    if (level >= 300)
        return {300, 0xffff99ffu};
    if (level >= 200)
        return {200, 0xffffe6d2u};
    if (level >= 100)
        return {100, 0xff18c900u};
    if (level >= 50)
        return {50, 0xff0096ffu};
    return {10, 0xff0000ffu};
}

// 0x5f3bf9..0x5f3c8a (CL9 hito 8): las 2000 entradas de la tabla de los gremios (0x787aa48, de 0x58 bytes) con la clave
// distinta de −1 e igual a la del gremio del otro (0x6c9324, la que trae el 37) suben su marca (0x5f16a0(i, 0)), la
// dibujan con el 0x22 en (x − 32, y + 40), de 32 × 32, y escriben el nombre (+0xd) en (x, y + 30) con 0x554240, que
// llama a 0x465950 como 0x554180 sin la caja (*estático*), con la fuente y los colores que dejó lo de antes.
Core::Expected<void, std::string> DrawTheOthersGuild(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    constexpr float MarkLeft = 32.0f;
    constexpr float MarkTop = 40.0f;
    constexpr float MarkSize = 32.0f;
    constexpr std::int32_t NameTop = 0x1e;
    const GuildState& guild = context.world.Runtime().guild;
    const std::int32_t other = context.world.Runtime().social.trade.otherGuild;
    for (std::size_t index = 0; index < GuildTableSize; ++index)
    {
        const GuildTableEntry& entry = guild.table[index];
        if (entry.key == -1 || entry.key != other)
            continue;
        if (context.guildMarks == nullptr)
            return Core::Unexpected(std::string("0x5f3c10: la marca del gremio (0x5f16a0) sin la tabla de las marcas"));
        auto slot = context.guildMarks->Upload(guild, static_cast<std::int32_t>(index), false);
        if (!slot.HasValue())
            return Core::Unexpected(std::move(slot).Error());
        Quad(context.sprites, slot.Value().value, F32(static_cast<double>(x) - static_cast<double>(MarkLeft)),
             F32(static_cast<double>(y) + static_cast<double>(MarkTop)), MarkSize, MarkSize, 1.0f, 1.0f);
        const auto end = std::ranges::find(entry.guildName, '\0');
        DrawText(context, x, y + NameTop,
                 std::string_view(entry.guildName.data(), static_cast<std::size_t>(end - entry.guildName.begin())));
    }
    return {};
}

// El nombre del otro (0x78aba1c, diez bytes y el 0 que pone el 37), hasta el primer 0.
std::string_view NameUntilZero(std::span<const std::byte> name)
{
    const auto end = std::ranges::find(name, std::byte{0});
    return {reinterpret_cast<const char*>(name.data()), static_cast<std::size_t>(end - name.begin())};
}
} // namespace

Core::Expected<void, std::string> DrawTheTradeWindow(GameHudContext& context, UI::PanelRegistry& panels)
{
    if (!panels.IsOpen(UI::TradePanel))
        return {};
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    const WorldRuntime& runtime = context.world.Runtime();
    const TradeState& trade = runtime.social.trade;
    hud.tradeX = TradeWindowX;
    hud.tradeY = TradeWindowY;
    const std::int32_t x = hud.tradeX;
    const std::int32_t y = hud.tradeY;
    // 0x5f3b78: 0x5ee220(x, y, 1), el fondo y la línea del comercio.
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    Quad(sprites, TradingLineSlot, static_cast<float>(x),
         F32(static_cast<double>(static_cast<float>(y)) + TradingLineTop), 190.0f, 10.0f, 0.7421875f, 0.625f);
    // 0x5f3b7d..0x5f3bda: los fondos de las dos grillas (0x5e3660).
    const float gridLeft = TradePlace(x, TradeGridLeft);
    DrawTheGridBoxes(sprites, gridLeft, TradePlace(y, OtherGridTop), trade.otherGrid, TradeColumns, TradeRows);
    DrawTheGridBoxes(sprites, gridLeft, TradePlace(y, OwnGridTop), trade.ownGrid, TradeColumns, TradeRows);
    sprites.SetColor(White);
    if (auto guild = DrawTheOthersGuild(context, x, y); !guild.HasValue())
        return guild;
    sprites.SetColor(White);
    // 0x5f3ca1..0x5f3cff: el título en negrita, centrado en 120 · W / 640.
    SelectFont(context, FontKind::Bold);
    hud.textColors = TextColors{.text = 0xffdcdcdcu, .background = 0xff141414u};
    auto title = TextOf(context, TradeTitleText, "0x5f3cf8");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    WindowDrawing::DrawTitle(context, x + 0x23, y + 0xc, title.Value());
    // 0x5f3d04..0x5f3d9e: el OK del otro, en rojo mientras 0x78abac4 está sobre 0.
    const Color okColor = trade.field78abac4 > 0 ? Color{1.0f, 0.0f, 0.0f, 1.0f} : White;
    sprites.SetColor(okColor);
    TradeQuad(sprites, trade.otherOk == 0 ? OkOffSlot : OkOnSlot, TradePlace(x, OtherOkLeft),
              TradePlace(y, OtherOkTop));
    sprites.SetColor(White);
    // 0x5f3da9..0x5f3ecf: el zen, Money_02 con el izquierdo abajo y su ayuda.
    hud.textColors = TextColors{.text = 0xffffffffu, .background = 0xff000000u};
    const float footY = TradePlace(y, FootTop);
    const float moneyX = TradePlace(x, MoneyButtonLeft);
    TradeQuad(sprites, MoneySlot, moneyX, footY);
    if (MouseInSquare(context, moneyX, footY, ButtonSize))
    {
        if (context.leftDown)
            TradeQuad(sprites, MoneyPressedSlot, moneyX, footY);
        if (auto help = TradeHelp(context, moneyX, footY, TradeMoneyHelpText, "0x5f3eb0"); !help.HasValue())
            return help;
    }
    // 0x5f3ed3..0x5f3ff4: el OK propio, en rojo mientras espera (0x78abac8), y su ayuda.
    const float okX = TradePlace(x, OwnOkLeft);
    sprites.SetColor(trade.okWait > 0 ? Color{1.0f, 0.0f, 0.0f, 1.0f} : White);
    TradeQuad(sprites, trade.ownOk == 0 ? OkOffSlot : OkOnSlot, okX, footY);
    sprites.SetColor(White);
    if (MouseInSquare(context, okX, footY, ButtonSize))
    {
        if (auto help = TradeHelp(context, okX, footY, TradeOkHelpText, "0x5f3fd5"); !help.HasValue())
            return help;
    }
    // 0x5f3ff8..0x5f40c9: cerrar y su ayuda.
    const float closeX = TradePlace(x, CloseLeft);
    TradeQuad(sprites, CloseSlot, closeX, footY);
    if (MouseInSquare(context, closeX, footY, ButtonSize))
    {
        if (auto help = TradeHelp(context, closeX, footY, TradeCloseHelpText, "0x5f40aa"); !help.HasValue())
            return help;
    }
    // 0x5f40cd..0x5f442b: el zen del otro (0x78abab4) y el propio (0x78abab8), en negrita.
    hud.textColors = TextColors{.text = 0xff96dcffu, .background = 0x80282828u};
    const float moneyBoxX = TradePlace(x, MoneyBoxLeft);
    SelectFont(context, FontKind::Bold);
    if (auto other = DrawTradeMoney(context, moneyBoxX, TradePlace(y, OtherMoneyTop), trade.otherMoney);
        !other.HasValue())
        return other;
    hud.textColors.text = 0xff96dcffu;
    if (auto own = DrawTradeMoney(context, moneyBoxX, TradePlace(y, OwnMoneyTop), trade.ownMoney); !own.HasValue())
        return own;
    // 0x5f4430..0x5f447b: el nombre del otro (0x78aba1c), en la grande.
    hud.textColors = TextColors{.text = 0xffffe6d2u, .background = 0x80282828u};
    SelectFont(context, FontKind::BigBold);
    DrawText(context, x + 0x14, y + 0x2d, NameUntilZero(trade.otherName));
    // 0x5f4489..0x5f4505: la flecha que sube y baja junto al OK del otro.
    sprites.SetColor(ArrowColor);
    const double bob = std::sin(static_cast<double>(context.worldClock) * static_cast<double>(ArrowBob));
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{ArrowSlot},
                                      .x = TradePlace(x, OtherOkLeft),
                                      .y = F32(bob + static_cast<double>(y + ArrowTop)),
                                      .width = 15.0f,
                                      .height = 24.0f,
                                      .u = 0.0f,
                                      .v = 0.4f,
                                      .du = 1.0f,
                                      .dv = 1.0f,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
    sprites.SetColor(White);
    // 0x5f4507..0x5f45d8: el nivel del otro (0x78abab0, con signo) por su piso, con el formato del texto 369.
    const auto [floor, levelColor] = LevelBracket(static_cast<std::int32_t>(trade.otherLevel));
    hud.textColors.text = levelColor;
    auto levelFormat = TextOf(context, TradeLevelFormatText, "0x5f4577");
    if (!levelFormat.HasValue())
        return Core::Unexpected(levelFormat.Error());
    const std::string level = UI::Sprintf(levelFormat.Value(), floor);
    SelectFont(context, FontKind::Bold);
    auto levelLabel = TextOf(context, TradeLevelText, "0x5f45a8");
    if (!levelLabel.HasValue())
        return Core::Unexpected(levelLabel.Error());
    DrawText(context, x + 0x8c, y + 0x2d, levelLabel.Value());
    DrawText(context, x + 0x8c, y + 0x37, level);
    // 0x5f45dd..0x5f461e: el nombre del héroe publicado.
    hud.textColors.text = 0xffffe6d2u;
    SelectFont(context, FontKind::Bold);
    const ActorState& hero = context.world.Actors()[runtime.hero];
    DrawText(context, x + 0x14, y + 0xfa, HeroNameOf(hero));
    // 0x5f4623..0x5f470b: el aviso que late y los tres renglones.
    double alpha =
        std::sin(static_cast<double>(context.worldClock) * static_cast<double>(WarningPulse)) * WarningSwing +
        WarningBase;
    if (WarningMost < alpha)
        alpha = WarningMost;
    hud.textColors.text = (static_cast<std::uint32_t>(Truncate(alpha)) << 24u) + WarningRed;
    for (const auto& [text, left, top, where] :
         {std::tuple{TradeWarningText, 0x14, 0xb9, "0x5f4682"}, std::tuple{TradeFirstLineText, 0x2d, 0xb9, "0x5f46a8"},
          std::tuple{TradeSecondLineText, 0x14, 200, "0x5f46d9"},
          std::tuple{TradeThirdLineText, 0x14, 0xd7, "0x5f4704"}})
    {
        auto line = TextOf(context, text, where);
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
        DrawText(context, x + left, y + top, line.Value());
        hud.textColors.text = 0xff96dcffu;
    }
    return {};
}
} // namespace Client::Scenes
