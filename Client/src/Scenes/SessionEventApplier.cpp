#include "Scenes/SessionEventApplier.h"

#include "App/CharacterEvent.h"
#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "App/WorldEvent.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/InventoryList.h"
#include "Protocol/Inventory.h"
#include "Protocol/Messages.h"
#include "Protocol/Movement.h"
#include "Protocol/OptionData.h"
#include "Protocol/Skill.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/InventoryReply.h"
#include "Scenes/MovementReplies.h"
#include "Scenes/ReturnToSelection.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SkillReplies.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitions.h"
#include "World/LeafWeather.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <span>
#include <string>
#include <variant>

namespace Client::Scenes
{
namespace
{
void ApplyWeather(const Protocol::World::Weather& received, World::LeafWeather& weather)
{
    // 0x4e36c4/0x4e36ed: sólo target; el default de 0x4e5b4a no escribe nada ni pide rand().
    constexpr std::uint8_t NibbleShift = 4;
    constexpr std::uint8_t IntensityMask = 0x0f;
    constexpr std::int32_t TargetPerIntensity = 6;
    const auto mode = received.code >> NibbleShift;
    if (mode == 0)
        weather.target = 0;
    else if (mode == 1)
        weather.target = static_cast<std::int32_t>(received.code & IntensityMask) * TargetPerIntensity;
}

// 0x42a760 manda el B9 02 por el socket del GS al cargar el 30: el de la sesión que aplica la respuesta.
WorldEntryResources WithTheSiegeRequest(const WorldEntryResources& resources, App::ServerSession& session)
{
    WorldEntryResources bound = resources;
    bound.requestCastleSiegeState = [&session] { session.RequestCastleSiegeState(); };
    return bound;
}

// Lo que el chat que llega lee del juego y de la sesión: 0x6d5edc es el elegido de la lista.
ChatReceiveContext ChatContextOf(const WorldEntryResources& resources, const App::ServerSession& session)
{
    const Protocol::Character::CharacterEntry* chosen = session.ChosenCharacter();
    return ChatReceiveContext{.world = resources.world,
                              .random = resources.cloth.random,
                              .measure = resources.textMeasure,
                              .screen = resources.screen ? resources.screen() : App::PixelSize{640, 480},
                              .scene = resources.scene ? resources.scene() : std::nullopt,
                              .texts = resources.texts,
                              .chosenSlot =
                                  chosen != nullptr ? std::optional<std::uint8_t>(chosen->slot) : std::nullopt};
}

// El 0x3d de la respuesta va a la sesión que la está aplicando.
class SessionPublisher final : public IWorldEntryPublisher, public IWorldTransitionPublisher
{
public:
    explicit SessionPublisher(App::ServerSession& session) : m_session(session) {}

    void PublishWorldEntry() override
    {
        m_session.PublishWorldEntry();
    }

    void ConfirmTeleport() override
    {
        m_session.ConfirmTeleport();
    }
    void RequestCrywolfState() override
    {
        m_session.RequestCrywolfState();
    }

private:
    App::ServerSession& m_session;
};
} // namespace

SessionEventApplier::SessionEventApplier(const WorldEntryResources& resources) : m_resources(resources) {}

Core::Expected<void, std::string> SessionEventApplier::ApplyCharacter(const App::CharacterEvent& event,
                                                                      const App::ServerSession&)
{
    return m_resources.selection.Apply(std::span(&event, 1), m_resources.camera, m_resources.cameraInput,
                                       &m_resources.cloth,
                                       m_resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel));
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorld(const App::WorldEvent& event,
                                                                  App::ServerSession& session)
{
    if (const auto* walk = std::get_if<Protocol::Movement::Walk>(&event))
        return ApplyWalkReply(*walk, session.PlayerIndex(), m_resources);
    if (const auto* damage = std::get_if<Protocol::Movement::Damage>(&event))
        return ApplyDamageReply(*damage, session.PlayerIndex(), m_resources);
    if (const auto* teleport = std::get_if<Protocol::World::Teleport>(&event))
    {
        SessionPublisher publisher(session);
        return ApplyTeleport(*teleport, session.PlayerIndex(), WithTheSiegeRequest(m_resources, session), publisher);
    }
    if (const auto* regen = std::get_if<Protocol::World::Regen>(&event))
        return ApplyRegen(*regen, session.PlayerIndex(), WithTheSiegeRequest(m_resources, session));
    if (const auto* chat = std::get_if<Protocol::Messages::Chat>(&event))
    {
        // 0x4c6095: en el login, el 00 es el saludo del CS y pide la lista por el socket abierto.
        if (m_resources.scene && m_resources.scene() == SceneId::Login)
        {
            session.ResendServerListRequest();
            return {};
        }
        return ApplyChat(*chat, ChatContextOf(m_resources, session));
    }
    if (const auto* chat = std::get_if<Protocol::Messages::KeyChat>(&event))
        return ApplyKeyChat(*chat, ChatContextOf(m_resources, session));
    if (const auto* whisper = std::get_if<Protocol::Messages::Whisper>(&event))
        return ApplyWhisper(*whisper, ChatContextOf(m_resources, session));
    if (const auto* result = std::get_if<Protocol::Messages::WhisperResult>(&event))
        return ApplyWhisperResult(*result, ChatContextOf(m_resources, session));
    if (const auto* notice = std::get_if<Protocol::Messages::Notice>(&event))
        return ApplyNotice(*notice, ChatContextOf(m_resources, session));
    if (const auto* options = std::get_if<Protocol::OptionData::OptionData>(&event))
        return ApplyOptionData(*options, ChatContextOf(m_resources, session));
    if (auto skill = ApplySkillEvent(event); skill)
        return std::move(*skill);
    return ApplyWorldContent(event, session);
}

// CL9 hito 4: las skills que llegan (Scenes/SkillReplies.h). nullopt si `event` no es una.
std::optional<Core::Expected<void, std::string>> SessionEventApplier::ApplySkillEvent(const App::WorldEvent& event)
{
    if (const auto* attack = std::get_if<Protocol::Skill::MagicAttack>(&event))
        return ApplyMagicAttack(*attack, m_resources);
    if (const auto* magic = std::get_if<Protocol::Skill::DurationMagic>(&event))
        return ApplyDurationMagic(*magic, m_resources);
    if (const auto* cancel = std::get_if<Protocol::Skill::MagicCancel>(&event))
    {
        ApplyMagicCancel(*cancel, m_resources);
        return Core::Expected<void, std::string>{};
    }
    if (const auto* state = std::get_if<Protocol::Skill::ActorState>(&event))
        return ApplyActorState(*state, m_resources);
    if (const auto* effect = std::get_if<Protocol::Skill::ActorEffect>(&event))
        return ApplyActorEffect(*effect, m_resources);
    if (const auto* target = std::get_if<Protocol::Skill::SkillTarget>(&event))
        return ApplySkillTarget(*target, m_resources);
    if (const auto* list = std::get_if<Protocol::Skill::SkillList>(&event))
        return ApplySkillList(*list, m_resources);
    if (const auto* summon = std::get_if<Protocol::Skill::SummonLife>(&event))
    {
        ApplySummonLife(*summon, m_resources);
        return Core::Expected<void, std::string>{};
    }
    return std::nullopt;
}

Core::Expected<void, std::string> SessionEventApplier::ApplyWorldContent(const App::WorldEvent& event,
                                                                         App::ServerSession& session)
{
    if (const auto* weather = std::get_if<Protocol::World::Weather>(&event))
    {
        ApplyWeather(*weather, m_resources.world.Runtime().leafWeather);
        return {};
    }
    if (std::holds_alternative<App::ClearInventoryReceived>(event))
    {
        ClearForInventoryReply(m_resources);
        return {};
    }
    if (std::holds_alternative<App::ReturnToSelectionReceived>(event))
    {
        ApplyReturnToSelection(m_resources);
        return {};
    }
    if (const auto* moved = std::get_if<Protocol::Inventory::ItemMoveResult>(&event))
        return ApplyItemMoveResult(*moved, m_resources);
    if (const auto* life = std::get_if<Protocol::Inventory::LifeRefill>(&event))
        return ApplyLifeRefill(*life, m_resources.world, m_resources.cloth.random);
    if (const auto* mana = std::get_if<Protocol::Inventory::ManaRefill>(&event))
        return ApplyManaRefill(*mana, m_resources.world, m_resources.cloth.random);
    if (const auto* deleted = std::get_if<Protocol::Inventory::ItemDelete>(&event))
        return ApplyItemDelete(*deleted, m_resources);
    if (const auto* durability = std::get_if<Protocol::Inventory::Durability>(&event))
        return ApplyDurability(*durability, m_resources);
    if (const auto* change = std::get_if<Protocol::Inventory::EquipmentChange>(&event))
        return ApplyEquipmentChange(*change, m_resources);
    if (const auto* put = std::get_if<Protocol::Inventory::ItemPut>(&event))
        return ApplyItemPut(*put, m_resources);
    if (const auto* inventory = std::get_if<Protocol::Character::InventoryList>(&event))
    {
        const auto applied = ApplyInventory(*inventory, m_resources);
        if (!applied.HasValue())
        {
            return Core::Unexpected(applied.Error());
        }
        // Los sonidos que pidió quedan sin tocar: MuRet no tiene audio hasta la CL10.
        return {};
    }
    const auto& reply = std::get<Protocol::Character::CharacterSelectResult>(event);
    SessionPublisher publisher(session);
    const auto entered =
        ApplyWorldEntry(reply, session.PlayerIndex(), WithTheSiegeRequest(m_resources, session), publisher);
    if (!entered.HasValue())
    {
        return Core::Unexpected(entered.Error());
    }
    // Los sonidos que pidió quedan sin tocar: MuRet no tiene audio hasta la CL10.
    return {};
}
} // namespace Client::Scenes
