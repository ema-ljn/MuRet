#pragma once

#include <memory>
#include <optional>

namespace Client::Actors
{
struct MovementSpeedSharedState;
struct Scan505820;
} // namespace Client::Actors

namespace Client::Actors::Drawing
{
struct GameItemRenderState;
} // namespace Client::Actors::Drawing

namespace Client::CastleSiege
{
struct CastleSiegeState;
} // namespace Client::CastleSiege

namespace Client::Chat
{
struct ChatLogFrame;
struct ChatRuntime;
} // namespace Client::Chat

namespace Client::Effects
{
class ModelEffects;
} // namespace Client::Effects

namespace Client::Events
{
struct EventGlobals;
struct EventMapObject;
} // namespace Client::Events

namespace Client::Guild
{
struct GuildState;
} // namespace Client::Guild

namespace Client::Hud
{
struct MapBanner;
} // namespace Client::Hud

namespace Client::Items
{
struct QuickSlots;
} // namespace Client::Items

namespace Client::Movement
{
struct GameMovementState;
} // namespace Client::Movement

namespace Client::Npc
{
struct NpcWindowState;
} // namespace Client::Npc

namespace Client::Quests
{
struct QuestState;
} // namespace Client::Quests

namespace Client::Social
{
struct SocialState;
} // namespace Client::Social

namespace Client::Tooltips
{
struct ItemHelpLevels;
struct TooltipLines;
} // namespace Client::Tooltips

namespace Client::Ui
{
struct DialogBoxState;
struct DialogState;
struct GameUiState;
struct MessageFrameState;
struct OldWindowManager;
struct PanelState;
struct SlideBanner;
} // namespace Client::Ui

namespace Client::Weather
{
struct LeafWeather;
} // namespace Client::Weather

namespace Client::Session
{
struct HudStates;
struct MapStates;
struct RuntimeStates;
} // namespace Client::Session

// The modules' states that HudState, WorldRuntime and WorldMap hold (ADR 0029, Decision 2): Session defines and
// builds them (Session/HeldModules.cpp), so Kernel includes none of their headers, and each field is a reference into
// its holder's own states. A copy copies the states and a move moves them, each into the holder's own, so a field never
// names another holder's.
namespace Client::Kernel
{
class HudModules
{
public:
    HudModules();
    HudModules(const HudModules& other);
    HudModules(HudModules&& other);
    HudModules& operator=(const HudModules& other);
    HudModules& operator=(HudModules&& other);
    ~HudModules();

private:
    explicit HudModules(std::unique_ptr<Session::HudStates> states);

    std::unique_ptr<Session::HudStates> m_states;

public:
    Items::QuickSlots& quickSlots;
    // What the chat's drawings write (milestone 3): the log's frame (the step sets its corner), the widths and the
    // blinks of the old area and the notices.
    Chat::ChatLogFrame& chatFrame;
    Ui::MessageFrameState& messageFrame;
    // [0x5763528]: Slide.bmd's banner (0x46aea0), which the 15 draws (0x46b090) and writes while drawing (the opacity,
    // the shift, the scrolling text and 0x5762ee4). The 0x3eb timer and the 0D fill it, and the UI step advances it.
    Ui::SlideBanner& slides;
    // [0x5763534]: the banner with the map's name (Scenes/MapBanner.h), which the 19 draws (0x65dd70).
    Hud::MapBanner& mapBanner;
    // 0x78793c8, 0x787a560 and 0x78a9230: the tooltips' lines, their colors and their bold flags (CL8 milestone 5).
    Tooltips::TooltipLines& tooltip;
    // 0x787a15c and 0x6c9334: the item help's per-level table and its item (−1 in the .data), which 0x5afc00 fills.
    Tooltips::ItemHelpLevels& itemHelp;
};

class RuntimeModules
{
public:
    RuntimeModules();
    RuntimeModules(const RuntimeModules& other);
    RuntimeModules(RuntimeModules&& other);
    RuntimeModules& operator=(const RuntimeModules& other);
    RuntimeModules& operator=(RuntimeModules&& other);
    ~RuntimeModules();

private:
    explicit RuntimeModules(std::unique_ptr<Session::RuntimeStates> states);

    std::unique_ptr<Session::RuntimeStates> m_states;

public:
    Actors::Drawing::GameItemRenderState& itemRender;
    // The panels' bytes (0x7d53708..0x7d53718 and the globals their functions write), the dialogs (0x7d3a10c,
    // 0x7d3a110) and the rest of the game UI (CL8 milestone 1): the .bss, which nobody clears on a scene change.
    Ui::PanelState& panels;
    Ui::DialogState& dialogs;
    // What the castle packets keep outside the battle (CL9 milestone 12): the .bss 0x7d5c504..0x7d5c658, the fields of
    // [0x7d536f8] and [0x7d53700] they write, and 0x78aba30/0x78aba34.
    CastleSiege::CastleSiegeState& castleSiege;
    // The guild, the alliance and the war (CL9 milestone 8): the guild table (0x787aa48), the members (0x787a5e8), the
    // war's globals and what the packets keep in the two guild windows.
    Guild::GuildState& guild;
    // 0x7d36a98, 0x7d368e4, 0x7d368b8 and 0x7d3a0f4..0x7d3a0f9: the old boxes' text and buttons (milestone 6).
    Ui::DialogBoxState& dialogBox;
    Ui::GameUiState& ui;
    // The NPC windows' grids and globals (CL9 milestone 6).
    Npc::NpcWindowState& npcWindows;
    // *(0x5763514), the old windows' manager, and [0x5763524]'s window list (milestone 7): WinMain builds them and
    // they last the whole game.
    Ui::OldWindowManager& oldWindows;
    Chat::ChatRuntime& chat;
    Movement::GameMovementState& movement;
    // [0x710148]: the quests' object (CL9 milestone 2). main.cpp copies quest.bmd's records (GameData::quests).
    Quests::QuestState& quest;
    // DAT_00723c60, the event object 0x447d70 builds. Empty like the .bss; only 0x447d70 frees it (0x447eb0), and only
    // to build another: leaving the event for a common map, it stays alive (Q-C320).
    std::optional<Events::EventMapObject>& eventObject;
    // The rest the events' packets write (CL9 milestone 11).
    Events::EventGlobals& events;
    // 0x7d3a0d0 and 0x7d3a0d4, Fenrir's: one for all characters (06, "Caminar", the speed). 0x4f2250 zeroes the first
    // (0x4f22a4). The DTO's zeros do not claim the original's initial value.
    Actors::MovementSpeedSharedState& movementSpeed;
    // [0x743a178] and [0x743a17c], what the last 0x505820 left (06, "El conjunto completo de 0x505820"): in the .bss,
    // 0 at start. 0x512a30 writes it with the published hero, and so do the characters' drawing (0x50ba30 at
    // 0x50c91e), the login's and the selection's (ScanTheDrawnCharacter). nullopt after a game frame that reaches the
    // characters (0x6374dd → 0x510d00), which MuRet does not draw: what it left is unknown, and 0x550990 stops with an
    // error if it reads it.
    std::optional<Actors::Scan505820>& fullSetScan;
    // The trade, the party and the duel (CL9 milestone 7).
    Social::SocialState& social;
    // 0x75fdc38: the 0F's target. 0x75fdc3c is the current count, which that packet does not change. 0x530490's
    // durable state for the future leaves binder; receiving a 0F does not create rain.
    Weather::LeafWeather& leafWeather;
};

// WorldMap is neither copied nor moved: one per world, like its globals.
class MapModules
{
public:
    MapModules();
    MapModules(const MapModules&) = delete;
    MapModules(MapModules&&) = delete;
    MapModules& operator=(const MapModules&) = delete;
    MapModules& operator=(MapModules&&) = delete;
    ~MapModules();

private:
    explicit MapModules(std::unique_ptr<Session::MapStates> states);

    std::unique_ptr<Session::MapStates> m_states;

public:
    // The effects with a model (0x7491fd8), the arrival effect's (0x61f) included.
    Effects::ModelEffects& effects;
};
} // namespace Client::Kernel
