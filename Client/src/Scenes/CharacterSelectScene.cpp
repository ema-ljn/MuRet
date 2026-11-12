#include "Scenes/CharacterSelectScene.h"
#include "Scenes/FaunaDrawing.h"
#include "Render/Models/HalfLifeMath.h"
#include <span>

#include "App/LoopTiming.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/TextureSet.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Character/CharacterDelete.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/CharacterName.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Pose.h"
#include "Render/Models/RayPicking.h"
#include "Render/Models/Skeleton.h"
#include "Render/OriginalQuad.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/ActorAppearance.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/HelperTable.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/CharacterSlots.h"
#include "Scenes/CreatePreview.h"
#include "Scenes/FullSetScan.h"
#include "Scenes/GameCharacters.h"
#include "Scenes/GameData.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Scenes/GameMessages.h"
#include "Scenes/ItemDrawResources.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SelectionWorld.h"
#include "UI/MessageArea.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/SelectionState.h"
#include "Scenes/TerrainPolygonFrame.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/DynamicWorldLight.h"
#include "Scenes/WorldMap.h"
#include "Data/Terrain/TerrainLight.h"
#include "Scenes/GameParticles.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/SelectionParticleKernels.h"
#include "World/TerrainHeight.h"
#include "World/TerrainLighting.h"
#include "World/WorldTerrain.h"
#include "UI/CreateCharacterWindow.h"
#include "UI/DeleteChecks.h"
#include "UI/GameCursor.h"
#include "UI/GameText.h"
#include "UI/NameChecks.h"
#include "UI/Notices.h"
#include "UI/SelectionBar.h"
#include "UI/Widget.h"

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

// La acción de parado de la selección. La elige 0x4f2250 por la mascota, las alas y el arma; la de
// la traza es la 3, con un arma de una mano. Las otras, sin leer (P-C14 c): hasta entonces, la 3.
constexpr std::uint8_t StandingAction = 3;

// La luz de 0x50ba30 en la escena 4: 0,4, y 0,5 para el "héroe" (DAT_0743a170: 0x50d219), más el +0xf4
// que pone 0x634680, 1 para el elegido. El héroe es el objeto 0, el del lugar 0, hasta la primera entrada
// al mundo; después de un F1 02 con 1, el lugar que sorteó el juego (Q-C213).
constexpr float CharacterLight = 0.4f;
// DAT_006d5dc8 de la selección: la 0x729 y el 6f1/2 la miran.
constexpr std::int32_t SelectionSceneId = 4;
constexpr float HeroLight = 0.5f;
constexpr float ChosenExtraLight = 1.0f;

// Los textos que 0x622c70 busca en el nombre de uno nuevo.
constexpr std::size_t OperationText = 457;
constexpr std::size_t ServerText = 458;
// 0x63420a: el formato del mensaje con los dos números de pkt[5] de la lista.
constexpr std::size_t AccountNoticeText = 1240;

// El bit de las banderas (+0x1d8) con el que el armado de la barra muestra los textos 436 y 437.
constexpr std::uint8_t BlockedAccountFlag = 0x10;

// 0x5bb8d0 en la selección: 640, sin las ventanas del juego (06 § El trapecio completo).
constexpr std::int32_t SelectionWorldWidth = 640;

// Si la entrada es la misma que dio el personaje: un crear en un lugar ocupado lo pisa.
bool SameEntry(const Protocol::Character::CharacterEntry& one, const Protocol::Character::CharacterEntry& other)
{
    return one.slot == other.slot && one.name == other.name && one.classByte == other.classByte &&
           one.equipment == other.equipment && one.withoutEquipment == other.withoutEquipment;
}

// Lo que 0x634680 le pasa a 0x5fbb00 (0x634736..0x63475c) y lo que 0x5fb570 lee de los globales: la escena 4,
// el mundo en curso (0x6c9dd0, el 54), el punto fijo de la selección, el yaw de la cámara (que en el 54 no se
// usa: gira 45°), el héroe publicado como selector del mapa 30 (solo en la escena 5) y el ancho de 0x5bb8d0,
// 640 sin las ventanas del juego (08 P-C41 e). El mapa 6 no llega a la selección.
World::TerrainPolygonInput SelectionPolygonInput(const World::GameCameraInput& input, const Render::Camera3D& camera)
{
    World::TerrainPolygonInput polygon;
    polygon.scene = static_cast<std::int32_t>(World::CullingScene::CharacterSelect);
    polygon.map = input.map;
    polygon.translation = SelectionCullingPoint;
    polygon.objectPosition = glm::vec2(input.hero.x, input.hero.y);
    polygon.cameraYaw = camera.angles.z;
    polygon.worldWidth = SelectionWorldWidth;
    return polygon;
}

// Los dos fondos, 0x621b60(0x717, 0, 25, 320, 320, …) y el 0x718 en x 320, escalados desde 640 × 480.
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

CharacterSelectScene::CharacterSelectScene(const SceneContext& context, const GameData& data,
                                           const IModelLibrary& models, App::ServerSession* session,
                                           const Render::Models::ModelTextureSlots* gameSlots)
    : m_context(context), m_texts(data.text ? &*data.text : nullptr),
      m_items(data.items ? &data.items->table : nullptr), m_models(models), m_session(session),
      m_textures(context.textures, SelectionTextureSet), m_windowTextures(context.textures, WindowTextureSet),
      m_world(context.textures, context.log),
      m_notice([this](Render::TextureSlot slot) { return m_windowTextures.BufferSize(slot); }, m_texts),
      m_bar([this](Render::TextureSlot slot) { return m_textures.BufferSize(slot); }, m_texts),
      m_nameFilters{.filterName = data.filterName,
                    .filter = data.filter,
                    .operation = UI::TextAt(m_texts, OperationText).value_or(std::string_view{}),
                    .server = UI::TextAt(m_texts, ServerText).value_or(std::string_view{})},
      m_create([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts), m_gameSlots(gameSlots)
{
}

Scenes::TextureBufferSize CharacterSelectScene::BufferSize(Render::TextureSlot slot) const
{
    const bool inTheWindows = std::ranges::any_of(WindowTextureSet, [slot](const Data::Textures::TextureSetEntry& entry)
                                                  { return entry.slot == slot.value; });
    return inTheWindows ? m_windowTextures.BufferSize(slot) : m_textures.BufferSize(slot);
}

World::GameCamera& CharacterSelectScene::CameraState()
{
    return m_context.camera != nullptr ? *m_context.camera : m_localCamera;
}

World::GameCameraInput& CharacterSelectScene::CameraInputState()
{
    return m_context.cameraInput != nullptr ? *m_context.cameraInput : m_localCameraInput;
}

CreatePreview& CharacterSelectScene::Preview()
{
    return m_context.selection != nullptr ? m_context.selection->PreviewActor() : m_preview;
}

const CreatePreview& CharacterSelectScene::Preview() const
{
    return m_context.selection != nullptr ? m_context.selection->PreviewActor() : m_preview;
}

Core::Expected<void, std::string> CharacterSelectScene::Enter()
{
    m_cameraFrames = {};
    m_fullSetScanFrames = {};
    m_previewDrawFrames = {};
    m_previewCamera = PreviewCamera();
    m_steps = 0;
    m_charactersPrepared = false;
    CameraInputState().scene = World::CullingScene::CharacterSelect;
    if (m_context.selection != nullptr)
    {
        // 0x633f20 carga World54 desde el primer paso habilitado por F3/00. Este reset precede
        // al único RequestCharacterList, al final de Enter: Poll/Step/Request corren en el mismo
        // hilo y Request entra de forma síncrona, sin Poll intermedio ni lista válida anterior.
        m_context.selection->BeginSelection();
    }
    if (auto loaded = m_textures.Load(m_context.dataRoot); !loaded.HasValue())
    {
        return loaded;
    }
    if (auto loaded = m_windowTextures.Load(m_context.dataRoot); !loaded.HasValue())
    {
        return loaded;
    }
    // Una textura de los NewFace que falta cierra el original (0x60d5f0 con su quinto argumento en 1).
    if (auto loaded = m_selectionModels.Load(m_context.dataRoot, m_context.textures, m_context.log, m_gameSlots);
        !loaded.HasValue())
    {
        return loaded;
    }
    // 0x633f20 → 0x65f4b0 → 0x63d1a0 → 0x63d7c0 → 0x63e3e0, al entrar: la vista previa con la clase
    // de la ventana (el Dark Knight) y la acción 1.
    if (m_context.world == nullptr || m_context.cloth == nullptr || m_context.selection == nullptr)
    {
        ChoosePreviewClass(Preview(), m_create.ChosenClass(), m_selectionModels);
    }
    // El F3 00 de la entrada desde el login (0x634dce). Después de un F1 02 con 1 ya lo mandó la sesión
    // (0x4c3653) y espera la lista: no se pide otra.
    if (m_session != nullptr && m_session->State() == App::SessionState::LoggedIn)
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
CharacterSelectScene::MakeCharacter(const Protocol::Character::CharacterEntry& entry) const
{
    Character character;
    character.slot = entry.slot;
    if (m_context.selection != nullptr)
    {
        character.revision = m_context.selection->Slots()[entry.slot].revision;
    }
    character.entry = entry;
    const std::uint8_t characterClass = Protocol::Character::CharacterClassOf(entry.classByte);
    // El que armó la respuesta de crear sale sin equipo (0x517270 sin 0x5139f0, Q-C103).
    character.appearance = entry.withoutEquipment ? GameLogic::ClassAppearance(characterClass, 0)
                                                  : GameLogic::AppearanceOf(characterClass, 0, entry.equipment);
    if (const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(Data::Models::PlayerSkeletonModel))
    {
        Render::Models::SetAction(character.animation.body, *skeleton, StandingAction);
    }
    return character;
}

void CharacterSelectScene::SyncTheCharacters()
{
    if (m_session == nullptr || !m_session->Characters())
    {
        return;
    }
    const std::vector<Protocol::Character::CharacterEntry>& entries = m_session->Characters()->characters;
    bool changed = !m_tookTheList || entries.size() != m_characters.size();
    std::vector<Character> synced;
    synced.reserve(entries.size());
    for (const Protocol::Character::CharacterEntry& entry : entries)
    {
        const SelectionState* selection = m_context.selection;
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
    // 0x634680 recorre los cinco registros del pool por índice, no en el orden del paquete.
    std::ranges::sort(synced, {}, &Character::slot);
    m_characters = std::move(synced);
    if (!m_tookTheList)
    {
        m_tookTheList = true;
        // 0x63e820, una vez: algún personaje de los cinco lugares con el bit 0x10 de las banderas.
        m_bar.SetBlockedAccount(std::ranges::any_of(entries, [](const Protocol::Character::CharacterEntry& entry)
                                                    { return (entry.flags & BlockedAccountFlag) != 0; }));
        // 0x4c20a0 → 0x622b20 → 0x63d7c0: las clases de la 0 a pkt[4].
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
    const ActorConstructionContext construction{.terrain = *terrain,
                                                .map = m_context.world->Runtime().map,
                                                .scene = World::CullingScene::CharacterSelect,
                                                .fixedHeight = CameraState().fixedHeight};
    constexpr std::uint8_t DefaultPreviewClass = 1;
    constexpr std::uint16_t DefaultPreviewModel = FirstNewFaceModel + DefaultPreviewClass;
    auto first = ConstructActor(Preview(), DefaultPreviewModel, {}, construction, *m_context.cloth);
    if (!first.HasValue())
    {
        m_failure = first.Error();
        return false;
    }
    Preview().characterClass = DefaultPreviewClass;
    Preview().byte88 = 0;
    auto window = RebuildPreview(Preview(), m_create.ChosenClass(), m_selectionModels, construction, *m_context.cloth);
    if (!window.HasValue())
    {
        m_failure = window.Error();
        return false;
    }
    // 0x634049..0x634177: el reset parcial, con el cierre del mismo registro de 0x4fc450.
    const Render::Models::LogicalAccess access(m_context.world->Runtime().ownRecordDepth, m_context.cloth->random,
                                               Render::Models::OwnRecordCloseRandoms);
    m_context.world->OwnCharacter().ResetForSelection();
    m_charactersPrepared = true;
    // 0x634199..0x634227: el campo del chat abierto (0x6c8e68), las 120 entradas vacías y, si alguno de los dos números
    // de pkt[5] de la lista (0x5767958 y 0x576795c, 0x4c20a0) no es 0, el 1240 con ellos, como sistema y sin nombre (el
    // nombre de 0x7d3a17c nadie lo escribe). El vaciado de los campos (0x553570) es de la entrada del chat.
    WorldRuntime& runtime = m_context.world->Runtime();
    runtime.ui.chatOpen = true;
    if (auto emptied = AddTheEmptyMessages(runtime, m_context.textMeasure); !emptied.HasValue())
    {
        m_failure = emptied.Error();
        return false;
    }
    if (auto noticed = AddTheAccountNotice(); !noticed.HasValue())
    {
        m_failure = noticed.Error();
        return false;
    }
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
    // El original usa el texto como formato de sprintf (Q-C61): MuRet pone los dos números en sus marcas.
    const std::string text =
        UI::PlaceArguments(UI::TextAt(m_texts, AccountNoticeText).value_or(std::string_view{}), arguments);
    return AddGameMessage(m_context.world->Runtime(), *m_context.textMeasure, m_screen, {}, text, UI::SystemMessage);
}

CharacterStepContext CharacterSelectScene::CharacterContext()
{
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    return {.world = *m_context.world,
            .random = m_context.cloth->random,
            .scene = SceneId::CharacterSelect,
            .playerSkeleton = m_models.SkeletonOf(Data::Models::PlayerSkeletonModel),
            .items = m_items,
            .terrain = m_context.selection->Terrain(),
            .darkSpiritSkeleton = m_models.SkeletonOf(DarkSpiritModel),
            .particles = &m_context.world->Particles(),
            .worldClock = clock,
            .counter = m_context.cloth->logicSteps,
            .newFaceSkeleton = m_selectionModels.SkeletonOf(Preview().type)};
}

// 0x6342f9, después 0x634303: los 400 registros y el objeto NewFace, antes de la UI.
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
            // 0x6342e1: 0x606560, los objetos del 54 que vio el último dibujo (0x4250f0).
            const auto map = static_cast<std::uint8_t>(m_context.world->Runtime().map);
            if (auto loaded = m_world.Load(m_context.dataRoot, map, m_gameSlots, &m_models); !loaded.HasValue())
            {
                m_failure = loaded.Error();
                return;
            }
            if (auto objects = m_world.StepObjects(*m_context.world, m_context.cloth->random, worldClock);
                !objects.HasValue())
            {
                m_failure = objects.Error();
                return;
            }
        }
        // 0x6342f4: 0x445230, los ayudantes, antes del pool. La escena 4 no apaga los de un jinete muerto (Q-C293).
        if (auto helpers = StepHelpers(HelperStepContext{.world = *m_context.world,
                                                         .models = &m_models,
                                                         .random = m_context.cloth->random,
                                                         .worldClock = worldClock,
                                                         .scene = SceneId::CharacterSelect,
                                                         .terrain = m_context.selection->Terrain(),
                                                         .logicSteps = m_context.cloth->logicSteps});
            !helpers.HasValue())
        {
            m_failure = "0x6342f4 → " + helpers.Error();
            return;
        }
        const CharacterStepContext context = CharacterContext();
        if (auto stepped = Scenes::StepCharacters(context); !stepped.HasValue())
        {
            m_failure = stepped.Error();
            return;
        }
        if (auto preview = StepCharacter(Preview(), context); !preview.HasValue())
        {
            m_failure = preview.Error();
            return;
        }
        const std::function<double(float, float)> height = TerrainHeight();
        if (world)
        {
            // 0x52d220 y 0x53c670, después del preview: los efectos con modelo y las juntas.
            // timeGetTime (0x6b8544), del calco 0x14e de los Fenrir: el reloj de la escena en ms.
            const auto systemTime =
                m_context.clock != nullptr
                    ? static_cast<std::uint32_t>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(m_context.clock->Now()).count())
                    : 0U;
            if (auto effects = m_world.StepEffects(*m_context.world, *m_context.cloth, worldClock, height, systemTime);
                !effects.HasValue())
            {
                m_failure = effects.Error();
                return;
            }
        }
        // 0x634315 → 0x5446e0, después del preview: también sortea el viento con la tabla vacía. La neblina
        // 622/1 lee su dueño entre los objetos del 54 y el reloj del mundo.
        World::PortableParticleTrigonometry trigonometry;
        if (auto particles = World::StepParticlesChecked(
                m_context.world->Particles(), context.random, std::cref(worldClock), &m_context.world->Runtime().map,
                trigonometry, height, ParticleOwnersAmong(m_context.world->Map().objects));
            !particles.HasValue())
        {
            m_failure = std::format("0x634315 → 0x5446e0: el paso de las partículas falla con error {}",
                                    static_cast<unsigned>(particles.Error()));
            return;
        }
        m_previewAdvanced = true;
        for (Character& character : m_characters)
        {
            const ActorState& actor = m_context.world->Actors()[character.slot];
            character.animation.body = actor.animation;
            StepCharacterAttachments(character.animation, AppearanceOfActor(actor), m_models);
        }
        return;
    }
    // Los tests de UI sin WorldState ni terreno conservan la animación de presentación.
    for (Character& character : m_characters)
    {
        // Lo que termina vuelve a la de parado (0x4fcb10), que en la selección es la 3: como la 3 repite,
        // es la de ahora, y SetAction no cambia nada.
        static_cast<void>(StepCharacterAnimation(character.animation, character.appearance, m_models,
                                                 Render::Models::AnimationScene::CharacterSelect));
    }
}

// Q-C70: en la selección, un FD_CLOSE abre el aviso 0x70, como en el login, una vez. MuRet lo abre
// también sin respuesta (NoReply), como el login (Q-C69).
bool CharacterSelectScene::ShowTheFailure()
{
    if (m_failureShown || m_session == nullptr || m_session->State() != App::SessionState::Failed)
    {
        return false;
    }
    m_failureShown = true;
    m_context.log.Info("la sesión falló en la selección: se abre el aviso {:#04x}",
                       static_cast<int>(UI::ConnectFailedNotice));
    m_notice.Open(UI::ConnectFailedNotice);
    return true;
}

bool CharacterSelectScene::TakeTheCreationResult()
{
    if (!m_waitingForTheCreation || m_session == nullptr ||
        m_session->State() == App::SessionState::WaitingCreateResult)
    {
        return false;
    }
    m_waitingForTheCreation = false;
    const std::optional<std::uint8_t> result = m_session->CreateResult();
    if (!result || m_failureShown)
    {
        return false;
    }
    const std::optional<UI::NoticeId> notice = UI::NoticeOfCreateResult(*result);
    if (!notice)
    {
        // Creado: 0x65f6c0(+0x8) cierra el 0x5d; el nuevo ya está en la lista.
        if (m_notice.Notice() == UI::PleaseWaitNotice)
        {
            m_notice.Close();
        }
        return false;
    }
    // La ventana antes que el aviso, que queda arriba (0x6431b0), con el campo vacío.
    m_create.Open();
    m_previewDrawFrames = {};
    m_notice.Open(*notice);
    return true;
}

void CharacterSelectScene::StepTheCreateWindow(const Input::InputState& input)
{
    if (m_create.Step(input, m_screen) == UI::CreateWindowEvent::Submitted)
    {
        SubmitTheCreation();
    }
    // Al final de 0x63d880: la vista previa de la clase que quedó, y su avance (0x63e430).
    StepThePreview();
}

void CharacterSelectScene::SubmitTheCreation()
{
    if (const std::optional<UI::NoticeId> refused =
            UI::NoticeOfNewName(m_create.Name(), m_create.Buffer(), m_nameFilters))
    {
        // El nombre no va al log: solo el aviso. La ventana queda abajo, con el nombre.
        m_context.log.Info("el nombre del personaje nuevo no pasa: el aviso {:#04x}", static_cast<int>(*refused));
        m_notice.Open(*refused);
        return;
    }
    if (m_session == nullptr)
    {
        return;
    }
    m_session->CreateCharacter(m_create.Name(), m_create.ChosenClass());
    if (m_session->State() == App::SessionState::WaitingCreateResult)
    {
        m_waitingForTheCreation = true;
        m_create.Close();
        m_notice.Open(UI::PleaseWaitNotice);
    }
}

bool CharacterSelectScene::TakeTheDeletionResult()
{
    if (!m_waitingForTheDeletion || m_session == nullptr ||
        m_session->State() == App::SessionState::WaitingDeleteResult)
    {
        return false;
    }
    m_waitingForTheDeletion = false;
    const std::optional<std::uint8_t> result = m_session->DeleteResult();
    // Con la sesión caída, el 0x70 de la falla queda arriba aunque la respuesta haya llegado antes.
    if (!result || m_failureShown)
    {
        return false;
    }
    m_notice.Open(UI::NoticeOfDeleteResult(*result));
    return true;
}

// El aviso abierto tiene el mouse (Q-C84) y 0x634680 no llama a 0x59ec80 (+0x8de8): el contorno queda
// en el último que estuvo bajo el mouse. El OK de uno que cierra el juego (el 0x70, Q-C69) lo dice en
// WantsToQuit; el de la confirmación del borrado abre el del código, y el del código manda el F3 02.
// Los demás solo se cierran.
void CharacterSelectScene::StepTheNotice(const Input::InputState& input)
{
    const std::optional<UI::NoticeId> closed = m_notice.Step(input, m_screen);
    if (!closed)
    {
        return;
    }
    switch (UI::FollowUpOf(*closed))
    {
    case UI::NoticeFollowUp::Quit:
        m_context.log.Info("el OK del aviso cierra el juego (el {:#04x})", static_cast<int>(*closed));
        m_quit = true;
        break;
    case UI::NoticeFollowUp::AskForTheCode:
        m_notice.Open(UI::DeletionCodeNotice);
        break;
    case UI::NoticeFollowUp::SendTheDeletion:
        SendTheDeletion();
        break;
    case UI::NoticeFollowUp::Close:
    case UI::NoticeFollowUp::ReopenAtAccount:
    case UI::NoticeFollowUp::ReopenAtPassword:
        break;
    }
}

void CharacterSelectScene::EnableTheBar()
{
    bool aFreeSlot = true;
    if (m_session != nullptr && m_session->Characters())
    {
        const std::vector<Protocol::Character::CharacterEntry>& entries = m_session->Characters()->characters;
        aFreeSlot = false;
        for (std::uint8_t slot = 0; slot <= Protocol::Character::MaxCharacterSlot; ++slot)
        {
            if (std::ranges::find(entries, slot, &Protocol::Character::CharacterEntry::slot) == entries.end())
            {
                aFreeSlot = true;
                break;
            }
        }
    }
    m_bar.Enable(aFreeSlot, m_session != nullptr && m_session->ChosenCharacter() != nullptr);
}

void CharacterSelectScene::OnTheBar(UI::SelectionBarClick click)
{
    switch (click)
    {
    case UI::SelectionBarClick::Connect:
        TryToEnter();
        break;
    case UI::SelectionBarClick::Menu:
        // El menú (+0x3d0) es de la CL8, como el del login (P-C13).
        m_context.log.Info("el menú de la selección todavía no está");
        break;
    case UI::SelectionBarClick::Create:
        m_create.Open();
        m_previewDrawFrames = {};
        break;
    case UI::SelectionBarClick::Delete:
        AskToDelete();
        break;
    }
}

void CharacterSelectScene::AskToDelete()
{
    const Protocol::Character::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    if (chosen == nullptr)
    {
        return;
    }
    const UI::NoticeId notice =
        UI::NoticeOfDeleteButton(*chosen, UI::FreeDeletionGroup(m_texts, m_session->ChosenServer()));
    m_context.log.Info("borrar el personaje del lugar {} abre el aviso {:#04x}", chosen->slot,
                       static_cast<int>(notice));
    // El nombre va en el %s del texto 1712 del 0x5e (0x6431b0); no va al log.
    m_notice.Open(notice, Protocol::Character::NameAsText(chosen->name));
}

void CharacterSelectScene::SendTheDeletion()
{
    if (m_session == nullptr)
    {
        return;
    }
    // El código, como la clave, no va al log, y no se queda en la ventana.
    m_session->DeleteCharacter(Protocol::Character::DeleteCodeFromText(m_notice.TakeCode()));
    if (m_session->State() == App::SessionState::WaitingDeleteResult)
    {
        m_waitingForTheDeletion = true;
        m_notice.Open(UI::PleaseWaitNotice);
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
        // 0x637cb2 → 0x6342c0 precede el viento y el contador de 0x637cec/0x637f91.
        Render::Models::StepTheClothWorld(*m_context.cloth);
    }
}

void CharacterSelectScene::StepTheScene(const Input::InputState& input)
{
    m_previewAdvanced = false;
    // Un aviso que se abre en este paso se atiende desde el siguiente: lo que se apretó en este no
    // lo cierra antes de que se vea.
    const bool failureOpened = ShowTheFailure();
    if (WaitingForTheList())
    {
        if (m_notice.IsOpen() && !failureOpened)
        {
            StepTheNotice(input);
        }
        return;
    }
    if (m_context.selection != nullptr && m_session != nullptr && !m_context.selection->IsInitialized())
    {
        if (auto initialized = m_context.selection->InitializeSelection(
                m_context.dataRoot, CameraInputState(),
                m_context.cloth != nullptr ? &m_context.cloth->random : nullptr);
            !initialized.HasValue())
        {
            m_failure = initialized.Error();
            return;
        }
    }
    if (m_context.world != nullptr)
    {
        const World::WorldTerrain* terrain =
            m_context.selection != nullptr ? m_context.selection->Terrain() : &m_context.world->Map().terrain;
        if (terrain != nullptr && terrain->lighting != nullptr)
        {
            m_context.world->DynamicLighting().Update(*terrain->lighting);
        }
        // 0x5fbf60, después de la copia de la luz: las olas de la ventana del rango con el reloj del paso.
        if (HasTheWorld() && m_context.clock != nullptr)
        {
            m_world.StepWaves(m_context.clock->Now(), m_context.world->DynamicLighting().Range(),
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
    const bool deletionOpened = TakeTheDeletionResult();
    const bool creationOpened = TakeTheCreationResult();
    EnableTheBar();
    if (m_notice.IsOpen())
    {
        m_bar.StepWithoutTheMouse(input);
        if (m_create.IsOpen())
        {
            m_create.StepUnderANotice(input);
        }
        if (!failureOpened && !deletionOpened && !creationOpened)
        {
            StepTheNotice(input);
        }
        return;
    }
    // La ventana de crear es modal (la base de toda la pantalla): tiene el mouse y el teclado.
    if (m_create.IsOpen())
    {
        m_bar.StepWithoutTheMouse(input);
        StepTheCreateWindow(input);
        return;
    }
    if (const std::optional<UI::SelectionBarClick> click = m_bar.Step(input, m_screen))
    {
        OnTheBar(*click);
        return;
    }
    const auto now = App::LogicStep * static_cast<std::int64_t>(m_steps);
    const bool doubleClick = m_doubleClick.Step(input, now);
    // Con el mouse sobre la barra, la UI lo tiene (+0x8de8): la escena no elige, no hace el doble clic
    // y no busca quién está debajo, así que el contorno queda en el último.
    if (!m_bar.Covers(UI::MouseInPixels(input, m_screen), m_screen))
    {
        m_hovered = UnderTheMouse(input);
        if (doubleClick && m_hovered)
        {
            // El doble clic elige y entra como Enter, sin el sonido (0x6342c0).
            Choose(m_hovered);
            TryToEnter();
            return;
        }
        if (input.WasPressed(Input::MouseButton::Left))
        {
            Choose(m_hovered);
        }
    }
    // El original pide GetAsyncKeyState(VK_RETURN) == 0x8001, y pierde un Enter corto entre dos
    // lecturas; MuRet lo ve, como en el login (Q-C45). La barra no lo traba.
    if (input.WasPressed(Input::Key::Enter))
    {
        TryToEnter();
    }
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
    const std::optional<std::size_t> nearest = Render::Models::NearestHit(origin, end, positions);
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
    const Protocol::Character::CharacterEntry* chosen = m_session->ChosenCharacter();
    if (chosen == nullptr)
    {
        return;
    }
    if (Protocol::Character::IsLocked(*chosen))
    {
        m_notice.Open(UI::LockedCharacterNotice);
        return;
    }
    if (m_context.world != nullptr)
    {
        if (m_context.cloth == nullptr)
        {
            m_context.log.Error("no se entra al mundo: falta el rand() de la CRT para cerrar el registro propio");
            return;
        }
        // 06, "La entrada al mundo", paso 1: el registro propio y la bandera de la escena 3.
        m_context.world->PrepareSelectedActor(*chosen, m_context.cloth->random);
    }
    m_context.log.Info("se elige el personaje del lugar {} para entrar al mundo", chosen->slot);
    m_next = SceneId::WorldLoading;
}

Render::Models::ObjectTransform CharacterSelectScene::ObjectOf(const Character& character) const
{
    const CharacterSlot& slot = CharacterSlots[character.slot];
    const glm::vec3 position = m_context.selection != nullptr
                                   ? m_context.selection->Slots()[character.slot].position
                                   : glm::vec3(slot.position.x, slot.position.y, CharacterSlotHeight);
    return Render::Models::ObjectTransform{position, {0.0f, 0.0f, slot.angle}, character.appearance.scale, 0.0f};
}

Render::Camera3D CharacterSelectScene::StepTheCameraAndTheTrapezoid(const World::GameCameraInput& input,
                                                                    Core::Duration clock)
{
    // Como CameraForFrame: un dibujo intermedio solo consulta la cámara del último frame.
    const int frames = m_cameraFrames.FramesAt(clock, OriginalFramePaceOf(m_context.pacing));
    if (frames == 0)
    {
        const Render::Camera3D camera = World::GameCameraOf(CameraState(), input);
        if (m_context.world != nullptr)
        {
            // El trapecio del pase del terreno y de los objetos, con el nivel que hay, sin escribir nada.
            auto polygon =
                TerrainPolygonBetweenFrames(m_context.world->Runtime(), SelectionPolygonInput(input, camera));
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
        camera = World::StepGameCamera(CameraState(), input);
        // 0x634736..0x63475c, después de los fondos: 0x5fbb00(w · 0,0015625, &p) con 0x620db0(0, 0, w, 48), el
        // pase que no dibuja. El rango de bloques no lo lee nadie de MuRet en la selección (08 P-C41 b); el
        // trapecio y su nivel van a los globales (0x5fbeef → 0x5fb570). Nada de 0x634736..0x634769 llama a
        // rand() (*estático*). Sin WorldState no hay dónde dejarlos: son los tests que no lo miran.
        if (m_context.world == nullptr)
        {
            continue;
        }
        auto polygon =
            StepTheTerrainPolygon(m_context.world->Runtime(), SelectionPolygonInput(input, camera), CameraState());
        if (!polygon.HasValue())
        {
            m_failure = polygon.Error();
            return camera;
        }
        m_polygon = polygon.Value();
    }
    return camera;
}

void CharacterSelectScene::Render(SceneCanvas& canvas, float alpha)
{
    if (m_failure)
    {
        return;
    }
    m_screen = canvas.screen;
    if (WaitingForTheList() ||
        (m_context.selection != nullptr && m_session != nullptr && !m_context.selection->IsInitialized()))
    {
        // Sin la lista, 0x634680 no dibuja; el aviso es de la UI, que sí, y lleva el cursor encima (el del
        // sistema está escondido). Sin aviso, el frame queda retenido (HoldsTheLastFrame).
        m_notice.Draw(canvas.sprites, canvas.text, canvas.screen);
        if (m_notice.IsOpen())
        {
            DrawTheCursor(canvas);
        }
        return;
    }
    // Los handlers reconstruyen o desactivan actores aun si este frame no corre un paso lógico.
    SyncTheCharacters();
    Render::SpriteBatch& sprites = canvas.sprites;
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    sprites.Quad(Background(0x717, 0.0f));
    sprites.Quad(Background(0x718, 320.0f));
    canvas.flushSprites();

    // 0x6346ca calcula la cámara con el héroe compartido vigente antes de dibujar los personajes, y 0x63475c el
    // trapecio del terreno.
    World::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.scene = World::CullingScene::CharacterSelect;
    const std::optional<SceneWorld> world = WorldOf(SceneId::CharacterSelect);
    const std::uint8_t map = world ? world->map : 0;
    cameraInput.map = map;
    const Render::Camera3D camera = StepTheCameraAndTheTrapezoid(cameraInput, canvas.frameClock);
    if (m_failure)
    {
        return;
    }
    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, WorldViewport, camera);
    m_terrainRange = World::TerrainRangeOf(pass.view, camera);
    if (m_context.world != nullptr)
    {
        m_context.world->DynamicLighting().PublishRange(m_terrainRange);
    }
    m_lastPass = pass;
    canvas.models.Begin(pass);
    if (world)
    {
        canvas.world.Draw(*world, pass, alpha);
    }
    const int scanFrames = m_fullSetScanFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    FaunaDrawCarry projection;
    if (scanFrames == 0 && m_context.world != nullptr)
        projection = m_context.world->PoseCarry();
    auto* poseCarry = m_context.world == nullptr ? nullptr
                      : scanFrames > 0           ? &m_context.world->PoseCarry()
                                                 : &projection;
    DrawTheWorldAndTheCharacters(canvas, alpha, pass, camera, map, poseCarry, scanFrames);
    // Como en el juego, el lote abierto se vacía también cuando el dibujo se corta.
    canvas.flushModels();
    if (m_failure)
    {
        return;
    }
    // 0x634949..0x63495f: los avisos y el área vieja. La UI del modo 4 (0x65fe00): la barra, la ventana de crear y el
    // aviso arriba. Y el cursor (0x634980).
    DrawTheMessages(canvas);
    // +0x511: el primer 0D con 1 que llegó en la selección, que guarda el juego.
    std::string_view serverNotice;
    if (m_context.world != nullptr && m_context.world->Runtime().chat.selectionNotice)
        serverNotice = *m_context.world->Runtime().chat.selectionNotice;
    m_bar.Draw(sprites, canvas.text, canvas.screen, serverNotice);
    DrawTheCreateWindow(canvas, alpha);
    m_notice.Draw(sprites, canvas.text, canvas.screen);
    DrawTheCursor(canvas);
}

bool CharacterSelectScene::HasTheWorld() const
{
    constexpr int SelectionWorldMap = 54;
    return m_context.world != nullptr && m_context.cloth != nullptr && m_context.selection != nullptr &&
           m_context.selection->IsInitialized() && m_context.selection->Terrain() != nullptr &&
           m_context.world->Runtime().map == SelectionWorldMap;
}

std::function<double(float, float)> CharacterSelectScene::TerrainHeight()
{
    const World::WorldTerrain* terrain = m_context.selection != nullptr ? m_context.selection->Terrain() : nullptr;
    if (terrain == nullptr || !terrain->heights || !terrain->attributes)
    {
        return {};
    }
    return [terrain, this](float x, float y)
    { return World::TerrainHeightAt(*terrain->heights, *terrain->attributes, x, y, CameraState().fixedHeight); };
}

void CharacterSelectScene::DrawTheWorldAndTheCharacters(SceneCanvas& canvas, float alpha, const Render::Pass3D& pass,
                                                        const Render::Camera3D& camera, std::uint8_t map,
                                                        FaunaDrawCarry* poseCarry, int scanFrames)
{
    // 0x6347ba..0x6347e2, después de 0x6346ca: Z163 y la luz +0xf4 en 0 en los cinco primeros registros del
    // pool, sin comprobar alive ni presencia de slot0, después de la espera de lista de 0x634680.
    // Hero apunta al registro0 incluso cuando la lista recibida está vacía.
    World::GameCameraInput& cameraInput = CameraInputState();
    if (m_context.selection != nullptr)
    {
        m_context.selection->DrawSelection(cameraInput);
    }
    else
    {
        cameraInput.hero.z = CharacterSlotHeight;
    }
    if (!HasTheWorld())
    {
        DrawTheCharacters(canvas, alpha, map, poseCarry, scanFrames);
        return;
    }
    WorldState& world = *m_context.world;
    if (auto loaded = m_world.Load(m_context.dataRoot, map, m_gameSlots, &m_models); !loaded.HasValue())
    {
        m_failure = loaded.Error();
        return;
    }
    const int frames = m_worldFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    m_world.BeginFrame(world, *m_context.cloth, frames);
    const SelectionWorldFrame frame{.batch = canvas.models,
                                    .polygon = m_polygon,
                                    .range = m_terrainRange,
                                    .frameClock = canvas.frameClock,
                                    .frames = frames,
                                    .fixedHeight = CameraState().fixedHeight,
                                    .flushModels = canvas.flushModels};
    // 0x6347e9..0x63483e: el elegido vivo lleva su luz a (1, 1, 1) y la suma al buffer dinámico en su lugar,
    // con radio 1 (0x5f9780), entre 0x620a90 y 0x620980.
    const Protocol::Character::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    ActorState* chosenActor =
        chosen != nullptr && chosen->slot < SelectionActorCount ? &world.Actors()[chosen->slot] : nullptr;
    if (chosenActor != nullptr && !chosenActor->alive)
    {
        chosenActor = nullptr;
    }
    if (chosenActor != nullptr)
    {
        chosenActor->light = glm::vec3(1.0f);
        World::AddTerrainLight(m_world.FrameLighting(), chosenActor->position.x, chosenActor->position.y,
                               Data::Terrain::TerrainLightColor{1.0f, 1.0f, 1.0f}, 1);
    }
    const World::WorldTerrain& terrain = *m_context.selection->Terrain();
    // 0x634844: 0x5fc260(0), el terreno, el pasto y las marcas.
    if (auto drawn = m_world.DrawTerrain(world, terrain, frame); !drawn.HasValue())
    {
        m_failure = drawn.Error();
        return;
    }
    // 0x63484c: 0x6046a0, los objetos del 54 con sus casos de 0x425260.
    if (poseCarry != nullptr)
    {
        if (auto drawn = m_world.DrawObjects(world, frame, *poseCarry); !drawn.HasValue())
        {
            m_failure = drawn.Error();
            return;
        }
    }
    // 0x634851: 0x510d00, los personajes (y el contorno de 0x59ec80 con el de bajo el mouse).
    DrawTheCharacters(canvas, alpha, map, poseCarry, scanFrames);
    if (m_failure || poseCarry == nullptr)
    {
        return;
    }
    // 0x63486a: 0x4453b0, los ayudantes, con la escala en 1 de la escena 4 (0x4452f1), la luz del terreno 54 y los
    // sprites del frame.
    if (auto drawn = DrawHelpers(HelperDrawContext{
            .batch = canvas.models,
            .models = m_models,
            .world = world,
            .random = m_world.FrameRandom(),
            .frameClock = canvas.frameClock,
            .stepAlpha = 0.0f,
            .originalFrame = frames > 0,
            .onPose = [&](const DrawnObject& object) { return PublishFaunaPose(*poseCarry, m_models, object, true); },
            .publishProjectedPose = true,
            .posedMeshes = [&]() { return std::span<const Render::Models::SkinnedMesh>(poseCarry->meshes); },
            .scene = SceneId::CharacterSelect,
            .lighting = &m_world.FrameLighting(),
            .sprites = &m_world.FrameSprites(),
            .worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
            .effects = &m_world.FrameEffects(),
            .particles = &m_world.FrameParticles(),
            // timeGetTime (0x6b8544), del calco 0x14e de los Fenrir: el reloj del dibujo en ms.
            .systemTime = [systemTime = static_cast<std::uint32_t>(
                               std::chrono::duration_cast<std::chrono::milliseconds>(canvas.frameClock).count())]()
            { return systemTime; },
            .globalPose = [&]() { return std::span<const Render::Models::BoneTransform>(poseCarry->bones); }});
        !drawn.HasValue())
    {
        m_failure = "0x63486a → " + drawn.Error();
        return;
    }
    // 0x63486f..0x634883: las cintas, las juntas(0), los efectos con modelo(0) y 0x549510.
    if (auto drawn = m_world.DrawAfterCharacters(world, terrain, frame, camera, *poseCarry); !drawn.HasValue())
    {
        m_failure = drawn.Error();
        return;
    }
    // 0x634896..0x6348dc: el elegido con la luz 0,15 (0x3e19999a), que prestan las dos 0x729 (la luz ancha y
    // la de Impack03 que sube) en su lugar y con sus ángulos, de escala 1 y sin dueño.
    if (chosenActor != nullptr)
    {
        chosenActor->light = glm::vec3(std::bit_cast<float>(0x3e19999au));
        for (const std::int32_t subtype : {0, 1})
        {
            const auto created = m_world.FrameParticles().CreateOwnedChecked(
                World::ChaseLightParticle, chosenActor->position, chosenActor->angles, chosenActor->light, subtype,
                1.0f, static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count()),
                m_world.FrameRandom(), SelectionSceneId);
            if (!created.HasValue())
            {
                m_failure = std::format("0x53fe70: la 0x729/{} del elegido falla con error {}", subtype,
                                        static_cast<unsigned>(created.Error()));
                return;
            }
        }
        // 0x634906: 0x4129f0, el nombre del elegido, es de la UI de los carteles (CL9).
    }
    canvas.flushModels();
    // 0x63490d..0x634927: el pase de ojo.
    if (auto drawn = m_world.DrawEye(pass, frame, m_textures, world.DrawTail().damage); !drawn.HasValue())
    {
        m_failure = drawn.Error();
        return;
    }
    canvas.models.Begin(pass);
}

void CharacterSelectScene::DrawTheCharacters(SceneCanvas& canvas, float alpha, std::uint8_t map,
                                             FaunaDrawCarry* poseCarry, int scanFrames)
{
    const Protocol::Character::CharacterEntry* chosen = m_session != nullptr ? m_session->ChosenCharacter() : nullptr;
    const std::size_t hero = m_context.world != nullptr ? m_context.world->Runtime().hero : 0;
    for (Character& character : m_characters)
    {
        ActorState* actor = m_context.world != nullptr && m_context.selection != nullptr && m_context.cloth != nullptr
                                ? &m_context.world->Actors()[character.slot]
                                : nullptr;
        if (actor != nullptr && (!actor->alive || !actor->seen))
        {
            continue;
        }
        const GameLogic::CharacterAppearance appearance =
            actor != nullptr ? AppearanceOfActor(*actor) : character.appearance;
        const FullSetScan scan = ScanTheCharacter(appearance, scanFrames);
        DrawnCharacter drawn;
        drawn.appearance = appearance;
        if (actor != nullptr)
        {
            character.animation.body = actor->animation;
            drawn.headAngles = actor->headTurn.angles;
        }
        drawn.body = BodyBetweenSteps(character.animation, m_models, alpha,
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
        }
        const bool isChosen = chosen != nullptr && chosen->slot == character.slot;
        // 0x50ba30: la base del lugar más +0xf4, que el dibujo dejó en 1 para el elegido vivo y en 0 para los
        // demás (sin el mundo, la escena lo escribe acá).
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
        // Q-C410: la lista (0x4c20a0) y la respuesta de crear (0x4c2290) arman cada personaje con 0x517270 →
        // 0x511290, que le pone +0x11c en 1 (0x511bf7) y le da su buffer de huesos (+0x120, 0x5123c4..0x5123ff).
        // 0x510d00 → 0x50ba30 → 0x6001b0 posa ahí el cuerpo (0x600259), que arranca de lo que dejó su dibujo
        // anterior y no del global; al global (0x68b181c) sólo va lo que cuelga.
        if (poseCarry != nullptr)
            drawn.poseBones = [poseCarry, actor](std::uint16_t model)
            {
                if (model != Data::Models::PlayerSkeletonModel)
                    return std::span<const Render::Models::BoneTransform>(poseCarry->bones);
                return actor != nullptr ? std::span<const Render::Models::BoneTransform>(actor->bodyPose)
                                        : std::span<const Render::Models::BoneTransform>();
            };
        if (poseCarry != nullptr)
            drawn.onPose = [this, poseCarry](std::uint16_t model, std::span<const Render::Models::BoneTransform> bones,
                                             const Render::Models::ObjectTransform& transform, bool translated)
            {
                if (model == Data::Models::PlayerSkeletonModel)
                    return;
                if (auto result = PublishFaunaPoseRaw(*poseCarry, m_models, model, bones, transform, translated);
                    !result.HasValue())
                    m_failure = result.Error();
            };
        // Con el mundo 54, el mismo 0x50ba30 que el juego: el cuerpo, las partes y la tela, y después lo que
        // cuelga y los efectos (0x50db57..0x510a7a), que no miran la escena salvo la luz (0x50cc29, 0x50d219).
        if (actor != nullptr && poseCarry != nullptr && HasTheWorld() && HasTheSkeleton())
        {
            // 0x50cca9..0x50d211: el héroe publicado (0x743a170, el lugar 0 desde WinMain) abre y cierra el registro
            // propio para su color, aun con los bits apagados, también en la escena 4, que después le pone 0,5 más
            // +0xf4 (0x50d219): 3.848 rand() al cerrarlo, en cada frame del original.
            if (static_cast<std::size_t>(character.slot) == hero)
            {
                for (int frame = 0; frame < scanFrames; ++frame)
                {
                    const Render::Models::LogicalAccess access(m_context.world->Runtime().ownRecordDepth,
                                                               m_context.cloth->random,
                                                               Render::Models::OwnRecordCloseRandoms);
                }
            }
            drawn.sprites = &m_world.FrameSprites();
            DrawCharacter(canvas.models, m_models, drawn, canvas.frameClock, CharacterDrawPass::Body);
            DrawTheTail(canvas, *actor, character.slot, drawn, *poseCarry, scan, scanFrames > 0);
            if (m_failure)
                return;
            continue;
        }
        DrawCharacter(canvas.models, m_models, drawn, canvas.frameClock);
        // Sólo un frame del original deja el cuerpo en sus huesos; un dibujo intermedio no los toca. 0x6001b0
        // copia antes la escala y la posición al modelo compartido de Player.bmd, con el alpha desde 0,01
        // (Q-C412).
        if (actor != nullptr && poseCarry != nullptr && scanFrames > 0)
        {
            actor->bodyPose = PoseCharacterBody(m_models, drawn);
            if (actor->alpha >= PlayerModelAlpha)
                m_context.world->PlacePlayerModel(*actor);
        }
    }
}

bool CharacterSelectScene::HasTheSkeleton() const
{
    const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    return skeleton != nullptr && !skeleton->actions.empty();
}

// 0x50db57..0x510ac2 sobre el cuerpo ya dibujado (GameCharacters.h, DrawCharacterTail), con las tablas del frame
// del mundo 54: las del mundo en un frame del original y sus copias entre frames. La luz de lo que cuelga
// (0x50db94, [ebp−0x44] más +0xf4) es la de la escena 4, 0,4 fijo (0x50cc29), o 0,5 en el héroe publicado, que la
// rama de su color pisa (0x50d231); con +0x375 desde 6, (1; 0,1; 0,1). El cuervo de un Dark Lord de la lista
// (EquipSelectionActor) va con el trapecio de la selección.
void CharacterSelectScene::DrawTheTail(SceneCanvas& canvas, ActorState& actor, std::size_t slot,
                                       const DrawnCharacter& drawn, FaunaDrawCarry& poseCarry, const FullSetScan& scan,
                                       bool originalFrame)
{
    WorldState& world = *m_context.world;
    ItemSpecialDrawContext special;
    ItemDrawContext itemContext;
    FillItemDrawContexts(special, itemContext,
                         ItemDrawResources{.frameClock = canvas.frameClock,
                                           .random = m_world.FrameCloth().random,
                                           .itemRender = m_world.FrameItemRender(),
                                           .sprites = m_world.FrameSprites(),
                                           .poseCarry = poseCarry,
                                           .modelState = m_world.FrameModelState(),
                                           .joints = m_world.FrameTail().joints,
                                           .effects = m_world.FrameEffects(),
                                           .map = world.Runtime().map,
                                           .originalFrame = originalFrame});
    constexpr std::uint8_t RedPk = 6;
    const float base = slot == world.Runtime().hero ? HeroLight : CharacterLight;
    actor.drawnLight = actor.pk >= RedPk ? glm::vec3{1.0f, 0.1f, 0.1f} : glm::vec3(base) + actor.light;
    const GameCharacterPass pass{.dynamicLighting = &m_world.FrameLighting(),
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
                                 .terrain = m_context.selection->Terrain()};
    if (auto drawnTail =
            DrawCharacterTail(canvas.models, m_models, world, actor, slot, drawn, pass, scan, originalFrame);
        !drawnTail.HasValue())
        m_failure = std::format("{}, en el personaje {} de la selección", drawnTail.Error(), slot);
}

// 0x50ba30 con cada uno de la lista, en 0x50c91e: son 0x20a con +0x7c y +0x3e4 en 0 (0x517270), y la vista
// previa de la ventana de crear no lo es. Sin las acciones de Player.bmd sale antes (0x50ba87), como
// DrawCharacter sin su esqueleto.
FullSetScan CharacterSelectScene::ScanTheCharacter(const GameLogic::CharacterAppearance& appearance, int frames)
{
    if (m_context.world == nullptr || m_context.cloth == nullptr ||
        m_models.SkeletonOf(Data::Models::PlayerSkeletonModel) == nullptr)
    {
        return {};
    }
    return ScanTheDrawnCharacter(*m_context.world, ScannedCharacterOf(appearance), m_context.cloth->random, frames);
}

void CharacterSelectScene::DrawTheMessages(SceneCanvas& canvas)
{
    if (m_context.world == nullptr)
        return;
    const int frames = m_messageFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    HudState projected;
    HudState& hud = frames > 0 ? m_context.world->Runtime().hud : (projected = m_context.world->Runtime().hud);
    const UI::Point mouse =
        m_context.input != nullptr ? UI::MouseIn640x480(*m_context.input, canvas.screen) : UI::Point{};
    Scenes::DrawTheMessages(*m_context.world, hud, canvas.sprites, canvas.text, mouse, false);
}

void CharacterSelectScene::DrawTheCursor(SceneCanvas& canvas) const
{
    if (m_context.input != nullptr)
    {
        UI::DrawGameCursor(canvas.sprites, *m_context.input, canvas.screen);
    }
}

void CharacterSelectScene::Leave()
{
    m_world.Release();
    m_textures.Release();
    m_windowTextures.Release();
    // 0x61cc50 suelta los slots de 0x6f8 a 0x71d.
    m_selectionModels.Release();
}

void CharacterSelectScene::StepThePreview()
{
    if (!m_create.IsOpen())
    {
        return;
    }
    m_previewAdvanced = true;
    // 0x63d880 → 0x63d7c0 → 0x63e3e0: la clase que eligió el clic de este paso arma otra vez la
    // vista previa, que después avanza.
    if (m_create.ChosenClass() != Preview().characterClass)
    {
        if (m_context.world != nullptr && m_context.cloth != nullptr && m_context.selection != nullptr)
        {
            const ActorConstructionContext construction{.terrain = *m_context.selection->Terrain(),
                                                        .map = m_context.world->Runtime().map,
                                                        .scene = World::CullingScene::CharacterSelect,
                                                        .fixedHeight = CameraState().fixedHeight};
            if (auto rebuilt = RebuildPreview(Preview(), m_create.ChosenClass(), m_selectionModels, construction,
                                              *m_context.cloth);
                !rebuilt.HasValue())
            {
                m_failure = rebuilt.Error();
                return;
            }
        }
        else
        {
            ChoosePreviewClass(Preview(), m_create.ChosenClass(), m_selectionModels);
        }
    }
    StepPreview(Preview(), m_selectionModels);
}

// 0x66a050: la base de la ventana; 0x63e460: la vista previa en su propio pase, sin limpiar la
// profundidad (se prueba contra la del pase de la selección, como en el original); y 0x63e0a0: lo
// demás, encima.
void CharacterSelectScene::DrawTheCreateWindow(SceneCanvas& canvas, float alpha)
{
    if (!m_create.IsOpen())
    {
        // 0x63e460 solo corre con la ventana dibujada: al reabrir no recupera el tiempo cerrada.
        m_previewDrawFrames = {};
        return;
    }
    m_create.DrawBase(canvas.sprites, canvas.screen);
    canvas.flushSprites();
    const UI::Rect box = UI::LayoutCreateWindow(canvas.screen, m_create.ChosenClass()).box;
    const int frames = m_previewDrawFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    const float stepAlpha = m_previewAdvanced ? alpha : 0.0f;
    DrawnObject drawn;
    for (int frame = 0; frame < frames; ++frame)
    {
        m_previewCamera = PreviewCamera(Preview());
        drawn = DrawnPreview(Preview(), m_selectionModels, stepAlpha);
    }
    if (frames == 0)
    {
        drawn = DrawnPreview(Preview(), m_selectionModels, stepAlpha, 0);
    }
    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, PreviewViewport(box, canvas.screen), m_previewCamera);
    canvas.models.Begin(pass);
    // Sin el avance de este paso (un aviso arriba), la vista previa va quieta: interpolar uno que no
    // va a dar la haría temblar.
    if (m_context.world != nullptr)
    {
        // Q-C410: 0x63e3e0 la arma con 0x511290 (0x63e409), que le pone +0x11c en 1 y le da su buffer (+0x120);
        // 0x63e637 → 0x50ba30 → 0x602e90 → 0x5ffdf0 la posa ahí (0x5fff13..0x5fff4c) y no en el global. Sólo un
        // frame del original guarda la pose en el buffer propio.
        FaunaDrawCarry own;
        own.bones = Preview().bodyPose;
        if (auto result = PublishFaunaPose(own, m_selectionModels, drawn, true); !result.HasValue())
        {
            m_failure = result.Error();
            return;
        }
        DrawObjectFromMeshes(canvas.models, m_selectionModels, drawn, canvas.frameClock, own.meshes);
        if (frames > 0)
            Preview().bodyPose = std::move(own.bones);
    }
    else
        DrawObject(canvas.models, m_selectionModels, drawn, canvas.frameClock);
    canvas.flushModels();
    m_create.Draw(canvas.sprites, canvas.text, canvas.screen);
}

std::optional<SceneId> CharacterSelectScene::Next() const
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

const ActorState& CharacterSelectScene::PreviewActor() const
{
    return Preview();
}

bool CharacterSelectScene::HoldsTheLastFrame() const
{
    // 0x634680 sale con al = 0 mientras 0x7d3a13f está en 0 o el estado está antes de 0x33, y sin ese
    // verdadero 0x637700 no presenta el frame (0x6385e1..0x6385f7): queda el último del login. Hasta que un
    // paso vio la lista (y contó el primero de los suyos), salvo con un aviso abierto, que MuRet muestra.
    return m_session != nullptr && (m_steps == 0 || WaitingForTheList()) && !m_notice.IsOpen();
}
} // namespace Client::Scenes
