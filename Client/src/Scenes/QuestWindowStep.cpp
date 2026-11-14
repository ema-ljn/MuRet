#include "Scenes/QuestWindow.h"

#include "Core/Expected.h"
#include "Data/Text/DialogTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/QuestState.h"
#include "Scenes/Quests.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x41b5bb..0x41b64c: la esquina de la ventana con el personaje abierto y sin él.
constexpr std::int32_t BesideTheCharacterX = 0x104;
constexpr std::int32_t AloneX = 0x1c2;
// 0x41b5cf..0x41b5f3: el botón de las misiones de la ventana del personaje.
constexpr std::int32_t QuestButtonLeft = 0x1f9;
constexpr std::int32_t QuestButtonRight = 0x211;
constexpr std::int32_t QuestButtonTop = 0x18b;
constexpr std::int32_t QuestButtonBottom = 0x1a3;
// 0x41aa0e..0x41aa38 y 0x41b1a1..0x41b1c5: el mouse sobre la ventana, de 0xbe × 0x1b1 desde la esquina.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x41b0e7..0x41b115 y 0x41b52d..0x41b55b: el botón de cerrar, de 0x18 × 0x18 en (0x19, 0x18b) de la esquina.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x41aa57..0x41aa8e: el botón de terminar, de 0x78 desde x + 0x23, en y absolutas (Q-C872).
constexpr std::int32_t FinishLeft = 0x23;
constexpr std::int32_t FinishWidth = 0x78;
constexpr std::int32_t FinishTop = 0x163;
constexpr std::int32_t FinishBottom = 0x17b;
// 0x419bf7..0x419c3a: las respuestas.
constexpr std::int32_t AnswersTop = 0x42;
constexpr std::int32_t AnswersTopWithoutTheQuest = 0xfa;
// 0x419c62..0x419c71: a no más de 0x6a de x = 0x236 (Q-C871).
constexpr std::int32_t AnswersCenterX = 0x236;
constexpr std::int32_t AnswersHalfWidth = 0x6a;
// 0x41b1d5..0x41b22e: las tres pestañas, de 0x31 cada 0x33 desde x + 0x14; la elegida de 0x10 desde y = 0xeb y las
// otras de 0xe desde 0xed (y absolutas, Q-C872).
constexpr std::int32_t TabCount = 3;
constexpr std::int32_t TabLeft = 0x14;
constexpr std::int32_t TabStep = 0x33;
constexpr std::int32_t TabWidth = 0x31;
constexpr std::int32_t ChosenTabTop = 0xeb;
constexpr std::int32_t ChosenTabHeight = 0x10;
constexpr std::int32_t TabTop = 0xed;
constexpr std::int32_t TabHeight = 0xe;
// 0x6c8dcc con el clic de un botón.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x41ed10(0x1c, 0, 0), el sonido de los botones (CL10).
constexpr std::int32_t ButtonSound = 0x1c;

// El clic gastado por un botón de la ventana: 0x7d366d4 en 0, 0x77f3b10 en 0 y 0x6c8dcc en 6.
void SpendTheClick(GameMovementState& movement)
{
    movement.pendingClick = false;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
}

bool MouseIn(const GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

// Los C3 A2 de 0x419bd0 (0x419d13.. y 0x41a35b..) y de 0x41a9e0 (0x41aaa1..): C1 05 A2 con +0x2137e y 1, el XOR32 y la
// entrega en C3, con el rand() del relleno (0x419fc1, 0x41a60d, 0x41ad4f) y el serial (0x5767913) por la tabla de
// protección, que al cerrarse saca otro.
Core::Expected<void, std::string> SendTheQuestState(const GameUiContext& context, std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: el C3 A2 sin el cable", where));
    static_cast<void>(context.random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Render::Models::LogicalAccess serial(serialDepth, context.random, Render::Models::SerialCloseRandoms);
    }
    context.requests->SendSetQuest(context.world.Runtime().quest.current, QuestInProgress);
    return {};
}

// 0x419900: la ventana en 0 y el C1 03 31 en claro, sin sonido ni rand().
Core::Expected<void, std::string> CloseTheQuestWindow(const GameUiContext& context, std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: 0x419900 manda el C1 03 31 sin el cable", where));
    context.world.Runtime().panels.delegated.quest = 0;
    context.requests->SendWindowClose();
    return {};
}

Core::Expected<const Data::Text::DialogRecord*, std::string>
DialogRecordOf(const WorldRuntime& runtime, std::int32_t index, std::int32_t answer, std::string_view where)
{
    const Data::Text::DialogTable& table = runtime.dialogTable;
    if (index < 0 || static_cast<std::size_t>(index) >= table.size() ||
        static_cast<std::size_t>(answer) >= Data::Text::DialogAnswerCount)
    {
        return Core::Unexpected(std::format("{}: la respuesta {} del diálogo {} está fuera de Dialog.bmd (0x7688ac0)",
                                            where, answer, index));
    }
    return &table[static_cast<std::size_t>(index)];
}

// 0x419bd0.
Core::Expected<void, std::string> StepTheQuestAnswers(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    UI::DialogBoxState& box = runtime.dialogBox;
    GameMovementState& movement = runtime.movement;
    // 0x419bf7..0x419c22: ((7 − (respuestas + renglones)) · 0x12) / 2 + renglones · 0x12 + 0x42, la división con signo.
    const std::int32_t answers = box.answerCount;
    std::int32_t top =
        ((static_cast<std::int32_t>(UI::DialogRowCount) - (answers + box.lineCount)) * UI::DialogLineStep) / 2 +
        box.lineCount * UI::DialogLineStep + AnswersTop;
    if (quest.currentState != QuestInProgress && runtime.panels.delegated.quest == QuestDialogWindow)
        top = AnswersTopWithoutTheQuest;
    // 0x419c3f..0x419c7e: sale sin nada.
    if (context.mouseY < 0)
        return {};
    const std::int32_t offset = context.mouseY - top;
    if (offset >= answers * UI::DialogLineStep)
        return {};
    const std::int64_t distance = static_cast<std::int64_t>(AnswersCenterX) - context.mouseX;
    if ((distance < 0 ? -distance : distance) > AnswersHalfWidth)
        return {};
    if (!movement.pendingClick)
        return {};
    // 0x419c84..0x419cb7: gasta el clic antes de mirar la fila, que sale solo si es negativa (Q-C870).
    movement.pendingClick = false;
    movement.leftHeld = false;
    movement.mouseUpdateTime = 0;
    movement.mouseUpdateTimeMax = ButtonMouseUpdateTimeMax;
    const std::int32_t row = offset / UI::DialogLineStep;
    if (row < 0)
        return {};
    // 0x419cbd..0x419cc7: la acción de la respuesta, [0x6d5e28] · 0x100 + fila dwords desde 0x7688c18.
    const auto record = DialogRecordOf(runtime, box.dialogIndex, row, "0x419cc7");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const QuestContext questContext{.world = context.world, .random = context.random, .texts = context.texts};
    bool failed = false;
    switch (record.Value()->actions[static_cast<std::size_t>(row)])
    {
    case static_cast<std::int32_t>(Data::Text::DialogAction::CheckAndAccept):
    {
        // 0x419cd7..0x419d0e: 0x418fb0(registro, 1); si no se cumplen, 0x419710 con el diálogo que dejaron.
        const auto checked = CheckTheQuestConditions(questContext, quest.current, true);
        if (!checked.HasValue())
            return Core::Unexpected(checked.Error());
        if (!checked.Value())
        {
            failed = true;
            if (auto shown =
                    UI::ShowTheQuestDialog(box, runtime.dialogs, &runtime.dialogTable, context.texts, quest.dialog);
                !shown.HasValue())
                return shown;
            break;
        }
        if (auto sent = SendTheQuestState(context, "0x419d13"); !sent.HasValue())
            return sent;
        break;
    }
    case static_cast<std::int32_t>(Data::Text::DialogAction::Close):
        // 0x41a320..0x41a335: el clic gastado otra vez y 0x419900.
        SpendTheClick(movement);
        if (auto closed = CloseTheQuestWindow(context, "0x41a335"); !closed.HasValue())
            return closed;
        break;
    case static_cast<std::int32_t>(Data::Text::DialogAction::Accept):
        SpendTheClick(movement);
        if (auto sent = SendTheQuestState(context, "0x41a35b"); !sent.HasValue())
            return sent;
        break;
    default:
        break;
    }
    // 0x41a996..0x41a99a: con cualquier acción.
    runtime.lastSoundId = ButtonSound;
    // 0x41a99f..0x41a9c5: el diálogo al que lleva, con [0x6d5e28] leído otra vez.
    const auto now = DialogRecordOf(runtime, box.dialogIndex, row, "0x41a9b0");
    if (!now.HasValue())
        return Core::Unexpected(now.Error());
    const std::int32_t next = now.Value()->next[static_cast<std::size_t>(row)];
    if (next <= 0 || failed)
        return {};
    return UI::ShowTheQuestDialog(box, runtime.dialogs, &runtime.dialogTable, context.texts, next);
}

// 0x41a9e0.
Core::Expected<void, std::string> StepTheQuestDialog(const GameUiContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    GameMovementState& movement = runtime.movement;
    if (MouseIn(context, quest.x, quest.y, quest.x + WindowWidth, quest.y + WindowHeight))
        runtime.ui.mouseOnUi = 1;
    if (auto answered = StepTheQuestAnswers(context); !answered.HasValue())
        return answered;
    // 0x41aa4a: 0x419b10.
    const auto complete = CanCompleteTheQuest(context.world);
    if (!complete.HasValue())
        return Core::Unexpected(complete.Error());
    if (complete.Value() &&
        MouseIn(context, quest.x + FinishLeft, FinishTop, quest.x + FinishLeft + FinishWidth, FinishBottom) &&
        movement.pendingClick)
    {
        // 0x41aaa1..0x41b0cc: el A2, el clic y el botón abajo en 0 (0x77f3b10 y 0x6c8dcc quedan) y el sonido.
        if (auto sent = SendTheQuestState(context, "0x41aaa1"); !sent.HasValue())
            return sent;
        movement.pendingClick = false;
        movement.leftHeld = false;
        runtime.lastSoundId = ButtonSound;
    }
    if (MouseIn(context, quest.x + CloseLeft, quest.y + CloseTop, quest.x + CloseLeft + CloseSize,
                quest.y + CloseTop + CloseSize) &&
        movement.pendingClick)
    {
        SpendTheClick(movement);
        if (auto closed = CloseTheQuestWindow(context, "0x41b13d"); !closed.HasValue())
            return closed;
    }
    // 0x41b142..0x41b14c.
    movement.releasedClick = false;
    return {};
}

// 0x41b170.
Core::Expected<void, std::string> StepTheQuestList(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    GameMovementState& movement = runtime.movement;
    if (MouseIn(context, quest.x, quest.y, quest.x + WindowWidth, quest.y + WindowHeight))
        runtime.ui.mouseOnUi = 1;
    for (std::int32_t tab = 0; tab < TabCount; ++tab)
    {
        const bool chosen = tab == static_cast<std::int32_t>(quest.tab);
        const std::int32_t left = quest.x + TabLeft + tab * TabStep;
        const std::int32_t top = chosen ? ChosenTabTop : TabTop;
        const std::int32_t height = chosen ? ChosenTabHeight : TabHeight;
        if (!MouseIn(context, left, top, left + TabWidth, top + height) || !movement.pendingClick)
            continue;
        // 0x41b23e..0x41b24d: la 1 y la 2 no gastan el clic. 0x41b253..0x41b281: la 0 lo gasta y queda elegida; el
        // C1 9F de 0x41b287 pide una pestaña distinta de la elegida y de la 0, que no hay (Q-C845).
        if (tab == 0)
        {
            SpendTheClick(movement);
            quest.tab = 0;
        }
        break;
    }
    if (!MouseIn(context, quest.x + CloseLeft, quest.y + CloseTop, quest.x + CloseLeft + CloseSize,
                 quest.y + CloseTop + CloseSize) ||
        !movement.pendingClick)
        return {};
    SpendTheClick(movement);
    // 0x41b58b: 0x65cae0(8, 0), que pone la ventana en 0, manda el C1 03 31 y suena.
    auto closed = panels.Close(UI::QuestPanel);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheQuestWindow(const GameUiContext& context, UI::PanelRegistry& panels)
{
    WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    if (panels.IsOpen(UI::CharacterPanel))
    {
        quest.x = BesideTheCharacterX;
        quest.y = 0;
        if (MouseIn(context, QuestButtonLeft, QuestButtonTop, QuestButtonRight, QuestButtonBottom) &&
            runtime.movement.pendingClick)
        {
            SpendTheClick(runtime.movement);
            // 0x41b621..0x41b63b: Close(8) o Open(8), los dos con 0 de segundo argumento.
            auto toggled = panels.IsOpen(UI::QuestPanel) ? panels.Close(UI::QuestPanel) : panels.Open(UI::QuestPanel);
            if (!toggled.HasValue())
                return Core::Unexpected(std::move(toggled).Error());
        }
    }
    else
    {
        quest.x = AloneX;
        quest.y = 0;
    }
    // 0x41b656..0x41b671: la ventana, leída después de abrirla o cerrarla.
    switch (runtime.panels.delegated.quest)
    {
    case QuestDialogWindow:
        return StepTheQuestDialog(context);
    case QuestListWindow:
        return StepTheQuestList(context, panels);
    default:
        return {};
    }
}
} // namespace Client::Scenes
