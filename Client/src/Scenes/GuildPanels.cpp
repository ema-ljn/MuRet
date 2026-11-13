#include "Scenes/GuildPanels.h"

#include "Core/Expected.h"
#include "Protocol/Guild.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameUi.h"
#include "Scenes/GuildState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/OldWindows.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x6522f0 y 0x6570b0: 0x41ed10(0x1c, 0, 0). Queda en WorldRuntime::lastSoundId (CL10).
constexpr std::int32_t GuildCloseSound = 0x1c;
// 0x6522f0: la pestaña a la que vuelve.
constexpr std::int32_t FirstGuildTab = 1;
// 0x6570b0: C1 04 54 00.
constexpr std::uint8_t ClosedMasterAnswer = 0;
// 0x653c69..0x6545de: el resultado del E6 que manda el E5 aceptado y el rechazado.
constexpr std::uint8_t RelationshipAccepted = 1;
constexpr std::uint8_t RelationshipRefused = 0;
// 0x64dfc7: el tipo del E1 que quita el cargo.
constexpr std::uint8_t DismissType = 3;
// 0x64e55e: el diálogo del juego que pide el número para echar a un miembro.
constexpr std::int32_t KickDialogId = 0x7d;

Core::Expected<void, std::string> Send(const GuildSender& send, Protocol::Outgoing packet, const char* where)
{
    if (!send)
        return Core::Unexpected(std::string(where) + ": el paquete del gremio sin el cable");
    send(std::move(packet));
    return {};
}

UI::PanelDialogAnswer AnswerOf(WorldState& world, const GuildSender& send)
{
    return [&world, send](UI::PanelDialogOwner owner, std::uint32_t reason)
    { return AnswerThePanelDialog(world, send, owner, reason); };
}
} // namespace

Core::Expected<bool, std::string> AnswerThePanelDialog(WorldState& world, const GuildSender& send,
                                                       UI::PanelDialogOwner owner, std::uint32_t reason)
{
    const GuildState& guild = world.Runtime().guild;
    switch (owner)
    {
    case UI::PanelDialogOwner::GuildJoin:
    {
        // 0x4d6b83: 8 acepta; lo demás (Cancelar, Esc, el reloj) rechaza. La clave, de 0x78aba40 (SAR 8 y el byte).
        const auto key = static_cast<std::uint16_t>(guild.requester);
        if (auto sent = Send(send, Protocol::Guild::WriteJoinAnswer(reason == UI::PanelAnswerConfirm, key), "0x4d6b60");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case UI::PanelDialogOwner::Relationship:
    {
        // 0x653c23 y 0x654289: solo Confirmar y Cancelar mandan; con lo demás, nada.
        if (reason != UI::PanelAnswerConfirm && reason != UI::PanelAnswerCancel)
            return true;
        const std::uint8_t result = reason == UI::PanelAnswerConfirm ? RelationshipAccepted : RelationshipRefused;
        if (auto sent = Send(send,
                             Protocol::Guild::WriteRelationshipAnswer(guild.relation, guild.relationRequest, result,
                                                                      guild.relationKeyHigh, guild.relationKeyLow),
                             "0x653c00");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case UI::PanelDialogOwner::GuildDismiss:
    {
        // 0x64df13: con 8, C1 0F E1 03 00 y el nombre de 0x7d535a8 (10 bytes), y C1 03 52 (0x64e38b); con lo demás,
        // nada.
        if (reason != UI::PanelAnswerConfirm)
            return true;
        Protocol::Guild::CharacterName name{};
        for (std::size_t index = 0; index < name.size(); ++index)
            name[index] = static_cast<std::byte>(guild.chosenName[index]);
        if (auto sent = Send(send, Protocol::Guild::WriteAssignStatus(DismissType, 0, name), "0x64def0");
            !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        if (auto sent = Send(send, Protocol::Guild::WriteMemberListRequest(), "0x64e38b"); !sent.HasValue())
            return Core::Unexpected(std::move(sent).Error());
        return true;
    }
    case UI::PanelDialogOwner::GuildKick:
    {
        // 0x64e553: con 8, el diálogo 0x7d del juego (0x7d3a10c), que pide el número del maestro y manda el C1 53
        // (0x62a878), con el lugar del miembro de 0x7d536c8 en 0x6d5e2c. Ese diálogo corta en su paso (0x62a810).
        if (reason != UI::PanelAnswerConfirm)
            return true;
        WorldRuntime& runtime = world.Runtime();
        runtime.dialogs.current = KickDialogId;
        runtime.guild.kickMember = runtime.guild.kickIndex;
        return true;
    }
    case UI::PanelDialogOwner::None:
        break;
    }
    return Core::Unexpected(std::string("0x6600d0: el diálogo de los paneles responde sin quien lo abrió"));
}

Core::Expected<void, std::string> CloseTheGuildWindow(WorldState& world, const GuildSender& send)
{
    WorldRuntime& runtime = world.Runtime();
    UI::DelegatedPanels& delegated = runtime.panels.delegated;
    GuildWindowState& window = runtime.guild.window;
    if (delegated.guild == 0)
        return {};
    delegated.guild = 0;
    window.unionRequested = 0;
    window.tab = FirstGuildTab;
    runtime.lastSoundId = GuildCloseSound; // 0x41ed10 (CL10)
    return CancelTheGuildWindowDialog(world, send);
}

Core::Expected<void, std::string> CancelTheGuildWindowDialog(WorldState& world, const GuildSender& send)
{
    UI::DelegatedPanels& delegated = world.Runtime().panels.delegated;
    if (UI::PanelDialogHandle(delegated.panelDialog) != world.Runtime().guild.window.dialog)
        return {};
    return UI::CancelThePanelDialog(delegated.panelDialog, AnswerOf(world, send));
}

Core::Expected<void, std::string> CloseTheSecondGuildWindow(WorldState& world, const GuildSender& send)
{
    WorldRuntime& runtime = world.Runtime();
    UI::DelegatedPanels& delegated = runtime.panels.delegated;
    GuildState& guild = runtime.guild;
    if (delegated.secondGuild == 0)
        return {};
    delegated.secondGuild = 0;
    ActorState& hero = world.Actors()[runtime.hero];
    if (static_cast<std::int16_t>(hero.word1f2) == CreatingGuildIndex)
        hero.word1f2 = static_cast<std::uint16_t>(NoGuildIndex);
    runtime.lastSoundId = GuildCloseSound; // 0x41ed10 (CL10)
    guild.createKind = 0;
    guild.createStep = 0;
    // 0x657117: 0x77f3b68, el campo del alta, en 0 (el EDIT queda como estaba: Q-C1291).
    runtime.chat.input.dialogFieldOn = false;
    // 0x654f50: el abierto es +0x2a0 o +0x2a4.
    const std::uint32_t open = UI::PanelDialogHandle(delegated.panelDialog);
    if (open == guild.secondWindow.dialog2a0 || open == guild.secondWindow.dialog2a4)
    {
        if (auto cancelled = UI::CancelThePanelDialog(delegated.panelDialog, AnswerOf(world, send));
            !cancelled.HasValue())
            return cancelled;
    }
    return Send(send, Protocol::Guild::WriteMasterAnswer(ClosedMasterAnswer), "0x6570b0");
}

void LendTheGuildTo(UI::PanelContext& context, WorldState& world, const GuildSender& send)
{
    if (!send)
        return;
    context.guildOpened = [send]() { send(Protocol::Guild::WriteMemberListRequest()); };
    context.guildClosed = [&world, send]() { return CloseTheGuildWindow(world, send); };
    context.secondGuildClosed = [&world, send]() { return CloseTheSecondGuildWindow(world, send); };
    context.panelDialogAnswer = AnswerOf(world, send);
}

void LendThePanelDialogStepTo(UI::PanelContext& context, WorldState& world, std::int32_t mouseX, std::int32_t mouseY,
                              std::uint32_t now)
{
    context.panelDialogStep = [&world, &context, mouseX, mouseY, now]() -> Core::Expected<void, std::string>
    {
        WorldRuntime& runtime = world.Runtime();
        const UI::OldWindowGlobals globals = OldWindowGlobalsOf(runtime);
        return UI::StepThePanelDialog(runtime.panels.delegated.panelDialog, globals, mouseX, mouseY, now,
                                      context.panelDialogAnswer);
    };
}
} // namespace Client::Scenes
