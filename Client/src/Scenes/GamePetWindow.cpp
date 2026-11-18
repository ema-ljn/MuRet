#include "Scenes/GamePetWindow.h"

#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDamage.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/PanelRegistry.h"
#include "UI/SprintfFormat.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
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
using Render::Color;
using Render::Text::FontKind;

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor3f(0,6; 0,6; 0,6) de la pestaña que no está elegida (0x424ad9) y glColor3f(0,9; 0,9; 0,6) de las líneas del
// marco (0x424c24).
constexpr Color OtherTabColor{0.6f, 0.6f, 0.6f, 1.0f};
constexpr Color FrameLineColor{0.9f, 0.9f, 0.6f, 1.0f};
// 0x414ac8 y 0x414aec: el fondo de la barra de durabilidad y lo lleno (0x3f61e1e2 y 0x3d70f0f1).
constexpr Color DurabilityBackColor{0.2f, 0.0f, 0.0f, 1.0f};
constexpr Color DurabilityColor{0.88235295f, 0.05882353f, 0.0f, 1.0f};

// Los slots de 0x61ccb0: pet1.jpg y pet2.jpg (0x61d4b0 y 0x61d4cc), win_button.tga (0x61d17a), Level_box.jpg
// (0x61cf8f), y los de 0x61ae60: Trading_line.jpg y command.jpg.
constexpr std::uint16_t PetButtonSlot = 0x65d;
constexpr std::uint16_t PetButtonOpenSlot = 0x65e;
constexpr std::uint16_t TabSlot = 0x646;
constexpr std::uint16_t LineSlot = 0x117;
constexpr std::uint16_t StatsBackSlot = 0xf5;
constexpr std::uint16_t CommandsSlot = 0x12c;

// 0x723911 en 2: la ventana abierta (0x42474c).
constexpr std::uint8_t PetWindowOpen = 2;
// 0x723910: la pestaña 0, la del caballo (0x424e40), y la 1, la del cuervo (0x416670).
constexpr std::uint8_t HorseTab = 0;
constexpr std::uint8_t RavenTab = 1;
constexpr std::uint8_t TabCount = 2;
// La esquina de la ventana: 0x1c2, o 0x104 con el personaje abierto.
constexpr std::int32_t WindowLeft = 0x1c2;
constexpr std::int32_t WindowBesideTheCharacter = 0x104;

// 0x4249c4..0x424a74: el botón de la mascota, fijo en (535, 395), de 24 × 24 (0x4405c000 y 0x43c58000), y su ayuda
// con el mouse en [0x217, 0x22f) × [0x18b, 0x1a3), en (0x217, 0x17e).
constexpr float PetButtonX = 535.0f;
constexpr float PetButtonY = 395.0f;
constexpr float PetButtonSize = 24.0f;
constexpr float PetButtonUv = 0.75f;
constexpr std::int32_t PetButtonLeft = 0x217;
constexpr std::int32_t PetButtonRight = 0x22f;
constexpr std::int32_t PetButtonTop = 0x18b;
constexpr std::int32_t PetButtonBottom = 0x1a3;
constexpr std::int32_t PetButtonHelpY = 0x17e;

// 0x424ac9..0x424ba6: las pestañas, de a 45 desde 15 (0x6b87bc), de 49 de ancho; la elegida en y = 63 y de 20 de alto,
// la otra en 67 y de 16, con du = 0,765625 y dv = 1.
constexpr std::int32_t TabStep = 45;
constexpr double TabLeft = 15.0;
constexpr float TabWidth = 49.0f;
constexpr float OtherTabTop = 67.0f;
constexpr float OtherTabHeight = 16.0f;
constexpr float ChosenTabTop = 63.0f;
constexpr float ChosenTabHeight = 20.0f;
constexpr float TabDu = 0.765625f;
// 0x424bb9..0x424c1c: las dos líneas de 190 × 10 en y = 80 y 380, con du = 0,7421875 y dv = 0,625.
constexpr float LineWidth = 190.0f;
constexpr float LineHeight = 10.0f;
constexpr float TopLineY = 80.0f;
constexpr float BottomLineY = 380.0f;
constexpr float LineDu = 0.7421875f;
constexpr float LineDv = 0.625f;
// 0x424c35..0x424ca8: el marco de 1 punto, 12 (0x6b8864) adentro de la ventana de 190 (0x6b8a08): arriba y abajo de
// 166 en y = 90 y 380, a la izquierda de 290 y a la derecha de 291.
constexpr double FrameInset = 12.0;
constexpr double WindowWidth = 190.0;
constexpr float FrameTop = 90.0f;
constexpr float FrameBottom = 380.0f;
constexpr float FrameWidth = 166.0f;
constexpr float FrameLeftHeight = 290.0f;
constexpr float FrameRightHeight = 291.0f;
// 0x424de1..0x424e26: el título en (x + 0x23, 0xc) y los nombres de las pestañas en (x + 0x28, 0x46) y (x + 0x55,
// 0x46).
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
constexpr std::int32_t HorseTabNameLeft = 0x28;
constexpr std::int32_t RavenTabNameLeft = 0x55;
constexpr std::int32_t TabNameTop = 0x46;

// 0x424e8c..0x424ec7 y 0x4166a0..0x416702: el fondo de Level_box en x + 19 − 6 (0x6b8870 y 0x6b87f4), de 75 × 21, con
// du = 0,5859375 y dv = 0,65625; el caballo uno en y = 96, el cuervo dos, en 96 y 236 (0x8c más).
constexpr double StatsBackLeft = 19.0;
constexpr double StatsBackShift = 6.0;
constexpr double StatsBackTop = 96.0;
constexpr std::int32_t StatsBackStep = 0x8c;
constexpr std::int32_t StatsBackEnd = 0x118;
constexpr float StatsBackWidth = 75.0f;
constexpr float StatsBackHeight = 21.0f;
constexpr float StatsBackDu = 0.5859375f;
constexpr float StatsBackDv = 0.65625f;
// 0x424eff..0x424f1e: el nombre del caballo en (x + 0x55, 0x14); 0x416767..0x416772: el del cuervo, el título en
// (x + 0x23, 0x14).
constexpr std::int32_t HorseNameLeft = 0x55;
constexpr std::int32_t PetNameTop = 0x14;
// 0x424f61..0x424fb2 y 0x4241d6..0x424215: "no tiene" en (x + 0x5f, 0xdc), negro, y encima en (x + 0x5e, 0xdb).
constexpr std::int32_t MissingShadowLeft = 0x5f;
constexpr std::int32_t MissingShadowTop = 0xdc;
constexpr std::int32_t MissingLeft = 0x5e;
constexpr std::int32_t MissingTop = 0xdb;

// 0x4149b0: los renglones desde y + 100 (0x6b86b4), el nivel en x + 0xe en una caja de 70 y los demás en x + 0x18 en
// una de 145, de a 15 (0x6b87bc) salvo el primero, a 20 (0x6b86d0). La barra de durabilidad: el fondo de 145 × 8 en
// x + 0x18 y lo lleno de 6 de alto en (x + 0x19, + 1), de durabilidad · 1/255 · 143 de ancho (0x6b88f8 y 0x6b88f4).
constexpr double StatsTop = 100.0;
constexpr double FirstStep = 20.0;
constexpr double StatsStep = 15.0;
constexpr std::int32_t LevelLeft = 0xe;
constexpr std::int32_t LineLeft = 0x18;
constexpr std::uint32_t LevelBox = 70;
constexpr std::uint32_t LineBox = 145;
constexpr float DurabilityBackWidth = 145.0f;
constexpr float DurabilityBackHeight = 8.0f;
constexpr float DurabilityHeight = 6.0f;
constexpr std::uint16_t MaxDurability = 0xff;
constexpr float InverseMaxDurability = 1.0f / 255.0f;
constexpr double DurabilityWidth = 143.0;
// 0x414d18: el % del cetro (el byte +0x16 del lugar 0) por 0,01 (0x6b86c0).
constexpr float Percent = 0.01f;
// 0x414fd4: la skill 0x3e de Skill.bmd, el daño del caballo.
constexpr std::uint8_t HorseSkill = 0x3e;
// 0x415249..0x415255: el liderazgo que pide, nivel · 15 + 0xb9.
constexpr std::int32_t LeadershipPerLevel = 15;
constexpr std::int32_t LeadershipBase = 0xb9;
// 0x4155c8..0x4157af: las órdenes, el título en (x + 0xe, y + 0xf0) y los cuatro íconos de command.jpg de 22 × 26 en
// x + 24 (0x6b8800) e y + 260, 288, 316 y 344, con u = 0,125 · k, du = 0,125 y dv = 0,13671875, y su texto en
// (_ftol(x) + 30, _ftol(y) + 10). 0x4157c5..0x415830: el mouse sobre el ícono k de y + k · 28 (0x6b880c) + 260, con
// los bordes incluidos (22 y 26 más, 0x6b8820 y 0x6b88e0).
constexpr std::int32_t CommandsTitleTop = 0xf0;
constexpr double CommandsLeft = 24.0;
constexpr std::array<double, 4> CommandTops{260.0, 288.0, 316.0, 344.0};
constexpr double CommandStep = 28.0;
constexpr float CommandWidth = 22.0f;
constexpr float CommandHeight = 26.0f;
constexpr float CommandDu = 0.125f;
constexpr float CommandDv = 0.13671875f;
constexpr std::int32_t CommandTextLeft = 30;
constexpr std::int32_t CommandTextTop = 10;

// 0x418486..0x418508: el cartel del cuervo a la izquierda del ancho del mundo, 50 (0x6b86b0) más 50 con la party y 60
// (0x6b86cc) más con una mascota de 0x1c12 a 0x1c16 o Fenrir, y 15 (0x6b87bc); el nombre termina en _ftol(x) + 50 en
// y = 4, y la barra (0x5a6af0) de 50 × 2 en y = 16, con la durabilidad del lugar 1 por 50 / 255.
constexpr float PartyOffset = 50.0f;
constexpr double PetOffset = 60.0;
constexpr double RavenHudWidth = 50.0;
constexpr double RavenHudRight = 15.0;
constexpr std::int32_t RavenNameRight = 50;
constexpr std::int32_t RavenNameTop = 4;
constexpr float RavenGaugeTop = 16.0f;
constexpr float RavenGaugeWidth = 50.0f;
constexpr float RavenGaugeHeight = 2.0f;
constexpr std::int32_t RavenGaugeFill = 50;
constexpr std::int32_t RavenGaugeFull = 255;
// 0x4187f6..0x418895: el ícono de la orden de command.jpg, de 16 × 20 en (ancho − 20 − corrimiento − 65, 5): u =
// ((orden % 8) · 32 + 6) / 256 y v = ((orden / 8) · 28 + 3) / 256 (0x6b87f4, 0x6b86b8 y 0x6b8940), du = 0,078125 y
// dv = 0,10546875. El mouse, con los bordes incluidos: 12 de ancho (0x6b8864), de 5 a 25 (0x6b87f8 y 0x6b8808), y la
// ayuda en (_ftol(x), 25).
constexpr double OrderIconShift = 20.0;
constexpr double OrderIconRight = 65.0;
constexpr float OrderIconTop = 5.0f;
constexpr float OrderIconWidth = 16.0f;
constexpr float OrderIconHeight = 20.0f;
constexpr double OrderColumnWidth = 32.0;
constexpr double OrderColumnLeft = 6.0;
constexpr double OrderRowHeight = 28.0;
constexpr double OrderRowTop = 3.0;
constexpr double Texel = 0.00390625;
constexpr float OrderIconDu = 0.078125f;
constexpr float OrderIconDv = 0.10546875f;
constexpr double OrderHoverWidth = 12.0;
constexpr double OrderHoverTop = 5.0;
constexpr double OrderHoverBottom = 25.0;
constexpr std::int32_t OrderHelpTop = 25;

// Los modelos de las mascotas (0x418486..0x4184c9): de 0x1c12 a 0x1c16 y Fenrir, y el caballo oscuro (0x424e7d).
constexpr std::int16_t FirstPetModel = 0x1c12;
constexpr std::int16_t LastPetModel = 0x1c16;
constexpr std::int16_t FenrirModel = 0x1c37;
constexpr std::uint16_t DarkHorseModel = 0x1c16;

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
constexpr std::size_t CloseHelpText = 1002;
constexpr std::size_t NoPetText = 1169;
constexpr std::size_t DarkHorseText = 1187;
constexpr std::size_t DarkRavenText = 1214;
constexpr std::size_t PetText = 1217;
constexpr std::size_t CommandsTitleText = 1218;
constexpr std::size_t FirstCommandText = 1219;
constexpr std::size_t FirstCommandHelpText = 1223;
constexpr std::size_t MissingPetText = 1233;
constexpr std::size_t LevelText = 200;
constexpr std::size_t ExperienceText = 201;
constexpr std::size_t DamageText = 203;
constexpr std::size_t AttackSpeedText = 64;
constexpr std::size_t LifeText = 358;
constexpr std::size_t LeadershipText = 698;
constexpr std::size_t ScepterText = 1234;

// Los colores de 0x6c8e5c y 0x6c8e64.
constexpr Render::Text::TextColors HelpColors{.text = 0xffffffffu, .background = 0xff000000u};
constexpr Render::Text::TextColors NameColors{.text = 0xffdcdcdcu, .background = 0};
constexpr Render::Text::TextColors MissingShadowColors{.text = 0xff000000u, .background = 0};
constexpr std::uint32_t MissingColor = 0xff3c3cdcu;
constexpr std::uint32_t LevelColor = 0xff67bfdfu;
constexpr Render::Text::TextColors LineColors{.text = 0xffe6e6e6u, .background = 0x80000000u};
constexpr std::uint32_t LackingColor = 0xff3c3cdcu;
constexpr Render::Text::TextColors CommandsTitleColors{.text = 0xff67bfdfu, .background = 0x0000000fu};
constexpr Render::Text::TextColors RavenHudColors{.text = 0xffdcdcdcu, .background = 0x80000000u};

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

// _ftol de un float que no es infinito.
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

// Un acceso al registro propio ([0x7681eb4]): abre sin gastar y, si lo deja en 0, cierra con 3.848 rand().
Render::Models::LogicalAccess OwnAccess(GameHudContext& context)
{
    return Render::Models::LogicalAccess(context.ownRecordDepth, context.random, Render::Models::OwnRecordCloseRandoms);
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

void SetColors(GameHudContext& context, Render::Text::TextColors colors)
{
    context.hud.textColors = colors;
}

// 0x554340(x, y, texto): la ayuda, que deja la fuente normal en el DC y los colores −1 y 0.
void DrawHelp(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help)
{
    UI::DrawHelp(context.sprites, context.text, x, y, help);
    WindowDrawing::SelectFont(context, FontKind::Normal);
    SetColors(context, Render::Text::TextColors{.text = 0xffffffffu, .background = 0});
}

// 0x2000000 (el personaje) no saca rand().
std::int32_t WindowLeftOf(UI::PanelRegistry& panels)
{
    return panels.IsOpen(UI::CharacterPanel) ? WindowBesideTheCharacter : WindowLeft;
}

// "No tiene" de la pestaña (0x424f4d..0x424fb7 y 0x4241c3..0x424215): la grande en negrita (0x576355c), en negro una
// vez y en rojo un punto más arriba y a la izquierda, centrada con 0x623690.
void DrawTheMissingPet(GameHudContext& context, std::int32_t left, std::string_view text)
{
    WindowDrawing::SelectFont(context, FontKind::BigBold);
    SetColors(context, MissingShadowColors);
    WindowDrawing::DrawCenteredText(context, left + MissingShadowLeft, MissingShadowTop, text);
    context.hud.textColors.text = MissingColor;
    WindowDrawing::DrawCenteredText(context, left + MissingLeft, MissingTop, text);
}

// 0x4249a0: el botón de la mascota y, con 0x723911 en 2, el marco de la ventana. Devuelve el 0x723911 que leen los que
// siguen. Un dibujo intermedio de MuRet no escribe el 0 del personaje cerrado (lo vuelve a escribir el paso, 0x424230),
// pero lo devuelve igual.
Core::Expected<std::uint8_t, std::string> DrawTheButtonAndFrame(GameHudContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (!panels.IsOpen(UI::CharacterPanel))
    {
        if (!context.projecting)
            runtime.panels.delegated.pet = 0;
        return std::uint8_t{0};
    }
    Render::SpriteBatch& sprites = context.sprites;
    const std::uint8_t pet = runtime.panels.delegated.pet;
    Quad(sprites, pet == PetWindowOpen ? PetButtonOpenSlot : PetButtonSlot, PetButtonX, PetButtonY, PetButtonSize,
         PetButtonSize, 0.0f, 0.0f, PetButtonUv, PetButtonUv);
    const UI::Point mouse = context.mouse;
    if (mouse.x >= PetButtonLeft && mouse.x < PetButtonRight && mouse.y >= PetButtonTop && mouse.y < PetButtonBottom)
    {
        auto help = TextOf(context, PetText, "0x424a3f");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::SelectFont(context, FontKind::Normal);
        SetColors(context, HelpColors);
        DrawHelp(context, PetButtonLeft, PetButtonHelpY, UI::Sprintf("%s", help.Value()));
    }
    if (runtime.panels.delegated.pet != PetWindowOpen)
        return runtime.panels.delegated.pet;
    const std::int32_t left = WindowLeftOf(panels);
    WindowDrawing::DrawTheWindowBack(sprites, left, 0);
    // 0x424ac9..0x424b34 y 0x424b3d..0x424ba6: primero la que no está elegida, después la elegida, con 0x723910 leído
    // en cada vuelta.
    const auto tabX = [left](std::uint8_t tab) { return F32(static_cast<double>(tab * TabStep + left) + TabLeft); };
    for (std::uint8_t tab = 0; tab < TabCount; ++tab)
    {
        if (runtime.petTab == tab)
            continue;
        sprites.SetColor(OtherTabColor);
        Quad(sprites, TabSlot, tabX(tab), OtherTabTop, TabWidth, OtherTabHeight, 0.0f, 0.0f, TabDu, 1.0f);
    }
    for (std::uint8_t tab = 0; tab < TabCount; ++tab)
    {
        if (runtime.petTab != tab)
            continue;
        sprites.SetColor(White);
        Quad(sprites, TabSlot, tabX(tab), ChosenTabTop, TabWidth, ChosenTabHeight, 0.0f, 0.0f, TabDu, 1.0f);
    }
    sprites.SetColor(White);
    const auto x = static_cast<float>(left);
    Quad(sprites, LineSlot, x, TopLineY, LineWidth, LineHeight, 0.0f, 0.0f, LineDu, LineDv);
    Quad(sprites, LineSlot, x, BottomLineY, LineWidth, LineHeight, 0.0f, 0.0f, LineDu, LineDv);
    sprites.SetColor(FrameLineColor);
    const float frameLeft = F32(static_cast<double>(x) + FrameInset);
    sprites.ColorQuad(frameLeft, FrameTop, FrameWidth, 1.0f);
    sprites.ColorQuad(frameLeft, FrameBottom, FrameWidth, 1.0f);
    sprites.ColorQuad(frameLeft, FrameTop, 1.0f, FrameLeftHeight);
    sprites.ColorQuad(F32(static_cast<double>(x) + WindowWidth - FrameInset), FrameTop, 1.0f, FrameRightHeight);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    // 0x424cc8..0x424d8a: el botón de cerrar en (x + 25, 395) y, con el mouse encima, la ayuda en (_ftol(x + 25), 382)
    // con la fuente normal y los colores −1 y 0xff000000.
    if (WindowDrawing::DrawTheCloseButton(context, left, 0))
    {
        auto help = TextOf(context, CloseHelpText, "0x424d66");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, left, 0, help.Value());
    }
    sprites.SetColor(White);
    auto title = TextOf(context, PetText, "0x424dfb");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    auto horse = TextOf(context, DarkHorseText, "0x424e0b");
    if (!horse.HasValue())
        return Core::Unexpected(std::move(horse).Error());
    WindowDrawing::SelectFont(context, FontKind::Bold);
    SetColors(context, NameColors);
    WindowDrawing::DrawTitle(context, left + TitleLeft, TitleTop, title.Value());
    WindowDrawing::DrawCenteredText(context, left + HorseTabNameLeft, TabNameTop, horse.Value());
    WindowDrawing::DrawCenteredText(context, left + RavenTabNameLeft, TabNameTop, title.Value());
    return runtime.panels.delegated.pet;
}

// 0x4149b0(tipo, x, y, datos, órdenes): los datos de una mascota, el caballo (tipo 1, sin órdenes, desde 0x424e40) o
// el cuervo (el +0x444 del objeto, que solo escribe 0x4167b0 en 0, con las órdenes, desde 0x416670).
Core::Expected<void, std::string> DrawTheStats(GameHudContext& context, std::int32_t type, std::int32_t x,
                                               std::int32_t y, const PetStats& stats, bool commands)
{
    WorldState& world = context.world;
    const OwnCharacterRecord& own = world.OwnCharacter();
    Render::SpriteBatch& sprites = context.sprites;
    const std::int32_t lineBox = WindowDrawing::BoxWidth(context, LineBox);
    const std::int32_t lineLeft = x + LineLeft;
    // fild y; fst [ebp − 0x20]; fadd 100: la suma sale del entero, y el float de y queda para las órdenes.
    const auto floatY = static_cast<float>(y);
    float top = F32(static_cast<double>(y) + StatsTop);
    const auto drawLine = [&](std::string_view text)
    {
        WindowDrawing::DrawClipped(context, lineLeft, Truncate(top), text, lineBox);
        top = F32(static_cast<double>(top) + StatsStep);
    };
    const auto format = [&](std::size_t index, std::string_view where) { return TextOf(context, index, where); };

    auto level = format(LevelText, "0x4149da");
    if (!level.HasValue())
        return Core::Unexpected(std::move(level).Error());
    context.hud.textColors.text = LevelColor;
    WindowDrawing::DrawInBox(context, x + LevelLeft, Truncate(top),
                             UI::Sprintf(level.Value(), static_cast<std::uint32_t>(stats.level)),
                             WindowDrawing::BoxWidth(context, LevelBox));
    top = F32(static_cast<double>(top) + FirstStep);

    auto life = format(LifeText, "0x414a55");
    if (!life.HasValue())
        return Core::Unexpected(std::move(life).Error());
    SetColors(context, LineColors);
    WindowDrawing::DrawClipped(
        context, lineLeft, Truncate(top),
        UI::Sprintf(life.Value(), static_cast<std::uint32_t>(stats.durability), std::uint32_t{MaxDurability}), lineBox);
    top = F32(static_cast<double>(top) + StatsStep);
    // 0x414ac8..0x414b3e: la barra de la durabilidad, recortada a 255.
    sprites.SetColor(DurabilityBackColor);
    sprites.ColorQuad(static_cast<float>(lineLeft), top, DurabilityBackWidth, DurabilityBackHeight);
    sprites.SetColor(DurabilityColor);
    const std::uint16_t durability = stats.durability < MaxDurability ? stats.durability : MaxDurability;
    const double scaled = static_cast<double>(durability) * static_cast<double>(InverseMaxDurability);
    sprites.ColorQuad(static_cast<float>(lineLeft + 1), F32(static_cast<double>(top) + 1.0),
                      F32(scaled * DurabilityWidth), DurabilityHeight);
    top = F32(static_cast<double>(top) + StatsStep);
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);

    auto experience = format(ExperienceText, "0x414b7b");
    if (!experience.HasValue())
        return Core::Unexpected(std::move(experience).Error());
    drawLine(UI::Sprintf(experience.Value(), stats.experience, stats.nextExperience));

    // 0x414bca..0x414d24: el caballo, con su propio acceso al registro propio, el % del cetro (el byte +0x16 del lugar
    // 0 del equipo), si hay algo en ese lugar.
    float scepter = 0.0f;
    if (type == 1)
    {
        const auto access = OwnAccess(context);
        const World::ItemState& weapon = own.equipment[0];
        if (weapon.id != EmptyOwnItemId)
        {
            auto scepterText = format(ScepterText, "0x414cc4");
            if (!scepterText.HasValue())
                return Core::Unexpected(std::move(scepterText).Error());
            const auto percent = static_cast<float>(weapon.byte16);
            drawLine(UI::Sprintf(scepterText.Value(), static_cast<std::uint32_t>(weapon.byte16)));
            scepter = F32(static_cast<double>(percent) * static_cast<double>(Percent));
        }
    }
    // 0x414ebf..0x41518b: el daño de +0xc y +0xe. El tipo 0 le suma el % (que solo el tipo 1 lee, así que es 0,
    // Q-C1040); el 1, el de la skill 0x3e (0x5516e0) con el registro propio abierto.
    std::int32_t minimum = stats.word0c;
    std::int32_t maximum = stats.word0e;
    if (type == 0)
    {
        minimum += Truncate(static_cast<double>(minimum) * static_cast<double>(scepter));
        maximum += Truncate(static_cast<double>(maximum) * static_cast<double>(scepter));
    }
    else if (type == 1)
    {
        ItemSetOptionList projected = world.ItemSets().list;
        ItemSetOptionList& setOptions = context.projecting ? projected : world.ItemSets().list;
        OwnSkillDamage damage{};
        {
            const auto access = OwnAccess(context);
            auto skill = SkillDamageOf(HorseSkill, context.skills, world.Runtime().movement.skillDepths[HorseSkill],
                                       context.random, setOptions);
            if (!skill.HasValue())
                return Core::Unexpected(std::format("0x414fd6: {}", skill.Error()));
            damage = skill.Value();
        }
        minimum += damage.min;
        maximum += damage.max;
    }
    auto damageText = format(DamageText, "0x4151ac");
    if (!damageText.HasValue())
        return Core::Unexpected(std::move(damageText).Error());
    drawLine(UI::Sprintf(damageText.Value(), minimum, maximum, static_cast<std::uint32_t>(stats.word12)));
    auto speed = format(AttackSpeedText, "0x41520b");
    if (!speed.HasValue())
        return Core::Unexpected(std::move(speed).Error());
    drawLine(UI::Sprintf(speed.Value(), static_cast<std::uint32_t>(stats.word10)));

    // 0x415240..0x4155bd: el liderazgo que pide, en rojo si el del registro propio (+0x36 + +0x20, de 16 bits) no
    // alcanza, con su acceso. Siempre es positivo: el nivel es de 16 bits.
    const std::int32_t leadership = static_cast<std::int32_t>(stats.level) * LeadershipPerLevel + LeadershipBase;
    if (leadership > 0)
    {
        auto leadershipText = format(LeadershipText, "0x415343");
        if (!leadershipText.HasValue())
            return Core::Unexpected(std::move(leadershipText).Error());
        const auto access = OwnAccess(context);
        const auto own16 = static_cast<std::uint16_t>(own.word36 + own.leadership);
        if (static_cast<std::int32_t>(own16) < leadership)
            context.hud.textColors.text = LackingColor;
        drawLine(UI::Sprintf(leadershipText.Value(), leadership));
        SetColors(context, LineColors);
    }

    if (!commands)
    {
        if (type != 0)
            return {};
        return Core::Unexpected(std::string("0x415869: los datos del tipo 0 sin las órdenes no los llama nadie (las "
                                            "llamadas de 0x424e40 y 0x416670 son con el tipo 1 o con las órdenes)"));
    }
    // 0x4155c8..0x4157af: las cuatro órdenes del cuervo.
    auto commandsTitle = format(CommandsTitleText, "0x41560b");
    if (!commandsTitle.HasValue())
        return Core::Unexpected(std::move(commandsTitle).Error());
    SetColors(context, CommandsTitleColors);
    WindowDrawing::DrawInBox(context, x + LevelLeft, y + CommandsTitleTop, commandsTitle.Value(),
                             WindowDrawing::BoxWidth(context, LevelBox));
    const float commandX = F32(static_cast<double>(x) + CommandsLeft);
    SetColors(context, LineColors);
    for (std::size_t command = 0; command < CommandTops.size(); ++command)
    {
        auto text = format(FirstCommandText + command, "0x415691");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        const float commandY = F32(static_cast<double>(floatY) + CommandTops[command]);
        Quad(sprites, CommandsSlot, commandX, commandY, CommandWidth, CommandHeight,
             CommandDu * static_cast<float>(command), 0.0f, CommandDu, CommandDv);
        WindowDrawing::DrawText(context, Truncate(static_cast<double>(commandX)) + CommandTextLeft,
                                Truncate(static_cast<double>(commandY)) + CommandTextTop, text.Value());
    }
    // 0x4157b4..0x415858: la ayuda de la primera orden con el mouse encima, y sale.
    const auto mouseX = static_cast<double>(static_cast<float>(context.mouse.x));
    const auto mouseY = static_cast<double>(context.mouse.y);
    for (std::size_t command = 0; command < CommandTops.size(); ++command)
    {
        const float commandY =
            F32(static_cast<double>(command) * CommandStep + static_cast<double>(floatY) + CommandTops.front());
        if (mouseX < static_cast<double>(commandX) ||
            static_cast<double>(commandX) + static_cast<double>(CommandWidth) < mouseX)
            continue;
        if (mouseY < static_cast<double>(commandY) ||
            mouseY > static_cast<double>(commandY) + static_cast<double>(CommandHeight))
            continue;
        auto help = format(FirstCommandHelpText + command, "0x415845");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        DrawHelp(context, Truncate(static_cast<double>(commandX)), Truncate(static_cast<double>(commandY)),
                 help.Value());
        return {};
    }
    return {};
}

// 0x424e40(héroe): la pestaña del caballo.
Core::Expected<void, std::string> DrawTheHorseTab(GameHudContext& context, UI::PanelRegistry& panels,
                                                  const ActorState& hero)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.petTab != HorseTab)
        return {};
    const std::int32_t left = WindowLeftOf(panels);
    auto horse = TextOf(context, DarkHorseText, "0x424eff");
    if (!horse.HasValue())
        return Core::Unexpected(std::move(horse).Error());
    if (hero.pet.model != DarkHorseModel)
    {
        auto format = TextOf(context, MissingPetText, "0x424f69");
        if (!format.HasValue())
            return Core::Unexpected(std::move(format).Error());
        DrawTheMissingPet(context, left, UI::Sprintf(format.Value(), horse.Value()));
        return {};
    }
    Render::SpriteBatch& sprites = context.sprites;
    Quad(sprites, StatsBackSlot, F32(static_cast<double>(left) + StatsBackLeft - StatsBackShift),
         static_cast<float>(StatsBackTop), StatsBackWidth, StatsBackHeight, 0.0f, 0.0f, StatsBackDu, StatsBackDv);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    WindowDrawing::SelectFont(context, FontKind::Bold);
    SetColors(context, NameColors);
    WindowDrawing::DrawCenteredText(context, left + HorseNameLeft, PetNameTop, horse.Value());
    return DrawTheStats(context, 1, left, 0, runtime.pets[DarkHorsePetStats], false);
}

// 0x416670, thiscall sobre el cuervo: la pestaña del cuervo.
Core::Expected<void, std::string> DrawTheRavenTab(GameHudContext& context, UI::PanelRegistry& panels,
                                                  const DarkSpirit& raven)
{
    const std::int32_t left = WindowLeftOf(panels);
    Render::SpriteBatch& sprites = context.sprites;
    const float backX = F32(static_cast<double>(left) + StatsBackLeft - StatsBackShift);
    for (std::int32_t back = 0; back < StatsBackEnd; back += StatsBackStep)
        Quad(sprites, StatsBackSlot, backX, F32(static_cast<double>(back) + StatsBackTop), StatsBackWidth,
             StatsBackHeight, 0.0f, 0.0f, StatsBackDu, StatsBackDv);
    sprites.SetBlend(BlendMode::Alpha);
    sprites.SetColor(White);
    auto name = TextOf(context, DarkRavenText, "0x416767");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    WindowDrawing::SelectFont(context, FontKind::Bold);
    SetColors(context, NameColors);
    WindowDrawing::DrawTitle(context, left + TitleLeft, PetNameTop, name.Value());
    // 0x41677d..0x41679b: los 0x14 bytes de +0x448 y el tipo de +0x444 (0, Q-C1041).
    const PetStats stats{.experience = raven.experience,
                         .nextExperience = raven.nextExperience,
                         .level = raven.level,
                         .durability = raven.durability,
                         .word0c = raven.word454,
                         .word0e = raven.word456,
                         .word10 = raven.word458,
                         .word12 = raven.word45a};
    return DrawTheStats(context, 0, left, 0, stats, true);
}

// La vtable + 0xc del cuervo (0x4182e0 → 0x418460): su cartel arriba a la derecha, con o sin la ventana.
Core::Expected<void, std::string> DrawTheRavenHud(GameHudContext& context, UI::PanelRegistry& panels,
                                                  const ActorState& hero, const DarkSpirit& raven)
{
    Render::SpriteBatch& sprites = context.sprites;
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    WindowDrawing::SelectFont(context, FontKind::Bold);
    SetColors(context, RavenHudColors);
    float offset = context.hud.partyCount > 0 ? PartyOffset : 0.0f;
    // 0x4184ac: la mascota del héroe de [0x743a170], con signo.
    const auto pet = static_cast<std::int16_t>(hero.pet.model);
    if ((pet >= FirstPetModel && pet <= LastPetModel) || pet == FenrirModel)
        offset = F32(static_cast<double>(offset) + PetOffset);
    const std::int32_t worldWidth = panels.WorldWidth();
    const float x = F32(static_cast<double>(worldWidth) - RavenHudWidth - static_cast<double>(offset) - RavenHudRight);
    std::int32_t fill = 0;
    {
        const auto access = OwnAccess(context);
        fill = static_cast<std::int32_t>(own.equipment[1].durability) * RavenGaugeFill / RavenGaugeFull;
    }
    sprites.SetBlend(BlendMode::Alpha);
    auto name = TextOf(context, DarkRavenText, "0x41877b");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    // 0x41877a..0x4187b1: GetTextExtentPoint32 sin usar lo que mide y 0x5542f0(_ftol(x) + 50, 4, texto, 0, 4, 0), que
    // termina en x con la fuente del DC y no dibuja un texto vacío ni mide.
    if (!name.Value().empty())
    {
        const std::int32_t at =
            Render::Text::EndingAt(Truncate(static_cast<double>(x)) + RavenNameRight,
                                   context.text.Measure(context.hud.dcFont, name.Value()), sprites.Screen());
        context.text.Draw(at, RavenNameTop, name.Value(),
                          Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors});
    }
    UI::DrawGauge(sprites, x, RavenGaugeTop, RavenGaugeWidth, RavenGaugeHeight, static_cast<float>(fill), false, true);
    sprites.SetColor(White);
    const std::int32_t iconWidth = panels.WorldWidth();
    const float iconX =
        F32(static_cast<double>(iconWidth) - OrderIconShift - static_cast<double>(offset) - OrderIconRight);
    const std::uint8_t order = raven.order;
    const float v = F32((static_cast<double>(order >> 3u) * OrderRowHeight + OrderRowTop) * Texel);
    const float u = F32((static_cast<double>(order & 7u) * OrderColumnWidth + OrderColumnLeft) * Texel);
    Quad(sprites, CommandsSlot, iconX, OrderIconTop, OrderIconWidth, OrderIconHeight, u, v, OrderIconDu, OrderIconDv);
    const auto mouseX = static_cast<double>(context.mouse.x);
    const auto mouseY = static_cast<double>(context.mouse.y);
    if (mouseX < static_cast<double>(iconX) || mouseX > static_cast<double>(iconX) + OrderHoverWidth ||
        mouseY < OrderHoverTop || mouseY > OrderHoverBottom)
        return {};
    auto help = TextOf(context, FirstCommandText + order, "0x4188f5");
    if (!help.HasValue())
        return Core::Unexpected(std::move(help).Error());
    DrawHelp(context, Truncate(static_cast<double>(iconX)), OrderHelpTop, help.Value());
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawThePetWindow(GameHudContext& context, UI::PanelRegistry& panels)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const ActorState& hero = world.Actors()[runtime.hero];
    auto pet = DrawTheButtonAndFrame(context, panels);
    if (!pet.HasValue())
        return Core::Unexpected(std::move(pet).Error());
    if (pet.Value() == PetWindowOpen)
    {
        if (auto horse = DrawTheHorseTab(context, panels, hero); !horse.HasValue())
            return horse;
        if (hero.darkSpirit)
        {
            if (runtime.petTab == RavenTab)
            {
                if (auto raven = DrawTheRavenTab(context, panels, *hero.darkSpirit); !raven.HasValue())
                    return raven;
            }
        }
        else if (runtime.petTab == RavenTab)
        {
            const std::int32_t left = WindowLeftOf(panels);
            auto noPet = TextOf(context, NoPetText, "0x4241d6");
            if (!noPet.HasValue())
                return Core::Unexpected(std::move(noPet).Error());
            DrawTheMissingPet(context, left, noPet.Value());
        }
    }
    if (hero.darkSpirit)
        return DrawTheRavenHud(context, panels, hero, *hero.darkSpirit);
    return {};
}
} // namespace Client::Scenes
