#include "Kernel/HeldModules.h"

#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/FullSetScan.h"
#include "Actors/MovementSpeed.h"
#include "Actors/PlayerActionSpeeds.h"
#include "CastleSiege/CastleSiegeState.h"
#include "Chat/ChatLog.h"
#include "Chat/ChatRuntime.h"
#include "Effects/ModelEffects.h"
#include "Events/EventGlobals.h"
#include "Events/EventMapObject.h"
#include "Fauna/FaunaState.h"
#include "Guild/GuildState.h"
#include "Hud/MapBanner.h"
#include "Items/HudItems.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Movement/GameMovementState.h"
#include "Npc/NpcWindowState.h"
#include "Particles/Particles.h"
#include "Player/ItemSetState.h"
#include "Quests/QuestState.h"
#include "Social/SocialState.h"
#include "Tooltips/ItemHelpLevels.h"
#include "Tooltips/TooltipLines.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "Ui/OldWindows.h"
#include "Ui/PanelRegistry.h"
#include "Ui/SlideBanner.h"
#include "Weather/LeafWeather.h"
#include "Weather/Leaves.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <utility>

// The states Kernel/HeldModules.h and Kernel/WorldState.h hold without including them, with the initial values the
// Kernel structs gave their fields.
namespace Client::Session
{
struct HudStates
{
    Items::QuickSlots quickSlots{};
    Chat::ChatLogFrame chatFrame{};
    Ui::MessageFrameState messageFrame{};
    Ui::SlideBanner slides{};
    Hud::MapBanner mapBanner = Hud::MakeTheMapBanner();
    Tooltips::TooltipLines tooltip{};
    Tooltips::ItemHelpLevels itemHelp{};
};

struct RuntimeStates
{
    Actors::Drawing::GameItemRenderState itemRender{};
    Ui::PanelState panels{};
    Ui::DialogState dialogs{};
    CastleSiege::CastleSiegeState castleSiege{};
    Guild::GuildState guild{};
    Ui::DialogBoxState dialogBox{};
    Ui::GameUiState ui{};
    Npc::NpcWindowState npcWindows{};
    Ui::OldWindowManager oldWindows{};
    Chat::ChatRuntime chat{};
    Movement::GameMovementState movement{};
    Quests::QuestState quest{};
    std::optional<Events::EventMapObject> eventObject{};
    Events::EventGlobals events{};
    Actors::MovementSpeedSharedState movementSpeed;
    std::optional<Actors::Scan505820> fullSetScan = Actors::Scan505820{};
    Social::SocialState social{};
    Weather::LeafWeather leafWeather{.firstWave = Kernel::InitialLeafCount};
};

struct MapStates
{
    Effects::ModelEffects effects;
};

struct WorldStates
{
    std::array<Actors::Drawing::CharacterCloth, Kernel::ActorPoolSize> actorCloths{};
    Player::ItemSetState itemSets;
    Particles::Particles particles{};
    Actors::PlayerActionSpeeds actionSpeeds;
    Fauna::FaunaState fauna;
    Weather::Leaves leaves{};
};
} // namespace Client::Session

namespace Client::Kernel
{
HudModules::HudModules() : HudModules(std::make_unique<Session::HudStates>()) {}

HudModules::HudModules(const HudModules& other) : HudModules(std::make_unique<Session::HudStates>(*other.m_states)) {}

HudModules::HudModules(HudModules&& other)
    : HudModules(std::make_unique<Session::HudStates>(std::move(*other.m_states)))
{
}

HudModules& HudModules::operator=(const HudModules& other)
{
    *m_states = *other.m_states;
    return *this;
}

HudModules& HudModules::operator=(HudModules&& other)
{
    if (this != &other)
        *m_states = std::move(*other.m_states);
    return *this;
}

HudModules::~HudModules() = default;

HudModules::HudModules(std::unique_ptr<Session::HudStates> states)
    : m_states(std::move(states)), quickSlots(m_states->quickSlots), chatFrame(m_states->chatFrame),
      messageFrame(m_states->messageFrame), slides(m_states->slides), mapBanner(m_states->mapBanner),
      tooltip(m_states->tooltip), itemHelp(m_states->itemHelp)
{
}

RuntimeModules::RuntimeModules() : RuntimeModules(std::make_unique<Session::RuntimeStates>()) {}

RuntimeModules::RuntimeModules(const RuntimeModules& other)
    : RuntimeModules(std::make_unique<Session::RuntimeStates>(*other.m_states))
{
}

RuntimeModules::RuntimeModules(RuntimeModules&& other)
    : RuntimeModules(std::make_unique<Session::RuntimeStates>(std::move(*other.m_states)))
{
}

RuntimeModules& RuntimeModules::operator=(const RuntimeModules& other)
{
    *m_states = *other.m_states;
    return *this;
}

RuntimeModules& RuntimeModules::operator=(RuntimeModules&& other)
{
    if (this != &other)
        *m_states = std::move(*other.m_states);
    return *this;
}

RuntimeModules::~RuntimeModules() = default;

RuntimeModules::RuntimeModules(std::unique_ptr<Session::RuntimeStates> states)
    : m_states(std::move(states)), itemRender(m_states->itemRender), panels(m_states->panels),
      dialogs(m_states->dialogs), castleSiege(m_states->castleSiege), guild(m_states->guild),
      dialogBox(m_states->dialogBox), ui(m_states->ui), npcWindows(m_states->npcWindows),
      oldWindows(m_states->oldWindows), chat(m_states->chat), movement(m_states->movement), quest(m_states->quest),
      eventObject(m_states->eventObject), events(m_states->events), movementSpeed(m_states->movementSpeed),
      fullSetScan(m_states->fullSetScan), social(m_states->social), leafWeather(m_states->leafWeather)
{
}

MapModules::MapModules() : MapModules(std::make_unique<Session::MapStates>()) {}

MapModules::~MapModules() = default;

MapModules::MapModules(std::unique_ptr<Session::MapStates> states)
    : m_states(std::move(states)), effects(m_states->effects)
{
}

WorldState::WorldState() : m_modules(std::make_unique<Session::WorldStates>()) {}

WorldState::~WorldState() = default;

std::span<Actors::Drawing::CharacterCloth, ActorPoolSize> WorldState::ActorCloths()
{
    return m_modules->actorCloths;
}

std::span<const Actors::Drawing::CharacterCloth, ActorPoolSize> WorldState::ActorCloths() const
{
    return m_modules->actorCloths;
}

Player::ItemSetState& WorldState::ItemSets()
{
    return m_modules->itemSets;
}

const Player::ItemSetState& WorldState::ItemSets() const
{
    return m_modules->itemSets;
}

Particles::Particles& WorldState::Particles()
{
    return m_modules->particles;
}

const Particles::Particles& WorldState::Particles() const
{
    return m_modules->particles;
}

Actors::PlayerActionSpeeds& WorldState::ActionSpeeds()
{
    return m_modules->actionSpeeds;
}

const Actors::PlayerActionSpeeds& WorldState::ActionSpeeds() const
{
    return m_modules->actionSpeeds;
}

Fauna::FaunaState& WorldState::Fauna()
{
    return m_modules->fauna;
}

const Fauna::FaunaState& WorldState::Fauna() const
{
    return m_modules->fauna;
}

Weather::Leaves& WorldState::Leaves()
{
    return m_modules->leaves;
}

const Weather::Leaves& WorldState::Leaves() const
{
    return m_modules->leaves;
}
} // namespace Client::Kernel
