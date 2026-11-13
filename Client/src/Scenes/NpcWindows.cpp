#include "Scenes/NpcWindows.h"

#include "Core/Expected.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/Inventory.h"
#include "Protocol/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
namespace Npc = Protocol::NpcWindows;

// 0x41ed10 (CL10): lo que dejan en WorldRuntime::lastSoundId.
constexpr std::int32_t SecondTalkSound = 0x1c;   // 0x4d28a2, después del 0x19
constexpr std::int32_t ItemSound = 0x1d;         // 0x4e3c4e y 0x4e3cb0
constexpr std::int32_t RepairSound = 0x25;       // 0x4e3d1d
constexpr std::int32_t ChaosListSound = 0x44;    // 0x4c5f88, después del 0x57
constexpr std::int32_t ChaosSuccessSound = 0x45; // 0x4da324, después del 0x57
// La tabla de 0x4d2914 (pkt[3] − 2) y el default de 0x4d2881.
constexpr std::uint8_t TalkWarehouse = 2;
constexpr std::uint8_t TalkChaosBox = 3;
constexpr std::uint8_t TalkEventNpc = 4;
constexpr std::uint8_t TalkPanel2000 = 5;
constexpr std::uint8_t TalkSecondEventNpc = 6;
constexpr std::uint8_t TalkTrainer = 7;
constexpr std::uint8_t TalkBloodCastle = 9;
constexpr std::uint8_t TalkCastleSiegeList = 12;
constexpr std::uint8_t TalkCastleSiegeRequest = 13;
constexpr std::int32_t BloodCastleDialog = 0xa1;
// 0x4c5f6b y 0x4c5fb0: las grillas del 31 que no son la de 0x78a7070.
constexpr std::uint8_t ChaosBoxGrid = 3;
constexpr std::uint8_t TrainerGrid = 5;
// Los textos de Text.bmd de los mensajes (0x76bd1dc + 300 · índice) y su tipo de 0x555190.
constexpr std::size_t ChaosBoxNotEmptyText = 593; // 0x76e88c8, 0x65894f
constexpr std::size_t MixFailedText = 594;        // 0x76e89f4, 0x4da2a6
constexpr std::size_t MixSucceededText = 595;     // 0x76e8b20, 0x4da2c7
constexpr std::size_t MixRefusedText = 596;       // 0x76e8c4c, 0x4da333
constexpr std::size_t MixNoticeText = 649;        // 0x76eca68, 0x4da352
constexpr std::size_t SecondMixNoticeText = 689;  // 0x76ef948, 0x4da36c
constexpr std::int32_t ErrorMessage = 2;
constexpr std::int32_t SystemMessage = 1;
// 0x4da38c/0x4da3a8: los resultados del 86.
constexpr std::uint8_t MixFailed = 0;
constexpr std::uint8_t MixRefused = 2;
constexpr std::uint8_t MixNotice = 4;
constexpr std::uint8_t SecondMixNotice = 9;
constexpr std::uint8_t SecondMixRefused = 0xb;
constexpr std::uint8_t TrainerMix = 100;
// 0x7d2dc80.
constexpr std::int32_t MixIdle = 0;
constexpr std::int32_t MixReady = 2;
// 0x4e60dc/0x4e60c0: el 83.
constexpr std::uint8_t WarehouseUnlocked = 0;
constexpr std::uint8_t WarehouseLockedState = 1;
constexpr std::uint8_t WarehouseFirstDialog = 0xa;
constexpr std::uint8_t WarehouseSecondDialog = 0xb;
constexpr std::uint8_t WarehousePasswordAccepted = 0xc;
constexpr std::uint8_t WarehouseThirdDialog = 0xd;
constexpr std::int32_t FirstWarehouseDialog = 0x85;
constexpr std::int32_t SecondWarehouseDialog = 0x86;
constexpr std::int32_t ThirdWarehouseDialog = 0x89;
// 0x4e477c: el lugar de lo que esperaba la contraseña cuando era zen.
constexpr std::int32_t PendingMoney = -1;
// 0x4e4828: el diálogo que el 82 y el 87 cierran, y los campos de Tab que deja (0x4e484c).
constexpr std::int32_t ClosedByTheServerDialog = 0x73;
constexpr std::int32_t ClosedChatFieldCount = 2;
// 0x4d28a9..0x4d28f6: el mundo de 260 sobre 640 y la pantalla en 640 × 480.
constexpr std::int32_t TalkWorldWidth = 260;
constexpr std::int32_t VirtualWidth = 640;
constexpr std::uint32_t VirtualHeight = 480;
// 0x40b0a0: el relleno y el serial de un C3 que arma la UI.
constexpr std::size_t C3SendRandoms = 2;

Core::Expected<void, std::string> Send(const NpcWindowSender& send, Protocol::Outgoing outgoing, std::string_view what)
{
    if (!send)
        return Core::Unexpected(std::format("{} sin la sesión", what));
    return send(std::move(outgoing));
}

// 0x657ab0 vacía las grillas así: el tipo en 0xffff y +0x3c en 0, nada más (0x658545..0x658557, 0x658914..0x658926).
void EmptyTheGrid(std::span<World::ItemState> cells)
{
    for (World::ItemState& cell : cells)
    {
        cell.id = EmptyOwnItemId;
        cell.occupied = 0;
    }
}

// 0x5e3c20(grilla, columnas, filas): 1 si ninguna casilla tiene un tipo y +0x3c mayor que 0.
bool GridIsEmpty(std::span<const World::ItemState> cells)
{
    return std::ranges::none_of(cells, [](const World::ItemState& cell)
                                { return cell.id != EmptyOwnItemId && static_cast<std::int32_t>(cell.occupied) > 0; });
}

Core::Expected<void, std::string> Message(const InventoryServices& services, std::size_t text, std::int32_t type,
                                          std::string_view where)
{
    if (!services.systemMessage)
        return Core::Unexpected(std::format("{}: 0x555190 sin el chat", where));
    return services.systemMessage(text, type);
}

Core::Expected<void, std::string> OpenTheBox(const WorldEntryResources& resources, std::size_t text)
{
    const std::optional<std::string_view> found = UI::TextAt(resources.texts, text);
    if (!found)
        return Core::Unexpected(std::format("0x632e30: sin el texto {} de Text.bmd", text));
    WorldRuntime& runtime = resources.world.Runtime();
    UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, *found);
    return {};
}

InventoryMoveContext MovesOf(const WorldEntryResources& resources, const InventoryServices& services)
{
    return InventoryMoveContext{.world = resources.world, .random = resources.cloth.random, .services = services};
}
} // namespace

void LendTheNpcWindows(GamePanels& panels, const WorldEntryResources& resources)
{
    panels.LendTheNpcWindows(resources.world, resources.cloth.random, InventoryServicesOf(resources),
                             resources.sendNpcWindowPacket);
}

Core::Expected<void, std::string> NpcWindowOpened(const NpcWindowContext& context, UI::PanelId id)
{
    NpcWindowState& npc = context.world.Runtime().npcWindows;
    if (id == UI::WarehousePanel)
    {
        npc.warehouseLocked = 0;
        context.world.Runtime().ui.field78abb20 = 0;
        return {};
    }
    if (id != UI::ChaosBoxPanel)
        return Core::Unexpected(std::format("0x6599a0({:#x}) no es una ventana de NPC", id));
    npc.field7d2dc9d = 0;
    if (auto sent = Send(context.send, Npc::WriteChaosRateRequest(), "0x65a956: el C1 04 BD 09"); !sent.HasValue())
        return sent;
    npc.chaosMixState = MixIdle;
    return {};
}

Core::Expected<bool, std::string> NpcWindowClosing(const NpcWindowContext& context, UI::PanelId id)
{
    WorldRuntime& runtime = context.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    switch (id)
    {
    case UI::WarehousePanel:
    {
        if (runtime.ui.field78abb39 != 0)
            return false;
        EmptyTheGrid(npc.grid);
        if (runtime.cursorItemCount > 0)
        {
            const InventoryMoveContext moves{
                .world = context.world, .random = context.random, .services = context.services};
            if (auto returned = ReturnTheCursorItem(moves); !returned.HasValue())
                return Core::Unexpected(returned.Error());
        }
        if (auto sent = Send(context.send, Npc::WriteWarehouseClose(), "0x658567: el C1 03 82"); !sent.HasValue())
            return Core::Unexpected(sent.Error());
        npc.warehouseLocked = 0;
        return true;
    }
    case UI::ChaosBoxPanel:
    {
        if (!GridIsEmpty(npc.chaosBox) || runtime.cursorItemCount > 0)
        {
            if (auto shown = Message(context.services, ChaosBoxNotEmptyText, ErrorMessage, "0x658959");
                !shown.HasValue())
                return Core::Unexpected(shown.Error());
            return false;
        }
        if (auto sent = Send(context.send, Npc::WriteChaosBoxClose(), "0x658767: el C1 03 87"); !sent.HasValue())
            return Core::Unexpected(sent.Error());
        EmptyTheGrid(npc.chaosBox);
        return true;
    }
    case UI::NpcShopPanel:
    {
        if (auto sent = Send(context.send, Protocol::Connection::WriteWindowClose(), "0x659063: el C1 03 31");
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        EmptyTheGrid(npc.grid);
        return true;
    }
    default:
        return Core::Unexpected(std::format("0x657ab0({:#x}) no es una ventana de NPC", id));
    }
}

CursorWarp NpcTalkCursorWarp(std::int32_t mouseX, std::int32_t mouseY, std::int32_t screenWidth,
                             std::int32_t screenHeight)
{
    // 0x4d28bd..0x4d28f2: el IMUL con signo y el redondeo hacia el 0; los MUL, sin signo.
    const std::int32_t worldX = (mouseX * TalkWorldWidth) / VirtualWidth;
    const auto x = (static_cast<std::uint32_t>(worldX) * static_cast<std::uint32_t>(screenWidth)) /
                   static_cast<std::uint32_t>(VirtualWidth);
    const auto y = (static_cast<std::uint32_t>(screenHeight) * static_cast<std::uint32_t>(mouseY)) / VirtualHeight;
    return CursorWarp{.x = static_cast<std::int32_t>(x), .y = static_cast<std::int32_t>(y)};
}

Core::Expected<void, std::string> ApplyTalkResult(const Npc::TalkResult& talk, const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    GamePanels panels(resources.world, resources.cloth.random,
                      GamePanelTables{.items = resources.items, .itemSetTables = resources.itemSetTables});
    LendTheNpcWindows(panels, resources);
    UI::PanelRegistry& registry = panels.Registry();
    // 0x4d250c..0x4d2512: 0x657a90.
    if (auto closed = registry.CloseAll(); !closed.HasValue())
        return closed;
    const auto open = [&registry](UI::PanelId id) -> Core::Expected<void, std::string>
    {
        if (auto opened = registry.Open(id); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        return {};
    };
    Core::Expected<void, std::string> done{};
    switch (talk.result)
    {
    case TalkWarehouse:
        done = open(UI::WarehousePanel);
        break;
    case TalkChaosBox:
        // 0x4d253b..0x4d2566: abre la caja y copia pkt[4..9] a 0x6c9da8, cada byte en un int.
        done = open(UI::ChaosBoxPanel);
        if (done.HasValue())
            std::ranges::copy(talk.rates, runtime.npcWindows.chaosRates.begin());
        break;
    case TalkEventNpc:
        done = open(UI::EventNpcPanel);
        break;
    case TalkPanel2000:
        done = open(UI::Panel2000);
        break;
    case TalkSecondEventNpc:
        done = open(UI::SecondEventNpcPanel);
        break;
    case TalkTrainer:
        done = open(UI::TrainerPanel);
        break;
    case TalkBloodCastle:
        // 0x4d259d..0x4d25b4: sin abrir nada.
        if (runtime.npcWindows.field6e5798 == 0)
            UI::PushDialog(runtime.dialogs, BloodCastleDialog);
        break;
    case TalkCastleSiegeList:
        return Core::Unexpected(
            std::string("0x4d25bc: [0x7d536fc] + 0x3d4 y la lista de CS (0x100000) son de la CL9.5"));
    case TalkCastleSiegeRequest:
        return Core::Unexpected(std::string("0x4d25dd: el C1 04 B2 00 es de la CL9.5"));
    default:
        // 0x4d2881..0x4d288e: la tienda de NPC (0, 1, 8, 10, 11 y desde 14).
        done = open(UI::NpcShopPanel);
        break;
    }
    if (!done.HasValue())
        return done;
    // 0x4d2893..0x4d28f6: los dos sonidos y el SetCursorPos.
    runtime.lastSoundId = SecondTalkSound;
    runtime.npcWindows.cursorWarpRequested = true;
    return {};
}

Core::Expected<void, std::string> ApplyItemList(const Npc::ItemList& list, const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    if (list.grid == TrainerGrid)
        return Core::Unexpected(
            std::string("0x4c5fb4: el texto 1208, 0x449c40 y 0x44ad70, la caja del entrenador (0x723c80), son de la "
                        "CL9 hito 6, con el NPC (fase 2)"));
    if (list.grid == ChaosBoxGrid)
    {
        npc.chaosMixState = MixReady;
        runtime.lastSoundId = ChaosListSound;
        EmptyTheGrid(npc.chaosBox);
    }
    else
        EmptyTheGrid(npc.grid);
    for (const Npc::ListedItem& entry : list.items)
    {
        const InventoryItemBytes bytes(entry.item);
        const auto placed = list.grid == ChaosBoxGrid
                                ? PlaceInOtherGrid(npc.chaosBox, NpcGridColumns, entry.slot, bytes, resources,
                                                   "0x4c6029, 0x5bc740(0x7d2d380)")
                                : PlaceInTheNpcGrid(npc.grid, entry.slot, bytes, resources);
        if (!placed.HasValue())
            return placed;
    }
    return {};
}

Core::Expected<void, std::string> ApplyBuyResult(const Npc::BuyResult& result, const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    if (result.slot != Npc::BuyRejected)
    {
        const Protocol::Inventory::ItemData& item = result.item;
        if (auto placed = PlaceMovedItem(result.slot, item, resources); !placed.HasValue())
        {
            runtime.npcWindows.buyLock = 0;
            return placed;
        }
        runtime.lastSoundId = ItemSound;
    }
    runtime.npcWindows.buyLock = 0;
    return {};
}

Core::Expected<void, std::string> ApplySellResult(const Npc::SellResult& result, const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    if (result.result == 0)
    {
        const InventoryServices services = InventoryServicesOf(resources);
        return ReturnTheCursorItem(MovesOf(resources, services));
    }
    runtime.cursorItemCount = 0;
    runtime.cursorItem.occupied = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                Render::Models::OwnRecordCloseRandoms);
        resources.world.OwnCharacter().zen = result.money;
    }
    runtime.lastSoundId = ItemSound;
    return {};
}

Core::Expected<void, std::string> ApplyRepairResult(const Npc::RepairResult& result,
                                                    const WorldEntryResources& resources)
{
    if (result.money == 0)
        return {};
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                Render::Models::OwnRecordCloseRandoms);
        OwnCharacterRecord& record = world.OwnCharacter();
        record.zen = result.money;
        auto recalculated = RecalculateOwnStats(record, OwnStatsContext{.itemSetTables = resources.itemSetTables,
                                                                        .itemSets = &world.ItemSets(),
                                                                        .items = resources.items,
                                                                        .publishedHero = world.Actors()[runtime.hero],
                                                                        .ownRecordAccesses = runtime.ownRecordDepth,
                                                                        .random = resources.cloth.random,
                                                                        .held = HeldItemOf(runtime),
                                                                        .scan505820 = runtime.fullSetScan});
        if (!recalculated.HasValue())
            return recalculated;
    }
    runtime.lastSoundId = RepairSound;
    return {};
}

Core::Expected<void, std::string> ApplyWarehouseMoney(const Npc::WarehouseMoney& money,
                                                      const WorldEntryResources& resources)
{
    if (money.result == 0)
        return {};
    WorldRuntime& runtime = resources.world.Runtime();
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Render::Models::OwnRecordCloseRandoms);
    OwnCharacterRecord& record = resources.world.OwnCharacter();
    record.warehouseZen = money.warehouseMoney;
    record.zen = money.inventoryMoney;
    return {};
}

Core::Expected<void, std::string> ApplyNpcWindowClosed(const Npc::NpcWindowClosed&,
                                                       const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    runtime.cursorItemCount = 0;
    runtime.cursorItem.occupied = 0;
    if (runtime.dialogs.current != ClosedByTheServerDialog)
        return {};
    // 0x4e4831..0x4e485d. 0x6c8e78 (0x2a) y 0x77f3b6a (0) son de la entrada sin EDIT y de los diálogos, que MuRet no
    // tiene.
    UI::PushDialog(runtime.dialogs, 0);
    UI::ResetTheTextBuffers(runtime.chat.input, false);
    runtime.chat.input.fieldCount = ClosedChatFieldCount;
    runtime.ui.chatOpen = false;
    return {};
}

Core::Expected<void, std::string> ApplyWarehouseState(const Npc::WarehouseState& state,
                                                      const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    switch (state.state)
    {
    case WarehouseUnlocked:
    case WarehouseLockedState:
        npc.warehouseLocked = state.state;
        npc.field78abb1c = 0;
        return {};
    case WarehouseFirstDialog:
        UI::PushDialog(runtime.dialogs, FirstWarehouseDialog);
        return {};
    case WarehouseSecondDialog:
        UI::PushDialog(runtime.dialogs, SecondWarehouseDialog);
        return {};
    case WarehouseThirdDialog:
        UI::PushDialog(runtime.dialogs, ThirdWarehouseDialog);
        return {};
    case WarehousePasswordAccepted:
        break;
    default:
        return {};
    }
    // 0x4e4765..0x4e47d5: con la traba y sin 0x78abb1c, lo que esperaba; siempre, los dos en 1.
    if (npc.warehouseLocked != 0 && npc.field78abb1c == 0)
    {
        const PendingWarehouseMove& pending = npc.pendingMove;
        if (pending.slot != PendingMoney)
            return Core::Unexpected(std::format("0x4e47c6: 0x449d80, el ítem del lugar {} que esperaba la contraseña "
                                                "del almacén, viene de soltarlo en el almacén (CL9 hito 6, fase 2)",
                                                pending.slot));
        // 0x4e6f80(tipo, zen), con el XOR32 y en claro.
        if (auto sent = Send(resources.sendNpcWindowPacket,
                             Npc::WriteWarehouseMoney(static_cast<std::uint8_t>(pending.second & 0xff),
                                                      static_cast<std::uint32_t>(pending.third)),
                             "0x4e478e: el C1 08 81");
            !sent.HasValue())
            return sent;
    }
    npc.warehouseLocked = 1;
    npc.field78abb1c = 1;
    return {};
}

Core::Expected<void, std::string> ApplyChaosMixResult(const Npc::ChaosMixResult& result,
                                                      const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    const InventoryServices services = InventoryServicesOf(resources);
    switch (result.result)
    {
    case MixFailed:
        npc.chaosMixState = MixReady;
        return Message(services, MixFailedText, ErrorMessage, "0x4da2ba");
    case Npc::ChaosMixSucceeded:
    {
        npc.chaosMixState = MixReady;
        if (auto shown = Message(services, MixSucceededText, SystemMessage, "0x4da2db"); !shown.HasValue())
            return shown;
        EmptyTheGrid(npc.chaosBox);
        const InventoryItemBytes bytes(result.item);
        if (auto placed =
                PlaceInOtherGrid(npc.chaosBox, NpcGridColumns, 0, bytes, resources, "0x4da310, 0x5bc740(0x7d2d380)");
            !placed.HasValue())
            return placed;
        runtime.lastSoundId = ChaosSuccessSound;
        return {};
    }
    case MixRefused:
    case SecondMixRefused:
        npc.chaosMixState = MixIdle;
        return Message(services, MixRefusedText, ErrorMessage, "0x4da347");
    case MixNotice:
    case SecondMixNotice:
    {
        auto opened = OpenTheBox(resources, result.result == MixNotice ? MixNoticeText : SecondMixNoticeText);
        npc.chaosMixState = MixReady;
        return opened;
    }
    case TrainerMix:
        return Core::Unexpected(std::string(
            "0x4da373: 0x449c40 y 0x44ad70, la caja del entrenador (0x723c80), son de la CL9 hito 6, con el "
            "NPC (fase 2)"));
    default:
        npc.chaosMixState = MixReady;
        return {};
    }
}

Core::Expected<void, std::string> SendRepair(Render::Models::CrtRandom& random, const NpcWindowSender& send,
                                             std::uint8_t position, std::uint8_t requestPosition)
{
    random.Discard(C3SendRandoms);
    return Send(send, Npc::WriteRepair(position, requestPosition), "0x5c4485: el C1 05 34");
}
} // namespace Client::Scenes
