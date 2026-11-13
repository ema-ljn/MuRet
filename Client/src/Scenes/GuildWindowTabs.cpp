#include "Scenes/GuildWindowTabs.h"

#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/ScreenScale.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/GuildMarks.h"
#include "Scenes/GuildState.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/OldControls.h"
#include "UI/OldWindowBase.h"
#include "UI/OldWindowDraw.h"
#include "UI/OldWindowDrawParts.h"
#include "UI/SprintfFormat.h"
#include "World/OriginalFtol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t NoticeTab = 0;
constexpr std::int32_t MembersTab = 1;
constexpr std::int32_t UnionTab = 2;
constexpr std::uint8_t MasterStatus = 0x80;
constexpr std::uint8_t AssistantStatus = 0x40;
constexpr std::uint8_t BattleMasterStatus = 0x20;

// 0x64fdf0: los marcos (0x471520), la marca de 48 (0x621b60(0x22, …)) y los renglones de a 0xd en cajas de 140 · rx
// (0x6b8ca8) y 136 · rx (0x6baa88).
constexpr float MarkBoxSide = 53.0f;
constexpr float MarkSide = 48.0f;
constexpr float InfoBoxWidth = 140.0f;
constexpr float InfoBoxHeight = 62.0f;
constexpr float NoticeBoxHeight = 95.0f;
constexpr double InfoTextBox = 140.0;
constexpr double NoticeTitleBox = 136.0;
constexpr std::int32_t InfoStep = 0xd;
constexpr std::size_t GuildText = 1332;
constexpr std::size_t ScoreText = 1256;
constexpr std::size_t MasterMembersText = 1362;
constexpr std::size_t MembersText = 1310;
constexpr std::size_t NoticeNameText = 1321;
constexpr std::size_t NoNoticeNameText = 1361;
constexpr std::size_t NoticeTitleText = 1323;
constexpr std::size_t MasterLeaveText = 188;
constexpr std::size_t LeaveText = 189;
// 0x650047..0x650098: la clase 4 (+0xb & 7) suma el liderazgo (+0x20) / 10 al nivel (+0xe) / 10, hasta 0x50.
constexpr std::uint8_t LeadingClass = 4;
constexpr std::int32_t LevelsPerMember = 10;
constexpr std::int32_t MostMembers = 0x50;
// 0x6502a1: la lista del aviso de 0x88 × 0x50.
constexpr std::int32_t NoticeListWidth = 0x88;
constexpr std::int32_t ListHeight = 0x50;
// 0x64f3e0..0x64f5c9: los textos de los botones del maestro y de +0x6d4 (0x45b170).
constexpr std::array<std::size_t, GuildMemberButtonCount> MemberButtonTexts{1307, 1308, 1309};
constexpr std::size_t UnionKickButtonText = 1422;
// 0x651400: sin alianza, los textos 1257 a 1267 en x + 0x19, con lo que suma cada uno a la y antes de dibujarse; desde
// el 1262, en blanco sobre 0xff0000cc.
constexpr std::size_t FirstNoUnionText = 1257;
constexpr std::array<std::int32_t, 11> NoUnionSteps{8, 0xf, 0xf, 0xf, 0xf, 0x19, 0xf, 0xf, 0x14, 0xf, 0xf};
constexpr std::size_t FirstColoredText = 5;
constexpr std::uint32_t NoUnionColor = 0xffffffffu;
constexpr std::uint32_t NoUnionBackground = 0xff0000ccu;
constexpr std::int32_t UnionListWidth = 0xa0;
constexpr std::size_t UnionMasterButtonText = 1324;
constexpr std::size_t UnionButtonText = 1325;
// 0x46be20, 0x46cb20 y 0x46daa0: la caja del 7 de la paleta con alfa 0,4 y los encabezados en y − h − 0xc.
constexpr std::int32_t ListBoxPalette = 7;
constexpr float ListBoxAlpha = 0.4f;
constexpr std::int32_t HeaderAbove = 0xc;
constexpr std::array<std::size_t, 3> MemberHeaders{1389, 1307, 1022};
constexpr std::array<std::int32_t, 3> MemberHeaderLefts{0xe, 0x41, 0x6a};
constexpr std::array<std::size_t, 2> UnionHeaders{182, 1330};
constexpr std::array<std::int32_t, 2> UnionHeaderLefts{0xf, 0x71};
// 0x46c2c0, 0x46d050 y 0x46dfa0: la franja del elegido (glColor4f(1, 1, 1, 0,5)) de 13 desde la y del renglón − 3, y
// los colores del texto.
constexpr std::int32_t RowAbove = 3;
constexpr float RowHeight = 13.0f;
constexpr float ChosenAlpha = 0.5f;
constexpr std::uint32_t ChosenTextColor = 0xff000000u;
constexpr std::uint32_t RowTextColor = 0xffc8dce6u;
// 0x46d07e..0x46d131: el color del puesto (glColor4ub, alfa 0x7f) y su texto (1300 a 1302) centrado en 0x46 desde
// x + 8 + 0x2d; el servidor + 1 en 0xff00c4ff en x + ancho − 0x1e.
struct StatusLook
{
    std::uint8_t status = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::size_t text = 0;
};
constexpr std::array<StatusLook, 3> StatusLooks{
    {{MasterStatus, 0x64, 0x32, 1300}, {AssistantStatus, 0x96, 0x50, 1301}, {BattleMasterStatus, 0xc8, 0x64, 1302}}};
constexpr float StatusAlpha = 127.0f / 255.0f;
constexpr std::int32_t MemberNameLeft = 8;
constexpr std::int32_t StatusLeft = 0x2d;
constexpr std::int32_t StatusBox = 0x46;
constexpr std::int32_t ServerRight = 0x1e;
constexpr std::uint8_t NoServer = 0xff;
constexpr std::uint32_t ServerColor = 0xff00c4ffu;
// 0x46c386 y 0x46e06c: el texto del aviso y la marca de la alianza en x + 4; la marca de 8 × 8 con la tabla 1999
// (0x78a5986), el nombre 0xc más a la derecha y el número con el modo 4 (a la derecha de x + 4 + 0x8a).
constexpr std::int32_t RowLeft = 4;
constexpr float UnionMarkSide = 8.0f;
constexpr std::int32_t UnionNameLeft = 0xc;
constexpr std::int32_t UnionNumberRight = 0x8a;

constexpr Render::Color White{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = 1.0f};

float F32(double value)
{
    return static_cast<float>(value);
}

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

UI::OldWindowDrawContext OldContextOf(GameHudContext& context)
{
    return UI::OldWindowDrawContext{.sprites = context.sprites,
                                    .text = context.text,
                                    .dcFont = context.hud.dcFont,
                                    .textColors = context.hud.textColors};
}

UI::OldWindowDrawView OldViewOf(const GameHudContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    return UI::OldWindowDrawView{.mouseX = context.mouse.x,
                                 .mouseY = context.mouse.y,
                                 .leftClick = runtime.movement.pendingClick,
                                 .focusedList = runtime.ui.focusedList};
}

// 0x5542f0(x, y, texto, caja, 1, 0) → 0x467060 en el modo 1 (0x467102..0x467119): la caja de ese ancho desde x; si el
// texto es más ancho, se ve desde la columna cx − caja. Vacío y sin caja, nada.
void DrawInTheBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t box)
{
    const Render::Text::TextStyle style{.font = context.hud.dcFont, .colors = context.hud.textColors};
    if (box <= 0)
    {
        if (!text.empty())
            context.text.Draw(x, y, text, style);
        return;
    }
    const int measured = context.text.Measure(context.hud.dcFont, text.empty() ? std::string_view("0") : text).width;
    context.text.DrawColumns(x, y, text, style, box < measured ? measured - box : 0, box);
}

std::int32_t BoxOf(const GameHudContext& context, double width)
{
    return Truncate(static_cast<double>(Render::ScaleX(1.0f, context.sprites.Screen())) * width);
}

// 0x45b2e0 sobre un botón de la ventana, con el mouse del dibujo.
void DrawTheButton(GameHudContext& context, const UI::OldButton& button)
{
    UI::DrawTheOldButton(OldContextOf(context), button,
                         UI::OldWindowDrawView{.mouseX = context.mouse.x, .mouseY = context.mouse.y});
}

// La primera vez, el texto de 0x45b170 (MuRet lo copia en el primer dibujo, como la ventana 2).
Core::Expected<void, std::string> LabelTheButton(GameHudContext& context, UI::OldButton& button, std::size_t text)
{
    if (button.label)
        return {};
    auto label = TextOf(context, text, "0x64f3e0");
    if (!label.HasValue())
        return Core::Unexpected(std::move(label).Error());
    button.label = std::string(label.Value());
    return {};
}

// La vtable + 0x30 del botón: el texto nuevo.
Core::Expected<void, std::string> RelabelTheButton(GameHudContext& context, UI::OldButton& button, std::size_t text,
                                                   std::string_view where)
{
    auto label = TextOf(context, text, where);
    if (!label.HasValue())
        return Core::Unexpected(std::move(label).Error());
    button.label = std::string(label.Value());
    return {};
}

// El principio de 0x46be20, 0x46cb20 y 0x46daa0 (la vtable + 0x7c): 0x620a00(1), la caja del 7 de la paleta con alfa
// 0,4 un punto afuera (x − 1, y − h − 1, w + 1, h + 2), 0x459ec0 y la barra (0x461890), y los encabezados en
// y − h − 0xc con 0x465950 en el modo 1, con la fuente y los colores que haya.
Core::Expected<void, std::string> DrawTheListHead(GameHudContext& context, UI::OldList& list, std::size_t size,
                                                  std::span<const std::size_t> headers,
                                                  std::span<const std::int32_t> lefts)
{
    Render::SpriteBatch& sprites = context.sprites;
    const UI::OldWindowBase& base = list.base;
    sprites.SetBlend(Render::BlendMode::Alpha);
    UI::OldDraw::SetThePalette(sprites, ListBoxPalette, ListBoxAlpha);
    sprites.ColorQuad(static_cast<float>(base.x - 1), static_cast<float>(base.y - base.height - 1),
                      static_cast<float>(base.width + 1), static_cast<float>(base.height + 2));
    UI::OldDraw::WhiteWithTexture(sprites);
    UI::OldDraw::DrawTheListBar(sprites, list, size, OldViewOf(context));
    for (std::size_t header = 0; header < headers.size(); ++header)
    {
        auto text = TextOf(context, headers[header], "0x46cb20");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        static_cast<void>(UI::OldDraw::DrawText(OldContextOf(context), base.x + lefts[header],
                                                base.y - base.height - HeaderAbove, text.Value()));
    }
    return {};
}

// El principio de 0x46c2c0, 0x46d050 y 0x46dfa0 después del color del puesto: el elegido (+0x68 igual al corrimiento
// + renglón + 1) sobre la franja blanca de alfa 0,5 con el texto en negro; los demás en 0xffc8dce6. glEnable de la
// textura y el fondo del texto en 0.
void DrawTheRowBand(GameHudContext& context, const UI::OldList& list, std::size_t size, std::int32_t row)
{
    Render::SpriteBatch& sprites = context.sprites;
    if (list.selection == list.scroll + row + 1)
    {
        sprites.SetColor(Render::Color{.red = 1.0f, .green = 1.0f, .blue = 1.0f, .alpha = ChosenAlpha});
        sprites.ColorQuad(static_cast<float>(list.base.x), static_cast<float>(UI::ListRowY(list, size, row) - RowAbove),
                          F32(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0),
                          RowHeight);
        sprites.SetColor(White);
        context.hud.textColors.text = ChosenTextColor;
    }
    else
        context.hud.textColors.text = RowTextColor;
    sprites.EnableTexture();
    context.hud.textColors.background = 0;
}

// 0x46c2c0, el renglón del aviso.
std::int32_t DrawTheNoticeRow(GameHudContext& context, const GuildWindowState& window, std::int32_t row, std::size_t at)
{
    const UI::OldList& list = window.noticeList;
    const std::size_t size = window.notice.size();
    context.sprites.SetBlend(Render::BlendMode::Alpha);
    DrawTheRowBand(context, list, size, row);
    static_cast<void>(UI::OldDraw::DrawText(OldContextOf(context), list.base.x + RowLeft, UI::ListRowY(list, size, row),
                                            window.notice[at].text));
    // 0x620980.
    context.sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x46d050, el renglón de los miembros.
Core::Expected<std::int32_t, std::string> DrawTheMemberRow(GameHudContext& context, const GuildWindowState& window,
                                                           std::int32_t row, std::size_t at)
{
    Render::SpriteBatch& sprites = context.sprites;
    const UI::OldList& list = window.memberList;
    const std::size_t size = window.members.size();
    const GuildMemberSlot& member = window.members[at].slot;
    const std::int32_t rowY = UI::ListRowY(list, size, row);
    sprites.SetBlend(Render::BlendMode::Alpha);
    std::optional<StatusLook> look;
    for (const StatusLook& candidate : StatusLooks)
    {
        if (candidate.status == member.status)
            look = candidate;
    }
    if (look)
    {
        sprites.SetColor(Render::Color{.red = 1.0f,
                                       .green = static_cast<float>(look->green) / 255.0f,
                                       .blue = static_cast<float>(look->blue) / 255.0f,
                                       .alpha = StatusAlpha});
        sprites.ColorQuad(static_cast<float>(list.base.x), static_cast<float>(rowY - RowAbove),
                          F32(static_cast<double>(list.base.width) - static_cast<double>(list.thumbWidth) + 1.0),
                          RowHeight);
        // 0x46d18a: glColor4f(255, 255, 255, 255), que GL acota a 1.
        sprites.SetColor(White);
    }
    DrawTheRowBand(context, list, size, row);
    const UI::OldWindowDrawContext old = OldContextOf(context);
    const std::int32_t left = list.base.x + MemberNameLeft;
    static_cast<void>(UI::OldDraw::DrawText(old, left, rowY, GuildTextOf(member.name)));
    if (look)
    {
        // 0x46d300: 0x465950(x, y, texto, 0x46, 0, 2, 0, 0x280), centrado en una caja de 0x46 sin escalar.
        auto text = TextOf(context, look->text, "0x46d2ca");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        const Render::Text::CenteredInBox placed =
            Render::Text::CenterInBox(context.text, context.hud.dcFont, text.Value(), StatusBox);
        context.text.DrawColumns(left + StatusLeft, rowY, text.Value(),
                                 Render::Text::TextStyle{.font = context.hud.dcFont, .colors = context.hud.textColors},
                                 -placed.textColumn, placed.width);
    }
    if (member.server != NoServer)
    {
        context.hud.textColors.background = 0;
        context.hud.textColors.text = ServerColor;
        static_cast<void>(UI::OldDraw::DrawText(old, list.base.x + list.base.width - ServerRight, rowY,
                                                UI::Sprintf("%d", static_cast<std::int32_t>(member.server) + 1)));
    }
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x46dfa0, el renglón de la alianza: la marca del elemento pasa por la entrada 1999 de la tabla (0x78a5986), que
// después toma la del gremio del héroe, o ceros sin gremio (Q-C1294).
Core::Expected<std::int32_t, std::string> DrawTheUnionRow(GameHudContext& context, GuildState& guild, std::int32_t row,
                                                          std::size_t at)
{
    Render::SpriteBatch& sprites = context.sprites;
    const GuildWindowState& window = guild.window;
    const UI::OldList& list = window.unionList;
    const std::size_t size = window.unions.size();
    const GuildUnionRow& element = window.unions[at];
    const std::int32_t rowY = UI::ListRowY(list, size, row);
    sprites.SetBlend(Render::BlendMode::Alpha);
    DrawTheRowBand(context, list, size, row);
    const std::int32_t left = list.base.x + RowLeft;
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x46e09f: la marca de la alianza (0x5f16a0) sin la tabla de las marcas"));
    auto& creating = guild.table[static_cast<std::size_t>(CreatingGuildIndex)].mark;
    creating = element.mark;
    // 0x46e09f: 0x5f16a0(1999, 1). MuRet guarda la marca en el slot de (1999, 1) y la dibuja con el lote: si la
    // alianza tiene varios gremios, cada fila sube la suya al mismo slot (P-C289).
    auto slot = context.guildMarks->Upload(guild, CreatingGuildIndex, true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    WindowDrawing::Quad(sprites, slot.Value().value, static_cast<float>(left), static_cast<float>(rowY), UnionMarkSide,
                        UnionMarkSide, 1.0f, 1.0f);
    const auto index = static_cast<std::int16_t>(context.world.Actors()[context.world.Runtime().hero].word1f2);
    if (index < 0)
        creating = {};
    else if (static_cast<std::size_t>(index) >= GuildTableSize)
        return Core::Unexpected(std::format("0x46e101: el gremio {} del héroe cae fuera de la tabla", index));
    else
        creating = guild.table[static_cast<std::size_t>(index)].mark;
    const UI::OldWindowDrawContext old = OldContextOf(context);
    static_cast<void>(UI::OldDraw::DrawText(old, left + UnionNameLeft, rowY, GuildTextOf(element.name)));
    // 0x46e1bd: 0x465950 en el modo 4 (0x467146): x = _ftol(x − cx / rx), y el texto desde ahí.
    const std::string number = UI::Sprintf("%d", static_cast<std::int32_t>(element.byte0));
    const auto measured = static_cast<double>(context.text.Measure(context.hud.dcFont, number).width);
    const std::int32_t right =
        Truncate(static_cast<double>(left + UnionNumberRight) - measured / UI::OldDraw::RatioX(sprites.Screen()));
    static_cast<void>(UI::OldDraw::DrawText(old, right, rowY, number));
    sprites.SetBlend(Render::BlendMode::Opaque);
    return 1;
}

// 0x47a810 (o 0x480520 y 0x481bf0) con la cabeza y el renglón de la instancia, sin cola (0x4724e0, 0x472110 y
// 0x472580: un ret). El primer error de un renglón corta.
template <typename Row>
Core::Expected<void, std::string> DrawTheList(GameHudContext& context, UI::OldList& list, std::size_t size,
                                              std::span<const std::size_t> headers, std::span<const std::int32_t> lefts,
                                              Row drawRow)
{
    std::optional<std::string> failed;
    const UI::OldDraw::OldListDrawing drawing{.head =
                                                  [&]()
                                              {
                                                  if (auto head = DrawTheListHead(context, list, size, headers, lefts);
                                                      !head.HasValue())
                                                      failed = std::move(head).Error();
                                              },
                                              .row = [&](std::int32_t row, std::size_t at) -> std::int32_t
                                              {
                                                  if (failed)
                                                      return 1;
                                                  auto drawn = drawRow(row, at);
                                                  if (!drawn.HasValue())
                                                  {
                                                      failed = std::move(drawn).Error();
                                                      return 1;
                                                  }
                                                  return drawn.Value();
                                              },
                                              .tail = {}};
    UI::OldDraw::DrawTheOldList(OldContextOf(context), list, size, drawing);
    if (failed)
        return Core::Unexpected(std::move(*failed));
    return {};
}

// 0x650047..0x6500dd: el tope de miembros del maestro, con el registro propio abierto.
std::int32_t MostMembersOf(GameHudContext& context)
{
    const Render::Models::LogicalAccess own(context.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    const OwnCharacterRecord& record = context.world.OwnCharacter();
    const std::int32_t levels = static_cast<std::int32_t>(record.level) / LevelsPerMember;
    if ((record.characterClass & 7u) != LeadingClass)
        return levels;
    const std::int32_t most = static_cast<std::int32_t>(record.leadership) / LevelsPerMember + levels;
    return most > MostMembers ? MostMembers : most;
}

// 0x64fdf0.
Core::Expected<void, std::string> DrawTheNoticeTab(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    Render::SpriteBatch& sprites = context.sprites;
    const std::int32_t x = window.base.x;
    const std::int32_t y = window.base.y;
    const ActorState& hero = context.world.Actors()[runtime.hero];
    WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + 0x45), static_cast<float>(y + 0x68), MarkBoxSide,
                                MarkBoxSide);
    if (context.guildMarks == nullptr)
        return Core::Unexpected(std::string("0x64fe4a: la marca del gremio (0x5f16a0) sin la tabla de las marcas"));
    auto slot = context.guildMarks->Upload(guild, static_cast<std::int16_t>(hero.word1f2), true);
    if (!slot.HasValue())
        return Core::Unexpected(std::move(slot).Error());
    WindowDrawing::Quad(sprites, slot.Value().value, static_cast<float>(x + 0x48), static_cast<float>(y + 0x6b),
                        MarkSide, MarkSide, 1.0f, 1.0f);
    WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + 0x19), static_cast<float>(y + 0xaa), InfoBoxWidth,
                                InfoBoxHeight);
    const std::int32_t left = x + 0x1e;
    std::int32_t top = y + 0xb2;
    const std::int32_t box = BoxOf(context, InfoTextBox);
    auto guildText = TextOf(context, GuildText, "0x64fec8");
    if (!guildText.HasValue())
        return Core::Unexpected(std::move(guildText).Error());
    DrawInTheBox(context, left, top, UI::Sprintf("%s :", guildText.Value()), box);
    top += InfoStep;
    auto score = TextOf(context, ScoreText, "0x64ff0c");
    if (!score.HasValue())
        return Core::Unexpected(std::move(score).Error());
    DrawInTheBox(context, left, top, UI::Sprintf(score.Value(), guild.score), box);
    top += InfoStep;
    std::string members;
    if (hero.guildStatus == MasterStatus)
    {
        auto format = TextOf(context, MasterMembersText, "0x6500e8");
        if (!format.HasValue())
            return Core::Unexpected(std::move(format).Error());
        const std::int32_t most = MostMembersOf(context);
        members = UI::Sprintf(format.Value(), guild.memberCount, most);
    }
    else
    {
        auto format = TextOf(context, MembersText, "0x650331");
        if (!format.HasValue())
            return Core::Unexpected(std::move(format).Error());
        members = UI::Sprintf(format.Value(), guild.memberCount);
    }
    DrawInTheBox(context, left, top, members, box);
    top += InfoStep;
    auto noticeName = TextOf(context, NoticeNameText, "0x650222");
    if (!noticeName.HasValue())
        return Core::Unexpected(std::move(noticeName).Error());
    std::string_view writer = GuildTextOf(window.name88);
    if (writer.empty())
    {
        auto nobody = TextOf(context, NoNoticeNameText, "0x65021c");
        if (!nobody.HasValue())
            return Core::Unexpected(std::move(nobody).Error());
        writer = nobody.Value();
    }
    DrawInTheBox(context, left, top, UI::Sprintf("%s : %s", noticeName.Value(), writer), box);
    top += 0x21;
    auto title = TextOf(context, NoticeTitleText, "0x650277");
    if (!title.HasValue())
        return Core::Unexpected(std::move(title).Error());
    DrawInTheBox(context, left, top, title.Value(), BoxOf(context, NoticeTitleBox));
    top += 0xa;
    // 0x650298..0x6502c2: el tamaño (la vtable + 8), el lugar (0x45a9d0) y el dibujo de la lista del aviso.
    UI::OldList& list = window.noticeList;
    list.base.width = NoticeListWidth;
    list.base.height = ListHeight;
    list.base.x = x + 0x1c;
    list.base.y = top + list.base.height;
    if (auto drawn = DrawTheList(context, list, window.notice.size(), {}, {},
                                 [&](std::int32_t row, std::size_t at) -> Core::Expected<std::int32_t, std::string>
                                 { return DrawTheNoticeRow(context, window, row, at); });
        !drawn.HasValue())
        return drawn;
    top -= 0xe;
    WindowDrawing::DrawTheFrame(sprites, static_cast<float>(x + 0x19), static_cast<float>(top), InfoBoxWidth,
                                NoticeBoxHeight);
    UI::OldButton& leave = window.tabButtons[GuildLeaveButton];
    if (auto labeled = RelabelTheButton(context, leave, hero.guildStatus == MasterStatus ? MasterLeaveText : LeaveText,
                                        "0x6502f8");
        !labeled.HasValue())
        return labeled;
    DrawTheButton(context, leave);
    return {};
}

// 0x6507d0.
Core::Expected<void, std::string> DrawTheMembersTab(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    if (guild.memberCount > 0)
    {
        if (auto drawn = DrawTheList(context, window.memberList, window.members.size(), MemberHeaders,
                                     MemberHeaderLefts, [&](std::int32_t row, std::size_t at)
                                     { return DrawTheMemberRow(context, window, row, at); });
            !drawn.HasValue())
            return drawn;
    }
    if (context.world.Actors()[runtime.hero].guildStatus != MasterStatus)
        return {};
    for (std::size_t button = 0; button < window.memberButtons.size(); ++button)
    {
        if (auto labeled = LabelTheButton(context, window.memberButtons[button], MemberButtonTexts[button]);
            !labeled.HasValue())
            return labeled;
        DrawTheButton(context, window.memberButtons[button]);
    }
    return {};
}

// 0x651400.
Core::Expected<void, std::string> DrawTheUnionTab(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GuildState& guild = runtime.guild;
    GuildWindowState& window = guild.window;
    const std::int32_t x = window.base.x + 0xf;
    const std::int32_t y = window.base.y + 0x62;
    const ActorState& hero = context.world.Actors()[runtime.hero];
    const auto index = static_cast<std::int16_t>(hero.word1f2);
    const auto name = GuildNameAt(guild, index);
    const auto alliance = UnionNameAt(guild, index);
    if (!name || !alliance)
        return Core::Unexpected(std::format("0x651428: el gremio {} del héroe cae fuera de la tabla", index));
    if (alliance->empty())
    {
        std::int32_t top = y;
        for (std::size_t line = 0; line < NoUnionSteps.size(); ++line)
        {
            top += NoUnionSteps[line];
            if (line == FirstColoredText)
            {
                context.hud.textColors.text = NoUnionColor;
                context.hud.textColors.background = NoUnionBackground;
            }
            auto text = TextOf(context, FirstNoUnionText + line, "0x651443");
            if (!text.HasValue())
                return Core::Unexpected(std::move(text).Error());
            WindowDrawing::DrawText(context, x + 0xa, top, text.Value());
        }
        // 0x651540: el texto en blanco otra vez (el fondo queda en 0xff0000cc).
        context.hud.textColors.text = NoUnionColor;
        return {};
    }
    // 0x651559..0x651580: el tamaño, el lugar (con el alto de la lista del aviso, +0x224: Q-C1293) y el dibujo.
    UI::OldList& list = window.unionList;
    list.base.width = UnionListWidth;
    list.base.height = ListHeight;
    list.base.x = x;
    list.base.y = y + 0xc + window.noticeList.base.height;
    if (auto drawn =
            DrawTheList(context, list, window.unions.size(), UnionHeaders, UnionHeaderLefts,
                        [&](std::int32_t row, std::size_t at) { return DrawTheUnionRow(context, guild, row, at); });
        !drawn.HasValue())
        return drawn;
    if (hero.guildStatus != MasterStatus)
        return {};
    UI::OldButton& leave = window.tabButtons[GuildUnionLeaveButton];
    UI::OldButton& kick = window.tabButtons[GuildUnionKickButton];
    if (*name == *alliance)
    {
        if (auto labeled = RelabelTheButton(context, leave, UnionMasterButtonText, "0x6515ed"); !labeled.HasValue())
            return labeled;
        kick.base.state = UI::ShownWindow;
        if (auto labeled = LabelTheButton(context, kick, UnionKickButtonText); !labeled.HasValue())
            return labeled;
        DrawTheButton(context, kick);
        DrawTheButton(context, leave);
        return {};
    }
    if (auto labeled = RelabelTheButton(context, leave, UnionButtonText, "0x651621"); !labeled.HasValue())
        return labeled;
    kick.base.state = UI::DisabledWindow;
    DrawTheButton(context, leave);
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheGuildTab(GameHudContext& context, std::int32_t tab)
{
    switch (tab)
    {
    case NoticeTab:
        return DrawTheNoticeTab(context);
    case MembersTab:
        return DrawTheMembersTab(context);
    case UnionTab:
        return DrawTheUnionTab(context);
    default:
        return {};
    }
}
} // namespace Client::Scenes
