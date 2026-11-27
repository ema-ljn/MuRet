#include "Scenes/LoginScene.h"
#include "Crt/CrtRandom.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/Skinning.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Assets/FaunaModels.h"

#include "App/LoopTiming.h"
#include "Audio/Music.h"
#include "Audio/Playback.h"
#include "App/ServerAddress.h"
#include "Session/ModuleStates.h"
#include "Session/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Tables/PlayerModels.h"
#include "Data/Textures/TextureSet.h"
#include "Kernel/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Render/OriginalQuad.h"
#include "Render/OriginalSkewedQuad.h"
#include "Render/Billboard.h"
#include "Render/Pass3D.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorAppearance.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/CharacterHead.h"
#include "Actors/CharacterStep.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/DarkSpirit.h"
#include "Actors/FullSetScan.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Tables/GameData.h"
#include "Scenes/GameDrawTail.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameParticles.h"
#include "Scenes/HandShine.h"
#include "Render/Frame/ITextDrawer.h"
#include "Actors/Drawing/ItemDrawResources.h"
#include "Scenes/LoginModels.h"
#include "Scenes/LoginActor.h"
#include "Scenes/OptionsSettings.h"
#include "Camera/SceneCameras.h"
#include "Camera/GameCamera.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Assets/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/SelectionState.h"
#include "Assets/TextureSets.h"
#include "Assets/WorldModels.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Scenes/WizardEffects.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/WorldMap.h"
#include "UI/CreditsWindow.h"
#include "UI/EscapeWindow.h"
#include "UI/GameCursor.h"
#include "UI/Widget.h"
#include "UI/GameText.h"
#include "UI/LoginWindow.h"
#include "UI/MenuBar.h"
#include "UI/MessageArea.h"
#include "UI/NewUi.h"
#include "UI/Notices.h"
#include "UI/OptionsWindow.h"
#include "UI/PanelRegistry.h"
#include "UI/ServerListModel.h"
#include "UI/ServerListWindow.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "World/Leaves.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/ObjectModelStepState.h"
#include "Terrain/SphereFrustum.h"
#include "World/Sprites.h"
#include "World/ParticleDrawing.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "Terrain/TerrainLighting.h"
#include "World/WizardModelEffects.h"
#include "World/WorldObjects.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ratio>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
constexpr std::int32_t ButtonClickSound = 0x19;

std::string ObjectCreationFailureMessage(const World::WorldObjectCreationFailure& failure)
{
    const char* reason = "error de creación desconocido";
    switch (failure.error)
    {
    case World::WorldObjectCreationError::InvalidAngle:
        reason = "ángulo fuera del dominio admitido";
        break;
    case World::WorldObjectCreationError::UnsupportedEffect:
        reason = "efecto de creación no disponible";
        break;
    case World::WorldObjectCreationError::UnsupportedMapHandler:
        reason = "handler de creación no disponible";
        break;
    }
    return "Objeto del mundo, registro " + std::to_string(failure.recordIndex) + ": " + reason;
}

// Lo que deja 0x620a00(1) con el blanco, antes de los fondos.
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor4f(1, 1, 1, 0,5) con la mezcla normal, antes de pasar a la aditiva: nada se dibuja con él.
constexpr Render::Color HalfWhite{1.0f, 1.0f, 1.0f, 0.5f};
// glColor3f(0,25; 0,1; 0,1), el de la arena (0x3e800000 y 0x3dcccccd), y glColor3f(0,8; 0,7; 0,6),
// el de los textos (0x3f4ccccd, 0x3f333333 y 0x3f19999a). glColor3f deja el alfa en 1.
constexpr Render::Color SandColor{0.25f, 0.1f, 0.1f, 1.0f};
constexpr Render::Color TextColor{0.8f, 0.7f, 0.6f, 1.0f};

// Un fondo: 0x621b60(slot, x, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0).
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

// Una arena de 0x622000 en (0, 25, 640, 430), v = 0, y su velocidad: la u es ((int) T % 100000) ·
// velocidad (0x6b8a94, 0,0001, y 0x6b8a68, 0,0005).
struct Sand
{
    std::uint16_t slot = 0;
    float speed = 0.0f;
    float du = 1.0f;
    float dv = 1.0f;
};
constexpr Sand SlowSand{.slot = 0x5c2, .speed = 0.0001f, .du = 0.3f, .dv = 0.3f};
constexpr Sand FastSand{.slot = 0x5c3, .speed = 0.0005f, .du = 3.0f, .dv = 2.0f};
// El módulo de la u (0x186a0).
constexpr std::int64_t SandPeriodMs = 100000;

// La x87 hace fild del resto y fmul con la velocidad en float, y guarda en float: con un resto de
// menos de 2¹⁷ y una velocidad de 24 bits, el producto entra exacto en un double, así que da lo
// mismo.
Render::OriginalSkewedQuad SandQuad(const Sand& sand, Core::Duration frameClock)
{
    const std::int64_t remainder =
        std::chrono::duration_cast<std::chrono::milliseconds>(frameClock).count() % SandPeriodMs;
    return Render::OriginalSkewedQuad{
        .slot = Render::TextureSlot{sand.slot},
        .x = 0.0f,
        .y = 25.0f,
        .width = 640.0f,
        .height = 430.0f,
        .u = static_cast<float>(static_cast<double>(remainder) * static_cast<double>(sand.speed)),
        .v = 0.0f,
        .du = sand.du,
        .dv = sand.dv};
}

// La fuente DAT_05763554, el texto en blanco (DAT_006c8e5c) y el fondo en 0x80000000
// (DAT_006c8e64).
constexpr Render::Text::TextStyle LoginTextStyle{Render::Text::FontKind::Normal,
                                                 Render::Text::TextColors{0xffffffff, 0x80000000}};
// Dónde se apoyan: la y de abajo, y la x donde termina el 454 y empieza el 455 (0x14f).
constexpr int TextBottom = 479;
constexpr int TextSplit = 335;

constexpr std::string_view VersionMark = "%s";
// Los textos del área de mensajes: el clic en un servidor (0x64630f y 0x646320), el envío de la cuenta (0x6427d9 y
// 0x6427ea) y la respuesta al F4 03 (0x4e33d0).
constexpr std::size_t ConnectingText = 470;
constexpr std::size_t PleaseWaitText = 471;
constexpr std::size_t CheckingAccountText = 472;
constexpr std::size_t WaitAgainText = 473;
constexpr std::size_t ConnectedText = 481;

// El lugar de un grupo sin botón para el título: solo llega con --auto-login, que elige el primero
// de la lista aunque la ventana no lo muestre. Cualquiera que no sea el 4 da lo mismo (IsNonPvp).
constexpr int NoSlot = -1;

[[nodiscard]] bool IsWindowTexture(Render::TextureSlot slot)
{
    return std::ranges::any_of(Assets::WindowTextureSet, [slot](const Data::Textures::TextureSetEntry& entry)
                               { return entry.slot == slot.value; });
}

// Los slots de las ilustraciones de los créditos (0x63fc50).
[[nodiscard]] bool IsIllustration(Render::TextureSlot slot)
{
    return std::ranges::any_of(Assets::CreditIllustrationSets.front(),
                               [slot](const Data::Textures::TextureSetEntry& entry)
                               { return entry.slot == slot.value; });
}

// Los dos estados del connect, que en el original es bloqueante (Q-C85).
[[nodiscard]] bool IsConnecting(App::SessionState state)
{
    return state == App::SessionState::ConnectingConnectServer || state == App::SessionState::ConnectingGameServer;
}
} // namespace

// El dragón: solo en el mapa 55, y su dueño, el 0xb4 que crea 0x616ec0 (0x6188ae).
constexpr std::uint8_t DragonMap = 0x37;
// El mapa del login (World56), el de las brasas de 0x530490.
constexpr std::uint8_t EmberMap = 0x37;
constexpr glm::vec3 DragonOwnerPosition{5600.0f, 23000.0f, 0.0f};

// Los modelos de los objetos del login: los del mundo y el logo (0xa2), que la tabla del original guarda
// junto con los demás.
class LoginObjectModels final : public Models::IModelLibrary
{
public:
    LoginObjectModels(const Models::IModelLibrary& world, const Models::IModelLibrary& logo)
        : m_world(world), m_logo(logo)
    {
    }

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override
    {
        return Of(model).Find(model);
    }
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return Of(model).SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return Of(model).MeshTextures(model);
    }

private:
    [[nodiscard]] const Models::IModelLibrary& Of(std::uint16_t model) const
    {
        return model == LoginLogoModel ? m_logo : m_world;
    }

    const Models::IModelLibrary& m_world;
    const Models::IModelLibrary& m_logo;
};

// Los modelos de los efectos con modelo del login: la tabla del original es una sola, con los de Object56\ (el
// dragón, 0x616ec0) y los de los efectos (Skill\Fire01 del meteoro 0xc2, 0x615700); MuRet busca primero en el mundo y
// después en los de la carga.
class LoginEffectModels final : public Models::IModelLibrary
{
public:
    LoginEffectModels(const Models::IModelLibrary& world, const Models::IModelLibrary& loaded)
        : m_world(world), m_loaded(loaded)
    {
    }

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override
    {
        return Of(model).Find(model);
    }
    [[nodiscard]] const Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return Of(model).SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return Of(model).MeshTextures(model);
    }

private:
    [[nodiscard]] const Models::IModelLibrary& Of(std::uint16_t model) const
    {
        return m_world.Find(model) != nullptr ? m_world : m_loaded;
    }

    const Models::IModelLibrary& m_world;
    const Models::IModelLibrary& m_loaded;
};

// El logo de 0x606660 como uno más de la lista: sin luz por vértice, como lo deja su caso.
World::WorldObject LogoWorldObject()
{
    const ModelObject logo = LoginLogo();
    World::WorldObject object;
    object.type = static_cast<std::int16_t>(logo.model);
    object.transform = logo.transform;
    object.state = logo.state;
    object.light = logo.light;
    object.speed = logo.speed;
    object.vertexLight = false;
    object.sectorX = World::SectorOf(object.transform.position.x);
    object.sectorY = World::SectorOf(object.transform.position.y);
    return object;
}

std::string PlaceVersion(std::string_view text, std::string_view version)
{
    const std::size_t mark = text.find(VersionMark);
    if (mark == std::string_view::npos)
    {
        return std::string(text);
    }
    std::string placed(text.substr(0, mark));
    placed.append(version);
    placed.append(text.substr(mark + VersionMark.size()));
    return placed;
}

LoginScene::LoginScene(const SceneContext& context, const Tables::GameData& data, const Models::IModelLibrary& models,
                       App::ServerSession* session, std::string_view version, std::mt19937::result_type seed,
                       const Models::ModelTextureSlots* gameSlots)
    : m_context(context), m_localWorld(context.world == nullptr ? std::make_unique<Kernel::WorldState>() : nullptr),
      m_localCloth{.random = Crt::CrtRandom(static_cast<std::uint32_t>(seed))}, m_models(models), m_session(session),
      m_texts(data.text ? &*data.text : nullptr), m_textures(context.textures, Assets::LoginTextureSet),
      m_windowTextures(context.textures, Assets::WindowTextureSet), m_world(WorldOf(SceneId::Login)),
      m_terrain(context.textures), m_gameSlots(gameSlots),
      m_serverList([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_menuBar([this](Render::TextureSlot slot) { return BufferSize(slot); }),
      m_login([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_notice([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_escape([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_options([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_credits([this](Render::TextureSlot slot) { return BufferSize(slot); }, data.credits)
{
    if (data.text)
    {
        m_left = data.text->Text(LoginLeftText);
        m_right = data.text->Text(LoginRightText);
        if (const auto format = data.text->Text(LoginVersionText))
        {
            m_version = PlaceVersion(*format, version);
        }
    }
}

LoginScene::~LoginScene()
{
    DetachTheDragonOwner();
}

void LoginScene::DetachTheDragonOwner()
{
    if (m_objects.empty())
        return;
    // El mundo compartido puede conservar efectos después de la escena, aun en lugares inactivos.
    // La vida y la visibilidad del último dibujo no dependen de este enlace.
    const void* owner = &m_objects.front();
    for (World::ModelEffect& effect : CharacterWorld().Map().effects.Effects())
    {
        if (effect.ownerContext == owner)
        {
            effect.ownerContext = nullptr;
            effect.hasOwner = false;
        }
    }
}

Camera::GameCamera& LoginScene::CameraState()
{
    return m_context.camera != nullptr ? *m_context.camera : m_localCamera;
}

Camera::GameCameraInput& LoginScene::CameraInputState()
{
    return m_context.cameraInput != nullptr ? *m_context.cameraInput : m_localCameraInput;
}

Terrain::TerrainPolygon& LoginScene::SharedTerrainPolygon()
{
    return m_context.states != nullptr ? m_context.states->camera.trapezoid : m_localTrapezoid;
}

Kernel::WorldState& LoginScene::CharacterWorld() const
{
    return m_context.world != nullptr ? *m_context.world : *m_localWorld;
}

Models::ClothWorld& LoginScene::CharacterRandomWorld()
{
    return m_context.cloth != nullptr ? *m_context.cloth : m_localCloth;
}

Core::Expected<void, std::string> LoginScene::Enter()
{
    m_failure.reset();
    m_cameraFrames = {};
    m_objectEffectFrames = {};
    m_fullSetScanFrames = {};
    m_characterCloth.RestartTheClock();
    m_drawnCamera = {};
    Camera::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.scene = Terrain::CullingScene::Login;
    cameraInput.map = m_world ? m_world->map : EmberMap;
    // 0x634c19 escribe solo el yaw; 0x634b60 no borra al héroe si su puntero es válido.
    CameraState().yaw = 0.0f;
    LoadTheWorld();
    auto loaded = m_textures.Load(m_context.dataRoot);
    if (!loaded.HasValue())
    {
        RetainTheTerrain();
        return loaded;
    }
    auto logo = LoadTheLogo();
    if (!logo.HasValue())
    {
        RetainTheTerrain();
        return logo;
    }
    auto windows = m_windowTextures.Load(m_context.dataRoot);
    if (!windows.HasValue())
    {
        RetainTheTerrain();
        return windows;
    }
    m_serverList.Close();
    m_menuBar.Close();
    m_login.Close();
    m_notice.Close();
    m_escape.Arm(UI::NewUiMode::Login);
    // 0x644120 con los globales (0x6446e0) y 0x63f020, que lee credit.bmd (en MuRet, GameData).
    m_options.Arm(OptionsSettingsOf(CharacterWorld().Runtime()));
    m_credits.Arm();
    ReleaseTheIllustrations();
    // 0x65f300: cada una en la cabeza, en este orden: el aviso, la de Esc, las opciones, la barra, la
    // lista, la cuenta y los créditos.
    const std::array<UI::NewUiWindow*, 7> uiWindows{&m_notice,     &m_escape, &m_options, &m_menuBar,
                                                    &m_serverList, &m_login,  &m_credits};
    m_ui.Build(UI::NewUiMode::Login, uiWindows);
    m_seen.reset();
    m_sawTheAddress = false;
    m_quit = false;
    if (m_session != nullptr)
    {
        m_session->Start();
    }
    m_messageFrames = {};
    // 0x634b45..0x634b5e: 120 mensajes vacíos.
    if (m_context.world != nullptr)
    {
        if (auto emptied = AddTheEmptyMessages(m_context.world->Runtime(), m_context.textMeasure); !emptied.HasValue())
        {
            RetainTheTerrain();
            return emptied;
        }
        // 0x634bf0..0x634c2e: la ventana de los comandos ([0x6e592c]) cerrada y sin comando.
        UI::ResetTheCommands(m_context.world->Runtime().panels.delegated);
    }
    auto placed = PlaceTheModels();
    if (!placed.HasValue())
    {
        RetainTheTerrain();
        return placed;
    }
    if (m_context.world != nullptr)
    {
        auto& runtime = m_context.world->Runtime();
        runtime.music.Play(runtime.audio, "Music\\main_theme.mp3");
    }
    return {};
}

void LoginScene::LoadTheWorld()
{
    if (m_context.selection != nullptr)
    {
        // El préstamo anterior deja de ser válido antes de destruir o reemplazar las tablas.
        m_context.selection->BorrowTerrain(m_world ? m_world->map : EmberMap, nullptr);
    }
    m_terrain.Release();
    m_lighting.reset();
    m_worldModels.Release();
    m_objectRecords.clear();
    if (!m_world)
    {
        return;
    }
    // 0x6349bc → 0x61a58c: la carga del mapa del login libera las reservas del mundo anterior.
    Kernel::WorldState& world = CharacterWorld();
    if (world.Runtime().audio != nullptr)
    {
        world.Runtime().audio->ReleaseWorldSounds();
        world.FaunaResources().ResetSoundRegistrationStamps();
    }
    // Rango inicial de 0x5fbb00 con el pase de entrada (los bloques que empiezan en x 0..128 e y 120..248, 06 §
    // El terreno del login). El trapecio no: es el global, que antes del primer dibujo del login tiene los
    // ceros de la .bss o el que dejó la escena de antes (Q-C217).
    const Render::Camera3D camera = Camera::LoginCamera();
    m_terrainRange = Terrain::TerrainRangeOf(
        Render::MakePass3D(Core::PixelSize{640, 480}, Camera::WorldViewport, camera).view, camera);
    LoadTheTerrain();
    LoadTheObjects();
}

void LoginScene::LoadTheTerrain()
{
    std::vector<std::string> skipped;
    auto loaded = m_terrain.Load(m_context.dataRoot, m_world->map, skipped);
    if (m_context.selection != nullptr)
    {
        m_context.selection->BorrowTerrain(m_world->map, m_terrain.Terrain());
        if (loaded.HasValue())
        {
            m_context.selection->RefreshCameraAttribute(CameraInputState());
        }
    }
    if (!loaded.HasValue())
    {
        m_context.log.Warning("el login va sin el terreno: {}; la selección necesita esos datos para sus alturas",
                              loaded.Error());
        // Sin el terreno, la luz de los modelos puede salir igual de sus dos archivos.
        auto lighting = Terrain::LoadTerrainLighting(m_context.dataRoot, m_world->map);
        if (!lighting.HasValue())
        {
            m_context.log.Warning("el login va sin la luz del terreno: {}", lighting.Error());
            return;
        }
        m_lighting = std::move(lighting).Value();
        return;
    }
    for (const std::string& tile : skipped)
    {
        m_context.log.Warning("el terreno del login va sin un tile, que el original saltea: {}", tile);
    }
}

void LoginScene::RetainTheTerrain()
{
    if (m_context.selection == nullptr || !m_world)
    {
        return;
    }
    if (auto terrain = m_terrain.TakeTerrain())
    {
        m_context.selection->RetainTerrain(m_world->map, std::move(*terrain));
    }
}

// 0x616ec0 y 0x607370: los modelos de Object56\ con la tabla de la carga, y los registros de
// EncTerrain56.obj. Sin el .obj, el login va sin los objetos, con el logo.
void LoginScene::LoadTheObjects()
{
    auto models = m_worldModels.Load(m_context.dataRoot, m_world->map, m_context.textures, m_context.log, m_gameSlots);
    if (!models.HasValue())
    {
        m_context.log.Warning("el login va sin los objetos del mundo: {}", models.Error());
        return;
    }
    auto records = Terrain::LoadWorldObjects(m_context.dataRoot, m_world->map);
    if (!records.HasValue())
    {
        m_context.log.Warning("el login va sin los objetos del mundo: {}", records.Error());
        return;
    }
    m_objectRecords = std::move(records).Value();
    // El original los saltea sin avisar (Q-C23); MuRet lo dice una vez, con los tipos.
    std::set<std::int16_t> missing;
    for (const Data::Terrain::TerrainObject& record : m_objectRecords)
    {
        if (m_worldModels.Find(static_cast<std::uint16_t>(record.type)) == nullptr)
        {
            missing.insert(record.type);
        }
    }
    if (!missing.empty())
    {
        std::string types;
        for (const std::int16_t type : missing)
        {
            types += (types.empty() ? "" : ", ") + std::to_string(type);
        }
        m_context.log.Warning("el .obj del mundo pide tipos que no tienen modelo y no se dibujan: {}", types);
    }
}

Core::Expected<void, std::string> LoginScene::LoadTheLogo()
{
    m_logo.Clear();
    auto model = Data::Models::LoadModelFile(m_context.dataRoot, LoginLogoFile);
    if (!model.HasValue())
    {
        m_context.log.Warning("el logo no se puede leer y no se dibuja: {}", model.Error().path);
        return {};
    }
    auto skeleton = Models::MakeSkeleton(model.Value());
    if (!skeleton.HasValue())
    {
        m_context.log.Warning("el logo no tiene un esqueleto que se pueda posar y no se dibuja");
        return {};
    }
    // Las texturas apuntan a lo que piden los slots: se van antes que ellos.
    m_logoTextures.reset();
    // Sin un 0x60d530 antes: donde dejó el próximo el mundo (Q-C92), o LoginLogoFirstSlot sin él.
    if (const Models::ModelTextureSlots* world = m_worldModels.TextureSlots())
    {
        m_logoSlots.emplace(world->ContinuedAt(world->Next()));
    }
    else
    {
        m_logoSlots.emplace(LoginLogoFirstSlot);
    }
    std::vector<Render::TextureSlot> meshTextures =
        m_logoSlots->Assign(model.Value(), LoginLogoTextureFolder, LoginLogoSampling);
    // Una textura del logo que falta cierra el original (0x60d5f0 con su quinto argumento en 1).
    m_logoTextures.emplace(m_context.textures, m_logoSlots->ToLoad());
    auto uploaded = m_logoTextures->Load(m_context.dataRoot);
    if (!uploaded.HasValue())
    {
        return uploaded;
    }
    m_logo.Set(LoginLogoModel, std::move(model).Value(), std::move(skeleton).Value(), std::move(meshTextures));
    return {};
}

// 0x6349a0 con 0x606660 (en la carga del mapa, 0x61a560, el dueño del dragón y los objetos del .obj; después,
// el logo), 0x517270 y el caballo (0x443fa0(0xbf, …), invisible), y la cuenta de las rutas en −1. La fase
// vuelve a 0 (el original no la vuelve: Q-C111).
Core::Expected<void, std::string> LoginScene::PlaceTheModels()
{
    const std::uint8_t map = m_world ? m_world->map : 0;
    DetachTheDragonOwner();
    m_objects.clear();
    // 0x616ec0 en el mapa 55 (0x6188ae..0x61891b), antes del .obj: 0x606660(0xb4, (5.600; 23.000; 0),
    // (0, 0, 0), 1), el dueño del dragón. No se dibuja (+0x58 en −2), pero 0x5ffdf0 lo posa en el buffer
    // global en cada dibujo (sus huesos 8 y 9 son los del brillo del farol, Q-C140) y 0x602ed0 le saca su
    // rand().
    if (map == DragonMap)
    {
        Data::Terrain::TerrainObject owner;
        owner.type = World::DragonEffect;
        owner.position = {DragonOwnerPosition.x, DragonOwnerPosition.y, DragonOwnerPosition.z};
        owner.scale = 1.0f;
        auto created = World::CreateWorldObjects(std::span(&owner, 1), map, World::ObjectScene::Login);
        if (created.failure.has_value())
        {
            return Core::Unexpected(ObjectCreationFailureMessage(*created.failure));
        }
        m_objects = std::move(created.objects);
    }
    auto fromTheFile = World::CreateWorldObjects(m_objectRecords, map, World::ObjectScene::Login);
    if (fromTheFile.failure.has_value())
    {
        return Core::Unexpected(ObjectCreationFailureMessage(*fromTheFile.failure));
    }
    m_objects.insert(m_objects.end(), fromTheFile.objects.begin(), fromTheFile.objects.end());
    m_logoIndex = m_objects.size();
    m_objects.push_back(LogoWorldObject());
    m_objectEffectRandoms.assign(m_objects.size(), std::nullopt);
    // 0x6070a0 vacía la tabla de efectos, y en el mapa 55 el dueño del dragón (el 0xb4 que la carga del
    // mapa crea en (5.600; 23.000; 0)) lo crea con 0x518cf0 (06 § El dragón de fondo).
    CharacterWorld().Map().effects.Clear();
    m_sprites.Clear();
    CharacterWorld().Particles().Clear();
    m_leaves.Clear();
    m_drawnSinceEntering = false;
    m_steppedSinceEntering = false;
    m_particleFailureReported = false;
    m_tailSprites.Clear();
    m_tailSpriteReplay.Clear();
    m_tailSpriteReplayValid = false;
    if (m_world && m_world->map == DragonMap)
    {
        // 0x606953..0x60697a: el dueño durable y la clave -1, después de la última realocación.
        (void)CharacterWorld().Map().effects.Create(World::ModelEffectArguments{.type = World::DragonEffect,
                                                                                .position = DragonOwnerPosition,
                                                                                .angles = glm::vec3(0.0f),
                                                                                .light = glm::vec3(1.0f),
                                                                                .owner = &m_objects.front(),
                                                                                .key = 0xffff,
                                                                                .scale = World::DragonScale});
    }
    m_routes = {};
    m_horse = {};
    return PlaceTheCharacter();
}

Core::Expected<void, std::string> LoginScene::PlaceTheCharacter()
{
    Kernel::WorldState& world = CharacterWorld();
    world.Runtime().map = m_world ? m_world->map : EmberMap;
    Terrain::WorldTerrain missingTerrain;
    Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    if (terrain == nullptr)
    {
        // Los tests de UI sin WorldState pueden dibujar sus fondos sin cargar un mundo.
        if (m_context.world == nullptr)
        {
            if (const auto* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModel))
            {
                PrepareLoginPresentationActor(world.Actors()[LoginActorSlot], *skeleton);
            }
            return {};
        }
        terrain = &missingTerrain;
    }
    const Models::Skeleton* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModel);
    // Un modelo todavía sin cargar tiene cero acciones: 0x4ea030 no cambia la acción, y el paso corta
    // donde necesita Player.bmd (0x4f9580). Así los tests de carga pueden inspeccionar las texturas.
    const Models::Skeleton empty;
    auto built = ConstructLoginActor(world.Actors()[LoginActorSlot],
                                     {.terrain = *terrain,
                                      .map = world.Runtime().map,
                                      .scene = Terrain::CullingScene::Login,
                                      .fixedHeight = CameraState().fixedHeight},
                                     world.Runtime(), CharacterRandomWorld(), skeleton != nullptr ? *skeleton : empty);
    if (built.HasValue() && m_context.selection != nullptr)
    {
        m_context.selection->EnterLogin();
    }
    return built;
}

void LoginScene::Step(const Input::InputState& input)
{
    if (m_failure)
    {
        return;
    }
    // El connect del original bloquea dentro de un paso (Q-C85), y lo que tarda no entra al acumulador, que
    // mide desde después de los pasos (02): mientras conecta no corre ningún paso. Acá los pasos siguen,
    // para la red y la UI, pero ni la tela ni el mundo avanzan: el primer frame después del connect sigue
    // donde quedó el último.
    const bool connecting = m_session != nullptr && IsConnecting(m_session->State());
    m_steppedSinceEntering = true;
    StepTheScene(input, connecting);
    if (m_failure || connecting)
    {
        return;
    }
    // 0x637cab → 0x634c60; después 0x637cec mueve el viento y 0x637f91 sube el contador.
    if (m_context.cloth != nullptr)
    {
        Models::StepTheClothWorld(*m_context.cloth);
    }
}

void LoginScene::StepTheScene(const Input::InputState& input, bool connecting)
{
    if (m_session == nullptr)
    {
        StepTheWorld();
        return;
    }
    // Lo que cambió con el Poll de este frame.
    FollowSession();
    NoticeTheServerAddress();
    if (m_failure)
    {
        return;
    }
    // Con la orden de cerrar el juego, el paso no atiende nada, ni el mundo: el original no corre otro
    // después del WM_DESTROY (Q-C106), y un Esc en la cuenta volvería a conectar.
    if (m_session->State() == App::SessionState::QuitOrdered)
    {
        return;
    }
    // La UI nueva antes que el paso de la escena (0x63778c, una vez por frame en el original y por paso en
    // MuRet, Q-C490): sus clics, y el Esc o el Enter que toma una ventana, no los ve el paso (Q-C45).
    StepTheWindows(input);
    if (m_failure || m_quit)
    {
        return;
    }
    if (!connecting)
    {
        StepTheWorld();
    }
    if (m_failure)
    {
        return;
    }
    // 0x634d2c: Esc (0x8001) con el aviso, la cuenta, la de Esc, las opciones y los créditos cerrados, y
    // la barra y la lista abiertas: el sonido 0x19 (0x41ed10, de la CL10) y la de Esc.
    if (input.TakePress(Input::Key::Escape) && !m_notice.IsOpen() && !m_login.IsOpen() && !m_escape.IsOpen() &&
        !m_options.IsOpen() && !m_credits.IsOpen() && m_menuBar.IsOpen() && m_serverList.IsOpen())
    {
        CharacterWorld().Runtime().RequestSound(ButtonClickSound);
        OpenTheEscapeWindow();
    }
    // Y lo que cambió con lo que la escena le pidió a la sesión: un login que se rechaza en el
    // próximo Poll vuelve a WaitingCredentials, y la escena tiene que haber visto que salió.
    FollowSession();
}

Core::Expected<void, std::string> LoginScene::StepTheCharacters()
{
    if (m_context.world == nullptr && m_terrain.Terrain() == nullptr)
    {
        // Los fixtures de UI/modelos no tienen un TerrainWall que pueda pasar 0x508f00.
        Kernel::ActorState& actor = CharacterWorld().Actors()[LoginActorSlot];
        const auto* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModel);
        if (actor.alive && skeleton != nullptr && actor.animation.action < skeleton->actions.size())
        {
            const auto& action = skeleton->actions[actor.animation.action];
            if (Models::AdvanceAnimation(actor.animation, action, action.speed, Models::AnimationScene::Other))
            {
                Models::SetAction(actor.animation, *skeleton, LoginCharacterStandingAction);
            }
        }
        return {};
    }
    Models::ClothWorld& cloth = CharacterRandomWorld();
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    return Actors::StepCharacters(
        {.world = CharacterWorld(),
         .random = cloth.random,
         .scene = SceneId::Login,
         .playerSkeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModel),
         .terrain = m_terrain.Terrain(),
         .darkSpiritSkeleton = m_models.SkeletonOf(DarkSpiritModel),
         .particles = &CharacterWorld().Particles(),
         .worldClock = clock,
         .counter = cloth.logicSteps,
         .soundRequest = [this](std::int32_t sound) { CharacterWorld().Runtime().RequestSound(sound); },
         .soundAt = [this](std::int32_t sound, const glm::vec3& origin)
         { CharacterWorld().Runtime().RequestSound(sound, origin); },
         .trapezoid = &SharedTerrainPolygon()});
}

void LoginScene::StepTheWorld()
{
    const Models::Skeleton* player = m_models.SkeletonOf(Tables::PlayerSkeletonModel);
    if (m_context.clock != nullptr && player != nullptr)
    {
        // 0x634cca: el gesto va antes de las olas y de todo el paso del mundo.
        const std::optional<std::uint8_t> action =
            LoginCharacterActionAt(m_context.clock->Now(), CharacterRandomWorld().random);
        if (action)
        {
            Models::SetAction(CharacterWorld().Actors()[LoginActorSlot].animation, *player, *action);
        }
    }
    // 0x634cf0: con los créditos abiertos (+0x4584), nada más: ni la luz, ni los objetos, ni el caballo, ni
    // las brasas, ni los personajes, ni los efectos, ni las partículas, ni las rutas (0x5fbf60..0x633920).
    if (m_credits.IsOpen())
    {
        return;
    }
    // 0x5fbf60: primero copia la base en la ventana que dejó el último dibujo de cualquier escena.
    const Terrain::TerrainLighting* base = m_terrain.Lighting() != nullptr ? m_terrain.Lighting() : m_lighting.get();
    if (base != nullptr)
    {
        CharacterWorld().DynamicLighting().Update(*base);
    }
    if (m_context.clock != nullptr)
    {
        m_terrain.Step(m_context.clock->Now(), CharacterWorld().DynamicLighting().Range());
    }
    // 0x606560 → 0x604ab0, solo con los objetos que ya se dibujaron (+0x16c, del frustum de 0x6046a0): los
    // del mundo con los casos del mapa 55, y el logo con los suyos.
    StepWorldObjects(m_objects, m_world ? m_world->map : 0, m_worldModels,
                     [this](World::WorldObject& object)
                     {
                         if (!m_logoIndex || &object != &m_objects[*m_logoIndex])
                         {
                             return false;
                         }
                         StepTheLogo(object);
                         return true;
                     });
    // 0x445230 → 0x444010: el caballo, con la acción que el jinete tiene en este paso (la de arriba, o la
    // del gesto que todavía no terminó).
    if (const Models::Skeleton* horse = m_models.SkeletonOf(DarkHorseModel))
    {
        // El aliento lee la pose del dibujo anterior: el caballo de antes de este paso (en 0x444010, el
        // avance de 0x4ec200 corre después del aliento; la acción que mira es la de este paso).
        const DarkHorse drawn = m_horse;
        StepDarkHorse(m_horse, *horse, CharacterWorld().Actors()[LoginActorSlot].animation.action);
        BreatheTheHorse(drawn);
    }
    // 0x530490, después de los ayudantes (0x634d0e): en el mapa 55, las 80 brasas, con el rand() de la CRT.
    // Nacen alrededor del héroe (el personaje 0), al que 0x6350b0 le escribe la posición recién en el
    // dibujo: en el arranque, antes del primero, en (0, 0, 0) (Q-C137). La y de la cámara que miran: al entrar,
    // 0x6349a0 deja la cámara en (0, 0, 0) (0x634b80) y 0x635ae0 le escribe la cámara compartida en el
    // dibujo (0x635f7e).
    if (const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
        m_context.cloth != nullptr && terrain != nullptr && m_world && m_world->map == EmberMap)
    {
        const World::EmberWorld embers{.hero = CameraInputState().hero,
                                       .cameraY = m_drawnSinceEntering ? m_drawnCamera.position.y : 0.0f,
                                       .heights = terrain->heights.get(),
                                       .attributes = terrain->attributes.get(),
                                       .fixedHeight = CameraState().fixedHeight};
        World::StepLoginEmbers(m_leaves, embers, m_context.cloth->random);
    }
    // 0x634d13: el paso común incluye el cuerpo, el cuervo de cada dueño y sus efectos y cabeza.
    if (auto stepped = StepTheCharacters(); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    // 0x52d220, después de los objetos (0x606560), el caballo (0x445230) y los personajes (0x508f00): los
    // efectos con modelo, el dragón y los meteoros del 0x59.
    if (auto stepped = StepTheModelEffects(); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    // 0x53c670 (0x634d1d): los joints, después de los efectos con modelo y antes de las partículas.
    if (auto stepped = StepTheJoints(); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    // 0x5446e0, al final (0x634d22): el viento y las partículas, con el rand() de la CRT.
    if (m_context.cloth != nullptr)
    {
        if (auto stepped = StepTheParticles(); !stepped.HasValue())
        {
            m_failure = stepped.Error();
            return;
        }
    }
    AdvanceRoutes(m_routes);
}

std::function<double(float, float)> LoginScene::TerrainHeight()
{
    const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    if (terrain == nullptr || !terrain->heights || !terrain->attributes)
        return {};
    return [terrain, fixed = CameraState().fixedHeight](float x, float y)
    { return Terrain::TerrainHeightAt(*terrain->heights, *terrain->attributes, x, y, fixed); };
}

float LoginScene::WorldClock() const
{
    return m_context.clock != nullptr
               ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
               : 0.0f;
}

Core::Expected<void, std::string> LoginScene::StepTheModelEffects()
{
    Kernel::WorldState& world = CharacterWorld();
    Models::ClothWorld& cloth = CharacterRandomWorld();
    const float worldClock = WorldClock();
    const std::function<double(float, float)> terrainHeight = TerrainHeight();
    const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    const auto sound = [&world](std::int32_t id) { world.Runtime().RequestSound(id); };
    const auto publishAction = [&world](std::uint16_t type, std::uint8_t action)
    {
        auto placement = world.ModelPlacement(type);
        placement.action = action;
        world.PlaceModel(type, placement);
    };
    // Lo que 0x5208a0 lee afuera del efecto, como en el juego: los joints que suelta un efecto (0x530860) van a la
    // tabla del mundo, que el login avanza (0x53c670, StepTheJoints) y dibuja (0x53c6a0(0), DrawTheModels).
    const World::ModelEffectStepServices services{
        .terrainHeight = terrainHeight,
        .sprites = &m_sprites,
        .effects = &world.Map().effects,
        .lighting = &world.DynamicLighting().Lighting(),
        .terrainAttributes = terrain != nullptr ? terrain->attributes.get() : nullptr,
        .map = &world.Runtime().map,
        .cameraShake = &CameraInputState().shake,
        .sound = sound,
        .joint = [&world, &cloth](const World::FishEffectRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created = CreateFaunaJoint(world.DrawTail().joints, request, cloth.random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .publishAction = publishAction,
        .soundAt = [&world](std::int32_t id, const glm::vec3& origin) { world.Runtime().RequestSound(id, origin); },
        .hero = &world.Hero()};
    World::WizardEffectServices wizard =
        WizardEffectServicesOf(world, cloth.random,
                               WizardSceneParts{.particles = &world.Particles(),
                                                .worldClock = worldClock,
                                                .sprites = &m_sprites,
                                                .terrain = terrain,
                                                .fixedHeight = CameraState().fixedHeight,
                                                .cameraShake = &CameraInputState().shake,
                                                .sound = sound});
    // 0x52d222..0x52d22d: 0x44dc40(0, 0, 0) borra el desvío de la cámara antes del primer efecto.
    CameraInputState().effectTarget = glm::vec3(0.0f);
    const LoginEffectModels models(m_worldModels, m_models);
    for (World::ModelEffect& effect : world.Map().effects.Effects())
    {
        if (!effect.alive)
            continue;
        const Models::Skeleton* skeleton = models.SkeletonOf(static_cast<std::uint16_t>(effect.type));
        // 0x5208a0 con su primer rand() (0x5208c5) para todos: los de las magias (el meteoro 0xc2/9) por
        // World::WizardModelEffects y los demás (el dragón 0xb4) por World::StepGameModelEffect.
        const auto stepped = World::IsWizardModelEffect(effect.type)
                                 ? World::StepWizardModelEffect(effect, skeleton, cloth.random, wizard)
                                 : World::StepGameModelEffect(effect, skeleton, world.Particles(), worldClock,
                                                              cloth.random, world.Map().kalima.get(), services);
        if (!stepped.HasValue())
            return Core::Unexpected(stepped.Error() + ", en el login (0x634d18 → 0x52d220)");
    }
    return {};
}

Core::Expected<void, std::string> LoginScene::StepTheJoints()
{
    Kernel::WorldState& world = CharacterWorld();
    Models::ClothWorld& cloth = CharacterRandomWorld();
    // Los globales de 0x53c670, como en el juego (0x637123) y la selección: la luz dinámica (0x7b4ccc4), la altura del
    // terreno (0x5f93c0), los 400 personajes (0x743a16c), la tabla de efectos (0x7491fd8), el héroe ([0x743a170]) y
    // los modelos compartidos (0x5769ad8). Sin el choque de la flecha dueña (0x520170), el asedio ni los blancos del
    // héroe: los joints que los piden los crean skills que el login no corre; si llegaran, cortan con su dirección.
    const auto actors = world.Actors();
    const std::size_t heroSlot = world.Runtime().hero;
    const Kernel::ActorState* hero = heroSlot < actors.size() ? &actors[heroSlot] : nullptr;
    const auto stepped = StepJoints(
        world.DrawTail().joints, cloth.random, &cloth, &world.Particles(), WorldClock(), &m_sprites,
        &world.DynamicLighting().Lighting(), [&world](std::int32_t id) { world.Runtime().RequestSound(id); },
        TerrainHeight(), actors, {}, &world.Map().effects, hero, &world);
    if (!stepped.HasValue())
        return Core::Unexpected(stepped.Error() + ", en el login (0x634d1d → 0x53c670)");
    return {};
}

Core::Expected<void, std::string> LoginScene::StepTheParticles()
{
    Kernel::WorldState& world = CharacterWorld();
    const float worldClock = WorldClock();
    World::PortableParticleTrigonometry trigonometry;
    const auto stepped = World::StepParticlesChecked(
        world.Particles(), m_context.cloth->random, std::cref(worldClock), &world.Runtime().map, trigonometry,
        TerrainHeight(), ParticleOwnersAmong(m_objects), ParticleActorActionsAmong(world.Actors()),
        ParticleTextureHeightsOf(m_models), &world.DynamicLighting().Lighting(),
        ParticleActorHandsAmong(world.Actors(), world.Runtime().hero), ParticleOwnerBonesOf(world),
        [&world](std::int32_t id) { world.Runtime().RequestSound(id); }, ParticleOwnerStatesOf(world), &m_sprites);
    if (!stepped.HasValue())
        return Core::Unexpected(std::format("0x634d22 → 0x5446e0: el paso de las partículas del login falla con el "
                                            "error {}",
                                            static_cast<unsigned>(stepped.Error())));
    return {};
}

void LoginScene::BreatheTheHorse(const DarkHorse& drawn)
{
    // Con el jinete (+0x108), sin su 0x4000 (+0x78; el del login no lo tiene) y la acción del caballo
    // distinta de 1, rand() % 3 en 0: el 0x5f2 en el hueso 27 del buffer global con (50; −4; 0), con los
    // ángulos del caballo (los del jinete) y su luz (3, 3, 3, de 0x443d70).
    constexpr std::uint8_t DustAction = 1;
    constexpr int OneIn = 3;
    constexpr std::size_t BreathBone = 27;
    constexpr glm::vec3 BreathOffset{50.0f, -4.0f, 0.0f};
    constexpr glm::vec3 HorseLight{3.0f, 3.0f, 3.0f};
    if (m_context.cloth == nullptr || m_horse.animation.action == DustAction)
    {
        return;
    }
    Crt::CrtRandom& random = m_context.cloth->random;
    if (random.Next() % OneIn != 0)
    {
        return;
    }
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    (void)CharacterWorld().Particles().Create(World::BreathParticle, GlobalPosePoint(BreathBone, BreathOffset, drawn),
                                              LoginCharacterTransform().angles, HorseLight, 0, 1.0f, clock, random);
}

glm::vec3 LoginScene::GlobalPosePoint(std::size_t bone, const glm::vec3& offset, const DarkHorse& drawnHorse) const
{
    if (!m_drawnSinceEntering)
    {
        return glm::vec3(0.0f);
    }
    // Lo que el dibujo anterior posó en el buffer global, en su orden y con el estado de este paso (el
    // original no interpola): el caballo (0x4453b0), si se vio, y después los efectos con modelo
    // (0x52d620). Entre los dos van los objetos: los del .obj tienen 8 huesos o menos, y el dueño del
    // dragón (Dragon.bmd) tiene el 27 dummy, como el dragón; lo que cuelga del personaje va antes que el
    // caballo.
    constexpr float Drawn = 0.01f;
    std::vector<Actors::Drawing::PosedInTheBuffer> posed;
    if (drawnHorse.alpha >= Drawn)
    {
        posed.push_back(Actors::Drawing::PosedInTheBuffer{
            &m_models, DrawnDarkHorse(drawnHorse, m_models.SkeletonOf(DarkHorseModel), LoginCharacterTransform(),
                                      DarkHorseLoginScale, 0.0f)});
    }
    const LoginEffectModels effectModels(m_worldModels, m_models);
    for (const World::ModelEffect& effect : CharacterWorld().Map().effects.Effects())
    {
        if (!effect.alive || !effect.seen || effect.alpha < Drawn)
        {
            continue;
        }
        Actors::Drawing::DrawnObject object;
        object.model = static_cast<std::uint16_t>(effect.type);
        object.animation = effect.animation;
        object.transform = effect.transform;
        posed.push_back(Actors::Drawing::PosedInTheBuffer{&effectModels, object});
    }
    return Actors::Drawing::LastPosedBonePoint(posed, bone, offset).value_or(glm::vec3(0.0f));
}

void LoginScene::FollowSession()
{
    const App::SessionState state = m_session->State();
    if (m_seen == state)
    {
        return;
    }
    m_seen = state;
    switch (state)
    {
    case App::SessionState::ChoosingServer:
    {
        // 0x4c1860 con el F4 06.
        UI::ServerGroups groups = UI::GroupServers(m_session->ServerList());
        if (groups.ignored > 0)
        {
            m_context.log.Warning("{} servidores de la lista no se muestran: son de un grupo sin botón o pasan los "
                                  "20 de su grupo (Q-C62 y Q-C63)",
                                  groups.ignored);
        }
        m_ui.Close(m_login);
        m_serverList.Open(std::move(groups));
        m_ui.Raise(m_serverList);
        // Y la barra, después de la lista: va arriba de ella, con el foco (Q-C84).
        m_menuBar.Open();
        m_ui.Raise(m_menuBar);
        return;
    }
    case App::SessionState::WaitingCredentials:
        m_ui.Close(m_serverList);
        // Con un resultado, el login se rechazó: su aviso, y el OK abre la cuenta (0x643560). Sin
        // él, el F1 00 (0x4c1e20).
        if (const std::optional<std::uint8_t> result = m_session->LoginResult())
        {
            if (const std::optional<UI::NoticeId> notice = UI::NoticeOfLoginResult(*result))
            {
                OpenNotice(*notice);
                return;
            }
        }
        OpenLogin(UI::LoginFocus::Account);
        return;
    case App::SessionState::Failed:
        m_ui.Close(m_serverList);
        m_ui.Close(m_login);
        ShowFailure();
        return;
    case App::SessionState::Idle:
    case App::SessionState::ConnectingConnectServer:
    case App::SessionState::WaitingConnectHello:
    case App::SessionState::WaitingServerList:
    case App::SessionState::WaitingServerAddress:
    case App::SessionState::ConnectingGameServer:
    case App::SessionState::WaitingGameHello:
    case App::SessionState::WaitingLoginResult:
    case App::SessionState::LoggedIn:
    case App::SessionState::WaitingCharacterList:
    case App::SessionState::ChoosingCharacter:
    case App::SessionState::WaitingCreateResult:
    case App::SessionState::WaitingDeleteResult:
    case App::SessionState::EnteringWorld:
    case App::SessionState::InWorld:
        m_ui.Close(m_serverList);
        m_ui.Close(m_login);
        return;
    case App::SessionState::QuitOrdered:
        // El juego se cierra al final de este frame (main.cpp), sin aviso: el original no dibuja
        // otro después del WM_DESTROY (Q-C106), así que las ventanas quedan como estaban.
        return;
    }
}

void LoginScene::NoticeTheServerAddress()
{
    const std::optional<App::ServerAddress> address = m_session->GameServer();
    const bool fresh = address.has_value() && !m_sawTheAddress;
    m_sawTheAddress = address.has_value();
    if (!fresh)
    {
        return;
    }
    // sprintf(481, dirección, puerto): el texto de esta edición no tiene marcas.
    const std::string port = std::to_string(address->port);
    const std::array<std::string_view, 2> arguments{address->host, port};
    AddTheMessage(ConnectedText, arguments);
}

void LoginScene::AddTheMessage(std::size_t index, std::span<const std::string_view> arguments)
{
    if (m_context.world == nullptr || m_context.textMeasure == nullptr || m_failure)
    {
        return;
    }
    const std::string text = UI::PlaceArguments(UI::TextAt(m_texts, index).value_or(std::string_view{}), arguments);
    if (auto added =
            AddGameMessage(m_context.world->Runtime(), *m_context.textMeasure, m_screen, {}, text, UI::SystemMessage);
        !added.HasValue())
    {
        m_failure = added.Error();
    }
}

void LoginScene::OnServerClick(const UI::ServerClick& click)
{
    switch (click.kind)
    {
    case UI::ServerClick::Kind::Chosen:
        m_session->ChooseServer(click.code);
        AddTheMessage(ConnectingText);
        AddTheMessage(PleaseWaitText);
        return;
    case UI::ServerClick::Kind::Full:
        OpenNotice(UI::ServerFullNotice);
        return;
    }
}

void LoginScene::OnLoginRequest(const UI::LoginRequest& request)
{
    switch (request.kind)
    {
    case UI::LoginRequest::Kind::Submit:
        m_session->SubmitCredentials(request.credentials);
        AddTheMessage(CheckingAccountText);
        AddTheMessage(WaitAgainText);
        return;
    case UI::LoginRequest::Kind::EmptyAccount:
        OpenNotice(UI::EmptyAccountNotice);
        return;
    case UI::LoginRequest::Kind::EmptyPassword:
        OpenNotice(UI::EmptyPasswordNotice);
        return;
    case UI::LoginRequest::Kind::Cancel:
        // 0x642810: cierra el GameServer y conecta otra vez al ConnectServer.
        m_context.log.Info("se cancela la cuenta: se vuelve a pedir la lista");
        m_session->Start();
        return;
    }
}

void LoginScene::OnMenuBarClick(UI::MenuBarClick click)
{
    switch (click)
    {
    case UI::MenuBarClick::Menu:
        // 0x6411d8: la de Esc, con 0x65f690 y sin sonido.
        OpenTheEscapeWindow();
        return;
    case UI::MenuBarClick::Credit:
        OpenTheCredits();
        return;
    }
}

void LoginScene::OpenTheCredits()
{
    // 0x641203: el C1 04 F4 06 al socket (sin rand()), los créditos con 0x65f690 (0x63f5a0 con 1, que los
    // empieza con 0x63fbf0) y la música: 0x4acf60 apaga main_theme.mp3 y 0x4acfd0 toca Mutheme.mp3, de la
    // CL10 (P-C79).
    m_session->SendCreditsNotice();
    // 0x65f690: primero los abre (y con la tabla vacía, o un 0 al principio, 0x63ffe0 ya los cerró con
    // 0x65f6c0) y después 0x65f610, que los pasa arriba solo si siguen abiertos.
    FollowTheCredits(m_credits.Open());
    if (m_context.world != nullptr)
    {
        auto& runtime = m_context.world->Runtime();
        runtime.music.Stop(runtime.audio, "Music\\main_theme.mp3");
        runtime.music.Play(runtime.audio, "Music\\Mutheme.mp3");
    }
    m_ui.Raise(m_credits);
}

void LoginScene::FollowTheCredits(const UI::CreditsStep& step)
{
    // 0x63fc50: el par nuevo, encima del que había (en MuRet, en su lugar). Una que falta corta, como el
    // MessageBox y el WM_DESTROY de 0x63b870.
    if (step.illustration)
    {
        ReleaseTheIllustrations();
        m_illustrations.emplace(m_context.textures, Assets::CreditIllustrationSets.at(*step.illustration));
        if (auto loaded = m_illustrations->Load(m_context.dataRoot); !loaded.HasValue())
        {
            m_failure = loaded.Error();
        }
    }
    // 0x63f9a0, cada vez: 0x65f6c0, el C1 04 F4 06 y la música (0x4acf60 apaga Mutheme.mp3 y 0x4acfd0 toca
    // main_theme.mp3, de la CL10, P-C79). Entre la carga y el F4 06 no hay nada que se vea: el orden del
    // paso (los cierres antes que la ilustración) no cambia lo que sale.
    for (int close = 0; close < step.closes; ++close)
    {
        if (step.buttonClickSound && close == step.closes - 1)
        {
            CharacterWorld().Runtime().RequestSound(ButtonClickSound);
        }
        m_ui.Close(m_credits);
        m_session->SendCreditsNotice();
        if (m_context.world != nullptr)
        {
            auto& runtime = m_context.world->Runtime();
            runtime.music.Stop(runtime.audio, "Music\\Mutheme.mp3");
            runtime.music.Play(runtime.audio, "Music\\main_theme.mp3");
        }
    }
}

void LoginScene::ReleaseTheIllustrations()
{
    if (m_illustrations)
    {
        m_illustrations->Release();
        m_illustrations.reset();
    }
}

void LoginScene::OnNoticeClosed(UI::NoticeId notice)
{
    const UI::NoticeFollowUp followUp = UI::FollowUpOf(notice);
    switch (followUp)
    {
    case UI::NoticeFollowUp::Close:
    // Los del borrado son de la selección: el login no los abre.
    case UI::NoticeFollowUp::AskForTheCode:
    case UI::NoticeFollowUp::SendTheDeletion:
        return;
    case UI::NoticeFollowUp::ReopenAtAccount:
    case UI::NoticeFollowUp::ReopenAtPassword:
        // Solo si la sesión sigue esperando la cuenta. Si falló, el aviso de la falla ya tapó a
        // este: la guarda es por las dudas.
        if (m_session->State() == App::SessionState::WaitingCredentials)
        {
            OpenLogin(followUp == UI::NoticeFollowUp::ReopenAtPassword ? UI::LoginFocus::Password
                                                                       : UI::LoginFocus::Account);
        }
        return;
    case UI::NoticeFollowUp::Quit:
        // El PostMessage con WM_CLOSE de 0x643560 (Q-C69): lo hace Main.
        m_context.log.Info("el OK del aviso cierra el juego (el {:#04x})", static_cast<int>(notice));
        m_quit = true;
        return;
    }
}

void LoginScene::OpenLogin(UI::LoginFocus focus)
{
    m_login.SetTitle(LoginTitle());
    m_login.Open(focus);
    m_ui.Raise(m_login);
}

void LoginScene::OpenNotice(UI::NoticeId notice)
{
    m_notice.Open(notice);
    m_ui.Raise(m_notice);
}

void LoginScene::OpenTheEscapeWindow()
{
    m_escape.Open();
    m_ui.Raise(m_escape);
}

void LoginScene::OpenTheOptions()
{
    // 0x647e75: 0x65f690 con las opciones (0x644640 con 1).
    m_options.Open();
    m_ui.Raise(m_options);
}

void LoginScene::StepTheWindows(const Input::InputState& input)
{
    // El original le pasa a cada ventana los milisegundos del frame; MuRet, los del paso (Q-C490).
    constexpr double StepMs = std::chrono::duration<double, std::milli>(App::LogicStep).count();
    m_ui.Step(
        input, m_screen,
        [&](UI::NewUiWindow& window, UI::NewUiTurn turn)
        {
            if (&window == &m_notice)
            {
                if (const std::optional<UI::NoticeId> closed = m_notice.Step(input, m_screen, turn))
                {
                    OnNoticeClosed(*closed);
                }
            }
            else if (&window == &m_escape)
            {
                if (const std::optional<UI::EscapeWindowEvent> event = m_escape.Step(input, m_screen, StepMs, turn))
                {
                    OnEscapeEvent(*event);
                }
            }
            else if (&window == &m_options)
            {
                if (const std::optional<UI::OptionsEvent> event =
                        m_options.Step(input, m_screen, StepMs, OptionsSettingsOf(CharacterWorld().Runtime()), turn))
                {
                    OnOptionsEvent(*event);
                }
            }
            else if (&window == &m_menuBar)
            {
                if (const std::optional<UI::MenuBarClick> click = m_menuBar.Step(input, m_screen, turn))
                {
                    OnMenuBarClick(*click);
                }
            }
            else if (&window == &m_serverList)
            {
                if (const std::optional<UI::ServerClick> click = m_serverList.Step(input, m_screen, turn))
                {
                    OnServerClick(*click);
                }
            }
            else if (&window == &m_login)
            {
                if (const std::optional<UI::LoginRequest> request = m_login.Step(input, m_screen, turn))
                {
                    OnLoginRequest(*request);
                }
            }
            else if (&window == &m_credits)
            {
                FollowTheCredits(m_credits.Step(input, m_screen, StepMs, turn));
            }
        },
        [&] { CharacterWorld().Runtime().RequestSound(ButtonClickSound); });
}

void LoginScene::OnOptionsEvent(const UI::OptionsEvent& event)
{
    // El botón de cerrar (sin sonido) y Esc (con el 0x19, de la CL10) ya las cerraron.
    Kernel::WorldRuntime& runtime = CharacterWorld().Runtime();
    ApplyOption(runtime, event);
    if (event.kind == UI::OptionsEvent::Kind::Volume)
        runtime.SetSoundVolume(static_cast<std::uint8_t>(event.value));
}

void LoginScene::OnEscapeEvent(const UI::EscapeWindowEvent& event)
{
    switch (event.kind)
    {
    case UI::EscapeWindowEvent::Kind::Countdown:
    {
        // 0x647ef0: sprintf del 380 con lo que falta y 0x555190, de sistema y sin nombre.
        const std::string seconds = std::to_string(event.seconds);
        const std::array<std::string_view, 1> arguments{seconds};
        AddTheMessage(UI::LeavingTheGameText, arguments);
        return;
    }
    case UI::EscapeWindowEvent::Kind::Quit:
        // 0x647d87: el log del original (0x41fa40) y el PostMessage con WM_CLOSE: lo hace Main.
        m_context.log.Info("> Menu - Exit game.");
        m_quit = true;
        return;
    case UI::EscapeWindowEvent::Kind::JoinAnotherServer:
        // En el login el 1 está deshabilitado (0x6478c0): no tiene clic.
        return;
    case UI::EscapeWindowEvent::Kind::Options:
        // 0x647e6c: 0x65f6c0 la cierra y 0x65f690 abre las opciones.
        OpenTheOptions();
        return;
    case UI::EscapeWindowEvent::Kind::Closed:
        // Con Esc, el sonido 0x19 (0x41ed10, de la CL10).
        return;
    }
}

void LoginScene::ShowFailure()
{
    // 0x4c1e20 abre el 0x6f con otra versión y el 0x70 cuando no conecta o el F1 00 no acepta. Con
    // una lista vacía, sin respuesta o una cuenta que no entra, MuRet abre el 0x70 también.
    const bool otherVersion = m_session->Failure() == App::SessionFailure::VersionMismatch;
    OpenNotice(otherVersion ? UI::VersionMismatchNotice : UI::ConnectFailedNotice);
}

std::string LoginScene::LoginTitle() const
{
    const std::optional<std::uint16_t> code = m_session->ChosenServer();
    if (!code)
    {
        return {};
    }
    const std::uint16_t group = UI::GroupOf(*code);
    const int number = UI::ServerNumber(*code);
    const std::optional<std::string_view> format =
        UI::TextAt(m_texts, UI::LoginTitleText(UI::GroupSlot(group).value_or(NoSlot), number));
    if (!format)
    {
        return {};
    }
    const std::string_view name = UI::TextAt(m_texts, UI::GroupNameText(group)).value_or(std::string_view{});
    const std::string numberText = std::to_string(number);
    const std::array<std::string_view, 2> arguments{name, numberText};
    return UI::PlaceArguments(*format, arguments);
}

Assets::TextureBufferSize LoginScene::BufferSize(Render::TextureSlot slot) const
{
    if (IsIllustration(slot))
    {
        return m_illustrations ? m_illustrations->BufferSize(slot) : Assets::TextureBufferSize{};
    }
    return IsWindowTexture(slot) ? m_windowTextures.BufferSize(slot) : m_textures.BufferSize(slot);
}

void LoginScene::Render(Render::Frame::SceneCanvas& canvas, float alpha)
{
    // Las ventanas leen el mouse del próximo paso con esta pantalla.
    m_screen = canvas.screen;
    Render::SpriteBatch& sprites = canvas.sprites;
    // 1 y 2: la mezcla normal, el blanco y los dos fondos.
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(White);
    sprites.Quad(Background(0x15, 0.0f));
    sprites.Quad(Background(0x16, 320.0f));
    // 3 y 4: el fin del 2D (0x621a90) y el pase 3D. El pase que no dibuja, de 0x620db0(0, 0, w,
    // 48), no va.
    canvas.flushSprites();
    // La cámara compartida de la escena 2 (0x635ae0), en el viewport de 0x620db0(0, 25, 640, 430).
    // 0x6350d3..0x6350e1: el héroe se escribe aquí, no al entrar ni en el paso.
    Camera::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.hero = Camera::LoginHeroPosition;
    cameraInput.scene = Terrain::CullingScene::Login;
    cameraInput.map = m_world ? m_world->map : EmberMap;
    const Render::Camera3D camera = Camera::CameraForFrame(CameraState(), cameraInput, m_cameraFrames,
                                                           canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    m_drawnCamera = camera;
    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, Camera::WorldViewport, camera);
    // 0x6351b7: 0x5fbb00(w · 0,0015625, &héroe), el rango y el trapecio, que va al global (0x5fbeef → 0x5fb570).
    // Con el nivel 0: la transición del nivel efectivo (0x5fb70e..0x5fb73d) no corre acá; con el nivel pedido
    // en 0 después de 0x635ae0 y el efectivo en 0 desde la .bss, da lo mismo (08 P-C41 d). El héroe, el mapa
    // y el yaw del login son fijos: un dibujo intermedio escribe el mismo trapecio que el último frame.
    m_terrainRange = Terrain::TerrainRangeOf(pass.view, camera);
    CharacterWorld().DynamicLighting().PublishRange(m_terrainRange);
    Terrain::TerrainPolygon& polygon = SharedTerrainPolygon();
    polygon =
        Terrain::TerrainPolygonOf(Terrain::CullingScene::Login, cameraInput.map, cameraInput.hero, camera.angles.z);
    canvas.models.Begin(pass);
    // 0x6351f0: con los créditos abiertos (+0x4584), ni el terreno ni los modelos (0x5fc260..0x5306a0, con
    // 0x549510); siguen los sprites y las partículas. Lo que corre una vez por frame dibujado corre uno en el
    // primer dibujo después: los efectos de los objetos, 0x505820, la tela y el cetro, como con el connect.
    const bool credits = m_credits.IsOpen();
    if (credits)
    {
        m_objectEffectFrames = {};
        m_fullSetScanFrames = {};
        m_characterCloth.RestartTheClock();
    }
    else
    {
        // 0x5fc260(0): el terreno, antes de los personajes.
        m_terrain.DrawTerrain(canvas.models, canvas.frameClock, m_terrainRange, polygon, CameraState().fixedHeight,
                              &CharacterWorld().DynamicLighting().Lighting());
        DrawTheModels(canvas, alpha);
    }
    canvas.flushModels();
    // Los sprites del frame, los que pidió el dibujo del personaje (los 30 destellos de la tela, 0x455930,
    // y los cinco del cetro, 0x509340): 0x549510 y, con la modelview en la identidad (0x6213e0),
    // 0x549440(0), y las partículas (0x548550(0)). Sin ninguno, el pase de ojo no dibuja nada y no se abre.
    if (m_sprites.Any() || CharacterWorld().Particles().Any())
    {
        if (!credits)
        {
            m_sprites.MarkSeen();
        }
        Render::Pass3D eye = pass;
        eye.view = glm::mat4(1.0f);
        canvas.models.Begin(eye);
        DrawFrameSprites(canvas.models, m_sprites, m_models, Render::ViewRows(pass.view));
        const auto clock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
        const auto particles =
            DrawParticles(canvas.models, CharacterWorld().Particles(), m_models, Render::ViewRows(pass.view), clock);
        if (!particles.HasValue())
        {
            ReportParticleFailure(particles.Error());
        }
        canvas.flushModels();
    }
    // 5: las dos arenas, aditivas.
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(HalfWhite);
    sprites.SetBlend(Render::BlendMode::Additive);
    sprites.SetColor(SandColor);
    sprites.SkewedQuad(SandQuad(SlowSand, canvas.frameClock));
    sprites.SkewedQuad(SandQuad(FastSand, canvas.frameClock));
    // 6: los textos, con la mezcla aditiva que quedó (0x554000 no la cambia).
    sprites.SetColor(TextColor);
    DrawTexts(canvas);
    // 7: el fundido no dibuja nada (Q-C66). Los avisos (0x554810, que pone la mezcla normal y el blanco,
    // 0x554819..0x554845) y el área vieja. 8: la UI (CL5.5), cada ventana con su imagen y su color. 9: el cursor
    // (0x635560).
    DrawTheMessages(canvas);
    // 0x65fe00: de la cola a la cabeza, la última que se abrió o se apretó arriba (Q-C84).
    for (UI::NewUiWindow* window : m_ui.DrawOrder())
    {
        if (window == &m_serverList)
        {
            m_serverList.Draw(sprites, canvas.text, canvas.screen);
        }
        else if (window == &m_menuBar)
        {
            m_menuBar.Draw(sprites, canvas.screen);
        }
        else if (window == &m_login)
        {
            m_login.Draw(sprites, canvas.text, canvas.screen);
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
            m_options.Draw(sprites, canvas.text, canvas.screen, OptionsSettingsOf(CharacterWorld().Runtime()));
        }
        else if (window == &m_credits)
        {
            m_credits.Draw(sprites, canvas.text, canvas.screen);
        }
    }
    if (m_context.input != nullptr)
    {
        UI::DrawGameCursor(sprites, *m_context.input, canvas.screen);
    }
}

void LoginScene::ReportParticleFailure(const World::ParticleDrawingFailure& failure)
{
    // El pase quedó cortado en ese lugar (sin rollback). Va una vez por entrada: el mismo corte se
    // repite en cada frame.
    if (m_particleFailureReported)
    {
        return;
    }
    m_particleFailureReported = true;
    const std::error_code& backend = failure.error.backendError;
    m_context.log.Error("el dibujo de las partículas (0x548550) se corta en el lugar {}: error {}, fase {}{}{}",
                        failure.slot, static_cast<int>(failure.error.reason), static_cast<int>(failure.error.phase),
                        backend ? ", " : "", backend ? backend.message() : std::string{});
}

// 0x50ba30 con el del login, en 0x50c91e: un 0x20a con +0x7c y +0x3e4 en 0 (0x517270). Con la clase 4 no
// abre el registro. Sin las acciones de Player.bmd sale antes (0x50ba87), como DrawCharacter sin su
// esqueleto.
Actors::FullSetScan LoginScene::ScanTheCharacter(const Kernel::CharacterAppearance& appearance, int frames)
{
    if (m_context.cloth == nullptr || m_models.SkeletonOf(Tables::PlayerSkeletonModel) == nullptr)
    {
        return {};
    }
    return Actors::ScanTheDrawnCharacter(CharacterWorld(), Actors::ScannedCharacterOf(appearance),
                                         m_context.cloth->random, frames);
}

Actors::Drawing::DrawnCharacter LoginScene::TheCharacterToDraw(float alpha)
{
    const Kernel::ActorState& actor = CharacterWorld().Actors()[LoginActorSlot];
    Actors::Drawing::DrawnCharacter character;
    character.appearance = actor.alive ? Actors::AppearanceOfActor(actor) : LoginCharacter();
    character.body = actor.animation;
    if (const auto* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModel);
        skeleton != nullptr && actor.animation.action < skeleton->actions.size())
    {
        const auto& action = skeleton->actions[actor.animation.action];
        character.body = Models::Interpolated(actor.animation, action, action.speed, alpha);
    }
    character.object = actor.alive ? Models::ObjectTransform{actor.position, actor.angles, actor.scale, 0.0f}
                                   : LoginCharacterTransform();
    // La cabeza del paso: el original no la interpola.
    character.headAngles = actor.headTurn.angles;
    if (actor.alive)
    {
        // 0x50bbff..0x50bc71: las guardas de la sombra del mismo 0x50ba30 (0x635206 → 0x510d00 → 0x510ea1): el alpha
        // desde 0,5, sin +0x3e4, fuera de los mapas sin sombra y, con las mascotas que se montan, en la zona segura
        // (+0x3e2, 0x50bc52).
        character.safeZone = actor.safeZone != 0;
        character.castsShadow = Actors::Drawing::CastsShadow(actor, CharacterWorld().Runtime().map);
    }
    character.light = TerrainLightAt(character.object.position) + actor.light;
    character.cloth = &m_characterCloth;
    character.clothWorld = m_context.cloth;
    character.map = m_world ? m_world->map : 0;
    character.sprites = &m_sprites;
    character.pace = OriginalFramePaceOf(m_context.pacing);
    return character;
}

// El personaje (0x510d00 → 0x50ba30, con la luz del terreno en su lugar más la suya), el caballo
// (0x4453b0 → 0x445260, con la escala de la escena 2) y después los objetos del mundo con el logo
// (0x6046a0 → 0x602e90, con el color de 0x5ffce0).
void LoginScene::DrawTheModels(Render::Frame::SceneCanvas& canvas, float alpha)
{
    const int nativeFrames = m_objectEffectFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    FaunaDrawCarry projection;
    if (nativeFrames == 0)
        projection = CharacterWorld().PoseCarry();
    auto& carry = nativeFrames > 0 ? CharacterWorld().PoseCarry() : projection;
    Kernel::ActorState& actor = CharacterWorld().Actors()[LoginActorSlot];
    Actors::Drawing::DrawnCharacter character = TheCharacterToDraw(alpha);
    // Q-C410: 0x6349a0 arma al del login con 0x517270 → 0x511290, que le pone +0x11c en 1 (0x511bf7) y le da su
    // buffer de huesos (+0x120, 0x5123c4..0x5123ff). 0x510d00 → 0x50ba30 → 0x6001b0 posa ahí el cuerpo (0x600259),
    // que arranca de lo que dejó su dibujo anterior y no del global; al global (0x68b181c) sólo va lo que cuelga.
    character.poseBones = [&](std::uint16_t model)
    {
        return model == Tables::PlayerSkeletonModel ? std::span<const Math::BoneTransform>(actor.bodyPose)
                                                    : std::span<const Math::BoneTransform>(carry.bones);
    };
    character.onPose = [&](std::uint16_t model, std::span<const Math::BoneTransform> bones,
                           const Models::ObjectTransform& transform, bool translated)
    {
        if (model == Tables::PlayerSkeletonModel)
            return;
        if (auto result = PublishFaunaPoseRaw(carry, m_models, model, bones, transform, translated); !result.HasValue())
            m_failure = result.Error();
    };
    if (actor.alive && actor.seen)
    {
        const Actors::FullSetScan scan =
            ScanTheCharacter(character.appearance,
                             m_fullSetScanFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing)));
        // Con el flujo global y Player.bmd, el mismo 0x50ba30 que el juego: el cuerpo, las partes y la tela, y
        // después lo que cuelga y los efectos (0x50db57..0x510a7a), que no miran la escena salvo 0x510ef0.
        const Models::Skeleton* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModel);
        if (m_context.cloth != nullptr && skeleton != nullptr && !skeleton->actions.empty())
        {
            Actors::Drawing::DrawCharacter(canvas.models, m_models, character, canvas.frameClock,
                                           Actors::Drawing::CharacterDrawPass::Body);
            // 0x50bcec: la sombra le copia la escala y la posición al 0x20b (WorldState::PlaceShadowModel), sólo en un
            // frame del original.
            if (nativeFrames > 0 && Actors::Drawing::DrawsTheShadow(character))
                CharacterWorld().PlaceShadowModel(actor);
            DrawTheTail(canvas, actor, character, carry, scan, nativeFrames);
        }
        else
        {
            Actors::Drawing::DrawCharacter(canvas.models, m_models, character, canvas.frameClock);
            // Sólo un frame del original deja el cuerpo en sus huesos; un dibujo intermedio no los toca. 0x6001b0
            // copia antes la escala y la posición al modelo compartido de Player.bmd, con el alpha desde 0,01
            // (Q-C412).
            if (nativeFrames > 0)
            {
                actor.bodyPose = Actors::Drawing::PoseCharacterBody(m_models, character);
                if (actor.alpha >= Kernel::PlayerModelAlpha)
                    CharacterWorld().PlacePlayerModel(actor);
                // 0x50bcec: la sombra le copia la escala y la posición al 0x20b (WorldState::PlaceShadowModel).
                if (Actors::Drawing::DrawsTheShadow(character))
                    CharacterWorld().PlaceShadowModel(actor);
            }
        }
    }
    // 0x4182f0 al final de 0x50ba30: el cuervo, si pasa el trapecio; sus huesos del estado del paso quedan
    // para la chispa del próximo (Q-C133).
    if (actor.alive && actor.seen && actor.darkSpirit)
    {
        DrawTheRaven(canvas, alpha, &carry);
    }

    Actors::Drawing::DrawnObject horse =
        DrawnDarkHorse(m_horse, m_models.SkeletonOf(DarkHorseModel), character.object, DarkHorseLoginScale, alpha);
    if (auto result = PublishFaunaPose(carry, m_models, horse, true); !result.HasValue())
        m_failure = result.Error();
    horse.retainedMeshes = carry.meshes;
    Actors::Drawing::DrawDarkHorse(canvas.models, m_models, horse, m_world ? m_world->map : EmberMap,
                                   canvas.frameClock);

    // 0x6046a0: los objetos del mundo y el logo, sector por sector, con lo de 0x602ed0: un rand() por
    // objeto y los dos sprites del farol en los huesos 8 y 9 del buffer global de la pose. Lo que cuelga
    // del personaje lo pisó antes; el caballo, si se dibuja (0x5ffdf0, con el alpha desde 0,01), después.
    constexpr float Drawn = 0.01f;
    std::vector<Actors::Drawing::PosedInTheBuffer> posed;
    if (m_horse.alpha >= Drawn)
    {
        posed.push_back(Actors::Drawing::PosedInTheBuffer{&m_models, horse});
    }
    const LoginObjectModels objects(m_worldModels, m_logo);
    const Terrain::TerrainLighting* lighting = &CharacterWorld().DynamicLighting().Lighting();
    DrawWorldObjects(canvas.models, objects, m_objects, SharedTerrainPolygon(), lighting, m_world ? m_world->map : 0,
                     canvas.frameClock, alpha,
                     ObjectFrameEffects{.random = m_context.cloth != nullptr ? &m_context.cloth->random : nullptr,
                                        .sprites = &m_sprites,
                                        .particles = &CharacterWorld().Particles(),
                                        .posedBefore = posed,
                                        .scene = World::ObjectScene::Login,
                                        .frames = nativeFrames,
                                        .lastRandoms = m_objectEffectRandoms,
                                        .onPose =
                                            [&](const Actors::Drawing::DrawnObject& object)
                                        {
                                            auto result = PublishFaunaPose(carry, objects, object, true);
                                            if (!result.HasValue())
                                                m_failure = result.Error();
                                            return result;
                                        },
                                        .retainedPose = &carry,
                                        .publishProjectedPose = true,
                                        .objectDrawCounter = &CharacterWorld().Runtime().objectDrawCounter,
                                        .modelEffects = &CharacterWorld().Map().effects,
                                        .onFailure = [&](const std::string& error) { m_failure = error; }});
    // 0x53c6a0(0) (0x635217), después de los objetos (0x6046a0) y antes de los efectos con modelo: los joints del
    // mundo. Sólo un frame del original saca los rand() de su dibujo (los giros del 0x610/6 y /8); entre frames van
    // sobre una copia y repiten los giros guardados.
    const float jointClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
    Crt::CrtRandom projectedJointRandom = CharacterRandomWorld().random;
    const JointDrawResources jointResources{nativeFrames > 0 ? CharacterRandomWorld().random : projectedJointRandom,
                                            m_sprites, nativeFrames > 0, jointClock};
    if (auto joints = DrawJoints(canvas.models, CharacterWorld().DrawTail().joints, 0, jointResources);
        !joints.HasValue())
        m_failure = joints.Error();
    // 0x52d620(0) (0x63521e): los efectos con modelo, después de los joints, con el dibujo del juego: el dragón por el
    // camino genérico (0x52da92 → 0x602e90) y el meteoro 0xc2/9 con su +0x64 en 1 (el blendMesh, 0x51adb5;
    // Scenes/WizardEffectDrawing). La posición va `alpha` de un paso más (D-C4).
    const LoginEffectModels effectModels(m_worldModels, m_models);
    const Terrain::SphereFrustum frustum = Terrain::SphereFrustumOf(canvas.models.Pass().view, m_drawnCamera);
    const std::span<World::ModelEffect> table = CharacterWorld().Map().effects.Effects();
    const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    Core::Expected<void, std::string> effects;
    if (terrain != nullptr && terrain->heights)
    {
        ModelEffectDrawResources resources{
            *terrain->heights,
            static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count())};
        resources.onPose = [&](const Actors::Drawing::DrawnObject& object)
        { return PublishFaunaPose(carry, effectModels, object, true); };
        resources.onUntranslatedPose = [&](const Actors::Drawing::DrawnObject& object)
        { return PublishFaunaPose(carry, effectModels, object, false); };
        resources.posedMeshes = [&]() { return std::span<const Models::SkinnedMesh>(carry.meshes); };
        resources.dynamicLighting = &CharacterWorld().DynamicLighting().Lighting();
        resources.originalFrame = nativeFrames > 0;
        effects = DrawGameModelEffects(canvas.models, effectModels, table, frustum, lighting, canvas.frameClock, alpha,
                                       resources, 0);
    }
    else
        effects = DrawGameModelEffects(canvas.models, effectModels, table, frustum, lighting, canvas.frameClock, alpha);
    if (!effects.HasValue())
        m_failure = effects.Error();
    // 0x5306a0, después de 0x549510: las brasas, en el mundo.
    DrawLeaves(canvas.models, m_leaves);
    m_drawnSinceEntering = true;
}

// 0x50db57..0x510a7a con el personaje del login, después de su cuerpo (GameCharacters.h, DrawCharacterTail): una vez
// por frame del original que cuenta el reloj (con varios, sólo el último dibuja), y con cero una sobre copias. La luz
// de lo que cuelga (0x50db94) es la del terreno en su lugar (0x5f99e0, fuera de la escena 4) más +0xf4, la misma del
// cuerpo, o el rojo de los PK. Lo que sortea (el brillo del cetro de 0x509340) cambia sólo en un frame del original:
// un dibujo intermedio repite sus sprites del último. El cuervo lo dibuja DrawTheRaven.
void LoginScene::DrawTheTail(Render::Frame::SceneCanvas& canvas, Kernel::ActorState& actor,
                             const Actors::Drawing::DrawnCharacter& character, FaunaDrawCarry& carry,
                             const Actors::FullSetScan& scan, int frames)
{
    Kernel::WorldState& world = CharacterWorld();
    Models::ClothWorld& cloth = *m_context.cloth;
    constexpr std::uint8_t RedPk = 6;
    actor.drawnLight = actor.pk >= RedPk ? glm::vec3{1.0f, 0.1f, 0.1f} : character.light;
    const bool originalFrame = frames > 0;
    // Entre frames, los globales de los ítems sobre copias, como GameScene y SelectionWorld.
    std::optional<Actors::Drawing::GameItemRenderState> projectedItems;
    std::optional<World::ObjectModelStepState> projectedModels;
    std::optional<Crt::CrtRandom> projectedRandom;
    Actors::Drawing::GameItemRenderState& items =
        originalFrame ? world.Runtime().itemRender : projectedItems.emplace(world.Runtime().itemRender);
    World::ObjectModelStepState& modelState =
        originalFrame ? world.Runtime().objectModelState : projectedModels.emplace(world.Runtime().objectModelState);
    Crt::CrtRandom& itemRandom = originalFrame ? cloth.random : projectedRandom.emplace(cloth.random);
    const int runs = std::max(frames, 1);
    for (int run = 0; run < runs; ++run)
    {
        const bool last = run + 1 == runs;
        Render::ModelBatch discarded;
        if (!last)
            discarded = canvas.models;
        Render::ModelBatch& batch = last ? canvas.models : discarded;
        m_tailSprites.Clear();
        Actors::Drawing::ItemSpecialDrawContext special;
        Actors::Drawing::ItemDrawContext itemContext;
        Actors::Drawing::FillItemDrawContexts(
            special, itemContext,
            Actors::Drawing::ItemDrawResources{.frameClock = canvas.frameClock,
                                               .random = itemRandom,
                                               .itemRender = items,
                                               .sprites = m_tailSprites,
                                               .poseCarry = carry,
                                               .modelState = modelState,
                                               .joints = world.DrawTail().joints,
                                               .effects = world.Map().effects,
                                               .map = world.Runtime().map,
                                               .originalFrame = originalFrame,
                                               .glowDetail = world.Runtime().ui.glowDetail});
        const Actors::Drawing::GameCharacterPass pass{.dynamicLighting = &world.DynamicLighting().Lighting(),
                                                      .clothWorld = &cloth,
                                                      .frameClock = canvas.frameClock,
                                                      .sprites = &m_tailSprites,
                                                      .poseCarry = &carry,
                                                      .joints = world.DrawTail().joints,
                                                      .particles = &world.Particles(),
                                                      .effects = &world.Map().effects,
                                                      .itemContext = &itemContext};
        if (auto drawn = Actors::Drawing::DrawCharacterTail(batch, m_models, world, actor, LoginActorSlot, character,
                                                            pass, scan, originalFrame);
            !drawn.HasValue())
        {
            m_failure = drawn.Error() + ", en el personaje del login";
            return;
        }
    }
    if (originalFrame)
    {
        m_tailSpriteReplay = m_tailSprites;
        m_tailSpriteReplayValid = true;
    }
    if (!originalFrame && !m_tailSpriteReplayValid)
        return;
    for (World::Sprite& sprite : (originalFrame ? m_tailSprites : m_tailSpriteReplay).Sprites())
    {
        if (sprite.alive)
            static_cast<void>(m_sprites.Create(sprite.texture, sprite.position, sprite.scale, sprite.light,
                                               sprite.rotation, sprite.blend));
    }
}

void LoginScene::DrawTheRaven(Render::Frame::SceneCanvas& canvas, float alpha, FaunaDrawCarry* carry)
{
    DarkSpirit& spirit = *CharacterWorld().Actors()[LoginActorSlot].darkSpirit;
    if (!DarkSpiritSeen(spirit, SharedTerrainPolygon()))
    {
        MissTheView(spirit);
        return;
    }
    const Models::Skeleton* skeleton = m_models.SkeletonOf(DarkSpiritModel);
    const glm::vec3 light = TerrainLightAt(spirit.position);
    const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    Actors::Drawing::DrawnObject drawn = DrawnDarkSpirit(spirit, skeleton, light, alpha);
    if (carry != nullptr)
    {
        if (auto result = PublishFaunaPose(*carry, m_models, drawn, true); !result.HasValue())
        {
            m_failure = result.Error();
            return;
        }
        drawn.retainedMeshes = carry->meshes;
    }
    const float ground = terrain != nullptr ? static_cast<float>(Terrain::TerrainHeightAt(
                                                  *terrain->heights, *terrain->attributes, drawn.transform.position.x,
                                                  drawn.transform.position.y, CameraState().fixedHeight))
                                            : 0.0f;
    Actors::Drawing::DrawDarkSpirit(canvas.models, m_models, drawn, ground, canvas.frameClock,
                                    m_world ? m_world->map : 0);
    if (carry != nullptr)
        carry->models[drawn.model].position = {drawn.transform.position.x, drawn.transform.position.y, ground};
    spirit.pose = Actors::Drawing::PosedWorldBones(m_models, DrawnDarkSpirit(spirit, skeleton, light, 0.0f));
}

// 0x604ab0 con el 0xa2 en la escena 2: la luz de las rutas en el objeto y en el blendMesh, y el avance a
// +0xd8 (no a la velocidad de la acción).
void LoginScene::StepTheLogo(World::WorldObject& logo) const
{
    const float light = LoginLogoLight(m_routes);
    logo.light = glm::vec3(light);
    logo.state.light = light;
    if (const Models::Skeleton* skeleton = m_logo.SkeletonOf(LoginLogoModel);
        skeleton != nullptr && logo.animation.action < skeleton->actions.size())
    {
        Models::AdvanceAnimation(logo.animation, skeleton->actions[logo.animation.action], logo.speed,
                                 Models::AnimationScene::Other);
    }
}

glm::vec3 LoginScene::TerrainLightAt(const glm::vec3& position) const
{
    const Terrain::TerrainLighting* lighting = &CharacterWorld().DynamicLighting().Lighting();
    return lighting != nullptr ? Terrain::LightAt(*lighting, position.x, position.y) : glm::vec3(0.0f);
}

void LoginScene::DrawTheMessages(Render::Frame::SceneCanvas& canvas)
{
    if (m_context.world == nullptr)
        return;
    const int frames = m_messageFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    Kernel::HudState projected;
    Kernel::HudState& hud = frames > 0 ? m_context.world->Runtime().hud : (projected = m_context.world->Runtime().hud);
    const UI::Point mouse =
        m_context.input != nullptr ? UI::MouseIn640x480(*m_context.input, canvas.screen) : UI::Point{};
    Scenes::DrawTheMessages(*m_context.world, hud, canvas.sprites, canvas.text, mouse, false);
}

void LoginScene::DrawTexts(Render::Frame::SceneCanvas& canvas) const
{
    Render::Frame::ITextDrawer& text = canvas.text;
    if (m_left)
    {
        const Render::Text::TextExtent extent = text.Measure(LoginTextStyle.font, *m_left);
        text.Draw(Render::Text::RightAlignedAt(TextSplit, extent, canvas.screen),
                  Render::Text::BottomAlignedAt(TextBottom, extent, canvas.screen), *m_left, LoginTextStyle);
    }
    if (m_right)
    {
        const Render::Text::TextExtent extent = text.Measure(LoginTextStyle.font, *m_right);
        text.Draw(TextSplit, Render::Text::BottomAlignedAt(TextBottom, extent, canvas.screen), *m_right,
                  LoginTextStyle);
    }
    if (m_version)
    {
        const Render::Text::TextExtent extent = text.Measure(LoginTextStyle.font, *m_version);
        text.Draw(0, Render::Text::BottomAlignedAt(TextBottom, extent, canvas.screen), *m_version, LoginTextStyle);
    }
}

void LoginScene::Leave()
{
    // Las ventanas también: la cuenta vacía la clave al cerrarse (LoginWindow::Close).
    m_serverList.Close();
    m_menuBar.Close();
    m_login.Close();
    m_notice.Close();
    m_escape.Close();
    m_options.Close();
    m_credits.Close();
    m_ui.Clear();
    // 0x61c7c0 vuelve a subir las brasas de 0x61ccb0. El slot común sobrevive a la salida del login.
    const Render::TextureSlot ember{World::EmberTexture};
    const auto commonSlot = m_models.TextureSlotOf(ember.value);
    m_textures.Release(commonSlot == ember ? commonSlot : std::nullopt);
    m_windowTextures.Release();
    ReleaseTheIllustrations();
    RetainTheTerrain();
    m_terrain.Release();
    m_worldModels.Release();
    DetachTheDragonOwner();
    m_objects.clear();
    m_logoIndex.reset();
    if (m_logoTextures)
    {
        m_logoTextures->Release();
    }
}

std::optional<SceneId> LoginScene::Next() const
{
    if (m_session != nullptr && m_session->State() == App::SessionState::LoggedIn)
    {
        return SceneId::CharacterSelect;
    }
    return std::nullopt;
}

bool LoginScene::WantsToQuit() const
{
    return m_quit;
}

bool LoginScene::WantsTextInput() const
{
    return m_login.IsOpen();
}

bool LoginScene::HoldsTheLastFrame() const
{
    // 0x6350b0 no dibuja ni presenta antes del primer paso (0x7d3a13d); con la sesión, lo de abajo lo
    // incluye: el paso todavía no vio el estado.
    if (m_session == nullptr)
    {
        return !m_steppedSinceEntering;
    }
    // Conectando, y también con lo que el paso todavía no vio (desde Enter, o un Poll que cambió el
    // estado en un frame sin pasos): un connect que falla enseguida sale en ese Poll, y el frame
    // sería el login sin el aviso.
    const App::SessionState state = m_session->State();
    return IsConnecting(state) || m_seen != state;
}

void LoginScene::OnFrameHeld()
{
    // Esperar al Step para mostrar una lista o un aviso no equivale al connect bloqueante original.
    if (m_session != nullptr && (IsConnecting(m_session->State()) || (m_seen && IsConnecting(*m_seen))))
    {
        // Lo que el original corre una vez por frame dibujado corre uno en el primer dibujo después del connect
        // (Q-C85), no los frames que duró: los efectos de los objetos, 0x505820, la tela y el cetro (08, P-C27 f).
        m_objectEffectFrames = {};
        m_fullSetScanFrames = {};
        m_characterCloth.RestartTheClock();
    }
}

const Kernel::CharacterHead& LoginScene::Head() const
{
    return CharacterWorld().Actors()[LoginActorSlot].headTurn;
}

std::optional<std::string> LoginScene::Failure() const
{
    return m_failure;
}
} // namespace Client::Scenes
