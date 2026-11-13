#include "UI/PanelRegistry.h"

#include "Core/Expected.h"
#include "Render/Models/LogicalAccess.h"
#include "UI/Dialogs.h"

#include <array>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x657ab0: el orden fijo de las consultas (0x657af0..0x659236).
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

// 0x65cae0 con 0x4000000: lo que cierra antes que el inventario (0x65cbd5).
constexpr PanelId ClosedWithTheInventory = 0x11d9e0;
// 0x65cae0 con 0x2000000: la mascota y la tienda personal (0x65cba4).
constexpr PanelId ClosedWithTheCharacter = PetPanel | PersonalShopPanel;
// 0x659fa3.. y 0x65c009: el modo de la lista de CS y el entrenador que van con el inventario.
constexpr std::int32_t InventoryCompanionMode = 3;
// 0x659860: el quest en 1 no deja abrir nada.
constexpr std::uint8_t BlockingQuestState = 1;
// 0x6598d4..0x6598e4: los amigos y los comandos no se abren en Chaos Castle (18 a 23).
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// 0x65c01d: el Dark Lord pide su mascota al abrir el inventario.
constexpr std::uint8_t DarkLordClass = 4;
// 0x65ca72..0x65ca88 y 0x65cc2c..0x65cc3a: los dos sonidos de abrir y cerrar.
constexpr std::int32_t FirstPanelSound = 0x19;
constexpr std::int32_t SecondPanelSound = 0x1c;
// 0x420cc0: el valor de 0x723911 con el panel de la mascota abierto.
constexpr std::uint8_t PetPanelOpen = 2;
// 0x657880: la tecla Esc.
constexpr std::int32_t EscapeKey = 0x1b;
// 0x659290(0) y 0x657880: los bits que recorren, de 1 a 0x40000000.
constexpr PanelId LastScannedBit = 0x40000000;

// Una consulta de un byte protegido: abre la entrada, lee el valor y la cierra (0x6594a3..0x6595cf con 0x10000 y
// 0x6597f9..0x659847 con 0x20). Si la cuenta queda en 0, saca un rand().
bool ReadProtected(const std::uint8_t& value, std::uint8_t& depth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, 1);
    return value != 0;
}

// La ola de cada panel que el hito 1 no abre ni cierra (06 § HUD y ventanas, "Los paneles").
const char* WaveOf(PanelId id)
{
    switch (id)
    {
    case FriendsPanel:
        return "el administrador viejo (hito 7) y la CL9.4";
    case GuildPanel:
    case TradePanel:
    case PersonalShopPanel:
    case OtherShopPanel:
    case SecondGuildPanel:
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
        return delegated.friends != 0;
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
        // 0x420cc0: solo el 2 (0x420cc0 compara con 2).
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
    // 0x661b90: un ítem en el cursor o en la caja del caos.
    const bool busy = m_context.cursorItemCount > 0 || m_state.delegated.chaosBoxHasItem;
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

Core::Expected<bool, std::string> PanelRegistry::Open(PanelId id)
{
    if (IsOpen(id) || !CanOpen(id) || m_context.logoutRequested)
        return false;
    if (m_context.dialogs.current != PanelFriendlyDialog)
        PushDialog(m_context.dialogs, 0);
    // 0x659a3f: todos menos el id y los amigos.
    PanelId mask = ~id ^ FriendsPanel;
    switch (id)
    {
    case MovePanel:
        // 0x659f57: sin cerrar nada.
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
            m_state.inventory = 1;
            if (m_context.heroClass == DarkLordClass)
                return Core::Unexpected(
                    std::string("0x65c039: el pedido de la mascota del Dark Lord (C1 A9) al abrir el inventario es de "
                                "la CL9"));
        }
        break;
    }
    case PartyPanel:
    {
        // 0x659a82..0x659c44: con lo que cerró, la cuenta en 0, el pedido de la lista y la party.
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
        // 0x65a91c..0x65a934: con lo que cerró, +4 de [0x6e592c].
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
            m_state.delegated.commands = 1;
        break;
    }
    case TradePanel:
    {
        // 0x659e46..0x659f38: con lo que cerró, el inventario (0x6599a0(0x4000000, 0), con sus sonidos) y el byte
        // protegido 0x7d53711 en 1: abrir la entrada, escribir y cerrarla (un rand() si queda sin accesos).
        auto closed = CloseByMask(mask);
        if (!closed.HasValue())
            return closed;
        if (closed.Value())
        {
            auto inventory = Open(InventoryPanel);
            if (!inventory.HasValue())
                return inventory;
            const Render::Models::LogicalAccess access(m_state.tradeDepth, m_context.random, 1);
            m_state.trade = 1;
        }
        break;
    }
    case FriendsPanel:
    case QuestPanel:
    case GuildPanel:
    case WarehousePanel:
    case ChaosBoxPanel:
    case PetPanel:
    case PersonalShopPanel:
    case EventNpcPanel:
    case Panel2000:
    case SecondEventNpcPanel:
    case TrainerPanel:
    case NpcShopPanel:
    case OtherShopPanel:
    case SecondGuildPanel:
    case CastleSiegePanel:
    case CastleSiegeListPanel:
    case SecondCastleSiegePanel:
    case SiegeWeaponPanel:
    case SecondSiegeWeaponPanel:
    case SiegeGatePanel:
        return Core::Unexpected(OpenCut(id));
    case CharacterPanel:
    {
        // 0x65c94b..0x65ca72: todos menos el inventario y los amigos; con lo que cerró, el personaje.
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
        // 0x65bfcb: un id sin caso sale con 0 y sin sonidos.
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
        // 0x65cb5b: el panel y, si cerró, el inventario, los dos con `aux`.
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
        // 0x65cb0d: con el cursor ocupado, el texto 1641; si no, la lista y, en el modo 3, el inventario.
        return Core::Unexpected(std::string("0x65cae0(0x100000): cerrar la lista de CS es de la CL9.5"));
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
        // 0x65cbbb: con un ítem en el cursor o en la caja del caos, solo los sonidos.
        if (m_context.cursorItemCount > 0 || m_state.delegated.chaosBoxHasItem)
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
    // [EBP−0xd] arranca en 1 y solo lo bajan el almacén, la caja del caos y el entrenador, que cortan antes.
    for (const PanelId id : CloseOrder)
    {
        if ((mask & id) == 0 || !IsOpen(id))
            continue;
        switch (id)
        {
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
            // 0x657fb5..0x6584cb: el 3D y las grillas, y el byte protegido en 0 (un rand() al cerrar la entrada).
            if (!m_context.tradeClosed)
                return Core::Unexpected(std::string("0x657fb5: cerrar el comercio sin sus grillas (0x78a5a20)"));
            if (auto closed = m_context.tradeClosed(aux); !closed.HasValue())
                return Core::Unexpected(std::move(closed).Error());
            const Render::Models::LogicalAccess access(m_state.tradeDepth, m_context.random, 1);
            m_state.trade = 0;
            break;
        }
        case CommandsPanel:
            ResetTheCommands(m_state.delegated);
            break;
        case InventoryPanel:
            // 0x659250..0x65926a.
            m_state.inventory = 0;
            m_state.field78abb09 = 0;
            m_state.repairMode = 0;
            m_state.field78abb00 = 0;
            m_state.field78abb04 = 0;
            break;
        default:
            return Core::Unexpected(CloseCut(id));
        }
    }
    return true;
}

Core::Expected<bool, std::string> PanelRegistry::PanelKey(std::int32_t virtualKey)
{
    if (m_state.delegated.panelDialog != 0)
        return Core::Unexpected(std::string("0x660410: las teclas del diálogo de los paneles son de la CL9.4 (P-C90)"));
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
    if (m_state.delegated.panelDialog != 0)
        return Core::Unexpected(std::string("0x6603c0: cerrar el diálogo de los paneles es de la CL9.4 (P-C90)"));
    return {};
}

Core::Expected<void, std::string> PanelRegistry::StepTheObjectPanels()
{
    if (m_state.delegated.panelDialog != 0)
        return Core::Unexpected(std::string("0x6605b0: el paso del diálogo de los paneles es de la CL9.4 (P-C90)"));
    for (const PanelId id :
         {SecondGuildPanel, GuildPanel, CastleSiegePanel, CastleSiegeListPanel, SecondCastleSiegePanel})
    {
        if (IsOpen(id))
            return Core::Unexpected(std::format("0x657930({:#x}): el paso de este panel es de {}", id, WaveOf(id)));
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
        // 0x5bb8e3..0x5bb9a3: con el inventario, cualquiera de estos deja el mundo en 260.
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
        // 0x5bb9ad..0x5bba5c: los pares que dan 260.
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
    // 0x5bbb86..0x5bbbb4: el arma de asedio y la puerta lo dejan en 450.
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
} // namespace Client::UI
