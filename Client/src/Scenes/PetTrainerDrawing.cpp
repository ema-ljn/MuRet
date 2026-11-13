#include "Scenes/PetTrainerDrawing.h"

#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ChaosMix.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/PetTrainer.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/SprintfFormat.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
using Render::BlendMode;
using Render::Text::FontKind;
using Render::Text::TextColors;
using WindowDrawing::DrawCenteredText;
using WindowDrawing::DrawTitle;
using WindowDrawing::Quad;
using WindowDrawing::SelectFont;

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
// 0x724580 en 4: nada (*estático*: nadie lo escribe así).
constexpr std::int32_t IdleMode = 4;

// Los textos de Text.bmd (0x76bd1dc + 300 · n).
constexpr std::size_t CloseHelpText = 1002;   // 0x7706814
constexpr std::size_t HorseText = 1187;       // 0x77140e0
constexpr std::size_t MenuText = 1202;        // 0x7715274
constexpr std::size_t NameText = 1203;        // 0x77153a0
constexpr std::size_t FirstButtonText = 1204; // 0x77154cc, y 1205 el segundo
constexpr std::size_t BoxTitleText = 1205;    // 0x77155f8
constexpr std::size_t MixText = 1212;         // 0x7715e2c
constexpr std::size_t NoMixText = 1213;       // 0x7715f58
constexpr std::size_t RavenText = 1214;       // 0x7716084
constexpr std::size_t TitleText = 1227;       // 0x7716fc0
constexpr std::size_t RecoveryText = 1228;    // 0x77170ec
constexpr std::size_t NoPetText = 1229;       // 0x7717218
constexpr std::size_t FullText = 1230;        // 0x7717344
constexpr std::size_t ShortText = 1231;       // 0x7717470
constexpr std::size_t CostText = 1232;        // 0x771759c
constexpr std::size_t RateText = 584;         // 0x76e7e3c
constexpr std::size_t JewelText = 586;        // 0x76e8094
constexpr std::size_t JewelSecondText = 587;  // 0x76e81c0
constexpr std::size_t PriceText = 1622;       // 0x7733ea4
constexpr std::size_t TaxText = 1623;         // 0x7733fd0

// Los buffers de la pila hasta lo que sigue: [EBP − 0x68] de 0x44b610 y 0x44d680 y [EBP − 0x84] de 0x44c850 (0x64
// bytes hasta el local de 0x44c850 [EBP − 0x20]), [EBP − 0x64] de 0x44b190 (hasta el EBP guardado) y [EBP − 0x1a8]
// del impuesto de 0x44d680 (0x100 hasta el precio con impuesto).
constexpr std::size_t LineRoom = 0x64;
constexpr std::size_t TaxRoom = 0x100;

// 0x44b619..0x44b72f y 0x44c85c..0x44c96e: el fondo de los modos 1 y 2.
constexpr std::uint16_t BackSlot = 0xfb;
constexpr std::uint16_t LineSlot = 0xfc;
constexpr std::uint16_t ButtonSlot = 0xf0;
constexpr float BackLeft = 213.0f;
constexpr float BackWidth = 213.0f;
constexpr float BackTileHeight = 40.0f;
constexpr std::int32_t BackTileStep = 0x28;
constexpr std::int32_t BackTilesEnd = 0xc8;
constexpr float BackTop = 90.0f; // 0x6b8948
constexpr float LineHeight = 5.0f;
constexpr float TopLine = 90.0f;
constexpr float BottomLine = 290.0f;
constexpr float BackDu = 0.83203f; // 0x3f54ffeb
constexpr float BackDv = 0.625f;
constexpr float ButtonLeft = 259.0f;
constexpr float ButtonWidth = 120.0f;
constexpr float ButtonHeight = 22.0f;
constexpr float ButtonDv = 1.0f;
// 0x44b6e8..0x44b72f: los del menú en 180 + 30 · i (0x6b86bc y 0x6b8858); 0x44c923..0x44c96e: los de recuperar en
// 0x96 + 0x3c · i.
constexpr float MenuButtonTop = 180.0f;
constexpr float MenuButtonStep = 30.0f;
constexpr std::int32_t RecoveryButtonTop = 0x96;
constexpr std::int32_t RecoveryButtonStep = 0x3c;
// Los textos: el título y lo centrado en 0x13f.
constexpr std::int32_t TitleLeft = 0xda;
constexpr std::int32_t TitleTop = 0x64;
constexpr std::uint32_t TitleBoxDesignWidth = 203;
constexpr std::int32_t CenterX = 0x13f;
constexpr std::int32_t MenuTextTop = 0x78;
constexpr std::int32_t NameTop = 0x8c;
// 0x44b7ff..0x44b867: los rótulos del menú en 0xb9 + 0x1e · i, en gruesa con el mouse en [0x103, 0x17b) × [y − 5,
// y + 0x11).
constexpr std::int32_t MenuLabelTop = 0xb9;
constexpr std::int32_t MenuLabelStep = 0x1e;
constexpr std::int32_t MenuLabelsEnd = 0xf5;
constexpr std::int32_t HoverLeft = 0x103;
constexpr std::int32_t HoverRight = 0x17b;
constexpr std::int32_t HoverAbove = 5;
constexpr std::int32_t HoverBelow = 0x11;
// 0x44ca3a..0x44ca6c: los de recuperar, con el mouse en [y, y + 0x16); el rótulo 5 más abajo y el precio 0x1e.
constexpr std::int32_t RecoveryHoverHeight = 0x16;
constexpr std::int32_t RecoveryLabelBelow = 5;
constexpr std::int32_t PriceBelow = 0x1e;
// El botón de cerrar de los modos 1 y 2 (0x44b869, 0x44cf54) y su ayuda en [0x18a, 0x1a2) × [0x109, 0x121).
constexpr std::uint16_t CloseSlot = 0x118;
constexpr float SmallCloseLeft = 394.0f;
constexpr float SmallCloseTop = 265.0f;
constexpr float CloseSize = 24.0f;
constexpr float CloseUv = 0.75f;
constexpr std::int32_t SmallHelpLeft = 0x18a;
constexpr std::int32_t SmallHelpRight = 0x1a2;
constexpr std::int32_t SmallHelpTop = 0x109;
constexpr std::int32_t SmallHelpBottom = 0x121;
constexpr std::int32_t SmallHelpY = 0xfc;

// 0x44d6a5..0x44db26: la caja.
constexpr std::int32_t BoxLeft = 0x104;
constexpr std::int32_t BoxTop = 0;
constexpr float GridLeft = 275.0f;
constexpr float GridTop = 110.0f;
constexpr std::size_t GridColumns = 8;
constexpr std::size_t GridRows = 4;
constexpr std::uint16_t BarSlot = 0x117;
constexpr float BarLeft = 260.0f;
constexpr float FirstBarTop = 100.0f;
constexpr float SecondBarTop = 190.0f;
constexpr float BarWidth = 190.0f;
constexpr float BarHeight = 10.0f;
constexpr float BarDu = 0.7421875f;
constexpr float BarDv = 0.625f;
constexpr std::uint16_t MixSlot = 0x124;
constexpr std::uint16_t MixDownSlot = 0x125;
constexpr float MixLeft = 335.0f;
constexpr float MixTop = 300.0f;
constexpr float MixWidth = 44.0f;
constexpr float MixHeight = 32.0f;
constexpr float MixDu = 0.6875f;
constexpr float MixDv = 1.0f;
constexpr std::int32_t MixHelpX = 0x14f;
constexpr std::int32_t MixHelpY = 0x11f;
constexpr float BoxCloseLeft = 280.0f;
constexpr float BoxCloseTop = 395.0f;
constexpr std::int32_t BoxHelpX = 0x118;
constexpr std::int32_t BoxHelpY = 0x17e;
constexpr std::int32_t BoxTitleLeft = 0x127;
constexpr std::int32_t BoxTitleTop = 8;
constexpr std::int32_t BoxTaxTop = 0x14;
constexpr std::int32_t BoxCenterX = 0x163;
constexpr std::int32_t BoxNameTop = 0x32;
// [EBP − 4]: 65 con una mezcla de 0..2 (50 con otra, que corta); 15 por renglón (0x6b87bc) y 120 más antes de los
// avisos (0x6b86c8).
constexpr float LinesAfterName = 65.0f;
constexpr float LineStep = 15.0f;
constexpr float WarningsGap = 120.0f;
// 0x44d96a: la chance de una mezcla.
constexpr std::int32_t MixRate = 0x3c;

// 0x6c8e5c y 0x6c8e64 (el texto y el fondo).
constexpr TextColors TitleColors{.text = 0, .background = 0xc8c8c8c8u};
constexpr TextColors MenuTextColors{.text = 0xc8c8c8c8u, .background = 0};
constexpr TextColors RecoveryTextColors{.text = 0xffc8c8c8u, .background = 0};
constexpr std::uint32_t LabelColor = 0xff67bfdfu;
constexpr std::uint32_t NoPetColor = 0xff1933ffu;
constexpr TextColors ShortColors{.text = 0xffffffffu, .background = 0xc80000ffu};
constexpr TextColors BoxTitleColors{.text = 0xffdcdcdcu, .background = 0};
constexpr std::uint32_t KindColor = 0xff19ccffu;
constexpr std::uint32_t NoKindColor = 0xff3030ffu;

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

// Lo que sprintf deja en un buffer de `room` bytes de la pila, si entra con su cero.
Core::Expected<std::string, std::string> Fits(std::string text, std::size_t room, std::string_view where)
{
    if (text.size() >= room)
        return Core::Unexpected(std::format("{}: el sprintf escribe {} bytes en un buffer de {:#x} y pisa la pila",
                                            where, text.size() + 1, room));
    return text;
}

// x en [left, right) e y en [top, bottom) con el mouse entero (los CMP del original).
bool MouseIn(const GameHudContext& context, std::int32_t left, std::int32_t right, std::int32_t top,
             std::int32_t bottom)
{
    return context.mouse.x >= left && context.mouse.x < right && context.mouse.y >= top && context.mouse.y < bottom;
}

// Los FCOM del modo 3 contra los floats de la .rdata: lo mismo con el mouse pasado a la x87.
bool MouseInFloats(const GameHudContext& context, float left, float right, float top, float bottom)
{
    const auto x = static_cast<double>(context.mouse.x);
    const auto y = static_cast<double>(context.mouse.y);
    return x >= static_cast<double>(left) && x < static_cast<double>(right) && y >= static_cast<double>(top) &&
           y < static_cast<double>(bottom);
}

// 0x554340 con la fuente normal. Antes de llamarla el entrenador escribe el blanco sobre 0xff000000 (0x44b8ec, 0x44cfde
// y 0x44db97) o sobre 0x80282828 (0x44d825), que 0x554340 pisa en 0x55450f con el blanco sobre transparente antes de
// escribir: no se ven. Deja la normal y ese blanco.
void DrawTheHelp(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help)
{
    SelectFont(context, FontKind::Normal);
    UI::DrawHelp(context.sprites, context.text, x, y, help);
    context.hud.textColors = TextColors{.text = 0xffffffffu, .background = 0};
}

// 0x44b619..0x44b72f y 0x44c85c..0x44c96e: sin mezcla, el blanco, cinco tiras de 0xfb, las dos líneas de 0xfc y los
// marcos de los botones.
void DrawTheSmallBack(GameHudContext& context, const std::array<float, 2>& buttons)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetBlend(BlendMode::Opaque);
    sprites.SetColor(White);
    for (std::int32_t tile = 0; tile < BackTilesEnd; tile += BackTileStep)
        Quad(sprites, BackSlot, BackLeft, static_cast<float>(static_cast<double>(tile) + BackTop), BackWidth,
             BackTileHeight, BackDu, BackDv);
    Quad(sprites, LineSlot, BackLeft, TopLine, BackWidth, LineHeight, BackDu, BackDv);
    Quad(sprites, LineSlot, BackLeft, BottomLine, BackWidth, LineHeight, BackDu, BackDv);
    for (const float top : buttons)
        Quad(sprites, ButtonSlot, ButtonLeft, top, ButtonWidth, ButtonHeight, BackDu, ButtonDv);
}

// 0x44b731..0x44b78f: el título en gruesa, negro sobre gris, en la caja de 203 · W / 640, y la mezcla normal.
Core::Expected<void, std::string> DrawTheSmallTitle(GameHudContext& context, std::string_view where)
{
    auto title = TextOf(context, TitleText, where);
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    SelectFont(context, FontKind::Bold);
    context.hud.textColors = TitleColors;
    DrawTitle(context, TitleLeft, TitleTop, title.Value(), TitleBoxDesignWidth);
    context.sprites.SetBlend(BlendMode::Alpha);
    SelectFont(context, FontKind::Normal);
    return {};
}

// 0x44b7a1..0x44b7c3 y 0x44c9df..0x44ca08: sprintf del texto 1203 con el nombre del héroe.
Core::Expected<std::string, std::string> TheNameLine(const GameHudContext& context, std::string_view where)
{
    auto format = TextOf(context, NameText, where);
    if (!format.HasValue())
        return Core::Unexpected(format.Error());
    const WorldRuntime& runtime = context.world.Runtime();
    const std::string_view name = HeroNameOf(context.world.Actors()[runtime.hero]);
    return Fits(UI::Sprintf(format.Value(), name), LineRoom, where);
}

// 0x44b869..0x44b8fb y 0x44cf54..0x44cfe8: el botón de cerrar y su ayuda.
Core::Expected<void, std::string> DrawTheSmallClose(GameHudContext& context, std::string_view where)
{
    Quad(context.sprites, CloseSlot, SmallCloseLeft, SmallCloseTop, CloseSize, CloseSize, CloseUv, CloseUv);
    if (!MouseIn(context, SmallHelpLeft, SmallHelpRight, SmallHelpTop, SmallHelpBottom))
        return {};
    auto help = TextOf(context, CloseHelpText, where);
    if (!help.HasValue())
        return Core::Unexpected(help.Error());
    DrawTheHelp(context, SmallHelpLeft, SmallHelpY, help.Value());
    return {};
}

// 0x44b610: el menú.
Core::Expected<void, std::string> DrawTheMenu(GameHudContext& context)
{
    DrawTheSmallBack(context, {MenuButtonTop, MenuButtonTop + MenuButtonStep});
    if (auto title = DrawTheSmallTitle(context, "0x44b783"); !title.HasValue())
        return title;
    context.hud.textColors = MenuTextColors;
    auto name = TheNameLine(context, "0x44b7c3");
    if (!name.HasValue())
        return Core::Unexpected(name.Error());
    auto menu = TextOf(context, MenuText, "0x44b7d5");
    if (!menu.HasValue())
        return Core::Unexpected(menu.Error());
    DrawCenteredText(context, CenterX, MenuTextTop, menu.Value());
    DrawCenteredText(context, CenterX, NameTop, name.Value());
    context.hud.textColors.text = LabelColor;
    std::size_t label = FirstButtonText;
    for (std::int32_t top = MenuLabelTop; top < MenuLabelsEnd; top += MenuLabelStep, ++label)
    {
        const bool hovered = MouseIn(context, HoverLeft, HoverRight, top - HoverAbove, top + HoverBelow);
        SelectFont(context, hovered ? FontKind::Bold : FontKind::Normal);
        auto text = TextOf(context, label, "0x44b850");
        if (!text.HasValue())
            return Core::Unexpected(text.Error());
        DrawCenteredText(context, CenterX, top, text.Value());
    }
    return DrawTheSmallClose(context, "0x44b8f6");
}

// El renglón del precio que deja 0x44b190 en el buffer de quien llama.
Core::Expected<std::string, std::string> ThePriceLine(const GameHudContext& context, const PetRecoveryQuote& quote)
{
    switch (quote.line)
    {
    case PetQuoteLine::NoPet:
    case PetQuoteLine::Full:
    {
        // 0x44b248 y 0x44b4ba: sprintf del texto, sin argumentos, en el buffer de quien llama.
        auto format = TextOf(context, quote.line == PetQuoteLine::NoPet ? NoPetText : FullText, "0x44b24e");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        return Fits(UI::Sprintf(format.Value()), LineRoom, "0x44b24e");
    }
    case PetQuoteLine::Short:
    case PetQuoteLine::Cost:
    {
        // 0x44b459..0x44b4ae: sprintf en [EBP − 0x64] de 0x44b190 con el buffer de quien llama (la cifra) y la copia.
        auto format = TextOf(context, quote.line == PetQuoteLine::Short ? ShortText : CostText, "0x44b46e");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        return Fits(UI::Sprintf(format.Value(), std::string_view(quote.number)), LineRoom, "0x44b46e");
    }
    }
    return Core::Unexpected(std::string("0x44b190: un renglón sin caso"));
}

// 0x44c850: recuperar el caballo o el cuervo.
Core::Expected<void, std::string> DrawTheRecovery(GameHudContext& context)
{
    DrawTheSmallBack(
        context, {static_cast<float>(RecoveryButtonTop), static_cast<float>(RecoveryButtonTop + RecoveryButtonStep)});
    if (auto title = DrawTheSmallTitle(context, "0x44c9c1"); !title.HasValue())
        return title;
    context.hud.textColors = RecoveryTextColors;
    // 0x44ca08: el nombre queda en el buffer y nadie lo dibuja (Q-C959); 0x44b190 escribe encima.
    if (auto name = TheNameLine(context, "0x44ca08"); !name.HasValue())
        return Core::Unexpected(name.Error());
    auto recovery = TextOf(context, RecoveryText, "0x44ca1a");
    if (!recovery.HasValue())
        return Core::Unexpected(recovery.Error());
    DrawCenteredText(context, CenterX, MenuTextTop, recovery.Value());
    context.hud.textColors.text = LabelColor;
    for (std::size_t button = 0; button < 2; ++button)
    {
        const std::int32_t top = RecoveryButtonTop + RecoveryButtonStep * static_cast<std::int32_t>(button);
        const bool hovered = MouseIn(context, HoverLeft, HoverRight, top, top + RecoveryHoverHeight);
        SelectFont(context, hovered ? FontKind::Bold : FontKind::Normal);
        // 0x44ca98: 0x44b190(botón + 1, buffer).
        auto quote = QuoteThePetRecovery(PetQuoteContext{.world = context.world,
                                                         .random = context.random,
                                                         .ownRecordDepth = context.ownRecordDepth,
                                                         .items = context.items,
                                                         .writesGlobals = !context.projecting},
                                         button);
        if (!quote.HasValue())
            return Core::Unexpected(std::move(quote).Error());
        const PetRecoveryQuote& priced = quote.Value();
        if (priced.price == 0xffffffffu)
        {
            SelectFont(context, FontKind::Normal);
            context.hud.textColors.text = NoPetColor;
        }
        else
            context.hud.textColors.text = LabelColor;
        auto label = TextOf(context, button == 0 ? HorseText : RavenText, "0x44caed");
        if (!label.HasValue())
            return Core::Unexpected(label.Error());
        DrawCenteredText(context, CenterX, top + RecoveryLabelBelow, label.Value());
        auto line = ThePriceLine(context, priced);
        if (!line.HasValue())
            return Core::Unexpected(std::move(line).Error());
        {
            // 0x44caf5..0x44cf1d: el zen (sin signo) contra el precio, con el registro propio abierto.
            const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                                    Render::Models::OwnRecordCloseRandoms);
            if (context.world.OwnCharacter().zen < priced.price)
                context.hud.textColors = ShortColors;
        }
        DrawCenteredText(context, CenterX, top + PriceBelow, line.Value());
        context.hud.textColors.background = 0;
    }
    return DrawTheSmallClose(context, "0x44cfe8");
}

// 0x44d76d..0x44d834: el botón de mezclar, apretado con el botón abajo, y su ayuda con el mouse encima.
Core::Expected<void, std::string> DrawTheMixButton(GameHudContext& context)
{
    Render::SpriteBatch& sprites = context.sprites;
    Quad(sprites, MixSlot, MixLeft, MixTop, MixWidth, MixHeight, MixDu, MixDv);
    if (!MouseInFloats(context, MixLeft, MixLeft + MixWidth, MixTop, MixTop + MixHeight))
        return {};
    if (context.leftDown)
        Quad(sprites, MixDownSlot, MixLeft, MixTop, MixWidth, MixHeight, MixDu, MixDv);
    auto help = TextOf(context, MixText, "0x44d82f");
    if (!help.HasValue())
        return Core::Unexpected(help.Error());
    DrawTheHelp(context, MixHelpX, MixHelpY, help.Value());
    return {};
}

// 0x44d8e9..0x44d9fd: el nombre de la mezcla y desde dónde siguen los renglones.
Core::Expected<float, std::string> DrawTheMixName(GameHudContext& context, std::uint8_t kind, std::string_view mix)
{
    if (kind == 0)
    {
        auto format = TextOf(context, NoMixText, "0x44d9e5");
        if (!format.HasValue())
            return Core::Unexpected(format.Error());
        context.hud.textColors.text = NoKindColor;
        auto line = Fits(UI::Sprintf(format.Value(), mix), LineRoom, "0x44d9e5");
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
        DrawCenteredText(context, BoxCenterX, BoxNameTop, line.Value());
        return LinesAfterName;
    }
    context.hud.textColors.text = KindColor;
    // 0x44d91a..0x44d93d y 0x44d974..0x44d997: "%s %s" (0x6c6350) con la mascota y el 1212.
    auto pet = TextOf(context, kind == 1 ? HorseText : RavenText, "0x44d92d");
    if (!pet.HasValue())
        return Core::Unexpected(pet.Error());
    auto line = Fits(UI::Sprintf("%s %s", pet.Value(), mix), LineRoom, "0x44d92d");
    if (!line.HasValue())
        return Core::Unexpected(line.Error());
    DrawCenteredText(context, BoxCenterX, BoxNameTop, line.Value());
    return LinesAfterName;
}

// 0x44d680: la caja.
Core::Expected<void, std::string> DrawTheBox(GameHudContext& context)
{
    Render::SpriteBatch& sprites = context.sprites;
    HudState& hud = context.hud;
    const NpcWindowState& npc = context.world.Runtime().npcWindows;
    const std::uint8_t kind = context.world.Runtime().panels.delegated.field724584;
    // 0x44d913: con otra mezcla no hay nombre, y el precio lee 0x6b8c68 de más y [EBP − 0x88] sin escribir.
    if (kind > 2)
        return Core::Unexpected(
            std::format("0x44da59: la mezcla {} lee 0x6b8c68 fuera de la tabla y la cifra sin escribir", kind));
    sprites.SetColor(White);
    // 0x44d6ac: 0x5ee220(0x104, 0, 0) y la grilla (0x5e3660).
    WindowDrawing::DrawTheWindowBack(sprites, BoxLeft, BoxTop);
    DrawTheGridBoxes(sprites, GridLeft, GridTop, npc.trainerBox, GridColumns, GridRows);
    sprites.SetColor(White);
    Quad(sprites, BarSlot, BarLeft, FirstBarTop, BarWidth, BarHeight, BarDu, BarDv);
    Quad(sprites, BarSlot, BarLeft, SecondBarTop, BarWidth, BarHeight, BarDu, BarDv);
    if (auto button = DrawTheMixButton(context); !button.HasValue())
        return button;
    sprites.SetColor(White);
    SelectFont(context, FontKind::Bold);
    hud.textColors = BoxTitleColors;
    auto title = TextOf(context, BoxTitleText, "0x44d899");
    auto taxFormat = TextOf(context, TaxText, "0x44d8b4");
    auto mix = TextOf(context, MixText, "0x44d92d");
    if (!title.HasValue())
        return Core::Unexpected(title.Error());
    if (!taxFormat.HasValue())
        return Core::Unexpected(taxFormat.Error());
    if (!mix.HasValue())
        return Core::Unexpected(mix.Error());
    DrawTitle(context, BoxTitleLeft, BoxTitleTop, title.Value());
    auto tax = Fits(UI::Sprintf(taxFormat.Value(), npc.chaosTaxRate), TaxRoom, "0x44d8b4");
    if (!tax.HasValue())
        return Core::Unexpected(tax.Error());
    DrawTitle(context, BoxTitleLeft, BoxTaxTop, tax.Value());
    auto top = DrawTheMixName(context, kind, mix.Value());
    if (!top.HasValue())
        return Core::Unexpected(top.Error());
    float y = top.Value();
    // 0x44da04..0x44da30: la chance, 60 con una mezcla.
    hud.textColors.text = BoxTitleColors.text;
    auto rateFormat = TextOf(context, RateText, "0x44da19");
    if (!rateFormat.HasValue())
        return Core::Unexpected(rateFormat.Error());
    auto rate = Fits(UI::Sprintf(rateFormat.Value(), mix.Value(), kind != 0 ? MixRate : 0), LineRoom, "0x44da19");
    if (!rate.HasValue())
        return Core::Unexpected(rate.Error());
    DrawCenteredText(context, BoxCenterX, Truncate(static_cast<double>(y)), rate.Value());
    y = static_cast<float>(static_cast<double>(y) + static_cast<double>(LineStep));
    if (kind != 0)
    {
        // 0x44da4d..0x44da97: el precio con impuesto (0x5b12f0) y el de 0x5b1130.
        const std::uint32_t price = TrainerMixPrice(kind);
        auto priceFormat = TextOf(context, PriceText, "0x44da80");
        if (!priceFormat.HasValue())
            return Core::Unexpected(priceFormat.Error());
        const std::string taxed = TaxedPriceText(price, npc.chaosTaxRate);
        const std::string plain = ThousandsText(price);
        auto line = Fits(UI::Sprintf(priceFormat.Value(), std::string_view(taxed), std::string_view(plain)), LineRoom,
                         "0x44da80");
        if (!line.HasValue())
            return Core::Unexpected(line.Error());
        DrawCenteredText(context, BoxCenterX, Truncate(static_cast<double>(y)), line.Value());
    }
    // 0x44da9f..0x44daf4: los avisos, 135 más abajo, con el 1212.
    y = static_cast<float>(static_cast<double>(y) + static_cast<double>(LineStep) + static_cast<double>(WarningsGap));
    auto jewelFormat = TextOf(context, JewelText, "0x44dabf");
    auto second = TextOf(context, JewelSecondText, "0x44daf4");
    if (!jewelFormat.HasValue())
        return Core::Unexpected(jewelFormat.Error());
    if (!second.HasValue())
        return Core::Unexpected(second.Error());
    auto jewel = Fits(UI::Sprintf(jewelFormat.Value(), mix.Value()), LineRoom, "0x44dabf");
    if (!jewel.HasValue())
        return Core::Unexpected(jewel.Error());
    DrawCenteredText(context, BoxCenterX, Truncate(static_cast<double>(y)), jewel.Value());
    DrawCenteredText(context, BoxCenterX, Truncate(static_cast<double>(y) + static_cast<double>(LineStep)),
                     second.Value());
    // 0x44daf9..0x44dba1: el botón de cerrar (con el color que haya) y su ayuda.
    Quad(sprites, CloseSlot, BoxCloseLeft, BoxCloseTop, CloseSize, CloseSize, CloseUv, CloseUv);
    if (!MouseInFloats(context, BoxCloseLeft, BoxCloseLeft + CloseSize, BoxCloseTop, BoxCloseTop + CloseSize))
        return {};
    auto help = TextOf(context, CloseHelpText, "0x44dba1");
    if (!help.HasValue())
        return Core::Unexpected(help.Error());
    DrawTheHelp(context, BoxHelpX, BoxHelpY, help.Value());
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheTrainer(GameHudContext& context)
{
    switch (context.world.Runtime().panels.delegated.trainer)
    {
    case TrainerMenuMode:
        return DrawTheMenu(context);
    case TrainerRecoveryMode:
        return DrawTheRecovery(context);
    case TrainerBoxMode:
        return DrawTheBox(context);
    case IdleMode:
    default:
        return {};
    }
}
} // namespace Client::Scenes
