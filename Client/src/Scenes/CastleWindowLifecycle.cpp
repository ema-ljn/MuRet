#include "Scenes/CastleSiegeReceive.h"
#include "Protocol/Outgoing.h"
#include "Scenes/CastleWindows.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "App/FaithfulPacer.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Scenes/WorldEntry.h"
#include "Protocol/CastleSiege.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/NpcWindows.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/GuildPanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/CastleSiegeState.h"
#include "UI/GameText.h"
#include "UI/PanelDialog.h"
#include "UI/PanelRegistry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t GuildMaster = 0x80;
constexpr std::int32_t WindowCloseSound = 0x1c;
constexpr std::size_t BusySeniorText = (0x77354e8 - 0x76bd1dc) / 300;

Core::Expected<void, std::string> Warn(WorldRuntime& runtime, CastleWindowServices services, std::size_t id)
{
    const auto text = UI::TextAt(services.texts, id);
    if (!text || services.measure == nullptr)
        return Core::Unexpected(std::string("0x555190: el aviso del castillo sin Text.bmd o medidor"));
    return AddGameMessage(runtime, *services.measure, services.screen, {}, *text, 2);
}

Core::Expected<void, std::string> CancelOwnedDialog(WorldState& world, const CastleSender& send, std::uint32_t& owned)
{
    auto& dialog = world.Runtime().panels.delegated.panelDialog;
    if (UI::PanelDialogHandle(dialog) != owned)
        return {};
    auto cancelled = UI::CancelThePanelDialog(dialog, [&world, &send](UI::PanelDialogOwner owner, std::uint32_t reason)
                                              { return AnswerThePanelDialog(world, send, owner, reason); });
    if (!cancelled.HasValue())
        return cancelled;
    owned = 0;
    return {};
}

Core::Expected<bool, std::string> CloseSenior(UI::PanelContext& panels, WorldState& world, const CastleSender& send,
                                              CastleWindowServices services)
{
    auto& runtime = world.Runtime();
    auto& delegated = runtime.panels.delegated;
    if (delegated.castleSiegeList == 0)
        return true;
    if (CastleSeniorBusy(world))
    {
        if (auto warning = Warn(runtime, services, BusySeniorText); !warning.HasValue())
            return Core::Unexpected(std::move(warning).Error());
        return false;
    }
    if (!send)
        return Core::Unexpected(std::string("0x661bc0: cerrar Senior sin el cable"));
    // 0x661bc0: apagar Senior antes de cerrar su auxiliar; el modo de la pestaña no se reinicia.
    delegated.castleSiegeList = 0;
    UI::PanelRegistry registry(runtime.panels, panels);
    if (registry.IsOpen(UI::InventoryPanel))
    {
        if (auto inventory = registry.Close(UI::InventoryPanel); !inventory.HasValue())
            return Core::Unexpected(std::move(inventory).Error());
    }
    auto& window = runtime.castleSiege.senior;
    send(window.closeWith87 ? Protocol::NpcWindows::WriteChaosBoxClose() : Protocol::Connection::WriteWindowClose());
    runtime.RequestSound(WindowCloseSound);
    if (auto cancelled = CancelOwnedDialog(world, send, window.dialogId); !cancelled.HasValue())
        return Core::Unexpected(std::move(cancelled).Error());
    return true;
}

Core::Expected<bool, std::string> CloseHunt(WorldState& world, const CastleSender& send)
{
    auto& runtime = world.Runtime();
    if (runtime.panels.delegated.secondCastleSiege == 0)
        return true;
    if (!send)
        return Core::Unexpected(std::string("0x6487d0: cerrar HuntZone sin el cable"));
    send(Protocol::Connection::WriteWindowClose());
    runtime.panels.delegated.secondCastleSiege = 0;
    if (auto cancelled = CancelOwnedDialog(world, send, runtime.castleSiege.huntZone.dialogId); !cancelled.HasValue())
        return Core::Unexpected(std::move(cancelled).Error());
    return true;
}
} // namespace

bool CastleSeniorBusy(const WorldState& world)
{
    // 0x661b90: exactamente los 32 lugares de 0x7d2d380..0x7d2dc80, por type WORD, no por occupied.
    return world.Runtime().cursorItemCount > 0 ||
           std::ranges::any_of(world.Runtime().npcWindows.chaosBox, [](const auto& item) { return item.id != 0xffff; });
}

bool HeroCanCraftAtTheCastle(const WorldState& world)
{
    const auto& hero = world.Actors()[world.Runtime().hero];
    return static_cast<std::int16_t>(hero.word1f2) >= 0 && hero.guildStatus == GuildMaster;
}

void LendTheCastleLifecycleTo(UI::PanelContext& panels, WorldState& world, Crt::CrtRandom& /*random*/,
                              const CastleSender& send, CastleWindowServices services)
{
    panels.castleWindowOpened = [&world, send](UI::PanelId id) -> Core::Expected<void, std::string>
    {
        auto& delegated = world.Runtime().panels.delegated;
        if (id == UI::SecondCastleSiegePanel)
        {
            delegated.secondCastleSiege = 1; // 0x6487c0: sin consulta ni reinicio del modo.
            return {};
        }
        if (delegated.castleSiegeList != 0)
            return {};
        if (!send)
            return Core::Unexpected(std::string("0x661310: abrir Senior sin el cable"));
        // 0x661310: estos dos campos se escriben ANTES de los tres envíos.
        delegated.castleSiegeList = 1;
        delegated.castleSiegeListMode = 0;
        send(Protocol::CastleSiege::WriteNpcListRequest(1));
        send(Protocol::CastleSiege::WriteNpcListRequest(2));
        send(Protocol::CastleSiege::WriteTaxInfoRequest());
        return {};
    };
    panels.castleWindowClosing = [&panels, &world, send, services](UI::PanelId id)
    { return id == UI::SecondCastleSiegePanel ? CloseHunt(world, send) : CloseSenior(panels, world, send, services); };
    panels.castleSeniorBlocked = [&world, services]() { return Warn(world.Runtime(), services, BusySeniorText); };
}

void LendTheCastlePanelServices(GamePanels& panels, WorldState& world, Crt::CrtRandom& random, const CastleSender& send,
                                const WorldEntryResources* entryResources, CastleWindowServices services)
{
    panels.LendTheSession(world, random, send, {});
    if (send)
        panels.LendTheWindowClose([send]() { send(Protocol::Connection::WriteWindowClose()); });
    panels.LendTheCastleRequests(world, send, services);
    if (entryResources == nullptr)
        return;
    NpcWindowSender sender = [send](Protocol::Outgoing packet) -> Core::Expected<void, std::string>
    {
        if (!send)
            return Core::Unexpected(std::string("la ventana del NPC se cierra sin el cable"));
        send(std::move(packet));
        return {};
    };
    panels.LendTheNpcWindows(world, random, InventoryServicesOf(*entryResources), std::move(sender));
}

void LendTheCastlePanelServices(GamePanels& panels, const CastleReceiveContext& context)
{
    LendTheCastlePanelServices(
        panels, context.world, context.random, context.send, context.entryResources,
        {.measure = context.measure, .texts = context.texts, .screen = context.screen, .now = context.tick});
}

void LendTheCastlePanelServices(GamePanels& panels, const WorldEntryResources& resources, const CastleSender& send)
{
    LendTheCastlePanelServices(panels, resources.world, resources.cloth.random, send ? send : resources.sendGamePacket,
                               &resources,
                               {.measure = resources.textMeasure,
                                .texts = resources.texts,
                                .screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480},
                                .now = resources.clock != nullptr ? App::TickCountMs(resources.clock->Now()) : 0u});
    // La sesión puede prestar estos dos canales por separado; el de NPC conserva su Expected si send falla.
    if (resources.sendNpcWindowPacket)
        LendTheNpcWindows(panels, resources);
    if (resources.sendWindowClose)
        panels.LendTheWindowClose(resources.sendWindowClose);
}

void LendTheCastleWindowStepsTo(UI::PanelContext& panels, const GameUiContext& context)
{
    if (context.requests != nullptr)
        LendTheCastleLifecycleTo(
            panels, context.world, context.random,
            [requests = context.requests](Protocol::Outgoing packet) { requests->SendGamePacket(std::move(packet)); },
            {.measure = context.measure, .texts = context.texts, .screen = context.screen, .now = context.systemTime});
    panels.castleWindowStep = [context](UI::PanelId id) { return StepTheCastleWindow(context, id); };
}
} // namespace Client::Scenes
