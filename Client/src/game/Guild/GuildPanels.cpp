#include "Guild/GuildPanels.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "CastleSiege/CastleWindows.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Chat/GameMessages.h"
#include "Hud/GameUi.h"
#include "Guild/GuildState.h"
#include "Guild/GuildWindow.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Ui/Dialogs.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelDialog.h"
#include "Ui/PanelRegistry.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"
#include "Ui/GameUiState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Guild
{
namespace
{
// 0x6522f0 and 0x6570b0: 0x41ed10(0x1c, 0, 0). It stays in WorldRuntime::lastSoundId (CL10).
constexpr Audio::SoundId GuildCloseSound = Sound::CreateWindowSound;
// 0x6522f0: the tab it goes back to.
constexpr std::int32_t FirstGuildTab = 1;
// 0x6570b0: C1 04 54 00.
constexpr std::uint8_t ClosedMasterAnswer = 0;
// 0x653c69..0x6545de: the result of the E6 that the accepted and the refused E5 send.
constexpr std::uint8_t RelationshipAccepted = 1;
constexpr std::uint8_t RelationshipRefused = 0;
// 0x64dfc7: the type of the E1 that removes the position.
constexpr std::uint8_t DismissType = 3;
// 0x64e55e: the game dialog that asks for the number to kick a member.
constexpr std::int32_t KickDialogId = 0x7d;
// 0x64dec9: the Tab count (0x6c8e6c) with dialog 0x7d.
constexpr std::int32_t NumberDialogFields = 1;

Core::Expected<void, std::string> Send(const GuildSender& send, Protocol::Outgoing packet, const char* where)
{
    if (!send)
        return Core::Unexpected(std::string(where) + ": el paquete del gremio sin el cable");
    send(std::move(packet));
    return {};
}

Ui::PanelDialogAnswer AnswerOf(Kernel::WorldState& world, const GuildSender& send)
{
    return [&world, send](Ui::PanelDialogOwner owner, std::uint32_t reason)
    { return AnswerThePanelDialog(world, send, owner, reason); };
}
} // namespace

Core::Expected<std::uint32_t, std::string> PopUpThePanelDialog(Kernel::WorldRuntime& runtime,
                                                               const Ui::PanelDialogRequest& request,
                                                               const Render::Text::ITextMeasure& measure,
                                                               Core::PixelSize screen, std::uint32_t now)
{
    Ui::PanelDialogState& dialog = runtime.panels.delegated.panelDialog;
    switch (Ui::OpenThePanelDialog(dialog, request, measure, runtime.hud.dcFont, now))
    {
    case Ui::PanelDialogOpening::Opened:
        return Ui::PanelDialogHandle(dialog);
    case Ui::PanelDialogOpening::TooManyLines:
        return 0U;
    case Ui::PanelDialogOpening::Busy:
    {
        // 0x660109..0x660115: 0x555190(0x6e57a4, 0x6d69a8, 1); OutputDebugStringA (0x660103) no deja nada.
        auto added = Chat::AddGameMessage(runtime, measure, screen, {}, Ui::PanelDialogBusyNotice,
                                          Ui::PanelDialogBusyNoticeType);
        if (!added.HasValue())
            return Core::Unexpected(std::move(added).Error());
        return 0U;
    }
    case Ui::PanelDialogOpening::LineTooLong:
        break;
    }
    return Core::Unexpected(
        std::string("0x660163: un renglón de 0x100 bytes o más, que strcpy derrama sobre el lugar de "
                    "al lado del diálogo de los paneles (Q-C1305)"));
}

Core::Expected<bool, std::string> AnswerThePanelDialog(Kernel::WorldState& world, const GuildSender& send,
                                                       Ui::PanelDialogOwner owner, std::uint32_t reason)
{
    const GuildState& guild = world.Runtime().guild;
    switch (owner)
    {
    case Ui::PanelDialogOwner::GuildJoin:
    {
        // 0x4d6b83: 8 accepts; anything else (Cancel, Esc, the clock) refuses. The key, from 0x78aba40 (SAR 8 and the
        // byte).
        const auto key = static_cast<std::uint16_t>(guild.requester);
        if (auto sent = Send(send, Protocol::Guild::WriteJoinAnswer(reason == Ui::PanelAnswerConfirm, key), "0x4d6b60");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case Ui::PanelDialogOwner::Relationship:
    {
        // 0x653c23 and 0x654289: only Confirm and Cancel send; with anything else, nothing.
        if (reason != Ui::PanelAnswerConfirm && reason != Ui::PanelAnswerCancel)
            return true;
        const std::uint8_t result = reason == Ui::PanelAnswerConfirm ? RelationshipAccepted : RelationshipRefused;
        if (auto sent = Send(send,
                             Protocol::Guild::WriteRelationshipAnswer(guild.relation, guild.relationRequest, result,
                                                                      guild.relationKeyHigh, guild.relationKeyLow),
                             "0x653c00");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case Ui::PanelDialogOwner::GuildDismiss:
    {
        // 0x64df13: with 8, C1 0F E1 03 00 and the name from 0x7d535a8 (10 bytes), and C1 03 52 (0x64e38b); with
        // anything else, nothing.
        if (reason != Ui::PanelAnswerConfirm)
            return true;
        Protocol::FrontEnd::NameBytes name{};
        for (std::size_t index = 0; index < name.size(); ++index)
            name[index] = static_cast<std::byte>(guild.chosenName[index]);
        if (auto sent = Send(send, Protocol::Guild::WriteAssignStatus(DismissType, 0, name), "0x64def0");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        if (auto sent = Send(send, Protocol::Guild::WriteMemberListRequest(), "0x64e38b"); !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case Ui::PanelDialogOwner::GuildKick:
    {
        // 0x64e553: with 8, the game's dialog 0x7d (0x7d3a10c), which asks for the master's number and sends the C1 53
        // (0x62a878), with the member's slot from 0x7d536c8 in 0x6d5e2c. Unlike 0x64de90, it does not touch the chat
        // fields.
        if (reason != Ui::PanelAnswerConfirm)
            return true;
        Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.dialogs.current = KickDialogId;
        runtime.guild.kickMember = runtime.guild.kickIndex;
        return true;
    }
    case Ui::PanelDialogOwner::GuildDisband:
    {
        // 0x64de93: with 8, 0x6d5e2c at 0 (slot 0 of 0x787a5e8: the master, if the 52 sends it first, *inferred*) and
        // dialog 0x7d with 0x553570(0), the chat field closed (0x6c8e68) and the Tab count (0x6c8e6c) at 1. 0x6c8e78
        // and 0x77f31c0 belong to the input without an EDIT, which MuRet does not have.
        if (reason != Ui::PanelAnswerConfirm)
            return true;
        Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.guild.kickMember = 0;
        runtime.dialogs.current = KickDialogId;
        Ui::ResetTheTextBuffers(runtime.chatInput, false);
        runtime.ui.chatOpen = false;
        runtime.chatInput.fieldCount = NumberDialogFields;
        return true;
    }
    case Ui::PanelDialogOwner::UnionKick:
    {
        // 0x650858: with 8, C1 0C EB 01 and the 8 bytes from 0x7d535a8 (the name the +0x6d4 button left); with anything
        // else, nothing.
        if (reason != Ui::PanelAnswerConfirm)
            return true;
        Protocol::Guild::GuildName name{};
        for (std::size_t index = 0; index < name.size(); ++index)
            name[index] = static_cast<std::byte>(guild.chosenName[index]);
        if (auto sent = Send(send, Protocol::Guild::WriteUnionKick(name), "0x650830"); !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case Ui::PanelDialogOwner::CastleCrownRegistration:
        return (static_cast<std::uint8_t>(reason) & 0x40u) == 0;
    case Ui::PanelDialogOwner::None:
        break;
    default:
        return CastleSiege::AnswerTheCastleWindowDialog(world, send, owner, reason);
    }
    return Core::Unexpected(std::string("0x6600d0: el diálogo de los paneles responde sin quien lo abrió"));
}

Core::Expected<void, std::string> CloseTheGuildWindow(Kernel::WorldState& world, const GuildSender& send)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    GuildWindowState& window = runtime.guild.window;
    if (delegated.guild == 0)
        return {};
    delegated.guild = 0;
    window.unionRequested = 0;
    window.tab = FirstGuildTab;
    runtime.RequestSound(GuildCloseSound); // 0x41ed10 (CL10)
    return CancelTheGuildWindowDialog(world, send);
}

Core::Expected<void, std::string> CancelTheGuildWindowDialog(Kernel::WorldState& world, const GuildSender& send)
{
    Ui::DelegatedPanels& delegated = world.Runtime().panels.delegated;
    if (Ui::PanelDialogHandle(delegated.panelDialog) != world.Runtime().guild.window.dialog)
        return {};
    return Ui::CancelThePanelDialog(delegated.panelDialog, AnswerOf(world, send));
}

Core::Expected<void, std::string> CloseTheSecondGuildWindow(Kernel::WorldState& world, const GuildSender& send)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Ui::DelegatedPanels& delegated = runtime.panels.delegated;
    GuildState& guild = runtime.guild;
    if (delegated.secondGuild == 0)
        return {};
    delegated.secondGuild = 0;
    Kernel::ActorState& hero = world.Hero();
    if (static_cast<std::int16_t>(hero.word1f2) == CreatingGuildIndex)
        hero.word1f2 = static_cast<std::uint16_t>(NoGuildIndex);
    runtime.RequestSound(GuildCloseSound); // 0x41ed10 (CL10)
    guild.createKind = 0;
    guild.createStep = 0;
    // 0x657117: 0x77f3b68, the creation field, at 0 (the EDIT stays as it was: Q-C1291).
    runtime.chatInput.dialogFieldOn = false;
    // 0x654f50: the open one is +0x2a0 or +0x2a4.
    const std::uint32_t open = Ui::PanelDialogHandle(delegated.panelDialog);
    if (open == guild.secondWindow.dialog2a0 || open == guild.secondWindow.dialog2a4)
    {
        if (auto cancelled = Ui::CancelThePanelDialog(delegated.panelDialog, AnswerOf(world, send));
            !cancelled.HasValue())
            return cancelled;
    }
    return Send(send, Protocol::Guild::WriteMasterAnswer(ClosedMasterAnswer), "0x6570b0");
}

void LendTheGuildTo(Ui::PanelContext& context, Kernel::WorldState& world, const GuildSender& send)
{
    if (!send)
        return;
    context.guildOpened = [send]() { send(Protocol::Guild::WriteMemberListRequest()); };
    context.guildClosed = [&world, send]() { return CloseTheGuildWindow(world, send); };
    context.secondGuildClosed = [&world, send]() { return CloseTheSecondGuildWindow(world, send); };
    context.panelDialogAnswer = AnswerOf(world, send);
}

void LendThePanelDialogStepTo(Ui::PanelContext& context, Kernel::WorldState& world, const GuildSender& send,
                              std::int32_t mouseX, std::int32_t mouseY, std::uint32_t now)
{
    context.panelDialogStep = [&world, &context, send, mouseX, mouseY, now]() -> Core::Expected<void, std::string>
    {
        Kernel::WorldRuntime& runtime = world.Runtime();
        const Ui::OldWindowGlobals globals = Hud::OldWindowGlobalsOf(runtime);
        Ui::PanelDialogState& dialog = runtime.panels.delegated.panelDialog;
        if (auto stepped = Ui::StepThePanelDialog(dialog, globals, mouseX, mouseY, now, context.panelDialogAnswer);
            !stepped.HasValue())
            return stepped;
        // 0x660623: open and with +8, its own step after the mouse over the box (StepThePanelDialog did nothing more).
        if (Ui::PanelDialogHandle(dialog) == 0 || dialog.hooks != Ui::PanelDialogHooks::GuildPositions)
            return {};
        return StepTheGuildPositions(world, send, mouseX, mouseY, context.panelDialogAnswer);
    };
}
} // namespace Client::Guild
