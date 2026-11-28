#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/CastleSiege/CastleSiege.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Models/LogicalAccess.h"
#include "CastleSiege/CastleWindowSupport.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Hud/GameUi.h"
#include "Guild/GuildWindow.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Chat/ChatLogWindow.h"
#include "Chat/ChatInput.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"
#include "Chat/ChatRuntime.h"
#include "Social/SocialState.h"
#include "Ui/GameUiState.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace Client::CastleSiege
{
namespace
{
namespace Castle = Protocol::CastleSiege;
using CastleUi::ConsumeClick;
using CastleUi::Send;
constexpr Audio::SoundId ClickSound = Sound::ButtonClickSound;

bool Hit(const Ui::GameUiContext& context, std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h)
{
    return Ui::MouseInRect(context.mouseX, context.mouseY, x, y, w, h, 1);
}

void Cover(const Ui::GameUiContext& context, std::int32_t x, std::int32_t y)
{
    if (Hit(context, x, y, CastleUi::WindowWidth, CastleUi::WindowHeight))
        context.world.Runtime().ui.mouseOnUi = 1;
}

bool Click(const Ui::GameUiContext& context, Ui::OldButton& button)
{
    return Ui::ClickTheButton(button, Hud::OldWindowGlobalsOf(context.world.Runtime()), context.mouseX, context.mouseY);
}

Core::Expected<void, std::string> Close(const Ui::GameUiContext& context, Ui::PanelId id, std::int32_t x)
{
    auto& runtime = context.world.Runtime();
    if (!runtime.mouse.pendingClick || !Hit(context, x, CastleUi::CloseY, CastleUi::CloseSize, CastleUi::CloseSize))
        return {};
    ConsumeClick(runtime);
    runtime.RequestSound(ClickSound);
    Hud::GamePanels panels(context.world, context.random);
    panels.LendTheSession(context.world, context.random, context.requests);
    panels.LendTheCastleWindowSteps(context);
    // The button calls the window's logical destructor, not the wrapper that also closes its companions.
    auto closed = panels.Registry().CloseByMask(id);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return {};
}

Core::Expected<void, std::string> RegistrationAction(const Ui::GameUiContext& context)
{
    auto& world = context.world;
    auto& runtime = world.Runtime();
    auto& window = runtime.castleSiege.registration;
    if (window.state == 1)
    {
        if (world.Hero().guildStatus != CastleUi::Master || window.registered != 0 || !Click(context, window.button))
            return {};
        if (CastleUi::HeroCanRegister(world, context.random))
            return Send(context, Castle::WriteRegister());
        auto popup = CastleUi::PopupAt(context, {0x7725c78, 0x7725da4}, Ui::PanelDialogOwner::None, Ui::PanelDialogOk);
        if (!popup.HasValue())
            return Core::Unexpected(std::move(popup).Error());
    }
    else if (window.state == 3 && Click(context, window.button))
    {
        const auto slot = CastleUi::FirstMark(world, context.random, runtime.ownRecordDepth);
        if (slot != -1)
            return Send(context, Castle::WriteRegisterMark(static_cast<std::uint8_t>(slot)));
    }
    return {};
}

void StepList(const Ui::GameUiContext& context, Ui::OldList& list, std::deque<CastleSiege::CastleGuildRow>& rows)
{
    auto& runtime = context.world.Runtime();
    auto& ui = runtime.ui;
    ui.capture = list.base.id;
    while (!list.base.queue.empty())
    {
        list.base.current = std::move(list.base.queue.front());
        list.base.queue.pop_front();
        Ui::HandleTheListMessage(list, rows.size());
    }
    Ui::OldListGlobals globals{.leftClick = runtime.mouse.pendingClick,
                               .wheel = ui.wheel,
                               .capture = ui.capture,
                               .focusedList = ui.focusedList,
                               .mouseOnUi = ui.mouseOnUi,
                               .sound = runtime.lastSoundId,
                               .drawn = runtime.oldWindows.drawn != 0,
                               .modal = runtime.oldWindows.modal,
                               .soundRequest = [&runtime](Audio::SoundId sound) { runtime.RequestSound(sound); }};
    Ui::OldListView view{.mouseX = context.mouseX, .mouseY = context.mouseY, .keyDown = [&context](std::uint8_t key) {
                             return context.input.IsDown(key == 0x26 ? Input::Key::Up : Input::Key::Down);
                         }};
    if (Guild::PassesTheGuildWindowGate(list.base, ui, context.mouseX, context.mouseY))
        static_cast<void>(Ui::StepTheListMouse(
            list, rows.size(), globals, view,
            [&](std::int32_t row, std::size_t at)
            {
                const auto width =
                    static_cast<std::int32_t>(static_cast<float>(list.base.width) - list.thumbWidth + 1.0f);
                if (Hit(context, list.base.x, Ui::ListRowY(list, rows.size(), row) - 3, width, 13) &&
                    runtime.mouse.pendingClick)
                {
                    Ui::SelectInTheList(list, rows.size(), list.scroll + row + 1);
                    const std::uint32_t next = static_cast<std::uint32_t>(rows[at].toggled) + 1u;
                    rows[at].toggled = std::bit_cast<std::int32_t>(next) % 2;
                    runtime.mouse.pendingClick = false;
                }
                return 1;
            }));
    ui.capture = 0;
    if (Hud::OldWindowKeyEdge(ui, context.input, 0x21))
        Ui::ScrollTheList(list, rows.size(), -list.rows);
    if (Hud::OldWindowKeyEdge(ui, context.input, 0x22))
        Ui::ScrollTheList(list, rows.size(), list.rows);
}

Core::Expected<void, std::string> RegistrationList(const Ui::GameUiContext& context)
{
    auto& window = context.world.Runtime().castleSiege.registration;
    if (window.state == 1 || window.state == 3)
        StepList(context, window.siegeGuildList, window.siegeGuilds);
    else if (window.state == 5 || window.state == 6)
        StepList(context, window.registeredGuildList, window.registeredGuilds);
    if (!Click(context, window.button))
        return {};
    auto popup = CastleUi::PopupAt(context, {0x772ad54}, Ui::PanelDialogOwner::CastleGiveUp, Ui::PanelDialogConfirm);
    if (!popup.HasValue())
        return Core::Unexpected(std::move(popup).Error());
    return {};
}

Core::Expected<void, std::string> StepRegistration(const Ui::GameUiContext& context)
{
    auto& window = context.world.Runtime().castleSiege.registration;
    Cover(context, window.x, window.y);
    if (context.world.Runtime().mouse.leftHeld)
    {
        for (std::int32_t tab = 0; tab < 3; ++tab)
        {
            if (window.tab == tab || !Hit(context, window.x + 15 + 54 * tab, window.y + 91, 54, 16))
                continue;
            if (tab == 2)
            {
                Core::Expected<void, std::string> sent;
                if (window.state == 1 || window.state == 3)
                    sent = Send(context, Castle::WriteSiegeGuildListRequest());
                else if (window.state == 5 || window.state == 6)
                    sent = Send(context, Castle::WriteRegisteredGuildListRequest());
                if (!sent.HasValue())
                    return sent;
            }
            window.tab = tab;
            break;
        }
    }
    if (window.tab == 1)
    {
        if (auto action = RegistrationAction(context); !action.HasValue())
            return action;
    }
    else if (window.tab == 2)
    {
        if (auto list = RegistrationList(context); !list.HasValue())
            return list;
    }
    return Close(context, Ui::CastleSiegePanel, 475);
}

Core::Expected<void, std::string> EnterHunt(const Ui::GameUiContext& context)
{
    auto& world = context.world;
    auto& runtime = world.Runtime();
    auto& window = runtime.castleSiege.huntZone;
    if (window.mode != 1)
    {
        if (auto sent = Send(context, Castle::WriteHuntZoneEntry(window.fee)); !sent.HasValue())
            return sent;
        Hud::GamePanels panels(world, context.random);
        panels.LendTheCastleRequests(world, CastleUi::SenderOf(context));
        auto closed = panels.Registry().CloseByMask(Ui::SecondCastleSiegePanel);
        if (!closed.HasValue())
            return Core::Unexpected(std::move(closed).Error());
    }
    else
    {
        const Models::LogicalAccess access(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        if (window.allowed != 0 && world.OwnCharacter().zen > window.fee)
        {
            if (auto sent = Send(context, Castle::WriteHuntZoneEntry(window.fee)); !sent.HasValue())
                return sent;
        }
        else
        {
            if (auto cancelled = CastleUi::CancelPopup(world, CastleUi::SenderOf(context)); !cancelled.HasValue())
                return cancelled;
            auto popup = window.allowed == 1 ? CastleUi::PopupAt(context, {0x7734480, 0x77345ac},
                                                                 Ui::PanelDialogOwner::None, Ui::PanelDialogOk, 1)
                                             : CastleUi::PopupAt(context, {0x7734480}, Ui::PanelDialogOwner::None,
                                                                 Ui::PanelDialogOk, 1);
            if (!popup.HasValue())
                return Core::Unexpected(std::move(popup).Error());
        }
    }
    ConsumeClick(runtime);
    return {};
}

Core::Expected<void, std::string> StepHunt(const Ui::GameUiContext& context)
{
    auto& runtime = context.world.Runtime();
    auto& window = runtime.castleSiege.huntZone;
    if (window.mode == 0)
        return {};
    Cover(context, window.x, window.y);
    if (window.mode == 3)
    {
        if (runtime.mouse.pendingClick && Hit(context, window.x + 20, window.y + 115, 150, 24))
        {
            ConsumeClick(runtime);
            if (auto sent = Send(context, Castle::WriteHuntZoneGate(static_cast<std::uint8_t>(window.allowed ^ 1u)));
                !sent.HasValue())
                return sent;
        }
        if (Click(context, window.commit))
        {
            ConsumeClick(runtime);
            if (auto sent = Send(context, Castle::WriteTaxRate(3, static_cast<std::uint32_t>(window.editedFee)));
                !sent.HasValue())
                return sent;
        }
        if (Click(context, window.plus))
        {
            ConsumeClick(runtime);
            window.editedFee = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(window.editedFee) +
                                                           static_cast<std::uint32_t>(window.step));
            if (window.editedFee > window.max)
                window.editedFee = window.max;
        }
        if (Click(context, window.minus))
        {
            ConsumeClick(runtime);
            window.editedFee = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(window.editedFee) -
                                                           static_cast<std::uint32_t>(window.step));
            if (window.editedFee < 0)
                window.editedFee = 0;
        }
    }
    if (Click(context, window.enter))
        return EnterHunt(context);
    return Close(context, Ui::SecondCastleSiegePanel, 475);
}

Core::Expected<void, std::string> SeniorTabs(const Ui::GameUiContext& context)
{
    auto& runtime = context.world.Runtime();
    auto& window = runtime.castleSiege.senior;
    auto& tab = runtime.panels.delegated.castleSiegeListMode;
    const bool busy = CastleSeniorBusy(context.world);
    for (std::int32_t next = 0; next < 4; ++next)
    {
        if (tab == next || (busy && next != 3) || !Hit(context, window.x + 15 + 40 * next, window.y + 50, 40, 16))
            continue;
        if (next == 2)
        {
            auto permission = CastleUi::HeroCanEditTaxes(context.world);
            if (!permission.HasValue())
                return Core::Unexpected(std::move(permission).Error());
            if (!permission.Value())
                continue;
            auto& data = runtime.castleSiege.data;
            data.SetU32(CastleSiege::StoreTaxAt, data.U32(CastleSiege::ServerStoreTaxAt));
            data.SetU32(CastleSiege::ChaosTaxAt, data.U32(CastleSiege::ServerChaosTaxAt));
        }
        tab = next;
        if (Ui::PanelDialogHandle(runtime.panels.delegated.panelDialog) == window.dialogId)
        {
            if (auto cancelled = CastleUi::CancelPopup(context.world, CastleUi::SenderOf(context));
                !cancelled.HasValue())
                return cancelled;
            window.dialogId = 0;
        }
        Hud::GamePanels panels(context.world, context.random);
        panels.LendTheSession(context.world, context.random, context.requests);
        panels.LendTheCastleWindowSteps(context);
        if (next != 3)
        {
            if (auto closed = panels.Registry().Close(Ui::InventoryPanel); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
        }
        else
        {
            runtime.mouse.pendingClick = false;
            if (HeroCanCraftAtTheCastle(context.world))
            {
                runtime.ui.npcWindowX = 0x104;
                runtime.ui.npcWindowY = 0;
                if (auto opened = panels.Registry().Open(Ui::InventoryPanel); !opened.HasValue())
                    return Core::Unexpected(std::move(opened).Error());
            }
        }
        break;
    }
    return {};
}

Core::Expected<void, std::string> SeniorNpcDialog(const Ui::GameUiContext& context, bool statue, std::int32_t action)
{
    auto& runtime = context.world.Runtime();
    auto& data = runtime.castleSiege.data;
    const auto selected = data.U32(statue ? CastleSiege::SelectedStatueAt : CastleSiege::SelectedGateAt);
    const auto at =
        (statue ? CastleSiege::FirstStatueAt : CastleSiege::FirstGateAt) + selected * CastleSiege::NpcRecordBytes;
    if (!CastleSiege::CastleData::Holds(at, CastleSiege::NpcRecordBytes))
        return Core::Unexpected(std::string("la ventana Senior lee el registro elegido fuera de 0x7d5c504..0x7d5c658"));
    const bool alive = data.bytes.at(at + CastleSiege::RecordAlive - CastleSiege::CastleDataBase) != 0;
    constexpr std::array<std::uint32_t, 4> GateTexts{0x772eb70, 0x772f020, 0x772f14c, 0};
    constexpr std::array<std::uint32_t, 4> StatueTexts{0x7733094, 0x7733418, 0x7733544, 0x7733670};
    constexpr std::array<Ui::PanelDialogOwner, 4> GateOwners{
        Ui::PanelDialogOwner::CastleGate, Ui::PanelDialogOwner::CastleGateLife, Ui::PanelDialogOwner::CastleGateDefense,
        Ui::PanelDialogOwner::None};
    constexpr std::array<Ui::PanelDialogOwner, 4> StatueOwners{
        Ui::PanelDialogOwner::CastleStatue, Ui::PanelDialogOwner::CastleStatueLife,
        Ui::PanelDialogOwner::CastleStatueDefense, Ui::PanelDialogOwner::CastleStatueRecovery};
    auto first = CastleUi::Text(
        context.texts, action == 0 && alive ? (statue ? 0x77331c0 : 0x772ec9c)
                                            : (statue ? StatueTexts : GateTexts)[static_cast<std::size_t>(action)]);
    auto second = CastleUi::Text(context.texts, action == 0 ? 0x77338c8 : 0x772edc8);
    auto last = CastleUi::Text(context.texts, action == 0 && !alive ? 0x77332ec : 0x772eef4);
    if (!first.HasValue())
        return Core::Unexpected(std::move(first).Error());
    if (!second.HasValue())
        return Core::Unexpected(std::move(second).Error());
    if (!last.HasValue())
        return Core::Unexpected(std::move(last).Error());
    if (action == 0)
        second.Value() = Crt::Sprintf(second.Value(), CastleUi::NpcCost(data, at));
    else
    {
        const auto type = data.U32(at + CastleSiege::RecordType);
        const std::int32_t tier =
            action == 1
                ? CastleSiege::LifeLevelOf(type, std::bit_cast<std::int32_t>(data.U32(at + CastleSiege::RecordMaxLife)))
            : action == 2 ? CastleUi::DefenseLevel(data, at)
                          : CastleUi::RecoveryLevel(data, at);
        const auto requirement = statue ? (tier == 0 ? 3 : tier == 1 ? 5 : 7) : (tier == 0 ? 2 : tier == 1 ? 3 : 4);
        const auto zen = action == 1 ? 1000000 : action == 2 ? 3000000 : 5000000;
        second.Value() = Crt::Sprintf(second.Value(), requirement, zen);
    }
    const std::array<std::string, 3> lines{std::move(first).Value(), std::move(second).Value(),
                                           std::move(last).Value()};
    const auto owner = (statue ? StatueOwners : GateOwners)[static_cast<std::size_t>(action)];
    auto popup = CastleUi::Popup(context, lines, owner, Ui::PanelDialogConfirm);
    if (!popup.HasValue())
        return Core::Unexpected(std::move(popup).Error());
    runtime.castleSiege.senior.dialogId = popup.Value();
    return {};
}

Core::Expected<void, std::string> SeniorNpcs(const Ui::GameUiContext& context, bool statue)
{
    auto& runtime = context.world.Runtime();
    constexpr std::array<Ui::Point, 6> Gates{{{532, 122}, {514, 170}, {550, 170}, {498, 222}, {532, 222}, {566, 222}}};
    constexpr std::array<Ui::Point, 4> Statues{{{532, 107}, {532, 152}, {514, 197}, {550, 197}}};
    if (runtime.mouse.pendingClick && Hit(context, 465, 87, 160, 165))
    {
        const std::size_t count = statue ? Statues.size() : Gates.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& point = statue ? Statues[i] : Gates[i];
            if (Hit(context, point.x, point.y, 24, 24))
            {
                runtime.castleSiege.data.SetU32(statue ? CastleSiege::SelectedStatueAt : CastleSiege::SelectedGateAt,
                                                static_cast<std::uint32_t>(i));
                break;
            }
        }
    }
    auto& buttons = runtime.castleSiege.senior.buttons;
    const std::int32_t count = statue ? 4 : 3;
    for (std::int32_t action = 0; action < count; ++action)
    {
        if (Click(context, buttons[static_cast<std::size_t>(action)]))
            return SeniorNpcDialog(context, statue, action);
    }
    return {};
}

Core::Expected<void, std::string> SeniorTaxes(const Ui::GameUiContext& context)
{
    auto& runtime = context.world.Runtime();
    auto& window = runtime.castleSiege.senior;
    if (Click(context, window.buttons[4]))
    {
        auto chaos = CastleUi::Text(context.texts, 0x772fd04);
        auto store = CastleUi::Text(context.texts, 0x772fe30);
        auto confirm = CastleUi::Text(context.texts, 0x772ff5c);
        if (!chaos.HasValue())
            return Core::Unexpected(std::move(chaos).Error());
        if (!store.HasValue())
            return Core::Unexpected(std::move(store).Error());
        if (!confirm.HasValue())
            return Core::Unexpected(std::move(confirm).Error());
        const std::array<std::string, 3> lines{
            Crt::Sprintf(chaos.Value(), runtime.castleSiege.data.U32(CastleSiege::ChaosTaxAt)),
            Crt::Sprintf(store.Value(), runtime.castleSiege.data.U32(CastleSiege::StoreTaxAt)),
            std::move(confirm).Value()};
        auto popup = CastleUi::Popup(context, lines, Ui::PanelDialogOwner::CastleTaxes, Ui::PanelDialogConfirm);
        if (!popup.HasValue())
            return Core::Unexpected(std::move(popup).Error());
        window.dialogId = popup.Value();
    }
    else if (Click(context, window.buttons[5]))
    {
        auto& dialog = runtime.panels.delegated.panelDialog;
        dialog.inputWidth = 70;
        dialog.inputMaxChars = 8;
        dialog.inputFilter = 1;
        auto popup = CastleUi::PopupAt(context, {0x77302e0, 0x77301b4}, Ui::PanelDialogOwner::CastleMoney,
                                       Ui::PanelDialogTextField | Ui::PanelDialogYesNo);
        if (!popup.HasValue())
            return Core::Unexpected(std::move(popup).Error());
        window.dialogId = popup.Value();
        // Shared EDIT plumbing: the answer reads it before CloseThePanelDialog clears it.
        dialog.closeField = [&runtime]()
        {
            auto& input = runtime.chat.input;
            input.dialogEdit.text.Clear();
            input.dialogEdit.compositionText.clear();
            input.dialogEdit.compositionStart = 0;
            input.dialogEdit.compositionLength = 0;
            // The shared helper models the focus loss when hiding the HWND; 0x660350 does not call SetFocusGame.
            Ui::ShowTheDialogField(input, Ui::HiddenWindow);
            input.messageLength = 0;
            runtime.social.dialogFieldLimit = 10;
        };
    }
    return {};
}

bool HasCraftSpace(const Kernel::WorldState& world)
{
    // 0x5fd200: the 4×5 rectangle in 8×8; type==−1 defines each free cell, not occupied.
    const auto& cells = world.Inventory().cells;
    for (std::size_t row = 0; row <= 3; ++row)
    {
        for (std::size_t column = 0; column <= 4; ++column)
        {
            bool free = true;
            for (std::size_t y = 0; y < 5 && free; ++y)
                for (std::size_t x = 0; x < 4; ++x)
                    if (cells[(row + y) * 8 + column + x].id != 0xffff)
                        free = false;
            if (free)
                return true;
        }
    }
    return false;
}

Core::Expected<void, std::string> SeniorCraft(const Ui::GameUiContext& context)
{
    if (!HeroCanCraftAtTheCastle(context.world) ||
        !Click(context, context.world.Runtime().castleSiege.senior.buttons[6]))
        return {};
    if (HasCraftSpace(context.world))
        return Send(context, Protocol::Npc::WriteChaosMix(0x12));
    auto text = CastleUi::Text(context.texts, 0x7735740);
    if (!text.HasValue())
        return Core::Unexpected(std::move(text).Error());
    if (context.measure == nullptr)
        return Core::Unexpected(std::string("0x555190: el aviso de espacio del castillo sin medidor"));
    return Chat::AddGameMessage(context.world.Runtime(), *context.measure, context.screen, {}, text.Value(), 2);
}

Core::Expected<void, std::string> StepSenior(const Ui::GameUiContext& context)
{
    auto& runtime = context.world.Runtime();
    const auto& window = runtime.castleSiege.senior;
    Cover(context, window.x, window.y);
    if (Ui::PanelDialogHandle(runtime.panels.delegated.panelDialog) != 0)
        return {};
    if (runtime.mouse.leftHeld)
    {
        if (auto tabs = SeniorTabs(context); !tabs.HasValue())
            return tabs;
    }
    Core::Expected<void, std::string> stepped;
    switch (runtime.panels.delegated.castleSiegeListMode)
    {
    case 0:
        stepped = SeniorNpcs(context, false);
        break;
    case 1:
        stepped = SeniorNpcs(context, true);
        break;
    case 2:
        stepped = SeniorTaxes(context);
        break;
    case 3:
        stepped = SeniorCraft(context);
        break;
    default:
        break;
    }
    if (!stepped.HasValue())
        return stepped;
    return Close(context, Ui::CastleSiegeListPanel, window.x + 25);
}
} // namespace

Core::Expected<void, std::string> StepTheCastleWindow(const Ui::GameUiContext& context, Ui::PanelId id)
{
    if (id == Ui::CastleSiegePanel)
        return StepRegistration(context);
    if (id == Ui::SecondCastleSiegePanel)
        return StepHunt(context);
    return StepSenior(context);
}
} // namespace Client::CastleSiege
