#include "Ui/PanelRegistry.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Maps/MapId.h"
#include "Models/LogicalAccess.h"
#include "Ui/Dialogs.h"
#include "Ui/PanelDialog.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <array>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>

namespace Client::Ui
{
namespace
{
// 0x657ab0: the fixed order of the checks (0x657af0..0x659236).
constexpr std::array<PanelId, 26> CloseOrder{CharacterPanel,       PartyPanel,
                                             QuestPanel,           GuildPanel,
                                             SecondGuildPanel,     CastleSiegePanel,
                                             CastleSiegeListPanel, SecondCastleSiegePanel,
                                             SiegeWeaponPanel,     SecondSiegeWeaponPanel,
                                             SiegeGatePanel,       TradePanel,
                                             FriendsPanel,         MovePanel,
                                             WarehousePanel,       ChaosBoxPanel,
                                             CommandsPanel,        PetPanel,
                                             PersonalShopPanel,    OtherShopPanel,
                                             EventNpcPanel,        Panel2000,
                                             SecondEventNpcPanel,  TrainerPanel,
                                             NpcShopPanel,         InventoryPanel};

// 0x65cae0 with 0x4000000: what it closes before the inventory (0x65cbd5).
constexpr PanelId ClosedWithTheInventory = 0x11d9e0;
// 0x65cae0 with 0x2000000 (0x65cba4).
constexpr PanelId ClosedWithTheCharacter = PetPanel | PersonalShopPanel;
// 0x659fa3.. and 0x65c009: the mode of the CS list and of the trainer that go with the inventory.
constexpr std::int32_t InventoryCompanionMode = 3;
// 0x659860: the quest at 1 lets nothing open.
constexpr std::uint8_t BlockingQuestState = 1;
// 0x6598d4..0x6598e4: the friends and the commands do not open in Chaos Castle (18 to 23).
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
// 0x65ca72..0x65ca88 and 0x65cc2c..0x65cc3a: the two sounds of opening and closing.
constexpr Audio::SoundId FirstPanelSound = Sound::ButtonClickSound;
constexpr Audio::SoundId SecondPanelSound = Sound::CreateWindowSound;
// 0x420cc0: the value of 0x723911 with the pet panel open.
constexpr std::uint8_t PetPanelOpen = 2;
// 0x657880
constexpr std::int32_t EscapeKey = 0x1b;
// 0x659290(0) and 0x657880: the bits they walk, from 1 to 0x40000000.
constexpr PanelId LastScannedBit = 0x40000000;

// A read of a protected byte: opens the entry, reads the value and closes it (0x6594a3..0x6595cf with 0x10000 and
// 0x6597f9..0x659847 with 0x20). If the count ends at 0, it draws a rand().
bool ReadProtected(const std::uint8_t& value, std::uint8_t& depth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, 1);
    return value != 0;
}

// A write of a protected byte (0x65ba67..0x65bd57 opens the shop, 0x658e28..0x658fd7 closes it): the same access,
// with its rand() on closing it.
void WriteProtected(std::uint8_t& value, std::uint8_t& depth, Crt::CrtRandom& random, std::uint8_t written)
{
    const Models::LogicalAccess access(depth, random, 1);
    value = written;
}

// 0x6236f0(0x8e) on opening the chaos box (0x65aaf2) and its return to 0 on closing it (0x658928..0x658934).
constexpr std::int32_t ChaosBoxDialog = 0x8e;

// The wave of each panel milestone 1 neither opens nor closes (06 § HUD y ventanas, "Los paneles").
const char* WaveOf(PanelId id)
{
    switch (id)
    {
    case FriendsPanel:
        return "la ventana de amigos, sin el paso de las ventanas viejas (CL9 hito 9)";
    case GuildPanel:
    case SecondGuildPanel:
        return "la CL9.4 (hito 8, las ventanas del gremio)";
    case TradePanel:
        return "la CL9.4";
    case WarehousePanel:
    case ChaosBoxPanel:
    case TrainerPanel:
    case NpcShopPanel:
        return "la CL9.3";
    case EventNpcPanel:
    case SecondEventNpcPanel:
    case CastleSiegePanel:
    case CastleSiegeListPanel:
    case SecondCastleSiegePanel:
        return "la CL9.5";
    default:
        return "la CL9";
    }
}

std::string OpenCut(PanelId id)
{
    return std::format("0x6599a0({:#x}): abrir este panel es de {}", id, WaveOf(id));
}

std::string CloseCut(PanelId id)
{
    return std::format("0x657ab0({:#x}): cerrar este panel es de {}", id, WaveOf(id));
}
} // namespace

bool PanelRegistry::IsOpen(PanelId id)
{
    if (id == AnyPanel)
    {
        for (PanelId bit = 1; bit <= LastScannedBit; bit <<= 1)
        {
            if (IsOpen(bit))
                return true;
        }
        return false;
    }
    const DelegatedPanels& delegated = m_state.delegated;
    switch (id)
    {
    case FriendsPanel:
        return m_context.friendsShown ? m_context.friendsShown() : delegated.friends != 0;
    case MovePanel:
        return m_state.overlayMode == MoveOverlayMode;
    case PartyPanel:
        return m_state.party != 0;
    case QuestPanel:
        return delegated.quest != 0;
    case GuildPanel:
        return delegated.guild != 0;
    case TradePanel:
        return ReadProtected(m_state.trade, m_state.tradeDepth, m_context.random);
    case WarehousePanel:
        return m_state.warehouse != 0;
    case ChaosBoxPanel:
        return m_state.chaosBox != 0;
    case CommandsPanel:
        return delegated.commands != 0;
    case PetPanel:
        // 0x420cc0: only 2 (0x420cc0 compares with 2).
        return delegated.pet == PetPanelOpen;
    case PersonalShopPanel:
    case OtherShopPanel:
        return m_state.personalShop != 0;
    case EventNpcPanel:
        return m_state.eventNpcKind == 0 && m_state.eventNpc != 0;
    case Panel2000:
        return m_state.panel2000 != 0;
    case SecondEventNpcPanel:
        return m_state.eventNpcKind == 1 && m_state.eventNpc != 0;
    case TrainerPanel:
        return delegated.trainer > 0;
    case NpcShopPanel:
        return ReadProtected(m_state.npcShop, m_state.npcShopDepth, m_context.random);
    case SecondGuildPanel:
        return delegated.secondGuild != 0;
    case CastleSiegePanel:
        return delegated.castleSiege != 0;
    case CastleSiegeListPanel:
        return delegated.castleSiegeList != 0;
    case SecondCastleSiegePanel:
        return delegated.secondCastleSiege != 0;
    case SiegeWeaponPanel:
        return delegated.siegeWeapon == 1;
    case SecondSiegeWeaponPanel:
        return delegated.siegeWeapon == 2;
    case SiegeGatePanel:
        return delegated.siegeGate != 0;
    case CharacterPanel:
        return m_state.character != 0;
    case InventoryPanel:
        return m_state.inventory != 0;
    default:
        return false;
    }
}

bool PanelRegistry::CanOpen(PanelId id)
{
    if (IsOpen(NpcShopPanel) || IsOpen(TradePanel) || IsOpen(WarehousePanel) || IsOpen(ChaosBoxPanel) ||
        IsOpen(SecondGuildPanel) || IsOpen(Panel2000) || IsOpen(PersonalShopPanel) || IsOpen(OtherShopPanel))
        return false;
    const std::int32_t dialog = m_context.dialogs.current;
    if (dialog != PanelFriendlyDialog && dialog != 0)
        return false;
    if (m_state.field78abb00 != 0 || m_state.delegated.quest == BlockingQuestState)
        return false;
    // 0x661b90: an item on the cursor or in the chaos box.
    const bool busy = m_context.inventoryBusy ? m_context.inventoryBusy()
                                              : m_context.cursorItemCount > 0 || m_state.delegated.chaosBoxHasItem;
    if (IsOpen(CastleSiegeListPanel) && busy)
        return false;
    if (id == PetPanel)
        return IsOpen(CharacterPanel);
    if ((id == FriendsPanel || id == CommandsPanel) && m_context.map >= FirstChaosCastleMap &&
        m_context.map <= LastChaosCastleMap)
        return false;
    if (id == PersonalShopPanel)
        return IsOpen(InventoryPanel);
    return true;
}

Core::Expected<bool, std::string> PanelRegistry::Open(PanelId id, std::uint32_t auxiliary)
{
    if (IsOpen(id) || !CanOpen(id) || m_context.logoutRequested)
        return false;
    if (m_context.dialogs.current != PanelFriendlyDialog)
        PushDialog(m_context.dialogs, 0);
    // 0x659a3f: all but the id and the friends.
    PanelId mask = ~id ^ FriendsPanel;
    switch (id)
    {
    case MovePanel:
        // 0x659f57: without closing anything.
        m_state.overlayMode = MoveOverlayMode;
        break;
    case InventoryPanel:
    {
        // 0x65bfe0..0x65c01d.
        mask ^= CharacterPanel;
        if (m_state.delegated.trainer == InventoryCompanionMode)
            mask ^= TrainerPanel;
        if (m_state.delegated.castleSiegeListMode == InventoryCompanionMode)
            mask ^= CastleSiegeListPanel;
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            // 0x65c020..0x65c033: 0x7d53708 to 1 and, with a Dark Lord, the raven and horse requests.
            m_state.inventory = 1;
            // 0x65c039: builds the C1 A9 that asks for the pet; the plan puts the A9 in milestone 4 (CL9.2, 08 P-C195).
            if (m_context.heroClass == Kernel::CharacterClass::DarkLord)
            {
                if (!m_context.petRequests)
                    return Core::Unexpected(
                        std::string("0x65c039: abrir el inventario de un Dark Lord pide el cuervo y "
                                    "el caballo (C1 A9) sin el cable"));
                m_context.petRequests();
            }
        }
        break;
    }
    case PartyPanel:
    {
        // 0x659a82..0x659c44: with what it closed, the count to 0, the list request and the party.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.partyOpened)
                return Core::Unexpected(std::string("0x659aa0: abrir la party pide la lista (C1 03 42) sin el cable"));
            m_context.partyOpened();
            m_state.party = 1;
        }
        break;
    }
    case CommandsPanel:
    {
        // 0x65a91c..0x65a934: with what it closed, [0x6e592c]'s +4.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
            m_state.delegated.commands = 1;
        break;
    }
    case WarehousePanel:
    case ChaosBoxPanel:
    case NpcShopPanel:
    {
        if (auto opened = OpenTheNpcWindow(id, mask); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        break;
    }
    case TrainerPanel:
    {
        // 0x65be77..0x65be93: with what it closed, 0x449940(1), without opening the inventory.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.npcWindowOpened)
                return Core::Unexpected(std::string("0x65be8b: abrir el entrenador sin el mundo (0x449940)"));
            if (auto opened = m_context.npcWindowOpened(id); !opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
        }
        break;
    }
    case GuildPanel:
    {
        // 0x659c7a..0x659e41: with what it closed, the C1 03 52 and 0x6522d0.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.guildOpened)
                return Core::Unexpected(
                    std::string("0x6599a0(0x10): abrir el gremio pide la lista (C1 03 52) sin el cable"));
            m_context.guildOpened();
            m_state.delegated.guild = 1;
        }
        break;
    }
    case SecondGuildPanel:
    {
        // 0x65be98..0x65beb5: with what it closed, 0x657090.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
            m_state.delegated.secondGuild = 1;
        break;
    }
    case FriendsPanel:
        // 0x659f3d: without closing anything, 0x491080(0x186, 0x106) and the sounds.
        if (!m_context.friendsOpened)
            return Core::Unexpected(OpenCut(id));
        if (auto opened = m_context.friendsOpened(); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        break;
    case TradePanel:
    {
        // 0x659e46..0x659f38: with what it closed, the inventory (0x6599a0(0x4000000, 0), with its sounds) and the
        // protected byte 0x7d53711 to 1: opening the entry, writing and closing it (a rand() if it is left with no
        // accesses).
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            auto inventory = Open(InventoryPanel);
            if (!inventory.HasValue())
                return inventory;
            const Models::LogicalAccess access(m_state.tradeDepth, m_context.random, 1);
            m_state.trade = 1;
        }
        break;
    }
    case PersonalShopPanel:
    case OtherShopPanel:
    {
        // 0x65ac3f..0x65b227 (0x800, which leaves the inventory open) and 0x65b319..0x65ba41 (0x20000, which closes it
        // with the rest): with what it closed, the inventory (0x6599a0(0x4000000, 0)), the shop's part and 0x7d5370f
        // to 1.
        if (id == PersonalShopPanel)
            mask ^= InventoryPanel;
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            auto inventory = Open(InventoryPanel);
            if (!inventory.HasValue())
                return inventory;
            if (!m_context.personalShopOpened)
                return Core::Unexpected(std::format("{:#x}: abrir la tienda personal sin sus mapas", id));
            if (auto opened = m_context.personalShopOpened(id); !opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
            m_state.personalShop = 1;
        }
        break;
    }
    case QuestPanel:
    {
        // 0x659c50..0x659c70: all but the character and the friends; with what it closed, 0x419840(−1).
        mask ^= CharacterPanel;
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.questOpened)
                return Core::Unexpected(std::string("0x419840: abrir las misiones sin [0x710148]"));
            if (auto opened = m_context.questOpened(); !opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
        }
        break;
    }
    case Panel2000:
    {
        // 0x65b240..0x65b259: the mask closes before this window's two bytes are set.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            m_state.panel2000 = 1;
            m_state.field78abb08 = 0;
        }
        break;
    }
    case EventNpcPanel:
    case SecondEventNpcPanel:
    {
        // 0x65b265/0x65b29b: the inventory opens before the byte 0x7d53712 and the type 0x7d53714.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (auto inventory = Open(InventoryPanel); !inventory.HasValue())
                return inventory;
            m_state.eventNpc = 1;
            m_state.eventNpcKind = id == EventNpcPanel ? 0 : 1;
        }
        break;
    }
    case CastleSiegeListPanel:
    case SecondCastleSiegePanel:
    {
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.castleWindowOpened)
                return Core::Unexpected(OpenCut(id));
            if (auto opened = m_context.castleWindowOpened(id); !opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
        }
        break;
    }
    case SiegeWeaponPanel:
    case SecondSiegeWeaponPanel:
    case SiegeGatePanel:
    {
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.siegeControlOpened)
                return Core::Unexpected(OpenCut(id));
            if (auto opened = m_context.siegeControlOpened(id, auxiliary); !opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
        }
        break;
    }
    case CastleSiegePanel:
    {
        // 0x65bef0: the same mask of all but the id and the friends, with aux at 0.
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            if (!m_context.castleRegistrationOpened)
                return Core::Unexpected(OpenCut(id));
            if (auto opened = m_context.castleRegistrationOpened(); !opened.HasValue())
                return Core::Unexpected(std::move(opened).Error());
        }
        break;
    }
    case PetPanel:
    {
        // 0x659fd9..0x65a8af: all but the character and the friends; with what it closed, 0x420cb0 (0x723911 to 2) and
        // the raven and horse requests.
        mask ^= CharacterPanel;
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            m_state.delegated.pet = PetPanelOpen;
            if (!m_context.petRequests)
                return Core::Unexpected(std::string("0x659ff6: abrir la mascota pide el cuervo y el caballo (C1 A9) "
                                                    "sin el cable"));
            m_context.petRequests();
        }
        break;
    }
    case CharacterPanel:
    {
        // 0x65c94b..0x65ca72: all but the inventory and the friends; with what it closed, the character.
        mask ^= InventoryPanel;
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
            m_state.character = 1;
        if (!m_context.characterOpened)
            return Core::Unexpected(std::string("0x65c966: abrir el personaje sin el mundo ([0x6eed3c] y 0x550d10)"));
        if (auto opened = m_context.characterOpened(); !opened.HasValue())
            return Core::Unexpected(std::move(opened).Error());
        break;
    }
    default:
        // 0x65bfcb: an id without a case exits with 0 and without sounds.
        return false;
    }
    PlayTheSounds();
    return true;
}

Core::Expected<bool, std::string> PanelRegistry::Close(PanelId id)
{
    return Close(id, false);
}

Core::Expected<bool, std::string> PanelRegistry::Close(PanelId id, bool aux)
{
    if (!IsOpen(id))
        return false;
    switch (id)
    {
    case NpcShopPanel:
    case CastleSiegePanel:
    case TrainerPanel:
    case ClosedPanel:
    case TradePanel:
    case WarehousePanel:
    case ChaosBoxPanel:
    case EventNpcPanel:
    {
        // 0x65cb5b: the panel and, if it closed, the inventory, both with `aux`.
        auto closed = CloseByMask(id, aux);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            auto inventory = CloseByMask(InventoryPanel, aux);
            if (!inventory.HasValue())
                return inventory;
        }
        break;
    }
    case CastleSiegeListPanel:
    {
        // 0x65cb7a..0x65cb9d: 0x661b90 (cursor or box), notice 1641 and the wrapper's sounds even if it does not close.
        const bool busy = m_context.inventoryBusy ? m_context.inventoryBusy()
                                                  : m_context.cursorItemCount > 0 || m_state.delegated.chaosBoxHasItem;
        if (busy)
        {
            if (!m_context.castleSeniorBlocked)
                return Core::Unexpected(CloseCut(id));
            if (auto warning = m_context.castleSeniorBlocked(); !warning.HasValue())
                return Core::Unexpected(std::move(warning).Error());
            break;
        }
        auto closed = CloseByMask(id, aux);
        if (!closed.HasValue())
            return closed;
        if (closed.Value() && m_state.delegated.castleSiegeListMode == InventoryCompanionMode)
        {
            auto inventory = CloseByMask(InventoryPanel, aux);
            if (!inventory.HasValue())
                return inventory;
        }
        break;
    }
    case CharacterPanel:
    {
        auto companions = CloseByMask(ClosedWithTheCharacter);
        if (!companions.HasValue())
            return companions;
        if (companions.Value())
        {
            auto character = CloseByMask(CharacterPanel);
            if (!character.HasValue())
                return character;
        }
        break;
    }
    case InventoryPanel:
    {
        // 0x65cbbb: with an item on the cursor or in the chaos box, only the sounds.
        if (m_context.inventoryBusy ? m_context.inventoryBusy()
                                    : m_context.cursorItemCount > 0 || m_state.delegated.chaosBoxHasItem)
            break;
        auto companions = CloseByMask(ClosedWithTheInventory);
        if (!companions.HasValue())
            return companions;
        if (companions.Value())
        {
            auto inventory = CloseByMask(InventoryPanel);
            if (!inventory.HasValue())
                return inventory;
        }
        break;
    }
    default:
    {
        auto closed = CloseByMask(id);
        if (!closed.HasValue())
            return closed;
        break;
    }
    }
    PlayTheSounds();
    return true;
}

Core::Expected<bool, std::string> PanelRegistry::CloseByMask(PanelId mask)
{
    return CloseByMask(mask, false);
}

Core::Expected<bool, std::string> PanelRegistry::CloseByMask(PanelId mask, bool aux)
{
    // [EBP−0xd] starts at 1 and only the warehouse and the chaos box that do not close (0x658534, 0x658961) and the
    // trainer with something in its box (0x658df7) clear it; the panels after them close anyway.
    bool closedAll = true;
    for (const PanelId id : CloseOrder)
    {
        if ((mask & id) == 0 || !IsOpen(id))
            continue;
        switch (id)
        {
        case WarehousePanel:
        case ChaosBoxPanel:
        case TrainerPanel:
        case NpcShopPanel:
        {
            auto closed = CloseTheNpcWindow(id);
            if (!closed.HasValue())
                return closed;
            closedAll = closedAll && closed.Value();
            break;
        }
        case CharacterPanel:
            m_state.character = 0;
            break;
        case PartyPanel:
            m_state.party = 0;
            break;
        case MovePanel:
            m_state.overlayMode = 0;
            break;
        case TradePanel:
        {
            // 0x657fb5..0x6584cb: the 3D and the grids, and the protected byte to 0 (a rand() on closing the entry).
            if (!m_context.tradeClosed)
                return Core::Unexpected(std::string("0x657fb5: cerrar el comercio sin sus grillas (0x78a5a20)"));
            if (auto closed = m_context.tradeClosed(aux); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            const Models::LogicalAccess access(m_state.tradeDepth, m_context.random, 1);
            m_state.trade = 0;
            break;
        }
        case CommandsPanel:
            ResetTheCommands(m_state.delegated);
            break;
        case GuildPanel:
        case SecondGuildPanel:
        {
            // 0x657b50 and 0x657b73: vtable + 0x2c of [0x7d536f0] (0x6522f0) and of [0x7d536f4] (0x6570b0), without
            // checking anything. MuRet bails out without the lent close (Hud::GamePanels::LendTheGuild or
            // LendTheSession).
            const auto& closer = id == GuildPanel ? m_context.guildClosed : m_context.secondGuildClosed;
            if (!closer)
                return Core::Unexpected(CloseCut(id));
            if (auto window = closer(); !window.HasValue())
                return Core::Unexpected(std::move(window).Error());
            break;
        }
        case PersonalShopPanel:
        case OtherShopPanel:
        {
            // 0x6589b6..0x6589f4: 0x5eb020 and 0x7d5370f to 0. Both share the byte: the second one is no longer open.
            if (!m_context.personalShopClosed)
                return Core::Unexpected(std::string("0x6589cb: cerrar la tienda personal sin 0x5eb020"));
            if (auto closed = m_context.personalShopClosed(); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            m_state.personalShop = 0;
            break;
        }
        case PetPanel:
            // 0x6589b1: 0x420ca0, 0x723911 to 0.
            m_state.delegated.pet = 0;
            break;
        case QuestPanel:
            // 0x657b2b: 0x419900, the window to 0 and the C1 03 31.
            if (!m_context.questClosed)
                return Core::Unexpected(std::string("0x419900: cerrar las misiones manda el C1 03 31 sin el cable"));
            m_state.delegated.quest = 0;
            m_context.questClosed();
            break;
        case FriendsPanel:
            // 0x6584e5: 0x4914e0.
            if (!m_context.friendsClosed)
                return Core::Unexpected(CloseCut(id));
            if (auto closed = m_context.friendsClosed(); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            break;
        case InventoryPanel:
            // 0x659250..0x65926a.
            m_state.inventory = 0;
            m_state.field78abb09 = 0;
            m_state.repairMode = 0;
            m_state.field78abb00 = 0;
            m_state.field78abb04 = 0;
            break;
        case Panel2000:
            // 0x658be3..0x658bea: nothing sent; 0x5e74f0's buttons send the C1 03 31 before CloseAll.
            m_state.panel2000 = 0;
            m_state.field78abb08 = 0;
            break;
        case EventNpcPanel:
        case SecondEventNpcPanel:
            if (!m_context.eventNpcClosed)
                return Core::Unexpected(CloseCut(id));
            m_context.eventNpcClosed();
            m_state.eventNpc = 0;
            break;
        case SiegeWeaponPanel:
        case SecondSiegeWeaponPanel:
        case SiegeGatePanel:
            if (!m_context.siegeControlClosed)
                return Core::Unexpected(CloseCut(id));
            if (auto closed = m_context.siegeControlClosed(id); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            break;
        case CastleSiegePanel:
            if (!m_context.castleRegistrationClosed)
                return Core::Unexpected(CloseCut(id));
            if (auto closed = m_context.castleRegistrationClosed(); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            break;
        case CastleSiegeListPanel:
        case SecondCastleSiegePanel:
        {
            if (!m_context.castleWindowClosing)
                return Core::Unexpected(CloseCut(id));
            auto closed = m_context.castleWindowClosing(id);
            if (!closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            // 0x657bb7/0x657bda are void calls: a busy Senior does not clear the registry's return flag.
            break;
        }
        default:
            return Core::Unexpected(CloseCut(id));
        }
    }
    return closedAll;
}

Core::Expected<void, std::string> PanelRegistry::OpenTheNpcWindow(PanelId id, PanelId mask)
{
    // 0x659f66, 0x65a93d and 0x65ba46: with the rest closed, it goes on; otherwise, only the sounds.
    auto closed = CloseByMask(mask);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    if (!closed.Value())
        return {};
    // The wiring goes before opening the inventory because the C1 04 BD 09 goes first (0x65a956..0x65ac07). The
    // original writes 0x6c932f, 0x78abb20 (0x659f8d..0x659f94) and 0x7d2dc80 (0x65ac28) after, but 0x6599a0(0x4000000)
    // does not read them.
    if (id != NpcShopPanel)
    {
        if (!m_context.npcWindowOpened)
            return Core::Unexpected(std::format("0x6599a0({:#x}): abrir la ventana del NPC sin el mundo", id));
        if (auto opened = m_context.npcWindowOpened(id); !opened.HasValue())
            return opened;
    }
    // 0x659f81, 0x65aac1.. and 0x65ba62: without checking whether it opened.
    if (auto inventory = Open(InventoryPanel); !inventory.HasValue())
        return Core::Unexpected(std::move(inventory).Error());
    switch (id)
    {
    case WarehousePanel:
        m_state.warehouse = 1;
        break;
    case ChaosBoxPanel:
        m_state.chaosBox = 1;
        PushDialog(m_context.dialogs, ChaosBoxDialog);
        break;
    default:
        WriteProtected(m_state.npcShop, m_state.npcShopDepth, m_context.random, 1);
        m_state.repairCursor = 0;
        break;
    }
    return {};
}

Core::Expected<bool, std::string> PanelRegistry::CloseTheNpcWindow(PanelId id)
{
    // 0x658e0e..0x658fd7: the shop sets its byte to 0 before sending the C1 03 31 and emptying the grid.
    if (id == NpcShopPanel)
        WriteProtected(m_state.npcShop, m_state.npcShopDepth, m_context.random, 0);
    if (!m_context.npcWindowClosing)
        return Core::Unexpected(std::format("0x657ab0({:#x}): cerrar la ventana del NPC sin el mundo", id));
    auto closing = m_context.npcWindowClosing(id);
    if (!closing.HasValue() || !closing.Value())
        return closing;
    if (id == WarehousePanel)
        m_state.warehouse = 0;
    if (id == ChaosBoxPanel)
    {
        if (m_context.dialogs.current == ChaosBoxDialog)
            m_context.dialogs.current = 0;
        m_state.chaosBox = 0;
    }
    return true;
}

Core::Expected<bool, std::string> PanelRegistry::PanelKey(std::int32_t virtualKey)
{
    // 0x657880: 0x660410 first, which returns 0 when closed.
    auto dialog = ThePanelDialogKey(m_state.delegated.panelDialog, virtualKey, m_context.panelDialogAnswer);
    if (!dialog.HasValue() || dialog.Value())
        return dialog;
    if (virtualKey != EscapeKey)
        return false;
    for (PanelId bit = 1; bit <= LastScannedBit; bit <<= 1)
    {
        if (!IsOpen(bit))
            continue;
        auto closed = Close(bit);
        if (!closed.HasValue())
            return closed;
        return true;
    }
    return false;
}

Core::Expected<void, std::string> PanelRegistry::CloseAll()
{
    auto closed = CloseByMask(AllPanels);
    if (!closed.HasValue())
        return Core::Unexpected(std::move(closed).Error());
    return CancelThePanelDialog(m_state.delegated.panelDialog, m_context.panelDialogAnswer);
}

Core::Expected<void, std::string> PanelRegistry::StepTheObjectPanels()
{
    if (PanelDialogHandle(m_state.delegated.panelDialog) != 0)
    {
        if (!m_context.panelDialogStep)
            return Core::Unexpected(
                std::string("0x6605b0: el paso del diálogo de los paneles sin el mouse ni el reloj"));
        if (auto dialog = m_context.panelDialogStep(); !dialog.HasValue())
            return dialog;
    }
    if (IsOpen(SecondGuildPanel))
    {
        if (!m_context.secondGuildStep)
            return Core::Unexpected(
                std::string("0x657959: el paso de la ventana 2 del gremio sin el mouse ni el reloj"));
        if (auto window = m_context.secondGuildStep(); !window.HasValue())
            return window;
    }
    if (IsOpen(GuildPanel))
    {
        if (!m_context.guildStep)
            return Core::Unexpected(std::string("0x657977: el paso de la ventana del gremio sin el mouse ni el cable"));
        if (auto window = m_context.guildStep(); !window.HasValue())
            return window;
    }
    for (const PanelId id : {CastleSiegePanel, CastleSiegeListPanel, SecondCastleSiegePanel})
    {
        if (IsOpen(id))
        {
            if (!m_context.castleWindowStep)
                return Core::Unexpected(std::format("0x657930({:#x}): el paso del castillo sin sus servicios", id));
            if (auto stepped = m_context.castleWindowStep(id); !stepped.HasValue())
                return stepped;
        }
    }
    return {};
}

void ResetTheCommands(DelegatedPanels& delegated)
{
    delegated.commands = 0;
    delegated.commandsWindow.target = -1;
    delegated.commandsWindow.chosen = -1;
    delegated.commandsWindow.states.fill(CommandReady);
}

std::int32_t PanelRegistry::WorldWidth()
{
    std::int32_t width = OnePanelWorldWidth;
    bool decided = false;
    if (IsOpen(InventoryPanel))
    {
        // 0x5bb8e3..0x5bb9a3: with the inventory, any of these leaves the world at 260.
        if (IsOpen(CharacterPanel) || IsOpen(NpcShopPanel) || IsOpen(WarehousePanel) || IsOpen(ChaosBoxPanel) ||
            IsOpen(TradePanel) || IsOpen(EventNpcPanel) || IsOpen(SecondEventNpcPanel) || IsOpen(PersonalShopPanel) ||
            IsOpen(OtherShopPanel))
        {
            width = TwoPanelWorldWidth;
            decided = true;
        }
    }
    if (!decided)
    {
        // 0x5bb9ad..0x5bba5c: the pairs that give 260.
        if ((IsOpen(CharacterPanel) && IsOpen(QuestPanel)) || (IsOpen(CharacterPanel) && IsOpen(PetPanel)) ||
            (IsOpen(InventoryPanel) && IsOpen(TrainerPanel)) ||
            (IsOpen(InventoryPanel) && IsOpen(CastleSiegeListPanel) && m_context.heroSiegeMark &&
             m_state.delegated.castleSiegeListMode == InventoryCompanionMode))
        {
            width = TwoPanelWorldWidth;
        }
        else if (!IsOpen(InventoryPanel) && !IsOpen(CharacterPanel) && !IsOpen(PartyPanel) &&
                 !IsOpen(SecondGuildPanel) && !IsOpen(GuildPanel) && !IsOpen(CastleSiegePanel) &&
                 !IsOpen(CastleSiegeListPanel) && !IsOpen(SecondCastleSiegePanel) && m_state.field78abb00 == 0 &&
                 !IsOpen(QuestPanel) && !IsOpen(Panel2000) && !IsOpen(CommandsPanel))
        {
            width = FullWorldWidth;
        }
    }
    // 0x5bbb86..0x5bbbb4: the siege weapon and the gate leave it at 450.
    if (m_state.delegated.siegeWeapon != 0 || m_state.delegated.siegeGate != 0)
        width = OnePanelWorldWidth;
    return width;
}

void PanelRegistry::PlayTheSounds() const
{
    if (!m_context.sound)
        return;
    m_context.sound(FirstPanelSound);
    m_context.sound(SecondPanelSound);
}
} // namespace Client::Ui
