#include "Quests/QuestWindow.h"

#include "Core/Expected.h"
#include "Tables/DialogTable.h"
#include "Models/LogicalAccess.h"
#include "Ui/GameUiContext.h"
#include "Quests/QuestState.h"
#include "Quests/Quests.h"
#include "Kernel/MouseState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Crt/CrtRandom.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Quests
{
namespace
{
// 0x41b5bb..0x41b64c: the window's corner with the character open and without it.
constexpr std::int32_t BesideTheCharacterX = 0x104;
constexpr std::int32_t AloneX = 0x1c2;
// 0x41b5cf..0x41b5f3: the quest button of the character window.
constexpr std::int32_t QuestButtonLeft = 0x1f9;
constexpr std::int32_t QuestButtonRight = 0x211;
constexpr std::int32_t QuestButtonTop = 0x18b;
constexpr std::int32_t QuestButtonBottom = 0x1a3;
// 0x41aa0e..0x41aa38 and 0x41b1a1..0x41b1c5: the mouse over the window, of 0xbe × 0x1b1 from the corner.
constexpr std::int32_t WindowWidth = 0xbe;
constexpr std::int32_t WindowHeight = 0x1b1;
// 0x41b0e7..0x41b115 and 0x41b52d..0x41b55b: the close button, of 0x18 × 0x18 at (0x19, 0x18b) from the corner.
constexpr std::int32_t CloseLeft = 0x19;
constexpr std::int32_t CloseTop = 0x18b;
constexpr std::int32_t CloseSize = 0x18;
// 0x41aa57..0x41aa8e: the finish button, 0x78 wide from x + 0x23, in absolute y (Q-C872).
constexpr std::int32_t FinishLeft = 0x23;
constexpr std::int32_t FinishWidth = 0x78;
constexpr std::int32_t FinishTop = 0x163;
constexpr std::int32_t FinishBottom = 0x17b;
// 0x419bf7..0x419c3a: the answers.
constexpr std::int32_t AnswersTop = 0x42;
constexpr std::int32_t AnswersTopWithoutTheQuest = 0xfa;
// 0x419c62..0x419c71: no more than 0x6a from x = 0x236 (Q-C871).
constexpr std::int32_t AnswersCenterX = 0x236;
constexpr std::int32_t AnswersHalfWidth = 0x6a;
// 0x41b1d5..0x41b22e: the three tabs, 0x31 wide every 0x33 from x + 0x14; the chosen one 0x10 high from y = 0xeb and
// the others 0xe from 0xed (absolute y, Q-C872).
constexpr std::int32_t TabCount = 3;
constexpr std::int32_t TabLeft = 0x14;
constexpr std::int32_t TabStep = 0x33;
constexpr std::int32_t TabWidth = 0x31;
constexpr std::int32_t ChosenTabTop = 0xeb;
constexpr std::int32_t ChosenTabHeight = 0x10;
constexpr std::int32_t TabTop = 0xed;
constexpr std::int32_t TabHeight = 0xe;
// 0x6c8dcc with a button's click.
constexpr std::int32_t ButtonMouseUpdateTimeMax = 6;
// 0x41ed10(0x1c, 0, 0), the buttons' sound (CL10).
constexpr Audio::SoundId ButtonSound = Sound::CreateWindowSound;

// The click spent by a window button: 0x7d366d4 at 0, 0x77f3b10 at 0 and 0x6c8dcc at 6.
void SpendTheClick(Kernel::MouseState& mouse)
{
    mouse.pendingClick = false;
    mouse.updateTime = 0;
    mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
}

bool MouseIn(const Ui::GameUiContext& context, std::int32_t left, std::int32_t top, std::int32_t right,
             std::int32_t bottom)
{
    return context.mouseX >= left && context.mouseX < right && context.mouseY >= top && context.mouseY < bottom;
}

// The C3 A2 of 0x419bd0 (0x419d13.. and 0x41a35b..) and of 0x41a9e0 (0x41aaa1..): C1 05 A2 with +0x2137e and 1, the
// XOR32 and the delivery in C3, with the padding rand() (0x419fc1, 0x41a60d, 0x41ad4f) and the serial (0x5767913)
// through the protection table, which draws another on closing.
Core::Expected<void, std::string> SendTheQuestState(const Ui::GameUiContext& context, std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: el C3 A2 sin el cable", where));
    static_cast<void>(context.random.Next());
    std::uint8_t serialDepth = 0;
    {
        const Models::LogicalAccess serial(serialDepth, context.random, Models::SerialCloseRandoms);
    }
    context.requests->SendSetQuest(context.world.Runtime().quest.current, QuestInProgress);
    return {};
}

// 0x419900: the window at 0 and the C1 03 31 in the clear, without sound or rand().
Core::Expected<void, std::string> CloseTheQuestWindow(const Ui::GameUiContext& context, std::string_view where)
{
    if (context.requests == nullptr)
        return Core::Unexpected(std::format("{}: 0x419900 manda el C1 03 31 sin el cable", where));
    context.world.Runtime().panels.delegated.quest = 0;
    context.requests->SendWindowClose();
    return {};
}

Core::Expected<const Tables::DialogRecord*, std::string>
DialogRecordOf(const Kernel::WorldRuntime& runtime, std::int32_t index, std::int32_t answer, std::string_view where)
{
    const Tables::DialogTable& table = runtime.dialogTable;
    if (index < 0 || static_cast<std::size_t>(index) >= table.size() ||
        static_cast<std::size_t>(answer) >= Tables::DialogAnswerCount)
    {
        return Core::Unexpected(std::format("{}: la respuesta {} del diálogo {} está fuera de Dialog.bmd (0x7688ac0)",
                                            where, answer, index));
    }
    return &table[static_cast<std::size_t>(index)];
}

// 0x419bd0.
Core::Expected<void, std::string> StepTheQuestAnswers(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    Ui::DialogBoxState& box = runtime.dialogBox;
    Kernel::MouseState& mouse = runtime.mouse;
    // 0x419bf7..0x419c22: ((7 − (answers + lines)) · 0x12) / 2 + lines · 0x12 + 0x42, the signed division.
    const std::int32_t answers = box.answerCount;
    std::int32_t top =
        ((static_cast<std::int32_t>(Ui::DialogRowCount) - (answers + box.lineCount)) * Ui::DialogLineStep) / 2 +
        box.lineCount * Ui::DialogLineStep + AnswersTop;
    if (quest.currentState != QuestInProgress && runtime.panels.delegated.quest == QuestDialogWindow)
        top = AnswersTopWithoutTheQuest;
    // 0x419c3f..0x419c7e: leaves with nothing.
    if (context.mouseY < 0)
        return {};
    const std::int32_t offset = context.mouseY - top;
    if (offset >= answers * Ui::DialogLineStep)
        return {};
    const std::int64_t distance = static_cast<std::int64_t>(AnswersCenterX) - context.mouseX;
    if ((distance < 0 ? -distance : distance) > AnswersHalfWidth)
        return {};
    if (!mouse.pendingClick)
        return {};
    // 0x419c84..0x419cb7: spends the click before looking at the row, which leaves only if it is negative (Q-C870).
    mouse.pendingClick = false;
    mouse.leftHeld = false;
    mouse.updateTime = 0;
    mouse.updateTimeMax = ButtonMouseUpdateTimeMax;
    const std::int32_t row = offset / Ui::DialogLineStep;
    if (row < 0)
        return {};
    // 0x419cbd..0x419cc7: the answer's action, [0x6d5e28] · 0x100 + row dwords from 0x7688c18.
    const auto record = DialogRecordOf(runtime, box.dialogIndex, row, "0x419cc7");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const QuestContext questContext{.world = context.world, .random = context.random, .texts = context.texts};
    bool failed = false;
    switch (record.Value()->actions[static_cast<std::size_t>(row)])
    {
    case static_cast<std::int32_t>(Tables::DialogAction::CheckAndAccept):
    {
        // 0x419cd7..0x419d0e: 0x418fb0(record, 1); if they are not met, 0x419710 with the dialog they left.
        const auto checked = CheckTheQuestConditions(questContext, quest.current, true);
        if (!checked.HasValue())
            return Core::Unexpected(checked.Error());
        if (!checked.Value())
        {
            failed = true;
            if (auto shown =
                    Ui::ShowTheQuestDialog(box, runtime.dialogs, &runtime.dialogTable, context.texts, quest.dialog);
                !shown.HasValue())
                return shown;
            break;
        }
        if (auto sent = SendTheQuestState(context, "0x419d13"); !sent.HasValue())
            return sent;
        break;
    }
    case static_cast<std::int32_t>(Tables::DialogAction::Close):
        // 0x41a320..0x41a335: the click spent again and 0x419900.
        SpendTheClick(mouse);
        if (auto closed = CloseTheQuestWindow(context, "0x41a335"); !closed.HasValue())
            return closed;
        break;
    case static_cast<std::int32_t>(Tables::DialogAction::Accept):
        SpendTheClick(mouse);
        if (auto sent = SendTheQuestState(context, "0x41a35b"); !sent.HasValue())
            return sent;
        break;
    default:
        break;
    }
    // 0x41a996..0x41a99a: with any action.
    runtime.RequestSound(ButtonSound);
    // 0x41a99f..0x41a9c5: the dialog it leads to, with [0x6d5e28] read again.
    const auto now = DialogRecordOf(runtime, box.dialogIndex, row, "0x41a9b0");
    if (!now.HasValue())
        return Core::Unexpected(now.Error());
    const std::int32_t next = now.Value()->next[static_cast<std::size_t>(row)];
    if (next <= 0 || failed)
        return {};
    return Ui::ShowTheQuestDialog(box, runtime.dialogs, &runtime.dialogTable, context.texts, next);
}

// 0x41a9e0.
Core::Expected<void, std::string> StepTheQuestDialog(const Ui::GameUiContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    const QuestState& quest = runtime.quest;
    Kernel::MouseState& mouse = runtime.mouse;
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
        mouse.pendingClick)
    {
        // 0x41aaa1..0x41b0cc: the A2, the click and the button down at 0 (0x77f3b10 and 0x6c8dcc stay) and the sound.
        if (auto sent = SendTheQuestState(context, "0x41aaa1"); !sent.HasValue())
            return sent;
        mouse.pendingClick = false;
        mouse.leftHeld = false;
        runtime.RequestSound(ButtonSound);
    }
    if (MouseIn(context, quest.x + CloseLeft, quest.y + CloseTop, quest.x + CloseLeft + CloseSize,
                quest.y + CloseTop + CloseSize) &&
        mouse.pendingClick)
    {
        SpendTheClick(mouse);
        if (auto closed = CloseTheQuestWindow(context, "0x41b13d"); !closed.HasValue())
            return closed;
    }
    // 0x41b142..0x41b14c.
    mouse.releasedClick = false;
    return {};
}

// 0x41b170.
Core::Expected<void, std::string> StepTheQuestList(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    Kernel::MouseState& mouse = runtime.mouse;
    if (MouseIn(context, quest.x, quest.y, quest.x + WindowWidth, quest.y + WindowHeight))
        runtime.ui.mouseOnUi = 1;
    for (std::int32_t tab = 0; tab < TabCount; ++tab)
    {
        const bool chosen = tab == static_cast<std::int32_t>(quest.tab);
        const std::int32_t left = quest.x + TabLeft + tab * TabStep;
        const std::int32_t top = chosen ? ChosenTabTop : TabTop;
        const std::int32_t height = chosen ? ChosenTabHeight : TabHeight;
        if (!MouseIn(context, left, top, left + TabWidth, top + height) || !mouse.pendingClick)
            continue;
        // 0x41b23e..0x41b24d: tabs 1 and 2 do not spend the click. 0x41b253..0x41b281: tab 0 spends it and stays
        // chosen; the C1 9F of 0x41b287 asks for a tab other than the chosen one and 0, which does not exist (Q-C845).
        if (tab == 0)
        {
            SpendTheClick(mouse);
            quest.tab = 0;
        }
        break;
    }
    if (!MouseIn(context, quest.x + CloseLeft, quest.y + CloseTop, quest.x + CloseLeft + CloseSize,
                 quest.y + CloseTop + CloseSize) ||
        !mouse.pendingClick)
        return {};
    SpendTheClick(mouse);
    // 0x41b58b: 0x65cae0(8, 0), which sets the window to 0, sends the C1 03 31 and sounds.
    auto closed = panels.Close(Ui::QuestPanel);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}
} // namespace

Core::Expected<void, std::string> StepTheQuestWindow(const Ui::GameUiContext& context, Ui::PanelRegistry& panels)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    QuestState& quest = runtime.quest;
    if (panels.IsOpen(Ui::CharacterPanel))
    {
        quest.x = BesideTheCharacterX;
        quest.y = 0;
        if (MouseIn(context, QuestButtonLeft, QuestButtonTop, QuestButtonRight, QuestButtonBottom) &&
            runtime.mouse.pendingClick)
        {
            SpendTheClick(runtime.mouse);
            // 0x41b621..0x41b63b: Close(8) or Open(8), both with 0 as the second argument.
            auto toggled = panels.IsOpen(Ui::QuestPanel) ? panels.Close(Ui::QuestPanel) : panels.Open(Ui::QuestPanel);
            if (!toggled.HasValue())
                return Core::Unexpected(std::move(toggled).Error());
        }
    }
    else
    {
        quest.x = AloneX;
        quest.y = 0;
    }
    // 0x41b656..0x41b671: the window, read after opening or closing it.
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
} // namespace Client::Quests
