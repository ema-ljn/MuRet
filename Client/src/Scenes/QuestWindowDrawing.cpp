#include "Scenes/QuestWindow.h"

#include "Core/Expected.h"
#include "Data/Tables/QuestTable.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameInventoryHud.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameWindowDrawing.h"
#include "Scenes/HudItems.h"
#include "Scenes/NpcNames.h"
#include "Scenes/QuestState.h"
#include "Scenes/Quests.h"
#include "Scenes/UiItemDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/ZenText.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/HudPrimitives.h"
#include "UI/PanelRegistry.h"
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

constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
// 0x41baea: el botón de terminar sin todos los ítems; 0x41bb5c: con el clic encima.
constexpr Render::Color MissingItemsGrey{0.3f, 0.3f, 0.3f, 1.0f};
constexpr Render::Color PressedGrey{0.4f, 0.4f, 0.4f, 1.0f};
// 0x41c83e..0x41c848: las pestañas 1 y 2.
constexpr Render::Color DisabledTabRed{1.0f, 0.3f, 0.2f, 1.0f};

// Los textos de Text.bmd (la dirección − 0x76bd1dc, sobre 300).
constexpr std::size_t ZenLabelText = 198;           // 0x76cb9e4
constexpr std::size_t FinishText = 699;             // 0x76f0500
constexpr std::size_t StateNotStartedText = 930;    // 0x77013b4
constexpr std::size_t StateInProgressText = 931;    // 0x77014e0
constexpr std::size_t StateDoneText = 932;          // 0x770160c
constexpr std::size_t ListCloseHelpText = 927;      // 0x7701030
constexpr std::size_t DialogCloseHelpText = 1002;   // 0x7706814
constexpr std::size_t FirstTabText = 1140;          // 0x77109cc (también la ayuda del botón del personaje)
constexpr std::size_t SecondTabTitleText = 1145;    // 0x7710fa8
constexpr std::size_t ThirdTabTitleText = 1146;     // 0x77110d4
constexpr std::size_t ThirdTabCountText = 868;      // 0x76fcb0c
constexpr std::size_t ThirdTabHeaderText = 869;     // 0x76fcc38
constexpr std::size_t ThirdTabFirstLineText = 877;  // 0x76fd598
constexpr std::size_t ThirdTabSecondLineText = 878; // 0x76fd6c4
// 0x5b1cba y 0x5b1cfc: los nombres de los dos ítems de misión con nivel 1.
constexpr std::size_t FirstQuestItemLevelOneText = 906;  // 0x76ff794
constexpr std::size_t SecondQuestItemLevelOneText = 907; // 0x76ff8c0
// 0x6c64e8: la letra de la ventana, y 0x6c64e0: el título de la pestaña 0.
constexpr std::string_view QuestKey = "T";
constexpr std::string_view QuestTitle = "Quest";

// 0x6c8e5c y 0x6c8e64.
constexpr std::uint32_t BodyText = 0xffd2e6ffu;
constexpr std::uint32_t AnswerText = 0xff67bfdfu;
constexpr std::uint32_t HoveredAnswerText = 0xff0000ffu;
constexpr std::uint32_t MissingItemText = 0xff1e1effu;
constexpr std::uint32_t QuestNameText = 0xffffdcc8u;
constexpr std::uint32_t NpcNameText = 0xfff0ff96u;
constexpr TextColors ZenColors{.text = 0xff96dcffu, .background = 0xff282828u};
constexpr TextColors TabTitleColors{.text = 0xffe6e6e6u, .background = 0xff141414u};
constexpr TextColors HelpColors{.text = 0xffffffffu, .background = 0xff000000u};
constexpr TextColors AfterHelpColors{.text = 0xffffffffu, .background = 0};

// Interface\ (0x61ae60): el fondo de la ventana, la línea, el botón de terminar, el fondo del zen, las pestañas y los
// dos botones de las misiones de la ventana del personaje.
constexpr std::uint16_t WindowTopSlot = 0x104;
constexpr std::uint16_t LineSlot = 0x117;
constexpr std::uint16_t FinishSlot = 0xf0;
constexpr std::uint16_t ZenSlot = 0x10f;
constexpr std::uint16_t TabSlot = 0x646;
constexpr std::uint16_t QuestButtonOpenSlot = 0x103;
constexpr std::uint16_t QuestButtonClosedSlot = 0x102;

// 0x41b680: el texto y las respuestas.
constexpr std::int32_t TextCenter = 0x64;
constexpr std::int32_t TextTop = 0x42;
constexpr std::int32_t TextTopWithoutTheQuest = 0xfa;
constexpr std::int32_t AnswerHalfWidth = 0x6a;
// 0x41b810: los ítems pedidos.
constexpr float ItemsTop = 235.0f;
constexpr float NamedItemsTop = 240.0f;
constexpr double ItemStep = 25.0;
constexpr std::int32_t ItemNameLeft = 0x1fe;
constexpr float ItemLeft = 480.0f;
constexpr float ItemSize = 20.0f;
constexpr std::int32_t ItemGroupSize = 0x200;
constexpr std::uint8_t ItemEntry = 1;
// 0x41ca7e..0x41cb44: el botón de las misiones de la ventana del personaje.
constexpr float QuestButtonX = 505.0f;
constexpr std::int32_t QuestButtonLeft = 0x1f9;
constexpr std::int32_t QuestButtonRight = 0x211;
constexpr std::int32_t QuestButtonTop = 0x18b;
constexpr std::int32_t QuestButtonSize = 0x18;
constexpr std::int32_t HelpAbove = 0xd;
// 0x41bedc y 0x41c322: la caja del nombre de la misión.
constexpr std::uint32_t QuestNameBox = 150;
constexpr std::int32_t TitleLeft = 0x23;
constexpr std::int32_t TitleTop = 0xc;
constexpr std::int32_t QuestNameOffset = 0x16;
// 0x41be06..0x41be25: el NPC de la misión 2 con el héroe de clase 3 o 4 y 0x6eeda8.
constexpr std::size_t SpecialNpcQuest = 2;
// 0x41c3a7..0x41c414: el estado, centrado en (x + 0x5f, y + 0x131).
constexpr std::int32_t StateCenter = 0x5f;
constexpr std::int32_t StateTop = 0x131;

float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

// 0x621b60(slot, x, y, w, h, u, v, du, dv, 1, 1, 0).
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

Core::Expected<std::string_view, std::string> TextOf(const GameHudContext& context, std::size_t index,
                                                     std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: sin el texto {} de Text.bmd", where, index));
    return *text;
}

Core::Expected<const Data::Tables::QuestRecord*, std::string> RecordOf(const QuestState& quest, std::size_t record,
                                                                       std::string_view where)
{
    if (record >= quest.records.size())
    {
        return Core::Unexpected(std::format("{}: la misión {} está fuera de los {} registros de +0xc (Q-C842)", where,
                                            record, quest.records.size()));
    }
    return &quest.records[record];
}

// 0x5ee220(x, y, 1): el fondo y la línea de 190 × 10 en y + 225 (0x6ba640).
void DrawTheWindowBackWithTheLine(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y)
{
    WindowDrawing::DrawTheWindowBack(sprites, x, y);
    Quad(sprites, LineSlot, static_cast<float>(x), F32(static_cast<double>(y) + 225.0), 190.0f, 10.0f, 0.0f, 0.0f,
         0.7421875f, 0.625f);
}

// 0x554340 con la fuente normal y los colores −1 y 0xff000000 que le ponen antes; la deja con la normal, en blanco y
// sin fondo.
void DrawTheHelp(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help)
{
    WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = HelpColors;
    UI::DrawHelp(context.sprites, context.text, x, y, help);
    WindowDrawing::SelectFont(context, FontKind::Normal);
    context.hud.textColors = AfterHelpColors;
}

// 0x5b1c70(tipo, nivel, buffer), la rama de los ítems de misión (0x5b1c85..0x5b1d38), la única que pide quest.bmd.
Core::Expected<std::string, std::string> QuestItemNameOf(const GameHudContext& context, std::int32_t type,
                                                         std::int32_t level)
{
    constexpr std::int32_t FirstQuestItem = 0x1c17;
    constexpr std::int32_t SecondQuestItem = 0x1c18;
    constexpr std::int32_t LastQuestItem = 0x1c1a;
    if (type < FirstQuestItem || type > LastQuestItem)
        return Core::Unexpected(std::format("0x5b1d39: el nombre del tipo {} (0x5b1c70) es del hito 11", type));
    if ((type == FirstQuestItem || type == SecondQuestItem) && level != 0)
    {
        const bool first = type == FirstQuestItem;
        if (level != 1)
        {
            return Core::Unexpected(std::format("{}: con el nivel {}, 0x5b1c70 no escribe el nombre del {} y 0x41b810 "
                                                "lo lee de la pila sin iniciar (Q-C874)",
                                                first ? "0x5b1cb1" : "0x5b1cf3", level, type));
        }
        auto text = TextOf(context, first ? FirstQuestItemLevelOneText : SecondQuestItemLevelOneText,
                           first ? "0x5b1cba" : "0x5b1cfc");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        return std::string(text.Value());
    }
    // "%s" del registro de Item.bmd (*[0x7758550] + tipo · 0x54).
    if (context.items == nullptr)
        return Core::Unexpected(std::string("0x5b1c76: el nombre del ítem de misión sin Item.bmd"));
    const auto record = context.items->Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("0x5b1c8d: Item.bmd no tiene el tipo {}", type));
    return ItemRecordName(*record);
}

// 0x41b810(estado, conNombres): los ítems pedidos a la clase en la misión en curso, desde y = 235 (o 240 con los
// nombres) cada 25. Con los nombres, en negrita y sin fondo, "%s x %d" en (0x1fe, y) en color por si faltan
// (0x558410); sin ellos, el ítem en la caja (480, y) de 20 × 20 (0x5e1480 con el nivel << 3). Falso si la ventana está
// cerrada, si el estado no es `state` o si falta alguno.
Core::Expected<bool, std::string> DrawTheAskedItems(GameHudContext& context, std::uint8_t state, bool withNames)
{
    WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    if (runtime.panels.delegated.quest == 0 || quest.currentState != state)
        return false;
    float y = ItemsTop;
    bool result = true;
    if (withNames)
    {
        y = NamedItemsTop;
        WindowDrawing::SelectFont(context, FontKind::Bold);
        context.hud.textColors.background = 0;
    }
    const auto read = RecordOf(quest, quest.current, "0x41b88e");
    if (!read.HasValue())
        return Core::Unexpected(read.Error());
    const Data::Tables::QuestRecord& record = *read.Value();
    // 0x41b88e y 0x41b9af: la cuenta con signo, leída en cada vuelta; más de 16 seguiría en las condiciones.
    if (record.entryCount > static_cast<std::int16_t>(Data::Tables::QuestEntryCount))
        return Core::Unexpected(std::format("0x41b8b0: {} pedidos pasan los 16 del registro", record.entryCount));
    for (std::int16_t e = 0; e < record.entryCount; ++e)
    {
        const auto index = static_cast<std::size_t>(e);
        const Data::Tables::QuestEntry& entry = record.entries[index];
        if (QuestClassFlagOf(record, index, quest.characterClass) == 0 || entry.kind != ItemEntry)
            continue;
        const std::int32_t type = entry.itemGroup * ItemGroupSize + entry.itemIndex;
        const std::int32_t count = entry.itemCount;
        const std::int32_t level = entry.itemLevel;
        if (withNames)
        {
            if (MissingQuestItems(context.world.Inventory(), type, count, level) == 0)
                context.hud.textColors.text = AnswerText;
            else
            {
                context.hud.textColors.text = MissingItemText;
                result = false;
            }
            auto name = QuestItemNameOf(context, type, level);
            if (!name.HasValue())
                return Core::Unexpected(std::move(name).Error());
            // 0x6c64c0, "%s x %d".
            WindowDrawing::DrawText(context, ItemNameLeft, Truncate(static_cast<double>(y)),
                                    std::format("{} x {}", name.Value(), count));
        }
        else
        {
            const UiItemBox box{.x = ItemLeft, .y = y, .width = ItemSize, .height = ItemSize};
            if (auto drawn = DrawUiItem(context, box, type, static_cast<std::uint32_t>(level) << 3u, 0, 0, false);
                !drawn.HasValue())
                return Core::Unexpected(drawn.Error());
        }
        y = F32(static_cast<double>(y) + ItemStep);
    }
    return result;
}

// 0x41b680(conRespuestas): los renglones del diálogo centrados en x + 0x64 desde ((7 − respuestas − renglones) · 0x12)
// / 2 + 0x42, con la fuente que haya; con las respuestas, en negrita, cada una que no está vacía en su renglón, en rojo
// la del mouse a no más de 0x6a de x + 0x6a (Q-C871).
void DrawTheQuestText(GameHudContext& context, bool withAnswers)
{
    WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    const UI::DialogBoxState& box = runtime.dialogBox;
    Render::SpriteBatch& sprites = context.sprites;
    context.hud.textColors = TextColors{.text = BodyText, .background = 0};
    sprites.SetBlend(BlendMode::Alpha);
    const std::int32_t left = quest.x + TextCenter;
    std::int32_t top =
        ((static_cast<std::int32_t>(UI::DialogRowCount) - box.answerCount - box.lineCount) * UI::DialogLineStep) / 2 +
        TextTop;
    for (std::int32_t row = 0; row < box.lineCount; ++row)
    {
        WindowDrawing::DrawCenteredText(context, left, top, UI::DialogRow(box, static_cast<std::size_t>(row)));
        top += UI::DialogLineStep;
    }
    if (withAnswers)
    {
        if (quest.currentState != QuestInProgress && runtime.panels.delegated.quest == QuestDialogWindow)
            top = TextTopWithoutTheQuest;
        // 0x41b72e..0x41b74c: la división con signo, que trunca hacia el 0.
        const std::int32_t hovered = (context.mouse.y - top) / UI::DialogLineStep;
        WindowDrawing::SelectFont(context, FontKind::Bold);
        const std::int64_t distance = static_cast<std::int64_t>(quest.x) + AnswerHalfWidth - context.mouse.x;
        const bool overTheAnswers = (distance < 0 ? -distance : distance) <= AnswerHalfWidth;
        for (std::int32_t answer = 0; answer < box.answerCount; ++answer)
        {
            context.hud.textColors.text = hovered == answer && overTheAnswers ? HoveredAnswerText : AnswerText;
            const std::string_view text = UI::DialogAnswer(box, static_cast<std::size_t>(answer));
            if (text.empty())
                continue;
            WindowDrawing::DrawCenteredText(context, left, top, text);
            top += UI::DialogLineStep;
        }
    }
    sprites.SetColor(White);
}

// 0x41ba20.
Core::Expected<void, std::string> DrawTheQuestDialog(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    HudState& hud = context.hud;
    Render::SpriteBatch& sprites = context.sprites;
    const std::int32_t x = quest.x;
    const std::int32_t y = quest.y;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    DrawTheWindowBackWithTheLine(sprites, x, y);
    hud.textColors = TextColors{.text = BodyText, .background = 0};
    if (quest.currentState == QuestInProgress)
    {
        const auto left = static_cast<float>(x);
        Quad(sprites, LineSlot, left, F32(static_cast<double>(y) + 325.0), 190.0f, 10.0f, 0.0f, 0.0f, 0.7421875f,
             0.625f);
        const auto all = DrawTheAskedItems(context, quest.currentState, true);
        if (!all.HasValue())
            return Core::Unexpected(all.Error());
        if (!all.Value())
            sprites.SetColor(MissingItemsGrey);
        else
            hud.textColors.text = BodyText;
        // 0x41bb03..0x41bb84: el botón de terminar, con el mouse contra los float en la x87.
        const float button = F32(static_cast<double>(left) + 35.0);
        const auto mouseX = static_cast<double>(context.mouse.x);
        const auto mouseY = static_cast<double>(context.mouse.y);
        GameMovementState& movement = runtime.movement;
        if (mouseX >= static_cast<double>(button) && mouseX < static_cast<double>(button) + 120.0 && mouseY >= 355.0 &&
            mouseY < 379.0 && movement.pendingClick)
        {
            sprites.SetColor(PressedGrey);
            if (movement.releasedClick && !context.projecting)
            {
                movement.pendingClick = false;
                movement.leftHeld = false;
            }
        }
        WindowDrawing::SelectFont(context, FontKind::Normal);
        Quad(sprites, FinishSlot, button, 355.0f, 120.0f, 24.0f, 0.0f, 0.0f, 0.83203125f, 1.0f);
        auto finish = TextOf(context, FinishText, "0x41bbd5");
        if (!finish.HasValue())
            return Core::Unexpected(std::move(finish).Error());
        WindowDrawing::DrawCenteredText(context, Truncate(static_cast<double>(button) + 60.0), 0x168, finish.Value());
        sprites.SetColor(White);
    }
    else if (quest.currentState == QuestAvailable)
    {
        // 0x41bc0b..0x41bcca: el zen que pide la misión, como el del inventario.
        const float zenX = F32(static_cast<double>(x) + 50.0);
        const float zenY = F32(static_cast<double>(y) + static_cast<double>(367.7f));
        Quad(sprites, ZenSlot, zenX, zenY, 113.0f, 18.0f, 0.0f, 0.0f, 0.8828125f, 0.5625f);
        hud.textColors = ZenColors;
        const std::int32_t textY = Truncate(static_cast<double>(zenY)) + 3;
        const std::int32_t textX = Truncate(static_cast<double>(zenX));
        auto label = TextOf(context, ZenLabelText, "0x41bc88");
        if (!label.HasValue())
            return Core::Unexpected(std::move(label).Error());
        WindowDrawing::DrawText(context, textX - 30, textY, label.Value());
        hud.textColors.text = ZenColor(quest.zen);
        WindowDrawing::DrawText(context, textX + 10, textY, FormatZen(quest.zen));
    }
    // 0x41bcd8..0x41bdbe: el botón de cerrar sin glColor antes; el color ya es el blanco.
    if (WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto help = TextOf(context, DialogCloseHelpText, "0x41bd9f");
        if (!help.HasValue())
            return Core::Unexpected(std::move(help).Error());
        WindowDrawing::DrawTheCloseHelp(context, x, y, help.Value());
    }
    WindowDrawing::SelectFont(context, FontKind::Normal);
    hud.textColors = TextColors{.text = NpcNameText, .background = 0};
    // 0x41bde0..0x41be04: la clase entera del héroe (+0x1d4), sin el & 7.
    const std::uint8_t heroClass = context.world.Actors()[runtime.hero].characterClass;
    const bool special = (heroClass == 4 || heroClass == 3) && runtime.byte6eeda8 != 0;
    const auto npcRecord = RecordOf(quest, special ? SpecialNpcQuest : quest.current, "0x41be7c");
    if (!npcRecord.HasValue())
        return Core::Unexpected(npcRecord.Error());
    // 0x41be9e: 0x554180 con el NULL de 0x54ebb0 no dibuja y mide lstrlen(NULL), 0 caracteres.
    const std::optional<std::string> npc = NpcNameOf(runtime.npcNames, npcRecord.Value()->npcType);
    if (npc)
        WindowDrawing::DrawTitle(context, x + TitleLeft, y + TitleTop, *npc);
    else
        WindowDrawing::DrawText(context, x + TitleLeft, y + TitleTop, {});
    hud.textColors.text = QuestNameText;
    if (!special)
    {
        const auto current = RecordOf(quest, quest.current, "0x41bf04");
        if (!current.HasValue())
            return Core::Unexpected(current.Error());
        WindowDrawing::DrawInBox(context, x + QuestNameOffset, y + QuestNameOffset,
                                 Data::Tables::NameOf(*current.Value()), QuestNameBox);
    }
    DrawTheQuestText(context, true);
    sprites.SetColor(White);
    return {};
}

// 0x41c2a0, la pestaña 0.
Core::Expected<void, std::string> DrawTheFirstTab(GameHudContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    HudState& hud = context.hud;
    hud.textColors = TabTitleColors;
    WindowDrawing::SelectFont(context, FontKind::Bold);
    WindowDrawing::DrawTitle(context, quest.x + TitleLeft, quest.y + TitleTop, QuestTitle);
    hud.textColors.text = QuestNameText;
    const auto reached = RecordOf(quest, quest.reached, "0x41c349");
    if (!reached.HasValue())
        return Core::Unexpected(reached.Error());
    WindowDrawing::DrawInBox(context, quest.x + QuestNameOffset, quest.y + QuestNameOffset,
                             Data::Tables::NameOf(*reached.Value()), QuestNameBox);
    DrawTheQuestText(context, false);
    WindowDrawing::SelectFont(context, FontKind::BigBold);
    hud.textColors = TextColors{.text = AnswerText, .background = 0};
    std::size_t state = 0;
    switch (quest.currentState)
    {
    case QuestNotStarted:
    case QuestAvailable:
    case QuestConditionFailed:
        state = StateNotStartedText;
        break;
    case QuestInProgress:
        state = StateInProgressText;
        break;
    case QuestDone:
        state = StateDoneText;
        break;
    default:
        return {};
    }
    auto text = TextOf(context, state, "0x41c3b8");
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    WindowDrawing::DrawCenteredText(context, quest.x + StateCenter, quest.y + StateTop, text.Value());
    return {};
}

// 0x41c420 y el principio de 0x41c4a0: el título de la pestaña en negrita.
Core::Expected<void, std::string> DrawTheTabTitle(GameHudContext& context, std::size_t title, std::string_view where)
{
    const QuestState& quest = context.world.Runtime().quest;
    context.hud.textColors = TabTitleColors;
    WindowDrawing::SelectFont(context, FontKind::Bold);
    auto text = TextOf(context, title, where);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    WindowDrawing::DrawTitle(context, quest.x + TitleLeft, quest.y + TitleTop, text.Value());
    return {};
}

// 0x41c4a0, la pestaña 2: el encabezado (el 869 con 3 y los textos 1146 y 1140), dos renglones (877 y 878) y la cuenta
// de +6 + pestaña (el 868), del 9F.
Core::Expected<void, std::string> DrawTheThirdTab(GameHudContext& context)
{
    const QuestState& quest = context.world.Runtime().quest;
    HudState& hud = context.hud;
    if (auto title = DrawTheTabTitle(context, ThirdTabTitleText, "0x41c502"); !title.HasValue())
        return title;
    hud.textColors = TextColors{.text = AnswerText, .background = 0};
    const std::array<std::size_t, 5> indices{ThirdTabHeaderText, ThirdTabTitleText, FirstTabText, ThirdTabFirstLineText,
                                             ThirdTabSecondLineText};
    std::array<std::string_view, 5> texts{};
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        auto text = TextOf(context, indices[i], "0x41c528");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        texts[i] = text.Value();
    }
    const std::array<std::string_view, 3> header{"3", texts[1], texts[2]};
    WindowDrawing::DrawCenteredText(context, quest.x + StateCenter, quest.y + 0x50,
                                    UI::PlaceArguments(texts[0], header));
    hud.textColors.text = BodyText;
    WindowDrawing::DrawCenteredText(context, quest.x + 0x55, quest.y + 0x64, texts[3]);
    WindowDrawing::DrawCenteredText(context, quest.x + 0x69, quest.y + 0x78, texts[4]);
    WindowDrawing::SelectFont(context, FontKind::BigBold);
    auto format = TextOf(context, ThirdTabCountText, "0x41c5c7");
    if (!format.HasValue())
        return Core::Unexpected(std::move(format).Error());
    // 0x41c5c2: el byte +6 + pestaña, con la pestaña en 2.
    const std::string count = std::to_string(quest.flags[quest.tab]);
    const std::array<std::string_view, 1> arguments{count};
    WindowDrawing::DrawCenteredText(context, quest.x + StateCenter, quest.y + StateTop,
                                    UI::PlaceArguments(format.Value(), arguments));
    return {};
}

// 0x41c600.
Core::Expected<void, std::string> DrawTheQuestList(GameHudContext& context)
{
    const QuestState& quest = context.world.Runtime().quest;
    HudState& hud = context.hud;
    Render::SpriteBatch& sprites = context.sprites;
    const std::int32_t x = quest.x;
    const std::int32_t y = quest.y;
    sprites.SetColor(White);
    sprites.SetBlend(BlendMode::Alpha);
    DrawTheWindowBackWithTheLine(sprites, x, y);
    hud.textColors = TextColors{.text = BodyText, .background = 0};
    WindowDrawing::SelectFont(context, FontKind::Normal);
    // 0x41c670..0x41c7af: la línea de y + 250 (0x6b8920) y el marco de la lista, en y absolutas.
    Quad(sprites, LineSlot, static_cast<float>(x), F32(static_cast<double>(y) + 250.0), 190.0f, 10.0f, 0.0f, 0.0f,
         0.7421875f, 0.625f);
    const float frameLeft = F32(static_cast<double>(x) + 10.0);
    Quad(sprites, LineSlot, frameLeft, 260.0f, 170.0f, 5.0f, 0.0390625f, 0.3125f, 0.6640625f, 0.3125f);
    Quad(sprites, LineSlot, frameLeft, 380.0f, 170.0f, 6.0f, 0.0390625f, 0.0f, 0.6640625f, 0.375f);
    Quad(sprites, WindowTopSlot, frameLeft, 260.0f, 1.0f, 125.0f, 0.00390625f, 0.0f, 0.00390625f, 0.48828125f);
    Quad(sprites, WindowTopSlot, F32(static_cast<double>(frameLeft) + 170.0), 260.0f, 1.0f, 125.0f, 0.00390625f, 0.0f,
         0.00390625f, 0.48828125f);
    // 0x41c7b4..0x41c8d1: las tres pestañas, de 49 cada 51 desde x + 20.
    const float tabsLeft = F32(static_cast<double>(x) + 20.0);
    WindowDrawing::SelectFont(context, FontKind::Normal);
    hud.textColors.background = 0;
    for (std::int32_t tab = 0; tab < 3; ++tab)
    {
        hud.textColors.text = 0xffffffffu;
        float height = 14.0f;
        float top = 237.0f;
        if (tab == static_cast<std::int32_t>(quest.tab))
        {
            height = 16.0f;
            top = 235.0f;
            hud.textColors.text = AnswerText;
        }
        sprites.SetColor(tab == 0 ? White : DisabledTabRed);
        Quad(sprites, TabSlot, F32(static_cast<double>(tab) * 51.0 + static_cast<double>(tabsLeft)), top, 49.0f, height,
             0.0f, 0.0f, 0.765625f, 1.0f);
        auto text = TextOf(context, FirstTabText + static_cast<std::size_t>(tab), "0x41c897");
        if (!text.HasValue())
            return Core::Unexpected(std::move(text).Error());
        WindowDrawing::DrawCenteredText(
            context, Truncate(static_cast<double>(tab * 0x33) + static_cast<double>(tabsLeft) + 24.0),
            Truncate(static_cast<double>(top) + 5.0), text.Value());
    }
    // 0x41c8dd..0x41c9e3: el blanco, el botón de cerrar y la ayuda, el texto 927 con "T".
    if (WindowDrawing::DrawTheCloseButton(context, x, y))
    {
        auto format = TextOf(context, ListCloseHelpText, "0x41c9ab");
        if (!format.HasValue())
            return Core::Unexpected(std::move(format).Error());
        const std::array<std::string_view, 1> key{QuestKey};
        WindowDrawing::DrawTheCloseHelp(context, x, y, UI::PlaceArguments(format.Value(), key));
    }
    Core::Expected<void, std::string> tab{};
    switch (quest.tab)
    {
    case 0:
        tab = DrawTheFirstTab(context);
        break;
    case 1:
        tab = DrawTheTabTitle(context, SecondTabTitleText, "0x41c479");
        hud.textColors.text = QuestNameText;
        break;
    case 2:
        tab = DrawTheThirdTab(context);
        break;
    default:
        break;
    }
    if (!tab.HasValue())
        return tab;
    sprites.SetColor(White);
    return {};
}
} // namespace

Core::Expected<void, std::string> DrawTheQuestWindow(GameHudContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    const std::uint8_t window = runtime.panels.delegated.quest;
    if (window == QuestDialogWindow)
    {
        if (auto dialog = DrawTheQuestDialog(context); !dialog.HasValue())
            return dialog;
    }
    else if (window == QuestListWindow)
    {
        if (auto list = DrawTheQuestList(context); !list.HasValue())
            return list;
    }
    if (!panels.IsOpen(UI::CharacterPanel))
        return {};
    // 0x41ca7e..0x41cacd: con el color y la mezcla que haya.
    const std::int32_t top = quest.y + QuestButtonTop;
    Quad(context.sprites, window != 0 ? QuestButtonOpenSlot : QuestButtonClosedSlot, QuestButtonX,
         static_cast<float>(top), 24.0f, 24.0f, 0.0f, 0.0f, 0.75f, 0.75f);
    const UI::Point mouse = context.mouse;
    if (mouse.x < QuestButtonLeft || mouse.x >= QuestButtonRight || mouse.y < top || mouse.y >= top + QuestButtonSize)
        return {};
    auto name = TextOf(context, FirstTabText, "0x41cb10");
    if (!name.HasValue())
        return Core::Unexpected(std::move(name).Error());
    // 0x6c64ec, "%s(%s)".
    DrawTheHelp(context, QuestButtonLeft, top - HelpAbove, std::format("{}({})", name.Value(), QuestKey));
    return {};
}

Core::Expected<void, std::string> DrawTheQuestItems(GameHudContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    const std::uint8_t window = runtime.panels.delegated.quest;
    Core::Expected<bool, std::string> drawn = false;
    if (window == QuestDialogWindow)
        drawn = DrawTheAskedItems(context, QuestInProgress, false);
    else if (quest.byte21380 != 0 && window == QuestListWindow)
    {
        // 0x41ba02..0x41ba16: +0x21381 en 0 es el estado 1; en 1, el 2.
        if (quest.byte21381 == 0)
            drawn = DrawTheAskedItems(context, QuestInProgress, false);
        else if (quest.byte21381 == 1)
            drawn = DrawTheAskedItems(context, QuestDone, false);
    }
    if (!drawn.HasValue())
        return Core::Unexpected(drawn.Error());
    return {};
}
} // namespace Client::Scenes
