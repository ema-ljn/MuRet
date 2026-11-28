#include "Scenes/CharacterSelectScene.h"
#include "Models/IModelLibrary.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Math/HalfLifeMath.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"
#include <span>

#include "Kernel/LoopTiming.h"
#include "Session/ModuleStates.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Data/Textures/TextureSet.h"
#include "Kernel/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Maps/MapId.h"
#include "Protocol/FrontEnd/CharacterDelete.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Session/Logout.h"
#include "Render/Frame/SceneWorld.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Pose.h"
#include "Models/RayPicking.h"
#include "Models/Skeleton.h"
#include "Render/OriginalQuad.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Actors/CharacterAnimation.h"
#include "Actors/ActorAppearance.h"
#include "Actors/ActorConstruction.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorsState.h"
#include "Actors/CharacterStep.h"
#include "Pets/DarkSpirit.h"
#include "Pets/HelperTable.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "FrontEnd/CharacterSlots.h"
#include "FrontEnd/CreatePreview.h"
#include "Assets/GameModels.h"
#include "Scenes/GameProjection.h"
#include "Actors/FullSetScan.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Tables/GameData.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Chat/GameMessages.h"
#include "Hud/GamePanels.h"
#include "Session/WorldEntry.h"
#include "Protocol/Outgoing.h"
#include "Ui/PanelRegistry.h"
#include "Actors/Drawing/ItemDrawResources.h"
#include "Session/ItemCreators.h"
#include "Hud/OptionsSettings.h"
#include "Camera/SceneCameras.h"
#include "FrontEnd/SelectionWorld.h"
#include "FrontEnd/SelectionInventory.h"
#include "Ui/MessageArea.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "FrontEnd/SelectionState.h"
#include "Social/SocialEvents.h"
#include "Camera/TerrainPolygonFrame.h"
#include "Assets/TextureSets.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/WorldMap.h"
#include "Data/Terrain/TerrainLight.h"
#include "Kernel/CharacterClass.h"
#include "Effects/GameParticles.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/SelectionParticleKernels.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "FrontEnd/CharacterBalloons.h"
#include "FrontEnd/CreateCharacterWindow.h"
#include "FrontEnd/DeleteChecks.h"
#include "FrontEnd/EscapeWindow.h"
#include "Ui/GameCursor.h"
#include "Ui/GameText.h"
#include "Ui/MessageArea.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/NewUi.h"
#include "Ui/Notices.h"
#include "FrontEnd/OptionsWindow.h"
#include "Ui/PanelRegistry.h"
#include "FrontEnd/SelectionBar.h"
#include "Ui/Widget.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <ratio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{

constexpr Audio::SoundId ButtonClickSound = Sound::ButtonClickSound;

// 0x4f2250 picks it by the pet, the wings and the weapon; the trace's is 3, with a one-handed weapon. The others are
// unread (P-C14 c): until then, 3.
constexpr std::uint8_t StandingAction = 3;

// 0x50ba30's light in scene 4: 0.4, and 0.5 for the "hero" (DAT_0743a170: 0x50d219), plus the +0xf4 that 0x634680
// sets, 1 for the chosen one. The hero is object 0, slot 0's, until the first world entry; after an F1 02 with 1, the
// slot the game drew (Q-C213).
constexpr float CharacterLight = 0.4f;
// The selection's DAT_006d5dc8: 0x729 and 0x6f1/2 look at it.
constexpr std::int32_t SelectionSceneId = 4;
constexpr float HeroLight = 0.5f;
constexpr float ChosenExtraLight = 1.0f;

// The texts 0x622c70 looks for in a new one's name.
constexpr std::size_t OperationText = 457;
constexpr std::size_t ServerText = 458;
// 0x63420a: the message format with the list's two pkt[5] numbers.
constexpr std::size_t AccountNoticeText = 1240;

// The flags bit (+0x1d8) with which the bar's build shows texts 436 and 437.
constexpr std::uint8_t BlockedAccountFlag = 0x10;

// 0x5bb8d0 in the selection: 640, without the game windows (06 § El trapecio completo).
constexpr std::int32_t SelectionWorldWidth = 640;

// Whether the entry is the same one that gave the character: a create on an occupied slot overwrites it.
bool SameEntry(const Protocol::FrontEnd::CharacterEntry& one, const Protocol::FrontEnd::CharacterEntry& other)
{
    return one.slot == other.slot && one.name == other.name && one.classByte == other.classByte &&
           one.equipment == other.equipment && one.withoutEquipment == other.withoutEquipment;
}

// What 0x634680 passes 0x5fbb00 (0x634736..0x63475c) and what 0x5fb570 reads from the globals: scene 4, the current
// world (0x6c9dd0, the 54), the selection's fixed point, the camera yaw (unused in the 54: it turns 45°), the
// published hero as the map 30 selector (only in scene 5) and 0x5bb8d0's width, 640 without the game windows (08
// P-C41 e). Map 6 does not reach the selection.
Terrain::TerrainPolygonInput SelectionPolygonInput(const Camera::GameCameraInput& input, const Render::Camera3D& camera)
{
    Terrain::TerrainPolygonInput polygon;
    polygon.scene = static_cast<std::int32_t>(Terrain::CullingScene::CharacterSelect);
    polygon.map = input.map;
    polygon.translation = Camera::SelectionCullingPoint;
    polygon.objectPosition = glm::vec2(input.hero.x, input.hero.y);
    polygon.cameraYaw = camera.angles.z;
    polygon.worldWidth = SelectionWorldWidth;
    return polygon;
}

// The two backgrounds, 0x621b60(0x717, 0, 25, 320, 320, …) and 0x718 at x 320, scaled from 640 × 480.
Render::OriginalQuad Background(std::uint16_t slot, float x)
{
    return Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                .x = x,
                                .y = 25.0f,
                                .width = 320.0f,
                                .height = 320.0f,
                                .scaleSize = true,
                                .scalePosition = true};
}
} // namespace

CharacterSelectScene::CharacterSelectScene(const SceneContext& context, const Tables::GameData& data,
                                           const Models::IModelLibrary& models, App::ServerSession* session,
                                           const Models::ModelTextureSlots* gameSlots)
    : m_context(context), m_stepHooks(context.states != nullptr ? &context.states->monsters : nullptr),
      m_characterDrawHooks(context.states != nullptr ? &context.states->monsters : nullptr),
      m_texts(data.text ? &*data.text : nullptr), m_items(data.items ? &data.items->table : nullptr), m_models(models),
      m_session(session), m_textures(context.textures, Assets::SelectionTextureSet),
      m_windowTextures(context.textures, Assets::WindowTextureSet), m_world(context.textures, context.log),
      m_notice([this](Render::TextureSlot slot) { return m_windowTextures.BufferSize(slot); }, m_texts),
      m_bar([this](Render::TextureSlot slot) { return m_textures.BufferSize(slot); }, m_texts),
      m_balloons([this](Render::TextureSlot slot) { return m_textures.BufferSize(slot); }, m_texts),
      m_nameFilters{.filterName = data.filterName,
                    .filter = data.filter,
                    .operation = Ui::TextAt(m_texts, OperationText).value_or(std::string_view{}),
                    .server = Ui::TextAt(m_texts, ServerText).value_or(std::string_view{})},
      m_create([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_escape([this](Render::TextureSlot slot) { return m_windowTextures.BufferSize(slot); }, m_texts),
      m_options([this](Render::TextureSlot slot) { return m_windowTextures.BufferSize(slot); }, m_texts),
      m_gameSlots(gameSlots)
{
}

CharacterSelectScene::CharacterSelectScene(const SceneContext& context, const Tables::GameData& data,
                                           Assets::GameModels& models, App::ServerSession* session)
    : CharacterSelectScene(context, data, models, session, &models.TextureSlots())
{
}

CharacterSelectScene::CharacterSelectScene(const SceneContext& context, const Tables::GameData& data,
                                           Assets::GameModels& models, App::ServerSession* session,
                                           const Models::ModelTextureSlots* gameSlots)
    : CharacterSelectScene(context, data, static_cast<const Models::IModelLibrary&>(models), session, gameSlots)
{
    m_startupModels = &models;
}

Assets::TextureBufferSize CharacterSelectScene::BufferSize(Render::TextureSlot slot) const
{
    const bool inTheWindows =
        std::ranges::any_of(Assets::WindowTextureSet,
                            [slot](const Data::Textures::TextureSetEntry& entry) { return entry.slot == slot.value; });
    return inTheWindows ? m_windowTextures.BufferSize(slot) : m_textures.BufferSize(slot);
}

Camera::GameCamera& CharacterSelectScene::CameraState()
{
    return m_context.camera != nullptr ? *m_context.camera : m_localCamera;
}

Camera::GameCameraInput& CharacterSelectScene::CameraInputState()
{
    return m_context.cameraInput != nullptr ? *m_context.cameraInput : m_localCameraInput;
}

FrontEnd::CreatePreview& CharacterSelectScene::Preview()
{
    return m_context.selection != nullptr ? m_context.selection->PreviewActor() : m_preview;
}

const FrontEnd::CreatePreview& CharacterSelectScene::Preview() const
{
    return m_context.selection != nullptr ? m_context.selection->PreviewActor() : m_preview;
}

Core::Expected<void, std::string> CharacterSelectScene::Enter()
{
    m_cameraFrames = {};
    m_fullSetScanFrames = {};
    m_previewDrawFrames = {};
    m_previewCamera = FrontEnd::PreviewCamera();
    m_steps = 0;
    m_charactersPrepared = false;
    CameraInputState().scene = Terrain::CullingScene::CharacterSelect;
    if (m_context.selection != nullptr)
    {
        // From the login, the reset comes before the only RequestCharacterList at the end of Enter. On returning, F1/02
        // already did it: an F3/00 from the same Poll may have built the new list.
        if (m_session == nullptr || m_session->State() == Session::SessionState::LoggedIn)
            m_context.selection->BeginSelection();
        else
            m_context.selection->SetScene(Terrain::CullingScene::CharacterSelect);
    }
    if (auto loaded = m_textures.Load(m_context.dataRoot); !loaded.HasValue())
    {
        return loaded;
    }
    if (auto loaded = m_windowTextures.Load(m_context.dataRoot); !loaded.HasValue())
    {
        return loaded;
    }
    // A missing NewFace texture closes the original (0x60d5f0 with its fifth argument at 1).
    const auto startupTexture =
        m_startupModels != nullptr ? m_startupModels->TakeStartupSelectionTexture(m_context.textures) : std::nullopt;
    if (auto loaded =
            m_selectionModels.Load(m_context.dataRoot, m_context.textures, m_context.log, m_gameSlots, startupTexture);
        !loaded.HasValue())
    {
        return loaded;
    }
    // 0x633f20 → 0x65f4b0 → 0x63d1a0 → 0x63d7c0 → 0x63e3e0, on entering: the preview with the window's class (the Dark
    // Knight) and action 1.
    if (m_context.world == nullptr || m_context.cloth == nullptr || m_context.selection == nullptr)
    {
        FrontEnd::ChoosePreviewClass(Preview(), m_create.ChosenClass(), m_selectionModels);
    }
    // 0x65f4b0 (CL8, milestone 8): the notice, the Esc one, the options, the bar and the create one, each at the head.
    // The original builds it with the list (0x633f20), and until then keeps the login's UI, of which only the notice
    // opens here; the bar opens with the list (StepTheScene; Q-C495). The options are built again (0x644120) with the
    // globals (0x6446e0).
    m_notice.Close();
    m_create.Close();
    m_bar.Close();
    // The login's UI releases the balloons (0x65f300 → 0x63ce50) and they are built with the list (0x65f4b0).
    m_balloons.Release();
    m_escape.Arm(Ui::NewUiMode::Selection);
    m_options.Arm(CurrentOptions());
    const std::array<Ui::NewUiWindow*, 5> uiWindows{&m_notice, &m_escape, &m_options, &m_bar, &m_create};
    m_ui.Build(Ui::NewUiMode::Selection, uiWindows);
    m_openedTheBar = false;
    // The F3 00 of the entry from the login (0x634dce). After an F1 02 with 1 the session already sent it (0x4c3653)
    // and waits for the list: no other is asked for.
    if (m_session != nullptr && m_session->State() == Session::SessionState::LoggedIn)
    {
        m_session->RequestCharacterList();
    }
    return {};
}

bool CharacterSelectScene::WaitingForTheList() const
{
    if (m_session == nullptr)
    {
        return false;
    }
    return !m_session->Characters().has_value() ||
           (m_context.selection != nullptr && !m_context.selection->HasReceivedList());
}

CharacterSelectScene::Character
CharacterSelectScene::MakeCharacter(const Protocol::FrontEnd::CharacterEntry& entry) const
{
    Character character;
    character.slot = entry.slot;
    if (m_context.selection != nullptr)
    {
        character.revision = m_context.selection->Slots()[entry.slot].revision;
    }
    character.entry = entry;
    const std::uint8_t characterClass = Protocol::FrontEnd::CharacterClassOf(entry.classByte);
    // The one the create reply built comes without equipment (0x517270 without 0x5139f0, Q-C103).
    character.appearance = entry.withoutEquipment ? Kernel::ClassAppearance(characterClass, 0)
                                                  : Kernel::AppearanceOf(characterClass, 0, entry.equipment);
    if (const Models::Skeleton* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId))
    {
        Models::SetAction(character.animation.body, *skeleton, StandingAction);
    }
    return character;
}

void CharacterSelectScene::SyncTheCharacters()
{
    if (m_session == nullptr || !m_session->Characters())
    {
        return;
    }
    const std::vector<Protocol::FrontEnd::CharacterEntry>& entries = m_session->Characters()->characters;
    bool changed = !m_tookTheList || entries.size() != m_characters.size();
    std::vector<Character> synced;
    synced.reserve(entries.size());
    for (const Protocol::FrontEnd::CharacterEntry& entry : entries)
    {
        const FrontEnd::SelectionState* selection = m_context.selection;
        const auto same =
            std::ranges::find_if(m_characters,
                                 [&entry, selection](const Character& character)
                                 {
                                     return selection != nullptr
                                                ? character.slot == entry.slot &&
                                                      character.revision == selection->Slots()[entry.slot].revision
                                                : SameEntry(character.entry, entry);
                                 });
        if (same != m_characters.end())
        {
            synced.push_back(std::move(*same));
            continue;
        }
        synced.push_back(MakeCharacter(entry));
        changed = true;
    }
    // 0x634680 walks the pool's five records by index, not in the packet's order.
    std::ranges::sort(synced, {}, &Character::slot);
    m_characters = std::move(synced);
    if (!m_tookTheList)
    {
        m_tookTheList = true;
        // 0x63e820, once: some character of the five slots with flags bit 0x10.
        m_bar.SetBlockedAccount(std::ranges::any_of(entries, [](const Protocol::FrontEnd::CharacterEntry& entry)
                                                    { return (entry.flags & BlockedAccountFlag) != 0; }));
        // 0x4c20a0 → 0x622b20 → 0x63d7c0: classes 0 to pkt[4].
        m_create.EnableClasses(m_session->Characters()->creatableClasses);
        m_context.log.Info("la selección tiene {} personajes", m_characters.size());
    }
    else if (changed)
    {
        m_context.log.Info("la selección tiene ahora {} personajes", m_characters.size());
    }
}

bool CharacterSelectScene::PrepareTheCharacters()
{
    if (m_charactersPrepared)
    {
        return true;
    }
    if (m_context.world == nullptr || m_context.cloth == nullptr || m_context.selection == nullptr)
    {
        m_charactersPrepared = true;
        return true;
    }
    const auto* terrain = m_context.selection->Terrain();
    if (terrain == nullptr)
    {
        m_failure = "0x633f65: falta el terreno 54 del constructor de la vista previa";
        return false;
    }
    const Actors::ActorConstructionContext construction{.terrain = *terrain,
                                                        .map = m_context.world->Runtime().map,
                                                        .scene = Terrain::CullingScene::CharacterSelect,
                                                        .fixedHeight = CameraState().fixedHeight};
    constexpr std::uint8_t DefaultPreviewClass = Kernel::ClassNumberOf(Kernel::CharacterClass::DarkKnight);
    constexpr std::uint16_t DefaultPreviewModel = FrontEnd::FirstNewFaceModel + DefaultPreviewClass;
    auto first = Actors::ConstructActor(Preview(), DefaultPreviewModel, {}, construction, *m_context.cloth);
    if (!first.HasValue())
    {
        m_failure = first.Error();
        return false;
    }
    Preview().characterClass = DefaultPreviewClass;
    Preview().byte88 = 0;
    auto window =
        FrontEnd::RebuildPreview(Preview(), m_create.ChosenClass(), m_selectionModels, construction, *m_context.cloth);
    if (!window.HasValue())
    {
        m_failure = window.Error();
        return false;
    }
    // 0x633f91: emptying before the partial reset; not the F3/10 one, which touches other fields and grids.
    FrontEnd::ClearSelectionInventory(*m_context.world, m_context.cloth->random);
    // 0x5bc0e5..0x5bc102: first it closes 0x400000, then 0x800000, with aux0; the trade is emptied only afterwards.
    // Both windows share mode 1/2 and their close sends the 31 before lowering that mode (0x657ba7/0x657dc1).
    std::function<void(Protocol::Outgoing)> send;
    if (m_context.worldEntry != nullptr)
        send = m_context.worldEntry->sendGamePacket;
    if (!send && m_session != nullptr)
        send = [session = m_session](Protocol::Outgoing packet) { session->SendGamePacket(std::move(packet)); };
    Hud::GamePanels panels(*m_context.world, m_context.cloth->random);
    panels.LendTheSiegeControls(*m_context.world, std::move(send));
    for (const auto id : {Ui::SiegeWeaponPanel, Ui::SecondSiegeWeaponPanel})
    {
        auto closed = panels.Registry().Close(id, false);
        if (!closed.HasValue())
        {
            m_failure = std::move(closed).Error();
            return false;
        }
    }
    // 0x5bc107..0x5bc13f: then it empties the trade.
    Social::ClearTheTradeForSelection(m_context.world->Runtime().social.trade);
    // 0x634049..0x634177: the partial reset, with the close of 0x4fc450's same record.
    const Models::LogicalAccess access(m_context.world->Runtime().ownRecordDepth, m_context.cloth->random,
                                       Models::OwnRecordCloseRandoms);
    m_context.world->OwnCharacter().ResetForSelection();
    m_charactersPrepared = true;
    // 0x634199..0x634227: the dialog fields off (0x77f3b68 the guild registration one, 0x77f3b69 the panels one and
    // 0x77f3b6a the zen mode, CL9 milestone 8), the chat field open (0x6c8e68), the 120 entries empty and, if either of
    // the list's two pkt[5] numbers (0x5767958 and 0x576795c, 0x4c20a0) is not 0, 1240 with them, as system and without
    // a name (nobody writes the name at 0x7d3a17c). Emptying the fields (0x553570) belongs to the chat input.
    Kernel::WorldRuntime& runtime = m_context.world->Runtime();
    runtime.chat.input.dialogFieldOn = false;
    runtime.chat.input.otherInput = false;
    runtime.social.moneyField = 0;
    runtime.ui.chatOpen = true;
    if (auto emptied = Chat::AddTheEmptyMessages(runtime, m_context.textMeasure); !emptied.HasValue())
    {
        m_failure = emptied.Error();
        return false;
    }
    if (auto noticed = AddTheAccountNotice(); !noticed.HasValue())
    {
        m_failure = noticed.Error();
        return false;
    }
    // 0x634271..0x6342a0: the commands window ([0x6e592c]) closed and without a command.
    Ui::ResetTheCommands(runtime.panels.delegated);
    return true;
}

Core::Expected<void, std::string> CharacterSelectScene::AddTheAccountNotice()
{
    if (m_session == nullptr || !m_session->Characters() || m_context.textMeasure == nullptr)
        return {};
    const std::uint8_t numbers = m_session->Characters()->notice;
    const unsigned general = numbers & 0xfu;
    const unsigned gladiator = static_cast<unsigned>(numbers >> 4);
    if (general == 0 && gladiator == 0)
        return {};
    const std::string first = std::to_string(general);
    const std::string second = std::to_string(gladiator);
    const std::array<std::string_view, 2> arguments{first, second};
    // The original uses the text as the sprintf format (Q-C61): MuRet puts the two numbers in their marks.
    const std::string text =
        Ui::PlaceArguments(Ui::TextAt(m_texts, AccountNoticeText).value_or(std::string_view{}), arguments);
    return Chat::AddGameMessage(m_context.world->Runtime(), *m_context.textMeasure, m_screen, {}, text,
                                Ui::SystemMessage);
}

const Terrain::WorldTerrain* CharacterSelectScene::StepTerrain() const
{
    if (m_context.selection != nullptr && m_context.selection->Terrain() != nullptr)
        return m_context.selection->Terrain();
    return m_context.states != nullptr ? &m_context.states->terrain.current : nullptr;
}

Actors::CharacterStepContext CharacterSelectScene::CharacterContext()
{
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    return {.world = *m_context.world,
            .random = m_context.cloth->random,
            .scene = Kernel::SceneId::CharacterSelect,
            .playerSkeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId),
            .items = m_items,
            .terrain = StepTerrain(),
            .darkSpiritSkeleton = m_models.SkeletonOf(Models::ModelId{Pets::DarkSpiritModel}),
            .particles = &m_context.world->Particles(),
            .worldClock = clock,
            .counter = m_context.cloth->logicSteps,
            .newFaceSkeleton = m_selectionModels.SkeletonOf(Models::ModelId{Preview().type}),
            .soundRequest = [this](Audio::SoundId sound) { m_context.world->Runtime().RequestSound(sound); },
            .soundAt = [this](Audio::SoundId sound, const glm::vec3& origin)
            { m_context.world->Runtime().RequestSound(sound, origin); },
            .groundMarks = m_context.states != nullptr ? &m_context.states->terrain.groundMarks : nullptr,
            .trapezoid = m_context.states != nullptr ? &m_context.states->camera.trapezoid : nullptr,
            .dynamicLighting =
                m_context.states != nullptr ? &m_context.states->terrain.dynamicLight.Lighting() : nullptr,
            .faunaModels = m_context.states != nullptr ? &m_context.states->assets.fauna : nullptr,
            .reportedCell = m_context.states != nullptr ? &m_context.states->actors.reportedCell : nullptr,
            .hooks = &m_stepHooks,
            .effectHooks = &m_effectHooks};
}

// 0x6342f9, then 0x634303: the 400 records and the NewFace object, before the UI.
void CharacterSelectScene::StepCharacters()
{
    if (m_context.world != nullptr && m_context.cloth != nullptr && m_context.selection != nullptr)
    {
        const bool world = HasTheWorld();
        const float worldClock =
            m_context.clock != nullptr
                ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
                : 0.0f;
        if (world)
        {
            // 0x6342e1: 0x606560, the 54's objects the last draw saw (0x4250f0).
            const auto map = static_cast<std::uint8_t>(m_context.world->Runtime().map);
            if (auto loaded = m_world.Load(m_context.dataRoot, map, m_gameSlots, &m_models); !loaded.HasValue())
            {
                m_failure = loaded.Error();
                return;
            }
            if (auto objects = m_world.StepObjects(*m_context.world, m_context.states->terrain.current,
                                                   m_context.states->terrain.dynamicLight.Lighting(),
                                                   m_context.cloth->random, worldClock);
                !objects.HasValue())
            {
                m_failure = objects.Error();
                return;
            }
        }
        // 0x6342f4: 0x445230, the helpers, before the pool. Scene 4 does not turn off a dead rider's (Q-C293).
        if (auto helpers = Pets::StepHelpers(Pets::HelperStepContext{.world = *m_context.world,
                                                                     .models = &m_models,
                                                                     .random = m_context.cloth->random,
                                                                     .worldClock = worldClock,
                                                                     .scene = Kernel::SceneId::CharacterSelect,
                                                                     .terrain = StepTerrain(),
                                                                     .logicSteps = m_context.cloth->logicSteps});
            !helpers.HasValue())
        {
            m_failure = "0x6342f4 → " + helpers.Error();
            return;
        }
        const Actors::CharacterStepContext context = CharacterContext();
        if (auto stepped = Actors::StepCharacters(context); !stepped.HasValue())
        {
            m_failure = stepped.Error();
            return;
        }
        if (auto preview = Actors::StepCharacter(Preview(), context); !preview.HasValue())
        {
            m_failure = preview.Error();
            return;
        }
        const std::function<double(float, float)> height = TerrainHeight();
        if (world)
        {
            // 0x52d220 and 0x53c670, after the preview: the effects with a model and the joints. timeGetTime
            // (0x6b8544), from the Fenrirs' decal 0x14e: the scene clock in ms.
            const auto systemTime =
                m_context.clock != nullptr
                    ? static_cast<std::uint32_t>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                    : 0U;
            if (auto effects = m_world.StepEffects(*m_context.world, m_context.states->terrain.dynamicLight.Lighting(),
                                                   *m_context.cloth, worldClock, height, systemTime);
                !effects.HasValue())
            {
                m_failure = effects.Error();
                return;
            }
        }
        // 0x634315 → 0x5446e0, after the preview: it also draws the wind with the empty table. The 0x622/1 mist reads
        // its owner among the 54's objects and the world clock.
        Particles::PortableParticleTrigonometry trigonometry;
        if (auto particles = Particles::StepParticlesChecked(
                m_context.world->Particles(), context.random, std::cref(worldClock), &m_context.world->Runtime().map,
                trigonometry, height, Effects::ParticleOwnersAmong(m_context.world->Map().objects));
            !particles.HasValue())
        {
            m_failure = std::format("0x634315 → 0x5446e0: el paso de las partículas falla con error {}",
                                    static_cast<unsigned>(particles.Error()));
            return;
        }
        m_previewAdvanced = true;
        for (Character& character : m_characters)
        {
            const Kernel::ActorState& actor = m_context.world->Actors()[character.slot];
            character.animation.body = actor.animation;
            Actors::StepCharacterAttachments(character.animation, Actors::AppearanceOfActor(actor), m_models);
        }
        return;
    }
    // The UI tests without WorldState or terrain keep the presentation animation.
    for (Character& character : m_characters)
    {
        // What ends returns to the standing one (0x4fcb10), which in the selection is 3: since 3 repeats, it is the
        // current one, and SetAction changes nothing.
        static_cast<void>(Actors::StepCharacterAnimation(character.animation, character.appearance, m_models,
                                                         Models::AnimationScene::CharacterSelect));
    }
}

// Q-C70: in the selection, an FD_CLOSE opens notice 0x70, as in the login, once. MuRet also opens it without a reply
// (NoReply), like the login (Q-C69).
bool CharacterSelectScene::ShowTheFailure()
{
    if (m_failureShown || m_session == nullptr || m_session->State() != Session::SessionState::Failed)
    {
        return false;
    }
    m_failureShown = true;
    m_context.log.Info("la sesión falló en la selección: se abre el aviso {:#04x}",
                       static_cast<int>(Ui::ConnectFailedNotice));
    OpenNotice(Ui::ConnectFailedNotice);
    return true;
}

bool CharacterSelectScene::TakeTheCreationResult()
{
    if (!m_waitingForTheCreation || m_session == nullptr ||
        m_session->State() == Session::SessionState::WaitingCreateResult)
    {
        return false;
    }
    m_waitingForTheCreation = false;
    const std::optional<std::uint8_t> result = m_session->CreateResult();
    // 0x4c23d2: with the created one, the balloons again (0x63cfb0), even if the session has dropped.
    if (result && !Ui::NoticeOfCreateResult(*result))
    {
        UpdateTheBalloons();
    }
    if (!result || m_failureShown)
    {
        return false;
    }
    const std::optional<Ui::NoticeId> notice = Ui::NoticeOfCreateResult(*result);
    if (!notice)
    {
        // Created: 0x65f6c0(+0x8) closes 0x5d; the new one is already in the list.
        if (m_notice.Notice() == Ui::PleaseWaitNotice)
        {
            m_ui.Close(m_notice);
        }
        return false;
    }
    // The window before the notice, which stays on top (0x6431b0), with the field empty.
    OpenTheCreateWindow();
    OpenNotice(*notice);
    return true;
}

void CharacterSelectScene::StepTheCreateWindow(const Input::InputState& input, Ui::NewUiTurn turn)
{
    if (m_create.Step(input, m_screen, turn) == FrontEnd::CreateWindowEvent::Submitted)
    {
        SubmitTheCreation();
    }
    // At the end of 0x63d880, the logic (with the focus): the preview of the class that stayed, and its advance
    // (0x63e430).
    if (turn.logic)
    {
        StepThePreview();
    }
}

void CharacterSelectScene::OpenTheCreateWindow()
{
    m_create.Open();
    m_ui.Raise(m_create);
    m_previewDrawFrames = {};
}

void CharacterSelectScene::OpenNotice(Ui::NoticeId notice, std::string_view argument)
{
    m_notice.Open(notice, argument);
    m_ui.Raise(m_notice);
}

void CharacterSelectScene::OpenTheEscapeWindow()
{
    m_escape.Open();
    m_ui.Raise(m_escape);
}

void CharacterSelectScene::OpenTheOptions()
{
    m_options.Open();
    m_ui.Raise(m_options);
}

FrontEnd::OptionsSettings CharacterSelectScene::CurrentOptions() const
{
    return m_context.world != nullptr ? Hud::OptionsSettingsOf(m_context.world->Runtime()) : m_localOptions;
}

void CharacterSelectScene::OnOptionsEvent(const FrontEnd::OptionsEvent& event)
{
    // The close button (without sound) and Esc (with 0x19, from CL10) already closed them.
    if (m_context.world != nullptr)
    {
        Kernel::WorldRuntime& runtime = m_context.world->Runtime();
        Hud::ApplyOption(runtime, event);
        if (event.kind == FrontEnd::OptionsEvent::Kind::Volume)
            runtime.SetSoundVolume(static_cast<std::uint8_t>(event.value));
        return;
    }
    FrontEnd::ApplyOption(m_localOptions, event);
}

void CharacterSelectScene::SubmitTheCreation()
{
    if (const std::optional<Ui::NoticeId> refused =
            FrontEnd::NoticeOfNewName(m_create.Name(), m_create.Buffer(), m_nameFilters))
    {
        // The name does not go to the log: only the notice. The window stays below, with the name.
        m_context.log.Info("el nombre del personaje nuevo no pasa: el aviso {:#04x}", static_cast<int>(*refused));
        OpenNotice(*refused);
        return;
    }
    if (m_session == nullptr)
    {
        return;
    }
    m_session->CreateCharacter(m_create.Name(), m_create.ChosenClass());
    if (m_session->State() == Session::SessionState::WaitingCreateResult)
    {
        m_waitingForTheCreation = true;
        m_ui.Close(m_create);
        OpenNotice(Ui::PleaseWaitNotice);
    }
}

bool CharacterSelectScene::TakeTheDeletionResult()
{
    if (!m_waitingForTheDeletion || m_session == nullptr ||
        m_session->State() == Session::SessionState::WaitingDeleteResult)
    {
        return false;
    }
    m_waitingForTheDeletion = false;
    const std::optional<std::uint8_t> result = m_session->DeleteResult();
    // 0x643360..0x6433b3: notice 0x60 lowers the chosen one's +0 and sets the balloons again (0x63cfb0). In MuRet the
    // deleted one already left the session's list (Q-C104).
    if (result && Ui::NoticeOfDeleteResult(*result) == Ui::CharacterDeletedNotice)
    {
        UpdateTheBalloons();
    }
    // With the session dropped, the failure's 0x70 stays on top even if the reply arrived before.
    if (!result || m_failureShown)
    {
        return false;
    }
    OpenNotice(Ui::NoticeOfDeleteResult(*result));
    return true;
}

// The open notice has the mouse (Q-C84) and 0x634680 does not call 0x59ec80 (+0x8de8): the outline stays on the last
// one that was under the mouse. The OK of one that closes the game (0x70, Q-C69) says so in WantsToQuit; that of the
// delete confirmation opens the code one, and the code one sends the F3 02. The others just close.
void CharacterSelectScene::StepTheNotice(const Input::InputState& input, Ui::NewUiTurn turn)
{
    const std::optional<Ui::NoticeId> closed = m_notice.Step(input, m_screen, turn);
    if (!closed)
    {
        return;
    }
    switch (Ui::FollowUpOf(*closed))
    {
    case Ui::NoticeFollowUp::Quit:
        m_context.log.Info("el OK del aviso cierra el juego (el {:#04x})", static_cast<int>(*closed));
        m_quit = true;
        break;
    case Ui::NoticeFollowUp::AskForTheCode:
        OpenNotice(Ui::DeletionCodeNotice);
        break;
    case Ui::NoticeFollowUp::SendTheDeletion:
        SendTheDeletion();
        break;
    case Ui::NoticeFollowUp::Close:
    case Ui::NoticeFollowUp::ReopenAtAccount:
    case Ui::NoticeFollowUp::ReopenAtPassword:
        break;
    }
}

void CharacterSelectScene::EnableTheBar()
{
    bool aFreeSlot = true;
    if (m_session != nullptr && m_session->Characters())
    {
        const std::vector<Protocol::FrontEnd::CharacterEntry>& entries = m_session->Characters()->characters;
        aFreeSlot = false;
        for (std::uint8_t slot = 0; slot <= Protocol::FrontEnd::MaxCharacterSlot; ++slot)
        {
            if (std::ranges::find(entries, slot, &Protocol::FrontEnd::CharacterEntry::slot) == entries.end())
            {
                aFreeSlot = true;
                break;
            }
        }
    }
    m_bar.Enable(aFreeSlot, m_session != nullptr && m_session->ChosenCharacter() != nullptr);
}

void CharacterSelectScene::OnTheBar(FrontEnd::SelectionBarClick click)
{
    switch (click)
    {
    case FrontEnd::SelectionBarClick::Connect:
        TryToEnter();
        break;
    case FrontEnd::SelectionBarClick::Menu:
        // 0x63eb90 with +0x31a: the Esc one (0x65f690).
        OpenTheEscapeWindow();
        break;
    case FrontEnd::SelectionBarClick::Create:
        OpenTheCreateWindow();
        break;
    case FrontEnd::SelectionBarClick::Delete:
        AskToDelete();
        break;
    }
}

void CharacterSelectScene::AskToDelete()
{
    const Protocol::FrontEnd::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    if (chosen == nullptr)
    {
        return;
    }
    const Ui::NoticeId notice =
        FrontEnd::NoticeOfDeleteButton(*chosen, FrontEnd::FreeDeletionGroup(m_texts, m_session->ChosenServer()));
    m_context.log.Info("borrar el personaje del lugar {} abre el aviso {:#04x}", chosen->slot,
                       static_cast<int>(notice));
    // The name goes in the %s of text 1712 of 0x5e (0x6431b0); it does not go to the log.
    OpenNotice(notice, Protocol::FrontEnd::NameAsText(chosen->name));
}

void CharacterSelectScene::SendTheDeletion()
{
    if (m_session == nullptr)
    {
        return;
    }
    // The code, like the password, does not go to the log, and does not stay in the window.
    m_session->DeleteCharacter(Protocol::FrontEnd::DeleteCodeFromText(m_notice.TakeCode()));
    if (m_session->State() == Session::SessionState::WaitingDeleteResult)
    {
        m_waitingForTheDeletion = true;
        OpenNotice(Ui::PleaseWaitNotice);
    }
}

void CharacterSelectScene::Step(const Input::InputState& input)
{
    if (m_failure)
    {
        return;
    }
    StepTheScene(input);
    if (!m_failure && m_context.cloth != nullptr)
    {
        // 0x637cb2 → 0x6342c0 comes before the wind and the counter of 0x637cec/0x637f91.
        Models::StepTheClothWorld(*m_context.cloth);
    }
}

void CharacterSelectScene::StepTheScene(const Input::InputState& input)
{
    m_previewAdvanced = false;
    // The new UI before the scene's step (0x63778c; Q-C490), with what opened up to the previous step: a notice that
    // opens in this step is handled from the next one, and what was pressed in this one does not close it before it is
    // seen. The step does not see the Esc or the Enter a window takes (Q-C45).
    StepTheWindows(input);
    if (m_failure || m_quit)
    {
        return;
    }
    static_cast<void>(ShowTheFailure());
    if (WaitingForTheList())
    {
        return;
    }
    if (m_context.selection != nullptr && m_session != nullptr && !m_context.selection->IsInitialized())
    {
        if (m_context.states == nullptr)
        {
            m_failure = "0x633f20: la selección no carga su mundo sin los estados de los módulos";
            return;
        }
        if (auto initialized = m_context.selection->InitializeSelection(
                m_context.dataRoot, m_context.states->terrain, CameraInputState(),
                m_context.cloth != nullptr ? &m_context.cloth->random : nullptr, &m_context.states->assets.fauna);
            !initialized.HasValue())
        {
            m_failure = initialized.Error();
            return;
        }
    }
    // 0x633f20 → 0x65f4b0, with the list: the balloons (0x63ce70, at the start), the bar, with 0x65f690 (on top and
    // with the focus), and the balloons with the records (0x63cfb0, at the end).
    if (!m_openedTheBar)
    {
        m_balloons.Build();
        m_bar.Open();
        m_ui.Raise(m_bar);
        m_openedTheBar = true;
        UpdateTheBalloons();
    }
    if (m_context.world != nullptr)
    {
        const Terrain::WorldTerrain* terrain = m_context.selection != nullptr ? m_context.selection->Terrain()
                                               : m_context.states != nullptr  ? &m_context.states->terrain.current
                                                                              : nullptr;
        if (m_context.states != nullptr && terrain != nullptr && terrain->lighting != nullptr)
        {
            m_context.states->terrain.dynamicLight.Update(*terrain->lighting);
        }
        // 0x5fbf60, after the light copy: the waves of the range window with the step's clock.
        if (HasTheWorld() && m_context.clock != nullptr)
        {
            m_world.StepWaves(m_context.clock->Now(), m_context.states->terrain.dynamicLight.Range(),
                              static_cast<std::uint8_t>(m_context.world->Runtime().map));
        }
    }
    ++m_steps;
    SyncTheCharacters();
    if (!PrepareTheCharacters())
    {
        return;
    }
    StepCharacters();
    if (m_failure)
    {
        return;
    }
    static_cast<void>(TakeTheDeletionResult());
    static_cast<void>(TakeTheCreationResult());
    EnableTheBar();
    // 0x634341: Enter (0x8001) with the notice, the create one, the Esc one and the options closed: TryToEnter. If
    // Enter was not pressed, Esc with the same ones closed: sound 0x19 (0x41ed10, from CL10) and the Esc one
    // (0x6344ce). A pressed Enter does not let Esc be read, even if it does not enter. The original loses a short press
    // between two reads; MuRet sees it (Q-C45).
    const bool modal = m_notice.IsOpen() || m_create.IsOpen() || m_escape.IsOpen() || m_options.IsOpen();
    if (input.TakePress(Input::Key::Enter))
    {
        if (!modal)
        {
            if (m_session != nullptr && m_session->ChosenCharacter() != nullptr && m_context.world != nullptr)
            {
                m_context.world->Runtime().RequestSound(ButtonClickSound);
            }
            TryToEnter();
        }
    }
    else if (input.TakePress(Input::Key::Escape) && !modal)
    {
        if (m_context.world != nullptr)
        {
            m_context.world->Runtime().RequestSound(ButtonClickSound);
        }
        OpenTheEscapeWindow();
    }
    const auto now = Kernel::LogicStep * static_cast<std::int64_t>(m_steps);
    const bool doubleClick = m_doubleClick.Step(input, now);
    // 0x6344d3: with the mouse on a window, the UI has it (+0x8de8): the scene does not choose, does not do the double
    // click and does not look for who is below, so the outline stays on the last one.
    if (!m_ui.HasTheMouse())
    {
        m_hovered = UnderTheMouse(input);
        if (doubleClick && m_hovered)
        {
            // The double click chooses and enters like Enter, without the sound (0x6342c0).
            Choose(m_hovered);
            TryToEnter();
            return;
        }
        if (input.WasPressed(Input::MouseButton::Left))
        {
            // 0x634671: chosen, 0x63eb40 enables the bar's buttons, which the next UI step already handles that way.
            Choose(m_hovered);
            EnableTheBar();
        }
    }
}

void CharacterSelectScene::StepTheWindows(const Input::InputState& input)
{
    // The original passes each window the frame's milliseconds; MuRet, the step's (Q-C490).
    constexpr double StepMs = std::chrono::duration<double, std::milli>(Kernel::LogicStep).count();
    m_ui.Step(
        input, m_screen,
        [&](Ui::NewUiWindow& window, Ui::NewUiTurn turn)
        {
            if (&window == &m_notice)
            {
                StepTheNotice(input, turn);
            }
            else if (&window == &m_escape)
            {
                if (const std::optional<FrontEnd::EscapeWindowEvent> event =
                        m_escape.Step(input, m_screen, StepMs, turn))
                {
                    OnEscapeEvent(*event);
                }
            }
            else if (&window == &m_options)
            {
                if (const std::optional<FrontEnd::OptionsEvent> event =
                        m_options.Step(input, m_screen, StepMs, CurrentOptions(), turn))
                {
                    OnOptionsEvent(*event);
                }
            }
            else if (&window == &m_bar)
            {
                if (const std::optional<FrontEnd::SelectionBarClick> click = m_bar.Step(input, m_screen, turn))
                {
                    OnTheBar(*click);
                }
            }
            else if (&window == &m_create)
            {
                StepTheCreateWindow(input, turn);
            }
        },
        [this]
        {
            if (m_context.world != nullptr)
            {
                m_context.world->Runtime().RequestSound(ButtonClickSound);
            }
        });
}

void CharacterSelectScene::OnEscapeEvent(const FrontEnd::EscapeWindowEvent& event)
{
    switch (event.kind)
    {
    case FrontEnd::EscapeWindowEvent::Kind::Countdown:
        AddTheCountdown(event.seconds);
        return;
    case FrontEnd::EscapeWindowEvent::Kind::Quit:
        // 0x647d87: the original's log (0x41fa40) and the PostMessage with WM_CLOSE: Main does it.
        m_context.log.Info("> Menu - Exit game.");
        m_quit = true;
        return;
    case FrontEnd::EscapeWindowEvent::Kind::JoinAnotherServer:
        JoinAnotherServer();
        return;
    case FrontEnd::EscapeWindowEvent::Kind::Options:
        // 0x647e6c: 0x65f6c0 closes it and 0x65f690 opens the options.
        OpenTheOptions();
        return;
    case FrontEnd::EscapeWindowEvent::Kind::Closed:
        // With Esc, sound 0x19 (0x41ed10, from CL10).
        return;
    }
}

void CharacterSelectScene::AddTheCountdown(int seconds)
{
    // 0x647ef0: sprintf of 380 with what is left and 0x555190, as system and without a name.
    if (m_context.world == nullptr || m_context.textMeasure == nullptr)
    {
        return;
    }
    const std::string left = std::to_string(seconds);
    const std::array<std::string_view, 1> arguments{left};
    const std::string text =
        Ui::PlaceArguments(Ui::TextAt(m_texts, FrontEnd::LeavingTheGameText).value_or(std::string_view{}), arguments);
    if (auto added = Chat::AddGameMessage(m_context.world->Runtime(), *m_context.textMeasure, m_screen, {}, text,
                                          Ui::SystemMessage);
        !added.HasValue())
    {
        m_failure = added.Error();
    }
}

void CharacterSelectScene::JoinAnotherServer()
{
    // 0x647e08: the original's log (0x41fa40), the C3 F1 02 02 (0x647f60), and the Esc one (already closed in its step)
    // and the bar (+0x7f50, 0x647e48) close. What the GS answers, the return to the list, belongs to CL9 (Q-C79).
    m_context.log.Info("> Menu - Join another server.");
    if (m_context.cloth == nullptr)
    {
        m_context.log.Error("no se pide la salida: falta el rand() de la CRT del C3 F1 02 (0x647f60)");
    }
    else
    {
        // 0x648241: one rand() for the C3's padding; 0x64826b..: the serial (0x5767913) through the protection table,
        // one byte, which is encrypted again on closing with another.
        static_cast<void>(m_context.cloth->random.Next());
        std::uint8_t serialDepth = 0;
        {
            const Models::LogicalAccess serial(serialDepth, m_context.cloth->random, Models::SerialCloseRandoms);
        }
        if (m_session != nullptr)
        {
            m_session->RequestLogout(Protocol::Session::LogoutReturnsToServerList);
        }
    }
    m_ui.Close(m_escape);
    m_ui.Close(m_bar);
}

std::optional<std::uint8_t> CharacterSelectScene::UnderTheMouse(const Input::InputState& input) const
{
    if (!m_lastPass || m_characters.empty())
    {
        return std::nullopt;
    }
    const auto [origin, end] = m_lastPass->RayThrough(input.MouseX(), input.MouseY());
    std::vector<glm::vec3> positions;
    positions.reserve(m_characters.size());
    for (const Character& character : m_characters)
    {
        positions.push_back(ObjectOf(character).position);
    }
    const std::optional<std::size_t> nearest = Models::NearestHit(origin, end, positions);
    return nearest ? std::optional<std::uint8_t>{m_characters[*nearest].slot} : std::nullopt;
}

void CharacterSelectScene::Choose(std::optional<std::uint8_t> slot)
{
    if (m_session != nullptr)
    {
        m_session->ChooseCharacter(slot);
    }
}

void CharacterSelectScene::TryToEnter()
{
    if (m_session == nullptr)
    {
        return;
    }
    const Protocol::FrontEnd::CharacterEntry* chosen = m_session->ChosenCharacter();
    if (chosen == nullptr)
    {
        return;
    }
    if (Protocol::FrontEnd::IsLocked(*chosen))
    {
        OpenNotice(Ui::LockedCharacterNotice);
        return;
    }
    if (m_context.world != nullptr)
    {
        if (m_context.cloth == nullptr)
        {
            m_context.log.Error("no se entra al mundo: falta el rand() de la CRT para cerrar el registro propio");
            return;
        }
        // 06, "La entrada al mundo", step 1: the own record and scene 3's flag.
        m_context.world->PrepareSelectedActor(*chosen, m_context.cloth->random);
    }
    m_context.log.Info("se elige el personaje del lugar {} para entrar al mundo", chosen->slot);
    m_next = Kernel::SceneId::WorldLoading;
}

Models::ObjectTransform CharacterSelectScene::ObjectOf(const Character& character) const
{
    const FrontEnd::CharacterSlot& slot = FrontEnd::CharacterSlots[character.slot];
    const glm::vec3 position = m_context.selection != nullptr
                                   ? m_context.selection->Slots()[character.slot].position
                                   : glm::vec3(slot.position.x, slot.position.y, FrontEnd::CharacterSlotHeight);
    return Models::ObjectTransform{position, {0.0f, 0.0f, slot.angle}, character.appearance.scale, 0.0f};
}

Render::Camera3D CharacterSelectScene::StepTheCameraAndTheTrapezoid(const Camera::GameCameraInput& input,
                                                                    Core::Duration clock)
{
    // Like CameraForFrame: an intermediate draw only queries the last frame's camera.
    const int frames = m_cameraFrames.FramesAt(clock, OriginalFramePaceOf(m_context.pacing));
    if (frames == 0)
    {
        const Render::Camera3D camera = Camera::GameCameraOf(CameraState(), input);
        if (m_context.world != nullptr && m_context.states != nullptr)
        {
            // The trapezoid of the terrain and objects pass, with the current level, without writing anything.
            auto polygon =
                Camera::TerrainPolygonBetweenFrames(m_context.states->camera, SelectionPolygonInput(input, camera));
            if (!polygon.HasValue())
            {
                m_failure = polygon.Error();
                return camera;
            }
            m_polygon = polygon.Value();
        }
        return camera;
    }
    Render::Camera3D camera;
    for (int frame = 0; frame < frames; ++frame)
    {
        // 0x6346ca: 0x635ae0.
        camera = Camera::StepGameCamera(CameraState(), input);
        // 0x634736..0x63475c, after the backgrounds: 0x5fbb00(w · 0.0015625, &p) with 0x620db0(0, 0, w, 48), the pass
        // that does not draw. Nobody in MuRet reads the block range in the selection (08 P-C41 b); the trapezoid and
        // its level go to the globals (0x5fbeef → 0x5fb570). Nothing in 0x634736..0x634769 calls rand() (*static*).
        // Without WorldState there is nowhere to leave them: those are the tests that do not look at it.
        if (m_context.world == nullptr || m_context.states == nullptr)
        {
            continue;
        }
        auto polygon = Camera::StepTheTerrainPolygon(m_context.states->camera, SelectionPolygonInput(input, camera),
                                                     CameraState());
        if (!polygon.HasValue())
        {
            m_failure = polygon.Error();
            return camera;
        }
        m_polygon = polygon.Value();
    }
    return camera;
}

void CharacterSelectScene::Render(Render::Frame::SceneCanvas& canvas, float alpha)
{
    if (m_failure)
    {
        return;
    }
    m_screen = canvas.screen;
    if (WaitingForTheList() ||
        (m_context.selection != nullptr && m_session != nullptr && !m_context.selection->IsInitialized()))
    {
        // Without the list, 0x634680 does not draw; the notice belongs to the UI, which does, and carries the cursor on
        // top (the system one is hidden). Without a notice, the frame stays held (HoldsTheLastFrame).
        m_notice.Draw(canvas.sprites, canvas.text, canvas.screen);
        if (m_notice.IsOpen())
        {
            DrawTheCursor(canvas);
        }
        return;
    }
    // The handlers rebuild or deactivate actors even if this frame does not run a logic step.
    SyncTheCharacters();
    Render::SpriteBatch& sprites = canvas.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    sprites.Quad(Background(0x717, 0.0f));
    sprites.Quad(Background(0x718, 320.0f));
    canvas.flushSprites();

    // 0x6346ca computes the camera with the current shared hero before drawing the characters, and 0x63475c the terrain
    // trapezoid.
    Camera::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.scene = Terrain::CullingScene::CharacterSelect;
    const std::optional<Render::Frame::SceneWorld> world = WorldOf(Kernel::SceneId::CharacterSelect);
    const std::uint8_t map = world ? world->map : 0;
    cameraInput.map = map;
    const Render::Camera3D camera = StepTheCameraAndTheTrapezoid(cameraInput, canvas.frameClock);
    if (m_failure)
    {
        return;
    }
    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, Camera::WorldViewport, camera);
    m_terrainRange = Terrain::TerrainRangeOf(pass.view, camera);
    if (m_context.world != nullptr && m_context.states != nullptr)
    {
        m_context.states->terrain.dynamicLight.PublishRange(m_terrainRange);
    }
    m_lastPass = pass;
    canvas.models.Begin(pass);
    if (world)
    {
        canvas.world.Draw(*world, pass, alpha);
    }
    const int scanFrames = m_fullSetScanFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    Kernel::PoseBuffer projection;
    if (scanFrames == 0 && m_context.world != nullptr)
        projection = m_context.world->PoseCarry();
    auto* poseCarry = m_context.world == nullptr ? nullptr
                      : scanFrames > 0           ? &m_context.world->PoseCarry()
                                                 : &projection;
    DrawTheWorldAndTheCharacters(canvas, alpha, pass, camera, map, poseCarry, scanFrames);
    // As in the game, the open batch is also flushed when the draw stops.
    canvas.flushModels();
    if (m_failure)
    {
        return;
    }
    // 0x634949..0x63495f: the notices and the old area. The mode 4 UI (0x65fe00): the bar, the create window and the
    // notice on top. And the cursor (0x634980).
    DrawTheMessages(canvas);
    // 0x65fe00 → 0x63cf80: the balloons, before the windows.
    DrawTheBalloons(canvas, pass, camera.fieldOfView);
    if (m_failure)
    {
        return;
    }
    // +0x511: the first 0D with 1 that arrived in the selection, which the game keeps.
    std::string_view serverNotice;
    if (m_context.world != nullptr && m_context.world->Runtime().chat.selectionNotice)
        serverNotice = *m_context.world->Runtime().chat.selectionNotice;
    for (Ui::NewUiWindow* window : m_ui.DrawOrder())
    {
        if (window == &m_bar)
        {
            m_bar.Draw(sprites, canvas.text, canvas.screen, serverNotice);
        }
        else if (window == &m_create)
        {
            DrawTheCreateWindow(canvas, alpha);
        }
        else if (window == &m_notice)
        {
            m_notice.Draw(sprites, canvas.text, canvas.screen);
        }
        else if (window == &m_escape)
        {
            m_escape.Draw(sprites, canvas.text, canvas.screen);
        }
        else if (window == &m_options)
        {
            m_options.Draw(sprites, canvas.text, canvas.screen, CurrentOptions());
        }
    }
    DrawTheCursor(canvas);
}

std::array<FrontEnd::BalloonRecord, FrontEnd::BalloonCount> CharacterSelectScene::BalloonRecords() const
{
    std::array<FrontEnd::BalloonRecord, FrontEnd::BalloonCount> records{};
    if (m_context.world == nullptr)
    {
        return records;
    }
    const Protocol::FrontEnd::CharacterList* list =
        m_session != nullptr && m_session->Characters() ? &*m_session->Characters() : nullptr;
    for (std::size_t slot = 0; slot < FrontEnd::BalloonCount; ++slot)
    {
        const Kernel::ActorState& actor = m_context.world->Actors()[slot];
        const bool listed =
            m_session == nullptr ||
            (list != nullptr &&
             std::ranges::any_of(list->characters, [slot](const Protocol::FrontEnd::CharacterEntry& entry)
                                 { return entry.slot == slot; }));
        records[slot] = FrontEnd::BalloonRecord{.alive = actor.alive && listed,
                                                .characterClass = actor.characterClass,
                                                .level = actor.level,
                                                .controlCode = actor.controlCode,
                                                .name = Protocol::FrontEnd::NameAsText(actor.name),
                                                .guildStatus = actor.guildStatus};
    }
    return records;
}

void CharacterSelectScene::UpdateTheBalloons()
{
    const std::array<FrontEnd::BalloonRecord, FrontEnd::BalloonCount> records = BalloonRecords();
    if (auto updated = m_balloons.Update(records); !updated.HasValue() && !m_failure)
    {
        m_failure = updated.Error();
    }
}

void CharacterSelectScene::DrawTheBalloons(Render::Frame::SceneCanvas& canvas, const Render::Pass3D& pass,
                                           float fieldOfView)
{
    if (!m_balloons.IsBuilt())
    {
        return;
    }
    std::array<std::optional<FrontEnd::BalloonHead>, FrontEnd::BalloonCount> heads{};
    if (m_context.world != nullptr)
    {
        const auto projection = MakeGameProjection(pass, fieldOfView);
        for (std::size_t slot = 0; slot < FrontEnd::BalloonCount; ++slot)
        {
            if (!m_balloons.IsShown(slot))
            {
                continue;
            }
            if (!projection.HasValue())
            {
                m_failure = "0x63ca76 → " + projection.Error();
                return;
            }
            // 0x63ca20: the record's x and y, and z plus 280 (0x6b8c78, a double) in a float's fstp.
            const Kernel::ActorState& actor = m_context.world->Actors()[slot];
            const glm::vec3 head{actor.position.x, actor.position.y,
                                 static_cast<float>(static_cast<double>(actor.position.z) +
                                                    static_cast<double>(FrontEnd::BalloonHeadHeight))};
            const auto point = ProjectGamePoint(projection.Value(), head);
            if (!point.HasValue())
            {
                m_failure = "0x63ca76 → " + point.Error();
                return;
            }
            heads[slot] = FrontEnd::BalloonHead{.point = Ui::Point{point.Value().x, point.Value().y},
                                                .controlCode = actor.controlCode};
        }
    }
    if (auto drawn = m_balloons.Draw(canvas.sprites, canvas.text, canvas.screen, heads); !drawn.HasValue())
    {
        m_failure = drawn.Error();
    }
}

bool CharacterSelectScene::HasTheWorld() const
{
    constexpr int SelectionWorldMap = Maps::CharacterSelectMap;
    return m_context.world != nullptr && m_context.cloth != nullptr && m_context.selection != nullptr &&
           m_context.states != nullptr && m_context.selection->IsInitialized() &&
           m_context.selection->Terrain() != nullptr && m_context.world->Runtime().map == SelectionWorldMap;
}

std::function<double(float, float)> CharacterSelectScene::TerrainHeight()
{
    const Terrain::WorldTerrain* terrain = m_context.selection != nullptr ? m_context.selection->Terrain() : nullptr;
    if (terrain == nullptr || !terrain->heights || !terrain->attributes)
    {
        return {};
    }
    return [terrain, this](float x, float y)
    { return Terrain::TerrainHeightAt(*terrain->heights, *terrain->attributes, x, y, CameraState().fixedHeight); };
}

void CharacterSelectScene::DrawTheWorldAndTheCharacters(Render::Frame::SceneCanvas& canvas, float alpha,
                                                        const Render::Pass3D& pass, const Render::Camera3D& camera,
                                                        std::uint8_t map, Kernel::PoseBuffer* poseCarry, int scanFrames)
{
    // 0x6347ba..0x6347e2, after 0x6346ca: Z163 and light +0xf4 at 0 on the pool's first five records, without checking
    // alive or whether slot 0 is present, after 0x634680's wait for the list. The hero points at record 0 even when the
    // received list is empty.
    Camera::GameCameraInput& cameraInput = CameraInputState();
    if (m_context.selection != nullptr)
    {
        m_context.selection->DrawSelection(cameraInput);
    }
    else
    {
        cameraInput.hero.z = FrontEnd::CharacterSlotHeight;
    }
    if (!HasTheWorld())
    {
        DrawTheCharacters(canvas, alpha, map, poseCarry, scanFrames);
        return;
    }
    Kernel::WorldState& world = *m_context.world;
    if (auto loaded = m_world.Load(m_context.dataRoot, map, m_gameSlots, &m_models); !loaded.HasValue())
    {
        m_failure = loaded.Error();
        return;
    }
    const int frames = m_worldFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    m_world.BeginFrame(world, m_context.states->terrain.dynamicLight.Lighting(), *m_context.cloth, frames);
    const FrontEnd::SelectionWorldFrame frame{.batch = canvas.models,
                                              .polygon = m_polygon,
                                              .range = m_terrainRange,
                                              .frameClock = canvas.frameClock,
                                              .frames = frames,
                                              .fixedHeight = CameraState().fixedHeight,
                                              .flushModels = canvas.flushModels};
    // 0x6347e9..0x63483e: the living chosen one takes its light to (1, 1, 1) and adds it to the dynamic buffer at its
    // place, with radius 1 (0x5f9780), between 0x620a90 and 0x620980.
    const Protocol::FrontEnd::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    Kernel::ActorState* chosenActor =
        chosen != nullptr && chosen->slot < Kernel::SelectionActorCount ? &world.Actors()[chosen->slot] : nullptr;
    if (chosenActor != nullptr && !chosenActor->alive)
    {
        chosenActor = nullptr;
    }
    if (chosenActor != nullptr)
    {
        chosenActor->light = glm::vec3(1.0f);
        Terrain::AddTerrainLight(m_world.FrameLighting(), chosenActor->position.x, chosenActor->position.y,
                                 Data::Terrain::TerrainLightColor{1.0f, 1.0f, 1.0f}, 1);
    }
    const Terrain::WorldTerrain& terrain = *m_context.selection->Terrain();
    // 0x634844: 0x5fc260(0), the terrain, the grass and the marks.
    if (auto drawn = m_world.DrawTerrain(world, terrain, m_context.states->terrain.groundMarks, frame);
        !drawn.HasValue())
    {
        m_failure = drawn.Error();
        return;
    }
    // 0x63484c: 0x6046a0, the 54's objects with their cases of 0x425260.
    if (poseCarry != nullptr)
    {
        if (auto drawn = m_world.DrawObjects(world, frame, *poseCarry); !drawn.HasValue())
        {
            m_failure = drawn.Error();
            return;
        }
    }
    // 0x634851: 0x510d00, the characters (and 0x59ec80's outline with the one under the mouse).
    DrawTheCharacters(canvas, alpha, map, poseCarry, scanFrames);
    if (m_failure || poseCarry == nullptr)
    {
        return;
    }
    // 0x63486a: 0x4453b0, the helpers, with scene 4's scale at 1 (0x4452f1), the terrain 54 light and the frame's
    // sprites.
    if (auto drawn = Pets::DrawHelpers(Pets::HelperDrawContext{
            .batch = canvas.models,
            .models = m_models,
            .world = world,
            .random = m_world.FrameRandom(),
            .frameClock = canvas.frameClock,
            .stepAlpha = 0.0f,
            .originalFrame = frames > 0,
            .onPose = [&](const Actors::Drawing::DrawnObject& object)
            { return Actors::Drawing::PublishPose(*poseCarry, m_models, object, true); },
            .publishProjectedPose = true,
            .posedMeshes = [&]() { return std::span<const Models::SkinnedMesh>(poseCarry->meshes); },
            .scene = Kernel::SceneId::CharacterSelect,
            .lighting = &m_world.FrameLighting(),
            .sprites = &m_world.FrameSprites(),
            .worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
            .effects = &m_world.FrameEffects(),
            .particles = &m_world.FrameParticles(),
            // timeGetTime (0x6b8544), from the Fenrirs' decal 0x14e: the draw clock in ms.
            .systemTime = [systemTime = static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count())]()
            { return systemTime; },
            .globalPose = [&]() { return std::span<const Math::BoneTransform>(poseCarry->bones); },
            .trapezoid = &m_context.states->camera.trapezoid});
        !drawn.HasValue())
    {
        m_failure = "0x63486a → " + drawn.Error();
        return;
    }
    // 0x63486f..0x634883: the ribbons, the joints(0), the effects with a model(0) and 0x549510.
    if (auto drawn = m_world.DrawAfterCharacters(world, terrain, frame, camera, *poseCarry); !drawn.HasValue())
    {
        m_failure = drawn.Error();
        return;
    }
    // 0x634896..0x6348dc: the chosen one with light 0.15 (0x3e19999a), lent by the two 0x729 (the wide light and
    // Impack03's that rises) at its place and with its angles, with scale 1 and no owner.
    if (chosenActor != nullptr)
    {
        chosenActor->light = glm::vec3(std::bit_cast<float>(0x3e19999au));
        for (const std::int32_t subtype : {0, 1})
        {
            const auto created = m_world.FrameParticles().CreateOwnedChecked(
                Particles::ChaseLightParticle, chosenActor->position, chosenActor->angles, chosenActor->light, subtype,
                1.0f, static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
                m_world.FrameRandom(), SelectionSceneId);
            if (!created.HasValue())
            {
                m_failure = std::format("0x53fe70: la 0x729/{} del elegido falla con error {}", subtype,
                                        static_cast<unsigned>(created.Error()));
                return;
            }
        }
        // 0x634906→0x4129f0 stores the name in the object at [0x6eed7c] for B1/01 (0x412bb0), it does not draw a sign.
        // PrepareSelectedActor already keeps it in the own record; the transfer consumer is H13.
    }
    canvas.flushModels();
    // 0x63490d..0x634927: the eye pass.
    if (auto drawn = m_world.DrawEye(pass, frame, m_textures, world.DrawTail().damage); !drawn.HasValue())
    {
        m_failure = drawn.Error();
        return;
    }
    canvas.models.Begin(pass);
}

void CharacterSelectScene::DrawTheCharacters(Render::Frame::SceneCanvas& canvas, float alpha, std::uint8_t map,
                                             Kernel::PoseBuffer* poseCarry, int scanFrames)
{
    const Protocol::FrontEnd::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    const std::size_t hero = m_context.world != nullptr ? m_context.world->Runtime().hero : 0;
    for (Character& character : m_characters)
    {
        Kernel::ActorState* actor =
            m_context.world != nullptr && m_context.selection != nullptr && m_context.cloth != nullptr
                ? &m_context.world->Actors()[character.slot]
                : nullptr;
        if (actor != nullptr && (!actor->alive || !actor->seen))
        {
            continue;
        }
        const Kernel::CharacterAppearance appearance =
            actor != nullptr ? Actors::AppearanceOfActor(*actor) : character.appearance;
        const Actors::FullSetScan scan = ScanTheCharacter(appearance, scanFrames);
        Actors::Drawing::DrawnCharacter drawn;
        drawn.appearance = appearance;
        if (actor != nullptr)
        {
            character.animation.body = actor->animation;
            drawn.headAngles = actor->headTurn.angles;
        }
        drawn.body = Actors::BodyBetweenSteps(character.animation, m_models, alpha,
                                              m_context.world != nullptr ? &m_context.world->ActionSpeeds() : nullptr);
        drawn.pet = character.animation.pet.state;
        drawn.wings = character.animation.wings.state;
        drawn.object = ObjectOf(character);
        if (actor != nullptr)
        {
            drawn.object.angles = actor->angles;
            drawn.object.scale = actor->scale;
            drawn.alpha = actor->alpha;
            drawn.safeZone = actor->safeZone != 0;
            // 0x50bbff..0x50bc71: the shadow guards of the same 0x50ba30 (0x634851 → 0x510d00): the alpha from 0.5,
            // without +0x3e4 and outside the shadowless maps (the 54 is none of them).
            drawn.castsShadow = Actors::Drawing::CastsShadow(*actor, map);
        }
        const bool isChosen = chosen != nullptr && chosen->slot == character.slot;
        // 0x50ba30: the slot's base plus +0xf4, which the draw left at 1 for the living chosen one and at 0 for the
        // others (without the world, the scene writes it here).
        if (actor != nullptr && !HasTheWorld())
        {
            actor->light = glm::vec3(isChosen ? ChosenExtraLight : 0.0f);
        }
        const float extra = actor != nullptr ? actor->light.x : (isChosen ? ChosenExtraLight : 0.0f);
        drawn.light =
            glm::vec3((static_cast<std::size_t>(character.slot) == hero ? HeroLight : CharacterLight) + extra);
        if (actor != nullptr)
        {
            actor->drawnLight = drawn.light;
        }
        drawn.outlined = m_hovered == character.slot;
        drawn.cloth = &character.cloth;
        drawn.clothWorld = m_context.cloth;
        drawn.map = map;
        drawn.pace = OriginalFramePaceOf(m_context.pacing);
        // Q-C410: the list (0x4c20a0) and the create reply (0x4c2290) build each character with 0x517270 → 0x511290,
        // which sets its +0x11c to 1 (0x511bf7) and gives it its bone buffer (+0x120, 0x5123c4..0x5123ff). 0x510d00 →
        // 0x50ba30 → 0x6001b0 poses the body there (0x600259), which starts from what its previous draw left and not
        // from the global; only what hangs goes to the global (0x68b181c).
        if (poseCarry != nullptr)
            drawn.poseBones = [poseCarry, actor](std::uint16_t model)
            {
                if (model != Tables::PlayerSkeletonModel)
                    return std::span<const Math::BoneTransform>(poseCarry->bones);
                return actor != nullptr ? std::span<const Math::BoneTransform>(actor->bodyPose)
                                        : std::span<const Math::BoneTransform>();
            };
        if (poseCarry != nullptr)
            drawn.onPose = [this, poseCarry](std::uint16_t model, std::span<const Math::BoneTransform> bones,
                                             const Models::ObjectTransform& transform, bool translated)
            {
                if (model == Tables::PlayerSkeletonModel)
                    return;
                if (auto result =
                        Actors::Drawing::PublishPoseRaw(*poseCarry, m_models, model, bones, transform, translated);
                    !result.HasValue())
                    m_failure = result.Error();
            };
        // With world 54, the same 0x50ba30 as the game: the body, the parts and the cloth, and then what hangs and the
        // effects (0x50db57..0x510a7a), which do not look at the scene except for the light (0x50cc29, 0x50d219).
        if (actor != nullptr && poseCarry != nullptr && HasTheWorld() && HasTheSkeleton())
        {
            // 0x50cca9..0x50d211: the published hero (0x743a170, slot 0 since WinMain) opens and closes the own record
            // for its color, even with the bits off, also in scene 4, which then gives it 0.5 plus +0xf4 (0x50d219):
            // 3848 rand() on closing it, every original frame.
            if (static_cast<std::size_t>(character.slot) == hero)
            {
                for (int frame = 0; frame < scanFrames; ++frame)
                {
                    const Models::LogicalAccess access(m_context.world->Runtime().ownRecordDepth,
                                                       m_context.cloth->random, Models::OwnRecordCloseRandoms);
                }
            }
            drawn.sprites = &m_world.FrameSprites();
            Actors::Drawing::DrawCharacter(canvas.models, m_models, drawn, canvas.frameClock,
                                           Actors::Drawing::CharacterDrawPass::Body);
            // 0x50bcec: the shadow copies the scale and the position to the 0x20b (WorldState::PlaceShadowModel), only
            // on an original frame.
            if (scanFrames > 0 && Actors::Drawing::DrawsTheShadow(drawn))
                m_context.world->PlaceShadowModel(*actor);
            DrawTheTail(canvas, *actor, character.slot, drawn, *poseCarry, scan, scanFrames > 0);
            if (m_failure)
                return;
            continue;
        }
        Actors::Drawing::DrawCharacter(canvas.models, m_models, drawn, canvas.frameClock);
        // Only an original frame leaves the body in its bones; an intermediate draw does not touch them. 0x6001b0 first
        // copies the scale and the position to Player.bmd's shared model, with the alpha from 0.01 (Q-C412).
        if (actor != nullptr && poseCarry != nullptr && scanFrames > 0)
        {
            actor->bodyPose = Actors::Drawing::PoseCharacterBody(m_models, drawn);
            if (actor->alpha >= Kernel::PlayerModelAlpha)
                m_context.world->PlacePlayerModel(*actor);
            // 0x50bcec: the shadow copies the scale and the position to the 0x20b (WorldState::PlaceShadowModel).
            if (Actors::Drawing::DrawsTheShadow(drawn))
                m_context.world->PlaceShadowModel(*actor);
        }
    }
}

bool CharacterSelectScene::HasTheSkeleton() const
{
    const Models::Skeleton* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId);
    return skeleton != nullptr && !skeleton->actions.empty();
}

// 0x50db57..0x510ac2 on the body already drawn (GameCharacters.h, DrawCharacterTail), with the frame tables of world
// 54: the world's on an original frame and their copies between frames. The light of what hangs (0x50db94,
// [ebp−0x44] plus +0xf4) is scene 4's, a fixed 0.4 (0x50cc29), or 0.5 on the published hero, which its color branch
// overwrites (0x50d231); with +0x375 from 6, (1, 0.1, 0.1). The raven of a listed Dark Lord (EquipSelectionActor) goes
// with the selection's trapezoid.
void CharacterSelectScene::DrawTheTail(Render::Frame::SceneCanvas& canvas, Kernel::ActorState& actor, std::size_t slot,
                                       const Actors::Drawing::DrawnCharacter& drawn, Kernel::PoseBuffer& poseCarry,
                                       const Actors::FullSetScan& scan, bool originalFrame)
{
    Kernel::WorldState& world = *m_context.world;
    Actors::Drawing::ItemSpecialDrawContext special;
    Actors::Drawing::ItemDrawContext itemContext;
    const Actors::Drawing::ItemDrawResources itemResources{.frameClock = canvas.frameClock,
                                                           .random = m_world.FrameCloth().random,
                                                           .itemRender = m_world.FrameItemRender(),
                                                           .sprites = m_world.FrameSprites(),
                                                           .poseCarry = poseCarry,
                                                           .modelState = m_world.FrameModelState(),
                                                           .joints = m_world.FrameTail().joints,
                                                           .effects = m_world.FrameEffects(),
                                                           .map = world.Runtime().map,
                                                           .originalFrame = originalFrame,
                                                           .glowDetail = world.Runtime().ui.glowDetail};
    Actors::Drawing::FillItemDrawContexts(special, itemContext, itemResources);
    Session::FillItemCreators(itemContext, itemResources);
    constexpr std::uint8_t RedPk = 6;
    const float base = slot == world.Runtime().hero ? HeroLight : CharacterLight;
    actor.drawnLight = actor.pk >= RedPk ? glm::vec3{1.0f, 0.1f, 0.1f} : glm::vec3(base) + actor.light;
    const Actors::Drawing::GameCharacterPass pass{.dynamicLighting = &m_world.FrameLighting(),
                                                  .clothWorld = &m_world.FrameCloth(),
                                                  .frameClock = canvas.frameClock,
                                                  .sprites = &m_world.FrameSprites(),
                                                  .poseCarry = &poseCarry,
                                                  .joints = m_world.FrameTail().joints,
                                                  .particles = &m_world.FrameParticles(),
                                                  .effects = &m_world.FrameEffects(),
                                                  .itemContext = &itemContext,
                                                  .pace = OriginalFramePaceOf(m_context.pacing),
                                                  .polygon = &m_polygon,
                                                  .fixedHeight = CameraState().fixedHeight,
                                                  .terrain = StepTerrain(),
                                                  .hooks = &m_characterDrawHooks,
                                                  .effectHooks = &m_effectHooks};
    if (auto drawnTail = Actors::Drawing::DrawCharacterTail(canvas.models, m_models, world, actor, slot, drawn, pass,
                                                            scan, originalFrame);
        !drawnTail.HasValue())
        m_failure = std::format("{}, en el personaje {} de la selección", drawnTail.Error(), slot);
}

// 0x50ba30 with each one of the list, at 0x50c91e: they are 0x20a with +0x7c and +0x3e4 at 0 (0x517270), and the
// create window's preview is not. Without Player.bmd's actions it leaves early (0x50ba87), like DrawCharacter without
// its skeleton.
Actors::FullSetScan CharacterSelectScene::ScanTheCharacter(const Kernel::CharacterAppearance& appearance, int frames)
{
    if (m_context.world == nullptr || m_context.cloth == nullptr ||
        m_models.SkeletonOf(Tables::PlayerSkeletonModelId) == nullptr)
    {
        return {};
    }
    return Actors::ScanTheDrawnCharacter(*m_context.world, Actors::ScannedCharacterOf(appearance),
                                         m_context.cloth->random, frames);
}

void CharacterSelectScene::DrawTheMessages(Render::Frame::SceneCanvas& canvas)
{
    if (m_context.world == nullptr)
        return;
    const int frames = m_messageFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    Kernel::HudState projected;
    Kernel::HudState& hud = frames > 0 ? m_context.world->Runtime().hud : (projected = m_context.world->Runtime().hud);
    const Ui::Point mouse =
        m_context.input != nullptr ? Ui::MouseIn640x480(*m_context.input, canvas.screen) : Ui::Point{};
    Chat::DrawTheMessages(*m_context.world, hud, canvas.sprites, canvas.text, mouse, false);
}

void CharacterSelectScene::DrawTheCursor(Render::Frame::SceneCanvas& canvas) const
{
    if (m_context.input != nullptr)
    {
        Ui::DrawGameCursor(canvas.sprites, *m_context.input, canvas.screen);
    }
}

void CharacterSelectScene::Leave()
{
    m_escape.Close();
    m_options.Close();
    m_ui.Clear();
    m_world.Release();
    m_textures.Release();
    m_windowTextures.Release();
    // 0x61cc50 releases slots 0x6f8 to 0x71d.
    m_selectionModels.Release();
    if (m_startupModels != nullptr && m_startupModels->TakeStartupSelectionTexture(m_context.textures))
        m_context.textures.Release(Render::TextureSlot{Assets::StartupSelectionTextureSlot});
}

void CharacterSelectScene::StepThePreview()
{
    if (!m_create.IsOpen())
    {
        return;
    }
    m_previewAdvanced = true;
    // 0x63d880 → 0x63d7c0 → 0x63e3e0: the class this step's click chose builds the preview again, which then advances.
    if (m_create.ChosenClass() != Preview().characterClass)
    {
        if (m_context.world != nullptr && m_context.cloth != nullptr && m_context.selection != nullptr)
        {
            const Actors::ActorConstructionContext construction{.terrain = *m_context.selection->Terrain(),
                                                                .map = m_context.world->Runtime().map,
                                                                .scene = Terrain::CullingScene::CharacterSelect,
                                                                .fixedHeight = CameraState().fixedHeight};
            if (auto rebuilt = FrontEnd::RebuildPreview(Preview(), m_create.ChosenClass(), m_selectionModels,
                                                        construction, *m_context.cloth);
                !rebuilt.HasValue())
            {
                m_failure = rebuilt.Error();
                return;
            }
        }
        else
        {
            FrontEnd::ChoosePreviewClass(Preview(), m_create.ChosenClass(), m_selectionModels);
        }
    }
    FrontEnd::StepPreview(Preview(), m_selectionModels);
}

// 0x66a050: the window's base; 0x63e460: the preview in its own pass, without clearing the depth (it is tested
// against the selection pass's, as in the original); and 0x63e0a0: the rest, on top.
void CharacterSelectScene::DrawTheCreateWindow(Render::Frame::SceneCanvas& canvas, float alpha)
{
    if (!m_create.IsOpen())
    {
        // 0x63e460 only runs with the window drawn: on reopening it does not make up the time closed.
        m_previewDrawFrames = {};
        return;
    }
    m_create.DrawBase(canvas.sprites, canvas.screen);
    canvas.flushSprites();
    const Ui::Rect box = FrontEnd::LayoutCreateWindow(canvas.screen, m_create.ChosenClass()).box;
    const int frames = m_previewDrawFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    const float stepAlpha = m_previewAdvanced ? alpha : 0.0f;
    Actors::Drawing::DrawnObject drawn;
    for (int frame = 0; frame < frames; ++frame)
    {
        m_previewCamera = FrontEnd::PreviewCamera(Preview());
        drawn = FrontEnd::DrawnPreview(Preview(), m_selectionModels, stepAlpha);
    }
    if (frames == 0)
    {
        drawn = FrontEnd::DrawnPreview(Preview(), m_selectionModels, stepAlpha, 0);
    }
    const Render::Pass3D pass =
        Render::MakePass3D(canvas.screen, FrontEnd::PreviewViewport(box, canvas.screen), m_previewCamera);
    canvas.models.Begin(pass);
    // Without this step's advance (a notice on top), the preview stays still: interpolating one that will not come
    // would make it shake.
    if (m_context.world != nullptr)
    {
        // Q-C410: 0x63e3e0 builds it with 0x511290 (0x63e409), which sets its +0x11c to 1 and gives it its buffer
        // (+0x120); 0x63e637 → 0x50ba30 → 0x602e90 → 0x5ffdf0 poses it there (0x5fff13..0x5fff4c) and not in the
        // global. Only an original frame keeps the pose in the own buffer. 0x50ba30 calls with the second argument at 1
        // (0x5ffdf0 at 0x50baed, 0x602e90 at 0x50bd81): the bones stay in the model (0x4eb860 with the flag at 0) and
        // 0x4ebc80 runs with Translate at 1, the normal without the class scale (5.9 to 9.1) for the per-vertex light.
        Kernel::PoseBuffer own;
        own.bones = Preview().bodyPose;
        if (auto result = Actors::Drawing::PublishPose(own, m_selectionModels, drawn, false); !result.HasValue())
        {
            m_failure = result.Error();
            return;
        }
        Actors::Drawing::DrawObjectFromMeshes(canvas.models, m_selectionModels, drawn, canvas.frameClock, own.meshes);
        if (frames > 0)
            Preview().bodyPose = std::move(own.bones);
    }
    else
        Actors::Drawing::DrawObject(canvas.models, m_selectionModels, drawn, canvas.frameClock);
    canvas.flushModels();
    m_create.Draw(canvas.sprites, canvas.text, canvas.screen);
}

std::optional<Kernel::SceneId> CharacterSelectScene::Next() const
{
    return m_next;
}

bool CharacterSelectScene::WantsToQuit() const
{
    return m_quit;
}

bool CharacterSelectScene::WantsTextInput() const
{
    return m_notice.WantsTextInput() || m_create.WantsTextInput();
}

std::optional<std::string> CharacterSelectScene::Failure() const
{
    return m_failure;
}

const Kernel::ActorState& CharacterSelectScene::PreviewActor() const
{
    return Preview();
}

bool CharacterSelectScene::HoldsTheLastFrame() const
{
    // 0x634680 returns with al = 0 while 0x7d3a13f is 0 or the state is before 0x33, and without that true 0x637700
    // does not present the frame (0x6385e1..0x6385f7): the login's last one stays. Until a step saw the list (and
    // counted the first of its own), except with an open notice, which MuRet shows.
    return m_session != nullptr && (m_steps == 0 || WaitingForTheList()) && !m_notice.IsOpen();
}
} // namespace Client::Scenes
