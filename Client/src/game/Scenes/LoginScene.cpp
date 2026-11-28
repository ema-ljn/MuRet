#include "Scenes/LoginScene.h"
#include "Crt/CrtRandom.h"
#include "Math/HalfLifeMath.h"
#include "Models/IModelLibrary.h"
#include "Models/MeshTraits.h"
#include "Models/Skinning.h"
#include "Actors/Drawing/PosePublishing.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Assets/FaunaModels.h"

#include "Kernel/LoopTiming.h"
#include "Audio/MusicPlayer.h"
#include "Audio/Playback.h"
#include "Session/ServerAddress.h"
#include "Kernel/ModelEffectArguments.h"
#include "Session/CharacterStepHooks.h"
#include "Session/ModuleStates.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
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
#include "Models/ModelId.h"
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
#include "Pets/DarkHorse.h"
#include "Pets/DarkSpirit.h"
#include "Actors/FullSetScan.h"
#include "Actors/Drawing/GameCharacterEffects.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Tables/GameData.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointDrawing.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include "Actors/Drawing/GameItemSpecialDrawing.h"
#include "Chat/GameMessages.h"
#include "Effects/GameParticles.h"
#include "Effects/HandShine.h"
#include "Render/Frame/ITextDrawer.h"
#include "Actors/Drawing/ItemDrawResources.h"
#include "Session/ItemCreators.h"
#include "FrontEnd/LoginModels.h"
#include "FrontEnd/LoginActor.h"
#include "Hud/OptionsSettings.h"
#include "Camera/SceneCameras.h"
#include "Camera/GameCamera.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "FrontEnd/SelectionState.h"
#include "Assets/TextureSets.h"
#include "Assets/WorldModels.h"
#include "WorldObjects/WorldObjectsDrawing.h"
#include "Effects/ParticleDrawServices.h"
#include "Particles/SpriteDrawing.h"
#include "Weather/LeafDrawing.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/WizardEffects.h"
#include "Terrain/DynamicWorldLight.h"
#include "Kernel/WorldMap.h"
#include "FrontEnd/CreditsWindow.h"
#include "FrontEnd/EscapeWindow.h"
#include "Ui/GameCursor.h"
#include "Ui/Widget.h"
#include "Ui/GameText.h"
#include "FrontEnd/LoginWindow.h"
#include "Ui/MenuBar.h"
#include "Ui/MessageArea.h"
#include "Ui/NewUi.h"
#include "Ui/Notices.h"
#include "FrontEnd/OptionsWindow.h"
#include "Ui/PanelRegistry.h"
#include "FrontEnd/ServerListModel.h"
#include "FrontEnd/ServerListWindow.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainHeight.h"
#include "Weather/Leaves.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Kernel/WorldObject.h"
#include "Terrain/SphereFrustum.h"
#include "Particles/Sprites.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Terrain/TerrainLighting.h"
#include "Effects/WizardModelEffects.h"
#include "WorldObjects/WorldObjects.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"
#include "Kernel/JointRequest.h"
#include "Ui/GameUiState.h"

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
constexpr Audio::SoundId ButtonClickSound = Sound::ButtonClickSound;

std::string ObjectCreationFailureMessage(const WorldObjects::WorldObjectCreationFailure& failure)
{
    const char* reason = "error de creación desconocido";
    switch (failure.error)
    {
    case WorldObjects::WorldObjectCreationError::InvalidAngle:
        reason = "ángulo fuera del dominio admitido";
        break;
    case WorldObjects::WorldObjectCreationError::UnsupportedEffect:
        reason = "efecto de creación no disponible";
        break;
    case WorldObjects::WorldObjectCreationError::UnsupportedMapHandler:
        reason = "handler de creación no disponible";
        break;
    }
    return "Objeto del mundo, registro " + std::to_string(failure.recordIndex) + ": " + reason;
}

// What 0x620a00(1) leaves with the white, before the backgrounds.
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};
// glColor4f(1, 1, 1, 0.5) with the normal blend, before switching to the additive one: nothing is drawn with it.
constexpr Render::Color HalfWhite{1.0f, 1.0f, 1.0f, 0.5f};
// glColor3f(0.25, 0.1, 0.1), the sand's (0x3e800000 and 0x3dcccccd), and glColor3f(0.8, 0.7, 0.6), the texts'
// (0x3f4ccccd, 0x3f333333 and 0x3f19999a). glColor3f leaves alpha at 1.
constexpr Render::Color SandColor{0.25f, 0.1f, 0.1f, 1.0f};
constexpr Render::Color TextColor{0.8f, 0.7f, 0.6f, 1.0f};

// A background: 0x621b60(slot, x, 25, 320, 320, 0, 0, 1, 1, 1, 1, 0).
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

// A sand of 0x622000 at (0, 25, 640, 430), v = 0, and its speed: u is ((int) T % 100000) · speed (0x6b8a94, 0.0001,
// and 0x6b8a68, 0.0005).
struct Sand
{
    std::uint16_t slot = 0;
    float speed = 0.0f;
    float du = 1.0f;
    float dv = 1.0f;
};
constexpr Sand SlowSand{.slot = 0x5c2, .speed = 0.0001f, .du = 0.3f, .dv = 0.3f};
constexpr Sand FastSand{.slot = 0x5c3, .speed = 0.0005f, .du = 3.0f, .dv = 2.0f};
// The u's modulus (0x186a0).
constexpr std::int64_t SandPeriodMs = 100000;

// The x87 does fild of the remainder and fmul with the speed in float, and stores in float: with a remainder below 2¹⁷
// and a 24-bit speed, the product fits exactly in a double, so it makes no difference.
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

// The font DAT_05763554, the text in white (DAT_006c8e5c) and the background at 0x80000000 (DAT_006c8e64).
constexpr Render::Text::TextStyle LoginTextStyle{Render::Text::FontKind::Normal,
                                                 Render::Text::TextColors{0xffffffff, 0x80000000}};
// Where they rest: the bottom y, and the x where the 454 ends and the 455 starts (0x14f).
constexpr int TextBottom = 479;
constexpr int TextSplit = 335;

constexpr std::string_view VersionMark = "%s";
// The message area's texts: the click on a server (0x64630f and 0x646320), the account submission (0x6427d9 and
// 0x6427ea) and the reply to F4 03 (0x4e33d0).
constexpr std::size_t ConnectingText = 470;
constexpr std::size_t PleaseWaitText = 471;
constexpr std::size_t CheckingAccountText = 472;
constexpr std::size_t WaitAgainText = 473;
constexpr std::size_t ConnectedText = 481;

// The place of a group without a title button: it only comes with --auto-login, which picks the first of the list
// even if the window does not show it. Anything other than 4 gives the same (IsNonPvp).
constexpr int NoSlot = -1;

[[nodiscard]] bool IsWindowTexture(Render::TextureSlot slot)
{
    return std::ranges::any_of(Assets::WindowTextureSet, [slot](const Data::Textures::TextureSetEntry& entry)
                               { return entry.slot == slot.value; });
}

// The credits' illustration slots (0x63fc50).
[[nodiscard]] bool IsIllustration(Render::TextureSlot slot)
{
    return std::ranges::any_of(Assets::CreditIllustrationSets.front(),
                               [slot](const Data::Textures::TextureSetEntry& entry)
                               { return entry.slot == slot.value; });
}

// The connect's two states; the connect is blocking in the original (Q-C85).
[[nodiscard]] bool IsConnecting(Session::SessionState state)
{
    return state == Session::SessionState::ConnectingConnectServer ||
           state == Session::SessionState::ConnectingGameServer;
}
} // namespace

// The dragon: only on map 55, and its owner, the 0xb4 that 0x616ec0 creates (0x6188ae).
constexpr std::uint8_t DragonMap = 0x37;
// The login map (World56), the one of 0x530490's embers.
constexpr std::uint8_t EmberMap = 0x37;
constexpr glm::vec3 DragonOwnerPosition{5600.0f, 23000.0f, 0.0f};

// The login's object models: the world's and the logo (0xa2), which the original's table keeps together with the
// others.
class LoginObjectModels final : public Models::IModelLibrary
{
public:
    LoginObjectModels(const Models::IModelLibrary& world, const Models::IModelLibrary& logo)
        : m_world(world), m_logo(logo)
    {
    }

    [[nodiscard]] const Data::Models::Model* Find(Models::ModelId model) const override
    {
        return Of(model).Find(model);
    }
    [[nodiscard]] const Models::Skeleton* SkeletonOf(Models::ModelId model) const override
    {
        return Of(model).SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(Models::ModelId model) const override
    {
        return Of(model).MeshTextures(model);
    }
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(Models::ModelId model) const override
    {
        return Of(model).MeshTraitsOf(model);
    }

private:
    [[nodiscard]] const Models::IModelLibrary& Of(Models::ModelId model) const
    {
        return model.Value() == FrontEnd::LoginLogoModel ? m_logo : m_world;
    }

    const Models::IModelLibrary& m_world;
    const Models::IModelLibrary& m_logo;
};

// The models of the login's model effects: the original has a single table, with those of Object56\ (the dragon,
// 0x616ec0) and the effects' (Skill\Fire01 of the meteor 0xc2, 0x615700); MuRet looks in the world first and then in
// the loading's.
class LoginEffectModels final : public Models::IModelLibrary
{
public:
    LoginEffectModels(const Models::IModelLibrary& world, const Models::IModelLibrary& loaded)
        : m_world(world), m_loaded(loaded)
    {
    }

    [[nodiscard]] const Data::Models::Model* Find(Models::ModelId model) const override
    {
        return Of(model).Find(model);
    }
    [[nodiscard]] const Models::Skeleton* SkeletonOf(Models::ModelId model) const override
    {
        return Of(model).SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(Models::ModelId model) const override
    {
        return Of(model).MeshTextures(model);
    }
    [[nodiscard]] std::span<const Models::MeshTraits> MeshTraitsOf(Models::ModelId model) const override
    {
        return Of(model).MeshTraitsOf(model);
    }

private:
    [[nodiscard]] const Models::IModelLibrary& Of(Models::ModelId model) const
    {
        return m_world.Find(model) != nullptr ? m_world : m_loaded;
    }

    const Models::IModelLibrary& m_world;
    const Models::IModelLibrary& m_loaded;
};

// 0x606660's logo as one more in the list: without per-vertex light, as its case leaves it.
Kernel::WorldObject LogoWorldObject()
{
    const FrontEnd::ModelObject logo = FrontEnd::LoginLogo();
    Kernel::WorldObject object;
    object.type = static_cast<std::int16_t>(logo.model);
    object.transform = logo.transform;
    object.state = logo.state;
    object.light = logo.light;
    object.speed = logo.speed;
    object.vertexLight = false;
    object.sectorX = WorldObjects::SectorOf(object.transform.position.x);
    object.sectorY = WorldObjects::SectorOf(object.transform.position.y);
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
      m_localStates(context.states == nullptr ? std::make_unique<Session::ModuleStates>() : nullptr),
      m_localCloth{.random = Crt::CrtRandom(static_cast<std::uint32_t>(seed))}, m_models(models), m_session(session),
      m_texts(data.text ? &*data.text : nullptr), m_textures(context.textures, Assets::LoginTextureSet),
      m_windowTextures(context.textures, Assets::WindowTextureSet), m_world(WorldOf(Kernel::SceneId::Login)),
      m_terrain(context.textures), m_gameSlots(gameSlots),
      m_serverList([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_menuBar([this](Render::TextureSlot slot) { return BufferSize(slot); }),
      m_login([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_notice([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_escape([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_options([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_credits([this](Render::TextureSlot slot) { return BufferSize(slot); }, data.credits),
      m_characterDrawHooks(&States().monsters)
{
    if (m_localWorld != nullptr)
        m_localWorld->LendJointCreation(&Joints::CreateJoint);
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
    // The shared world can keep effects after the scene, even in inactive places.
    // The life and visibility of the last draw do not depend on this link.
    const void* owner = &m_objects.front();
    for (Effects::ModelEffect& effect : CharacterWorld().Map().effects.Effects())
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
    return States().camera.trapezoid;
}

Kernel::WorldState& LoginScene::CharacterWorld() const
{
    return m_context.world != nullptr ? *m_context.world : *m_localWorld;
}

Terrain::DynamicWorldLight& LoginScene::DynamicLight() const
{
    return States().terrain.dynamicLight;
}

Session::ModuleStates& LoginScene::States() const
{
    return m_context.states != nullptr ? *m_context.states : *m_localStates;
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
    // 0x634c19 writes only the yaw; 0x634b60 does not clear the hero if its pointer is valid.
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
    m_escape.Arm(Ui::NewUiMode::Login);
    // 0x644120 with the globals (0x6446e0) and 0x63f020, which reads credit.bmd (in MuRet, GameData).
    m_options.Arm(Hud::OptionsSettingsOf(CharacterWorld().Runtime()));
    m_credits.Arm();
    ReleaseTheIllustrations();
    // 0x65f300: each one at the head, in this order: the notice, the Esc one, the options, the bar, the list, the
    // account and the credits.
    const std::array<Ui::NewUiWindow*, 7> uiWindows{&m_notice,     &m_escape, &m_options, &m_menuBar,
                                                    &m_serverList, &m_login,  &m_credits};
    m_ui.Build(Ui::NewUiMode::Login, uiWindows);
    m_seen.reset();
    m_sawTheAddress = false;
    m_quit = false;
    if (m_session != nullptr)
    {
        m_session->Start();
    }
    m_messageFrames = {};
    // 0x634b45..0x634b5e: 120 empty messages.
    if (m_context.world != nullptr)
    {
        if (auto emptied = Chat::AddTheEmptyMessages(m_context.world->Runtime(), m_context.textMeasure);
            !emptied.HasValue())
        {
            RetainTheTerrain();
            return emptied;
        }
        // 0x634bf0..0x634c2e: the commands window ([0x6e592c]) closed and without a command.
        Ui::ResetTheCommands(m_context.world->Runtime().panels.delegated);
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
        // The previous loan stops being valid before the tables are destroyed or replaced.
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
    // 0x6349bc → 0x61a58c: loading the login map frees the previous world's reservations.
    Kernel::WorldState& world = CharacterWorld();
    if (world.Runtime().audio != nullptr)
    {
        world.Runtime().audio->ReleaseWorldSounds();
        States().assets.fauna.ResetSoundRegistrationStamps();
    }
    // 0x5fbb00's initial range with the entry pass (the blocks that start at x 0..128 and y 120..248, 06 § El terreno
    // del login). Not the trapezoid: it is the global one, which before the login's first draw has the .bss zeros or
    // the one the previous scene left (Q-C217).
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
        // Without the terrain, the models' light can still come from its two files.
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

// 0x616ec0 and 0x607370: the models of Object56\ with the loading's table, and the records of EncTerrain56.obj.
// Without the .obj, the login goes without the objects, with the logo.
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
    // The original skips them silently (Q-C23); MuRet says so once, with the types.
    std::set<std::int16_t> missing;
    for (const Data::Terrain::TerrainObject& record : m_objectRecords)
    {
        if (m_worldModels.Find(Models::ModelId{static_cast<std::uint16_t>(record.type)}) == nullptr)
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
    auto model = Data::Models::LoadModelFile(m_context.dataRoot, FrontEnd::LoginLogoFile);
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
    // The textures point at what the slots ask for: they go before them.
    m_logoTextures.reset();
    // Without a 0x60d530 before: where the world left the next one (Q-C92), or LoginLogoFirstSlot without it.
    if (const Models::ModelTextureSlots* world = m_worldModels.TextureSlots())
    {
        m_logoSlots.emplace(world->ContinuedAt(world->Next()));
    }
    else
    {
        m_logoSlots.emplace(FrontEnd::LoginLogoFirstSlot);
    }
    std::vector<Render::TextureSlot> meshTextures =
        m_logoSlots->Assign(model.Value(), FrontEnd::LoginLogoTextureFolder, FrontEnd::LoginLogoSampling);
    // A missing logo texture closes the original (0x60d5f0 with its fifth argument at 1).
    m_logoTextures.emplace(m_context.textures, m_logoSlots->ToLoad());
    auto uploaded = m_logoTextures->Load(m_context.dataRoot);
    if (!uploaded.HasValue())
    {
        return uploaded;
    }
    m_logo.Set(FrontEnd::LoginLogoModel, std::move(model).Value(), std::move(skeleton).Value(),
               std::move(meshTextures));
    return {};
}

// 0x6349a0 with 0x606660 (in the map load, 0x61a560, the dragon's owner and the .obj objects; then the logo),
// 0x517270 and the horse (0x443fa0(0xbf, …), invisible), and the route count at −1. The phase goes back to 0 (the
// original does not reset it: Q-C111).
Core::Expected<void, std::string> LoginScene::PlaceTheModels()
{
    const std::uint8_t map = m_world ? m_world->map : 0;
    DetachTheDragonOwner();
    m_objects.clear();
    // 0x616ec0 on map 55 (0x6188ae..0x61891b), before the .obj: 0x606660(0xb4, (5600, 23000, 0), (0, 0, 0), 1), the
    // dragon's owner. Not drawn (+0x58 at −2), but 0x5ffdf0 poses it in the global buffer on every draw (its bones 8
    // and 9 are the lantern glow's, Q-C140) and 0x602ed0 takes its rand().
    if (map == DragonMap)
    {
        Data::Terrain::TerrainObject owner;
        owner.type = Effects::DragonEffect;
        owner.position = {DragonOwnerPosition.x, DragonOwnerPosition.y, DragonOwnerPosition.z};
        owner.scale = 1.0f;
        auto created = WorldObjects::CreateWorldObjects(std::span(&owner, 1), map, WorldObjects::ObjectScene::Login);
        if (created.failure.has_value())
        {
            return Core::Unexpected(ObjectCreationFailureMessage(*created.failure));
        }
        m_objects = std::move(created.objects);
    }
    auto fromTheFile = WorldObjects::CreateWorldObjects(m_objectRecords, map, WorldObjects::ObjectScene::Login);
    if (fromTheFile.failure.has_value())
    {
        return Core::Unexpected(ObjectCreationFailureMessage(*fromTheFile.failure));
    }
    m_objects.insert(m_objects.end(), fromTheFile.objects.begin(), fromTheFile.objects.end());
    m_logoIndex = m_objects.size();
    m_objects.push_back(LogoWorldObject());
    m_objectEffectRandoms.assign(m_objects.size(), std::nullopt);
    // 0x6070a0 empties the effect table, and on map 55 the dragon's owner (the 0xb4 the map load creates at (5600,
    // 23000, 0)) creates it with 0x518cf0 (06 § El dragón de fondo).
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
        // 0x606953..0x60697a: the durable owner and key -1, after the last reallocation.
        (void)CharacterWorld().Map().effects.Create(Kernel::ModelEffectArguments{.type = Effects::DragonEffect,
                                                                                 .position = DragonOwnerPosition,
                                                                                 .angles = glm::vec3(0.0f),
                                                                                 .light = glm::vec3(1.0f),
                                                                                 .owner = &m_objects.front(),
                                                                                 .key = 0xffff,
                                                                                 .scale = Effects::DragonScale});
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
        // UI tests without a WorldState can draw their backgrounds without loading a world.
        if (m_context.world == nullptr)
        {
            if (const auto* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId))
            {
                FrontEnd::PrepareLoginPresentationActor(world.Actors()[FrontEnd::LoginActorSlot], *skeleton);
            }
            return {};
        }
        terrain = &missingTerrain;
    }
    const Models::Skeleton* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId);
    // A model not yet loaded has zero actions: 0x4ea030 does not change the action, and the step stops where it needs
    // Player.bmd (0x4f9580). So the loading tests can inspect the textures.
    const Models::Skeleton empty;
    auto built =
        FrontEnd::ConstructLoginActor(world.Actors()[FrontEnd::LoginActorSlot],
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
    // The original's connect blocks inside a step (Q-C85), and its time does not enter the accumulator, which measures
    // from after the steps (02): no step runs while it connects. Here the steps go on, for the network and the UI, but
    // neither the cloth nor the world advances: the first frame after the connect goes on where the last one stopped.
    const bool connecting = m_session != nullptr && IsConnecting(m_session->State());
    m_steppedSinceEntering = true;
    StepTheScene(input, connecting);
    if (m_failure || connecting)
    {
        return;
    }
    // 0x637cab → 0x634c60; then 0x637cec moves the wind and 0x637f91 raises the counter.
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
    // What changed with this frame's Poll.
    FollowSession();
    NoticeTheServerAddress();
    if (m_failure)
    {
        return;
    }
    // With the order to close the game, the step handles nothing, not even the world: the original runs no other step
    // after WM_DESTROY (Q-C106), and an Esc in the account would connect again.
    if (m_session->State() == Session::SessionState::QuitOrdered)
    {
        return;
    }
    // The new UI before the scene's step (0x63778c, once per frame in the original and per step in MuRet, Q-C490): the
    // step does not see its clicks, nor the Esc or Enter a window takes (Q-C45).
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
    // 0x634d2c: Esc (0x8001) with the notice, the account, the Esc one, the options and the credits closed, and the bar
    // and the list open: sound 0x19 (0x41ed10) and the Esc one.
    if (input.TakePress(Input::Key::Escape) && !m_notice.IsOpen() && !m_login.IsOpen() && !m_escape.IsOpen() &&
        !m_options.IsOpen() && !m_credits.IsOpen() && m_menuBar.IsOpen() && m_serverList.IsOpen())
    {
        CharacterWorld().Runtime().RequestSound(ButtonClickSound);
        OpenTheEscapeWindow();
    }
    // And what changed with what the scene asked of the session: a login rejected in the next Poll goes back to
    // WaitingCredentials, and the scene must have seen that it left.
    FollowSession();
}

Core::Expected<void, std::string> LoginScene::StepTheCharacters()
{
    if (m_context.world == nullptr && m_terrain.Terrain() == nullptr)
    {
        // The UI/model fixtures have no TerrainWall that 0x508f00 can pass.
        Kernel::ActorState& actor = CharacterWorld().Actors()[FrontEnd::LoginActorSlot];
        const auto* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId);
        if (actor.alive && skeleton != nullptr && actor.animation.action < skeleton->actions.size())
        {
            const auto& action = skeleton->actions[actor.animation.action];
            if (Models::AdvanceAnimation(actor.animation, action, action.speed, Models::AnimationScene::Other))
            {
                Models::SetAction(actor.animation, *skeleton, FrontEnd::LoginCharacterStandingAction);
            }
        }
        return {};
    }
    Models::ClothWorld& cloth = CharacterRandomWorld();
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    const Session::CharacterStepHooks hooks{&States().monsters};
    return Actors::StepCharacters(
        {.world = CharacterWorld(),
         .random = cloth.random,
         .scene = Kernel::SceneId::Login,
         .playerSkeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId),
         .terrain = m_terrain.Terrain(),
         .darkSpiritSkeleton = m_models.SkeletonOf(Models::ModelId{Pets::DarkSpiritModel}),
         .particles = &CharacterWorld().Particles(),
         .worldClock = clock,
         .counter = cloth.logicSteps,
         .soundRequest = [this](Audio::SoundId sound) { CharacterWorld().Runtime().RequestSound(sound); },
         .soundAt = [this](Audio::SoundId sound, const glm::vec3& origin)
         { CharacterWorld().Runtime().RequestSound(sound, origin); },
         .trapezoid = &SharedTerrainPolygon(),
         .dynamicLighting = &DynamicLight().Lighting(),
         .faunaModels = &States().assets.fauna,
         .hooks = &hooks});
}

void LoginScene::StepTheWorld()
{
    const Models::Skeleton* player = m_models.SkeletonOf(Tables::PlayerSkeletonModelId);
    if (m_context.clock != nullptr && player != nullptr)
    {
        // 0x634cca: the gesture goes before the waves and the whole world step.
        const std::optional<std::uint8_t> action =
            FrontEnd::LoginCharacterActionAt(m_context.clock->Now(), CharacterRandomWorld().random);
        if (action)
        {
            Models::SetAction(CharacterWorld().Actors()[FrontEnd::LoginActorSlot].animation, *player, *action);
        }
    }
    // 0x634cf0: with the credits open (+0x4584), nothing else: not the light, the objects, the horse, the embers, the
    // characters, the effects, the particles or the routes (0x5fbf60..0x633920).
    if (m_credits.IsOpen())
    {
        return;
    }
    // 0x5fbf60: first copies the base into the window the last draw of any scene left.
    const Terrain::TerrainLighting* base = m_terrain.Lighting() != nullptr ? m_terrain.Lighting() : m_lighting.get();
    if (base != nullptr)
    {
        DynamicLight().Update(*base);
    }
    if (m_context.clock != nullptr)
    {
        m_terrain.Step(m_context.clock->Now(), DynamicLight().Range());
    }
    // 0x606560 → 0x604ab0, only with the objects already drawn (+0x16c, from 0x6046a0's frustum): the world's with map
    // 55's cases, and the logo with its own.
    WorldObjects::StepWorldObjects(m_objects, m_world ? m_world->map : 0, m_worldModels,
                                   [this](Kernel::WorldObject& object)
                                   {
                                       if (!m_logoIndex || &object != &m_objects[*m_logoIndex])
                                       {
                                           return false;
                                       }
                                       StepTheLogo(object);
                                       return true;
                                   });
    // 0x445230 → 0x444010: the horse, with the action the rider has in this step (the one above, or that of the gesture
    // that has not finished yet).
    if (const Models::Skeleton* horse = m_models.SkeletonOf(Models::ModelId{Pets::DarkHorseModel}))
    {
        // The breath reads the previous draw's pose: the horse from before this step (in 0x444010, 0x4ec200's advance
        // runs after the breath; the action it looks at is this step's).
        const Kernel::DarkHorse drawn = m_horse;
        Pets::StepDarkHorse(m_horse, *horse, CharacterWorld().Actors()[FrontEnd::LoginActorSlot].animation.action);
        BreatheTheHorse(drawn);
    }
    // 0x530490, after the helpers (0x634d0e): on map 55, the 80 embers, with the CRT's rand(). They spawn around the
    // hero (character 0), whose position 0x6350b0 writes only in the draw: at startup, before the first, at (0, 0, 0)
    // (Q-C137). The camera y they look at: on entering, 0x6349a0 leaves the camera at (0, 0, 0) (0x634b80) and 0x635ae0
    // writes the shared camera into it in the draw (0x635f7e).
    if (const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
        m_context.cloth != nullptr && terrain != nullptr && m_world && m_world->map == EmberMap)
    {
        const Weather::EmberWorld embers{.hero = CameraInputState().hero,
                                         .cameraY = m_drawnSinceEntering ? m_drawnCamera.position.y : 0.0f,
                                         .heights = terrain->heights.get(),
                                         .attributes = terrain->attributes.get(),
                                         .fixedHeight = CameraState().fixedHeight};
        Weather::StepLoginEmbers(m_leaves, embers, m_context.cloth->random);
    }
    // 0x634d13: the common step includes the body, each owner's raven and its effects and head.
    if (auto stepped = StepTheCharacters(); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    // 0x52d220, after the objects (0x606560), the horse (0x445230) and the characters (0x508f00): the model effects,
    // the dragon and the 0x59's meteors.
    if (auto stepped = StepTheModelEffects(); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    // 0x53c670 (0x634d1d): the joints, after the model effects and before the particles.
    if (auto stepped = StepTheJoints(); !stepped.HasValue())
    {
        m_failure = stepped.Error();
        return;
    }
    // 0x5446e0, at the end (0x634d22): the wind and the particles, with the CRT's rand().
    if (m_context.cloth != nullptr)
    {
        if (auto stepped = StepTheParticles(); !stepped.HasValue())
        {
            m_failure = stepped.Error();
            return;
        }
    }
    FrontEnd::AdvanceRoutes(m_routes);
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
    const auto sound = [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); };
    const auto publishAction = [&world](std::uint16_t type, std::uint8_t action)
    {
        auto placement = world.ModelPlacement(type);
        placement.action = action;
        world.PlaceModel(type, placement);
    };
    // What 0x5208a0 reads outside the effect, as in the game: the joints an effect drops (0x530860) go to the world's
    // table, which the login steps (0x53c670, StepTheJoints) and draws (0x53c6a0(0), DrawTheModels).
    const Effects::ModelEffectStepServices services{
        .terrainHeight = terrainHeight,
        .sprites = &m_sprites,
        .effects = &world.Map().effects,
        .lighting = &DynamicLight().Lighting(),
        .terrainAttributes = terrain != nullptr ? terrain->attributes.get() : nullptr,
        .map = &world.Runtime().map,
        .cameraShake = &CameraInputState().shake,
        .sound = sound,
        .joint = [&world, &cloth](const Kernel::JointRequest& request) -> Core::Expected<void, std::string>
        {
            const auto created = Joints::CreateFaunaJoint(world.DrawTail().joints, request, cloth.random);
            if (!created.HasValue())
                return Core::Unexpected(created.Error());
            return {};
        },
        .publishAction = publishAction,
        .soundAt = [&world](Audio::SoundId id, const glm::vec3& origin) { world.Runtime().RequestSound(id, origin); },
        .hero = &world.Hero()};
    Effects::WizardEffectServices wizard =
        Effects::WizardEffectServicesOf(world, cloth.random,
                                        Effects::WizardSceneParts{.particles = &world.Particles(),
                                                                  .worldClock = worldClock,
                                                                  .sprites = &m_sprites,
                                                                  .terrain = terrain,
                                                                  .fixedHeight = CameraState().fixedHeight,
                                                                  .cameraShake = &CameraInputState().shake,
                                                                  .sound = sound,
                                                                  .dynamicLighting = &DynamicLight().Lighting(),
                                                                  .faunaModels = &States().assets.fauna});
    // 0x52d222..0x52d22d: 0x44dc40(0, 0, 0) clears the camera offset before the first effect.
    CameraInputState().effectTarget = glm::vec3(0.0f);
    const LoginEffectModels models(m_worldModels, m_models);
    for (Effects::ModelEffect& effect : world.Map().effects.Effects())
    {
        if (!effect.alive)
            continue;
        const Models::Skeleton* skeleton = models.SkeletonOf(Models::ModelId{static_cast<std::uint16_t>(effect.type)});
        // 0x5208a0 with its first rand() (0x5208c5) for all: the spells' (the meteor 0xc2/9) through
        // Effects/WizardModelEffects and the others (the dragon 0xb4) through Effects::StepGameModelEffect.
        const auto stepped = Effects::IsWizardModelEffect(effect.type)
                                 ? Effects::StepWizardModelEffect(effect, skeleton, cloth.random, wizard)
                                 : Effects::StepGameModelEffect(effect, skeleton, world.Particles(), worldClock,
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
    // 0x53c670's globals, as in the game (0x637123) and the selection: the dynamic light (0x7b4ccc4), the terrain
    // height (0x5f93c0), the 400 characters (0x743a16c), the effect table (0x7491fd8), the hero ([0x743a170]) and the
    // shared models (0x5769ad8). Without the owner arrow's hit (0x520170), the siege or the hero's targets: the joints
    // that ask for them come from skills the login does not run; if they arrived, they stop with their address.
    const auto actors = world.Actors();
    const std::size_t heroSlot = world.Runtime().hero;
    const Kernel::ActorState* hero = heroSlot < actors.size() ? &actors[heroSlot] : nullptr;
    const auto stepped = Joints::StepJoints(
        world.DrawTail().joints, cloth.random, &cloth, &world.Particles(), WorldClock(), &m_sprites,
        &DynamicLight().Lighting(), [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); }, TerrainHeight(),
        actors, {}, &world.Map().effects, hero, &world);
    if (!stepped.HasValue())
        return Core::Unexpected(stepped.Error() + ", en el login (0x634d1d → 0x53c670)");
    return {};
}

Core::Expected<void, std::string> LoginScene::StepTheParticles()
{
    Kernel::WorldState& world = CharacterWorld();
    const float worldClock = WorldClock();
    Particles::PortableParticleTrigonometry trigonometry;
    const auto stepped = Particles::StepParticlesChecked(
        world.Particles(), m_context.cloth->random, std::cref(worldClock), &world.Runtime().map, trigonometry,
        TerrainHeight(), Effects::ParticleOwnersAmong(m_objects), Effects::ParticleActorActionsAmong(world.Actors()),
        Effects::ParticleTextureHeightsOf(m_models), &DynamicLight().Lighting(),
        Effects::ParticleActorHandsAmong(world.Actors(), world.Runtime().hero), Effects::ParticleOwnerBonesOf(world),
        [&world](Audio::SoundId id) { world.Runtime().RequestSound(id); }, Effects::ParticleOwnerStatesOf(world),
        &m_sprites);
    if (!stepped.HasValue())
        return Core::Unexpected(std::format("0x634d22 → 0x5446e0: el paso de las partículas del login falla con el "
                                            "error {}",
                                            static_cast<unsigned>(stepped.Error())));
    return {};
}

void LoginScene::BreatheTheHorse(const Kernel::DarkHorse& drawn)
{
    // With the rider (+0x108), without its 0x4000 (+0x78; the login's does not have it) and the horse's action other
    // than 1, rand() % 3 at 0: the 0x5f2 at bone 27 of the global buffer with (50, −4, 0), with the horse's angles (the
    // rider's) and its light (3, 3, 3, from 0x443d70).
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
    (void)CharacterWorld().Particles().Create(
        Particles::BreathParticle, GlobalPosePoint(BreathBone, BreathOffset, drawn),
        FrontEnd::LoginCharacterTransform().angles, HorseLight, 0, 1.0f, clock, random);
}

glm::vec3 LoginScene::GlobalPosePoint(std::size_t bone, const glm::vec3& offset,
                                      const Kernel::DarkHorse& drawnHorse) const
{
    if (!m_drawnSinceEntering)
    {
        return glm::vec3(0.0f);
    }
    // What the previous draw posed in the global buffer, in its order and with this step's state (the original does not
    // interpolate): the horse (0x4453b0), if seen, and then the model effects (0x52d620). The objects go between the
    // two: the .obj ones have 8 bones or fewer, and the dragon's owner (Dragon.bmd) has the dummy 27, like the dragon;
    // what hangs from the character goes before the horse.
    constexpr float Drawn = 0.01f;
    std::vector<Actors::Drawing::PosedInTheBuffer> posed;
    if (drawnHorse.alpha >= Drawn)
    {
        posed.push_back(Actors::Drawing::PosedInTheBuffer{
            &m_models, Pets::DrawnDarkHorse(drawnHorse, m_models.SkeletonOf(Models::ModelId{Pets::DarkHorseModel}),
                                            FrontEnd::LoginCharacterTransform(), Pets::DarkHorseLoginScale, 0.0f)});
    }
    const LoginEffectModels effectModels(m_worldModels, m_models);
    for (const Effects::ModelEffect& effect : CharacterWorld().Map().effects.Effects())
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
    const Session::SessionState state = m_session->State();
    if (m_seen == state)
    {
        return;
    }
    m_seen = state;
    switch (state)
    {
    case Session::SessionState::ChoosingServer:
    {
        // 0x4c1860 with the F4 06.
        FrontEnd::ServerGroups groups = FrontEnd::GroupServers(m_session->ServerList());
        if (groups.ignored > 0)
        {
            m_context.log.Warning("{} servidores de la lista no se muestran: son de un grupo sin botón o pasan los "
                                  "20 de su grupo (Q-C62 y Q-C63)",
                                  groups.ignored);
        }
        m_ui.Close(m_login);
        m_serverList.Open(std::move(groups));
        m_ui.Raise(m_serverList);
        // And the bar, after the list: it goes above it, with the focus (Q-C84).
        m_menuBar.Open();
        m_ui.Raise(m_menuBar);
        return;
    }
    case Session::SessionState::WaitingCredentials:
        m_ui.Close(m_serverList);
        // With a result, the login was rejected: its notice, and the OK opens the account (0x643560). Without one, the
        // F1 00 (0x4c1e20).
        if (const std::optional<std::uint8_t> result = m_session->LoginResult())
        {
            if (const std::optional<Ui::NoticeId> notice = Ui::NoticeOfLoginResult(*result))
            {
                OpenNotice(*notice);
                return;
            }
        }
        OpenLogin(FrontEnd::LoginFocus::Account);
        return;
    case Session::SessionState::Failed:
        m_ui.Close(m_serverList);
        m_ui.Close(m_login);
        ShowFailure();
        return;
    case Session::SessionState::Idle:
    case Session::SessionState::ConnectingConnectServer:
    case Session::SessionState::WaitingConnectHello:
    case Session::SessionState::WaitingServerList:
    case Session::SessionState::WaitingServerAddress:
    case Session::SessionState::ConnectingGameServer:
    case Session::SessionState::WaitingGameHello:
    case Session::SessionState::WaitingLoginResult:
    case Session::SessionState::LoggedIn:
    case Session::SessionState::WaitingCharacterList:
    case Session::SessionState::ChoosingCharacter:
    case Session::SessionState::WaitingCreateResult:
    case Session::SessionState::WaitingDeleteResult:
    case Session::SessionState::EnteringWorld:
    case Session::SessionState::InWorld:
        m_ui.Close(m_serverList);
        m_ui.Close(m_login);
        return;
    case Session::SessionState::QuitOrdered:
        // The game closes at the end of this frame (main.cpp), without a notice: the original draws no other frame
        // after WM_DESTROY (Q-C106), so the windows stay as they were.
        return;
    }
}

void LoginScene::NoticeTheServerAddress()
{
    const std::optional<Session::ServerAddress> address = m_session->GameServer();
    const bool fresh = address.has_value() && !m_sawTheAddress;
    m_sawTheAddress = address.has_value();
    if (!fresh)
    {
        return;
    }
    // sprintf(481, address, port): this edition's text has no marks.
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
    const std::string text = Ui::PlaceArguments(Ui::TextAt(m_texts, index).value_or(std::string_view{}), arguments);
    if (auto added = Chat::AddGameMessage(m_context.world->Runtime(), *m_context.textMeasure, m_screen, {}, text,
                                          Ui::SystemMessage);
        !added.HasValue())
    {
        m_failure = added.Error();
    }
}

void LoginScene::OnServerClick(const FrontEnd::ServerClick& click)
{
    switch (click.kind)
    {
    case FrontEnd::ServerClick::Kind::Chosen:
        m_session->ChooseServer(click.code);
        AddTheMessage(ConnectingText);
        AddTheMessage(PleaseWaitText);
        return;
    case FrontEnd::ServerClick::Kind::Full:
        OpenNotice(Ui::ServerFullNotice);
        return;
    }
}

void LoginScene::OnLoginRequest(const FrontEnd::LoginRequest& request)
{
    switch (request.kind)
    {
    case FrontEnd::LoginRequest::Kind::Submit:
        m_session->SubmitCredentials(request.credentials);
        AddTheMessage(CheckingAccountText);
        AddTheMessage(WaitAgainText);
        return;
    case FrontEnd::LoginRequest::Kind::EmptyAccount:
        OpenNotice(Ui::EmptyAccountNotice);
        return;
    case FrontEnd::LoginRequest::Kind::EmptyPassword:
        OpenNotice(Ui::EmptyPasswordNotice);
        return;
    case FrontEnd::LoginRequest::Kind::Cancel:
        // 0x642810: closes the GameServer and connects to the ConnectServer again.
        m_context.log.Info("se cancela la cuenta: se vuelve a pedir la lista");
        m_session->Start();
        return;
    }
}

void LoginScene::OnMenuBarClick(Ui::MenuBarClick click)
{
    switch (click)
    {
    case Ui::MenuBarClick::Menu:
        // 0x6411d8: the Esc one, with 0x65f690 and without sound.
        OpenTheEscapeWindow();
        return;
    case Ui::MenuBarClick::Credit:
        OpenTheCredits();
        return;
    }
}

void LoginScene::OpenTheCredits()
{
    // 0x641203: the C1 04 F4 06 to the socket (without rand()), the credits with 0x65f690 (0x63f5a0 with 1, which
    // starts them with 0x63fbf0) and the music: 0x4acf60 stops main_theme.mp3 and 0x4acfd0 plays Mutheme.mp3 (P-C79).
    m_session->SendCreditsNotice();
    // 0x65f690: first opens them (and with the table empty, or a 0 at the start, 0x63ffe0 already closed them with
    // 0x65f6c0) and then 0x65f610, which raises them only if they are still open.
    FollowTheCredits(m_credits.Open());
    if (m_context.world != nullptr)
    {
        auto& runtime = m_context.world->Runtime();
        runtime.music.Stop(runtime.audio, "Music\\main_theme.mp3");
        runtime.music.Play(runtime.audio, "Music\\Mutheme.mp3");
    }
    m_ui.Raise(m_credits);
}

void LoginScene::FollowTheCredits(const FrontEnd::CreditsStep& step)
{
    // 0x63fc50: the new pair, on top of the old one (in MuRet, in its place). A missing one stops, like 0x63b870's
    // MessageBox and WM_DESTROY.
    if (step.illustration)
    {
        ReleaseTheIllustrations();
        m_illustrations.emplace(m_context.textures, Assets::CreditIllustrationSets.at(*step.illustration));
        if (auto loaded = m_illustrations->Load(m_context.dataRoot); !loaded.HasValue())
        {
            m_failure = loaded.Error();
        }
    }
    // 0x63f9a0, each time: 0x65f6c0, the C1 04 F4 06 and the music (0x4acf60 stops Mutheme.mp3 and 0x4acfd0 plays
    // main_theme.mp3, P-C79). Nothing visible happens between the load and the F4 06: the step's order (the closes
    // before the illustration) does not change the output.
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

void LoginScene::OnNoticeClosed(Ui::NoticeId notice)
{
    const Ui::NoticeFollowUp followUp = Ui::FollowUpOf(notice);
    switch (followUp)
    {
    case Ui::NoticeFollowUp::Close:
    // The deletion ones belong to the selection: the login does not open them.
    case Ui::NoticeFollowUp::AskForTheCode:
    case Ui::NoticeFollowUp::SendTheDeletion:
        return;
    case Ui::NoticeFollowUp::ReopenAtAccount:
    case Ui::NoticeFollowUp::ReopenAtPassword:
        // Only if the session is still waiting for the account. If it failed, the failure's notice already covered this
        // one: the guard is just in case.
        if (m_session->State() == Session::SessionState::WaitingCredentials)
        {
            OpenLogin(followUp == Ui::NoticeFollowUp::ReopenAtPassword ? FrontEnd::LoginFocus::Password
                                                                       : FrontEnd::LoginFocus::Account);
        }
        return;
    case Ui::NoticeFollowUp::Quit:
        // 0x643560's PostMessage with WM_CLOSE (Q-C69): Main does it.
        m_context.log.Info("el OK del aviso cierra el juego (el {:#04x})", static_cast<int>(notice));
        m_quit = true;
        return;
    }
}

void LoginScene::OpenLogin(FrontEnd::LoginFocus focus)
{
    m_login.SetTitle(LoginTitle());
    m_login.Open(focus);
    m_ui.Raise(m_login);
}

void LoginScene::OpenNotice(Ui::NoticeId notice)
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
    // 0x647e75: 0x65f690 with the options (0x644640 with 1).
    m_options.Open();
    m_ui.Raise(m_options);
}

void LoginScene::StepTheWindows(const Input::InputState& input)
{
    // The original passes each window the frame's milliseconds; MuRet, the step's (Q-C490).
    constexpr double StepMs = std::chrono::duration<double, std::milli>(Kernel::LogicStep).count();
    m_ui.Step(
        input, m_screen,
        [&](Ui::NewUiWindow& window, Ui::NewUiTurn turn)
        {
            if (&window == &m_notice)
            {
                if (const std::optional<Ui::NoticeId> closed = m_notice.Step(input, m_screen, turn))
                {
                    OnNoticeClosed(*closed);
                }
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
                if (const std::optional<FrontEnd::OptionsEvent> event = m_options.Step(
                        input, m_screen, StepMs, Hud::OptionsSettingsOf(CharacterWorld().Runtime()), turn))
                {
                    OnOptionsEvent(*event);
                }
            }
            else if (&window == &m_menuBar)
            {
                if (const std::optional<Ui::MenuBarClick> click = m_menuBar.Step(input, m_screen, turn))
                {
                    OnMenuBarClick(*click);
                }
            }
            else if (&window == &m_serverList)
            {
                if (const std::optional<FrontEnd::ServerClick> click = m_serverList.Step(input, m_screen, turn))
                {
                    OnServerClick(*click);
                }
            }
            else if (&window == &m_login)
            {
                if (const std::optional<FrontEnd::LoginRequest> request = m_login.Step(input, m_screen, turn))
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

void LoginScene::OnOptionsEvent(const FrontEnd::OptionsEvent& event)
{
    // The close button (without sound) and Esc (with the 0x19) already closed them.
    Kernel::WorldRuntime& runtime = CharacterWorld().Runtime();
    Hud::ApplyOption(runtime, event);
    if (event.kind == FrontEnd::OptionsEvent::Kind::Volume)
        runtime.SetSoundVolume(static_cast<std::uint8_t>(event.value));
}

void LoginScene::OnEscapeEvent(const FrontEnd::EscapeWindowEvent& event)
{
    switch (event.kind)
    {
    case FrontEnd::EscapeWindowEvent::Kind::Countdown:
    {
        // 0x647ef0: sprintf of the 380 with what remains and 0x555190, system and without a name.
        const std::string seconds = std::to_string(event.seconds);
        const std::array<std::string_view, 1> arguments{seconds};
        AddTheMessage(FrontEnd::LeavingTheGameText, arguments);
        return;
    }
    case FrontEnd::EscapeWindowEvent::Kind::Quit:
        // 0x647d87: the original's log (0x41fa40) and the PostMessage with WM_CLOSE: Main does it.
        m_context.log.Info("> Menu - Exit game.");
        m_quit = true;
        return;
    case FrontEnd::EscapeWindowEvent::Kind::JoinAnotherServer:
        // In the login the 1 is disabled (0x6478c0): it has no click.
        return;
    case FrontEnd::EscapeWindowEvent::Kind::Options:
        // 0x647e6c: 0x65f6c0 closes it and 0x65f690 opens the options.
        OpenTheOptions();
        return;
    case FrontEnd::EscapeWindowEvent::Kind::Closed:
        // With Esc, sound 0x19 (0x41ed10).
        return;
    }
}

void LoginScene::ShowFailure()
{
    // 0x4c1e20 opens the 0x6f with another version and the 0x70 when it does not connect or the F1 00 does not accept.
    // With an empty list, no answer or an account that does not get in, MuRet opens the 0x70 too.
    const bool otherVersion = m_session->Failure() == Session::SessionFailure::VersionMismatch;
    OpenNotice(otherVersion ? Ui::VersionMismatchNotice : Ui::ConnectFailedNotice);
}

std::string LoginScene::LoginTitle() const
{
    const std::optional<std::uint16_t> code = m_session->ChosenServer();
    if (!code)
    {
        return {};
    }
    const std::uint16_t group = FrontEnd::GroupOf(*code);
    const int number = FrontEnd::ServerNumber(*code);
    const std::optional<std::string_view> format =
        Ui::TextAt(m_texts, FrontEnd::LoginTitleText(FrontEnd::GroupSlot(group).value_or(NoSlot), number));
    if (!format)
    {
        return {};
    }
    const std::string_view name = Ui::TextAt(m_texts, FrontEnd::GroupNameText(group)).value_or(std::string_view{});
    const std::string numberText = std::to_string(number);
    const std::array<std::string_view, 2> arguments{name, numberText};
    return Ui::PlaceArguments(*format, arguments);
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
    // The windows read the next step's mouse with this screen.
    m_screen = canvas.screen;
    Render::SpriteBatch& sprites = canvas.sprites;
    // 1 and 2: the normal blend, the white and the two backgrounds.
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(White);
    sprites.Quad(Background(0x15, 0.0f));
    sprites.Quad(Background(0x16, 320.0f));
    // 3 and 4: the end of the 2D (0x621a90) and the 3D pass. The pass that draws nothing, from 0x620db0(0, 0, w, 48),
    // does not go.
    canvas.flushSprites();
    // Scene 2's shared camera (0x635ae0), in the viewport of 0x620db0(0, 25, 640, 430).
    // 0x6350d3..0x6350e1: the hero is written here, not on entering or in the step.
    Camera::GameCameraInput& cameraInput = CameraInputState();
    cameraInput.hero = Camera::LoginHeroPosition;
    cameraInput.scene = Terrain::CullingScene::Login;
    cameraInput.map = m_world ? m_world->map : EmberMap;
    const Render::Camera3D camera = Camera::CameraForFrame(CameraState(), cameraInput, m_cameraFrames,
                                                           canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    m_drawnCamera = camera;
    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, Camera::WorldViewport, camera);
    // 0x6351b7: 0x5fbb00(w · 0.0015625, &hero), the range and the trapezoid, which goes to the global (0x5fbeef →
    // 0x5fb570). With level 0: the effective level's transition (0x5fb70e..0x5fb73d) does not run here; with the
    // requested level at 0 after 0x635ae0 and the effective one at 0 from the .bss, it makes no difference (08 P-C41
    // d). The login's hero, map and yaw are fixed: an intermediate draw writes the same trapezoid as the last frame.
    m_terrainRange = Terrain::TerrainRangeOf(pass.view, camera);
    DynamicLight().PublishRange(m_terrainRange);
    Terrain::TerrainPolygon& polygon = SharedTerrainPolygon();
    polygon =
        Terrain::TerrainPolygonOf(Terrain::CullingScene::Login, cameraInput.map, cameraInput.hero, camera.angles.z);
    canvas.models.Begin(pass);
    // 0x6351f0: with the credits open (+0x4584), neither the terrain nor the models (0x5fc260..0x5306a0, with
    // 0x549510); the sprites and the particles go on. What runs once per drawn frame runs once in the first draw after:
    // the object effects, 0x505820, the cloth and the scepter, as with the connect.
    const bool credits = m_credits.IsOpen();
    if (credits)
    {
        m_objectEffectFrames = {};
        m_fullSetScanFrames = {};
        m_characterCloth.RestartTheClock();
    }
    else
    {
        // 0x5fc260(0): the terrain, before the characters.
        m_terrain.DrawTerrain(canvas.models, canvas.frameClock, m_terrainRange, polygon, CameraState().fixedHeight,
                              &DynamicLight().Lighting());
        DrawTheModels(canvas, alpha);
    }
    canvas.flushModels();
    // The frame's sprites, the ones the character's draw asked for (the cloth's 30 sparkles, 0x455930, and the
    // scepter's five, 0x509340): 0x549510 and, with the modelview at identity (0x6213e0), 0x549440(0), and the
    // particles (0x548550(0)). Without any, the eye pass draws nothing and is not opened.
    if (m_sprites.Any() || CharacterWorld().Particles().Any())
    {
        if (!credits)
        {
            m_sprites.MarkSeen();
        }
        Render::Pass3D eye = pass;
        eye.view = glm::mat4(1.0f);
        canvas.models.Begin(eye);
        Particles::DrawFrameSprites(canvas.models, m_sprites, m_models, Render::ViewRows(pass.view));
        const auto clock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
        const auto particles = Effects::DrawParticles(canvas.models, CharacterWorld().Particles(), m_models,
                                                      Render::ViewRows(pass.view), clock);
        if (!particles.HasValue())
        {
            ReportParticleFailure(particles.Error());
        }
        canvas.flushModels();
    }
    // 5: the two sands, additive.
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(HalfWhite);
    sprites.SetBlend(Render::BlendMode::Additive);
    sprites.SetColor(SandColor);
    sprites.SkewedQuad(SandQuad(SlowSand, canvas.frameClock));
    sprites.SkewedQuad(SandQuad(FastSand, canvas.frameClock));
    // 6: the texts, with the additive blend that remained (0x554000 does not change it).
    sprites.SetColor(TextColor);
    DrawTexts(canvas);
    // 7: the fade draws nothing (Q-C66). The notices (0x554810, which sets the normal blend and the white,
    // 0x554819..0x554845) and the old area. 8: the UI, each window with its image and its color. 9: the cursor
    // (0x635560).
    DrawTheMessages(canvas);
    // 0x65fe00: from the tail to the head, the last one opened or pressed on top (Q-C84).
    for (Ui::NewUiWindow* window : m_ui.DrawOrder())
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
            m_options.Draw(sprites, canvas.text, canvas.screen, Hud::OptionsSettingsOf(CharacterWorld().Runtime()));
        }
        else if (window == &m_credits)
        {
            m_credits.Draw(sprites, canvas.text, canvas.screen);
        }
    }
    if (m_context.input != nullptr)
    {
        Ui::DrawGameCursor(sprites, *m_context.input, canvas.screen);
    }
}

void LoginScene::ReportParticleFailure(const Particles::ParticleDrawingFailure& failure)
{
    // The pass was cut at that place (without rollback). Once per entry: the same cut repeats every frame.
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

// 0x50ba30 with the login's, at 0x50c91e: a 0x20a with +0x7c and +0x3e4 at 0 (0x517270). With class 4 it does not
// open the record. Without Player.bmd's actions it leaves earlier (0x50ba87), like DrawCharacter without its skeleton.
Actors::FullSetScan LoginScene::ScanTheCharacter(const Kernel::CharacterAppearance& appearance, int frames)
{
    if (m_context.cloth == nullptr || m_models.SkeletonOf(Tables::PlayerSkeletonModelId) == nullptr)
    {
        return {};
    }
    return Actors::ScanTheDrawnCharacter(CharacterWorld(), Actors::ScannedCharacterOf(appearance),
                                         m_context.cloth->random, frames);
}

Actors::Drawing::DrawnCharacter LoginScene::TheCharacterToDraw(float alpha)
{
    const Kernel::ActorState& actor = CharacterWorld().Actors()[FrontEnd::LoginActorSlot];
    Actors::Drawing::DrawnCharacter character;
    character.appearance = actor.alive ? Actors::AppearanceOfActor(actor) : FrontEnd::LoginCharacter();
    character.body = actor.animation;
    if (const auto* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId);
        skeleton != nullptr && actor.animation.action < skeleton->actions.size())
    {
        const auto& action = skeleton->actions[actor.animation.action];
        character.body = Models::Interpolated(actor.animation, action, action.speed, alpha);
    }
    character.object = actor.alive ? Models::ObjectTransform{actor.position, actor.angles, actor.scale, 0.0f}
                                   : FrontEnd::LoginCharacterTransform();
    // The step's head: the original does not interpolate it.
    character.headAngles = actor.headTurn.angles;
    if (actor.alive)
    {
        // 0x50bbff..0x50bc71: the shadow guards of the same 0x50ba30 (0x635206 → 0x510d00 → 0x510ea1): alpha from 0.5,
        // without +0x3e4, outside the maps without shadow and, with the mounted pets, in the safe zone (+0x3e2,
        // 0x50bc52).
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

// The character (0x510d00 → 0x50ba30, with the terrain light at its place plus its own), the horse (0x4453b0 →
// 0x445260, with scene 2's scale) and then the world objects with the logo (0x6046a0 → 0x602e90, with 0x5ffce0's
// color).
void LoginScene::DrawTheModels(Render::Frame::SceneCanvas& canvas, float alpha)
{
    const int nativeFrames = m_objectEffectFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing));
    Kernel::PoseBuffer projection;
    if (nativeFrames == 0)
        projection = CharacterWorld().PoseCarry();
    auto& carry = nativeFrames > 0 ? CharacterWorld().PoseCarry() : projection;
    Kernel::ActorState& actor = CharacterWorld().Actors()[FrontEnd::LoginActorSlot];
    Actors::Drawing::DrawnCharacter character = TheCharacterToDraw(alpha);
    // Q-C410: 0x6349a0 builds the login's with 0x517270 → 0x511290, which sets its +0x11c to 1 (0x511bf7) and gives it
    // its bone buffer (+0x120, 0x5123c4..0x5123ff). 0x510d00 → 0x50ba30 → 0x6001b0 poses the body there (0x600259),
    // starting from what its previous draw left and not from the global; only what hangs goes to the global
    // (0x68b181c).
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
        if (auto result = Actors::Drawing::PublishPoseRaw(carry, m_models, model, bones, transform, translated);
            !result.HasValue())
            m_failure = result.Error();
    };
    if (actor.alive && actor.seen)
    {
        const Actors::FullSetScan scan =
            ScanTheCharacter(character.appearance,
                             m_fullSetScanFrames.FramesAt(canvas.frameClock, OriginalFramePaceOf(m_context.pacing)));
        // With the global flow and Player.bmd, the same 0x50ba30 as the game: the body, the parts and the cloth, and
        // then what hangs and the effects (0x50db57..0x510a7a), which do not look at the scene except 0x510ef0.
        const Models::Skeleton* skeleton = m_models.SkeletonOf(Tables::PlayerSkeletonModelId);
        if (m_context.cloth != nullptr && skeleton != nullptr && !skeleton->actions.empty())
        {
            Actors::Drawing::DrawCharacter(canvas.models, m_models, character, canvas.frameClock,
                                           Actors::Drawing::CharacterDrawPass::Body);
            // 0x50bcec: the shadow copies the scale and the position to the 0x20b (WorldState::PlaceShadowModel), only
            // in a frame of the original.
            if (nativeFrames > 0 && Actors::Drawing::DrawsTheShadow(character))
                CharacterWorld().PlaceShadowModel(actor);
            DrawTheTail(canvas, actor, character, carry, scan, nativeFrames);
        }
        else
        {
            Actors::Drawing::DrawCharacter(canvas.models, m_models, character, canvas.frameClock);
            // Only a frame of the original leaves the body in its bones; an intermediate draw does not touch them.
            // 0x6001b0 first copies the scale and the position to Player.bmd's shared model, with alpha from 0.01
            // (Q-C412).
            if (nativeFrames > 0)
            {
                actor.bodyPose = Actors::Drawing::PoseCharacterBody(m_models, character);
                if (actor.alpha >= Kernel::PlayerModelAlpha)
                    CharacterWorld().PlacePlayerModel(actor);
                // 0x50bcec: the shadow copies the scale and the position to the 0x20b (WorldState::PlaceShadowModel).
                if (Actors::Drawing::DrawsTheShadow(character))
                    CharacterWorld().PlaceShadowModel(actor);
            }
        }
    }
    // 0x4182f0 at the end of 0x50ba30: the raven, if it passes the trapezoid; its bones from the step's state stay for
    // the next one's spark (Q-C133).
    if (actor.alive && actor.seen && actor.darkSpirit)
    {
        DrawTheRaven(canvas, alpha, &carry);
    }

    Actors::Drawing::DrawnObject horse =
        Pets::DrawnDarkHorse(m_horse, m_models.SkeletonOf(Models::ModelId{Pets::DarkHorseModel}), character.object,
                             Pets::DarkHorseLoginScale, alpha);
    if (auto result = Actors::Drawing::PublishPose(carry, m_models, horse, true); !result.HasValue())
        m_failure = result.Error();
    horse.retainedMeshes = carry.meshes;
    Actors::Drawing::DrawDarkHorse(canvas.models, m_models, horse, m_world ? m_world->map : EmberMap,
                                   canvas.frameClock);

    // 0x6046a0: the world objects and the logo, sector by sector, with 0x602ed0's work: one rand() per object and the
    // lantern's two sprites at bones 8 and 9 of the global pose buffer. What hangs from the character overwrote it
    // before; the horse, if drawn (0x5ffdf0, with alpha from 0.01), after.
    constexpr float Drawn = 0.01f;
    std::vector<Actors::Drawing::PosedInTheBuffer> posed;
    if (m_horse.alpha >= Drawn)
    {
        posed.push_back(Actors::Drawing::PosedInTheBuffer{&m_models, horse});
    }
    const LoginObjectModels objects(m_worldModels, m_logo);
    const Terrain::TerrainLighting* lighting = &DynamicLight().Lighting();
    WorldObjects::DrawWorldObjects(
        canvas.models, objects, m_objects, SharedTerrainPolygon(), lighting, m_world ? m_world->map : 0,
        canvas.frameClock, alpha,
        WorldObjects::ObjectFrameEffects{.random = m_context.cloth != nullptr ? &m_context.cloth->random : nullptr,
                                         .sprites = &m_sprites,
                                         .particles = &CharacterWorld().Particles(),
                                         .posedBefore = posed,
                                         .scene = WorldObjects::ObjectScene::Login,
                                         .frames = nativeFrames,
                                         .lastRandoms = m_objectEffectRandoms,
                                         .onPose =
                                             [&](const Actors::Drawing::DrawnObject& object)
                                         {
                                             auto result = Actors::Drawing::PublishPose(carry, objects, object, true);
                                             if (!result.HasValue())
                                                 m_failure = result.Error();
                                             return result;
                                         },
                                         .retainedPose = &carry,
                                         .publishProjectedPose = true,
                                         .objectDrawCounter = &CharacterWorld().Runtime().objectDrawCounter,
                                         .modelEffects = &CharacterWorld().Map().effects,
                                         .onFailure = [&](const std::string& error) { m_failure = error; }});
    // 0x53c6a0(0) (0x635217), after the objects (0x6046a0) and before the model effects: the world's joints. Only a
    // frame of the original takes the rand() of its draw (the turns of the 0x610/6 and /8); between frames they go on a
    // copy and repeat the saved turns.
    const float jointClock = static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count());
    Crt::CrtRandom projectedJointRandom = CharacterRandomWorld().random;
    const Joints::JointDrawResources jointResources{nativeFrames > 0 ? CharacterRandomWorld().random
                                                                     : projectedJointRandom,
                                                    m_sprites, nativeFrames > 0, jointClock};
    if (auto joints = Joints::DrawJoints(canvas.models, CharacterWorld().DrawTail().joints, 0, jointResources);
        !joints.HasValue())
        m_failure = joints.Error();
    // 0x52d620(0) (0x63521e): the model effects, after the joints, with the game's draw: the dragon through the generic
    // path (0x52da92 → 0x602e90) and the meteor 0xc2/9 with its +0x64 at 1 (the blendMesh, 0x51adb5;
    // Effects/WizardEffectDrawing). The position goes `alpha` of one more step (D-C4).
    const LoginEffectModels effectModels(m_worldModels, m_models);
    const Terrain::SphereFrustum frustum = Terrain::SphereFrustumOf(canvas.models.Pass().view, m_drawnCamera);
    const std::span<Effects::ModelEffect> table = CharacterWorld().Map().effects.Effects();
    const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    Core::Expected<void, std::string> effects;
    if (terrain != nullptr && terrain->heights)
    {
        ModelEffectDrawResources resources{
            *terrain->heights,
            static_cast<float>(std::chrono::duration<double, std::milli>(canvas.frameClock).count())};
        resources.onPose = [&](const Actors::Drawing::DrawnObject& object)
        { return Actors::Drawing::PublishPose(carry, effectModels, object, true); };
        resources.onUntranslatedPose = [&](const Actors::Drawing::DrawnObject& object)
        { return Actors::Drawing::PublishPose(carry, effectModels, object, false); };
        resources.posedMeshes = [&]() { return std::span<const Models::SkinnedMesh>(carry.meshes); };
        resources.dynamicLighting = &DynamicLight().Lighting();
        resources.originalFrame = nativeFrames > 0;
        effects = DrawGameModelEffects(canvas.models, effectModels, table, frustum, lighting, canvas.frameClock, alpha,
                                       resources, 0);
    }
    else
        effects = DrawGameModelEffects(canvas.models, effectModels, table, frustum, lighting, canvas.frameClock, alpha);
    if (!effects.HasValue())
        m_failure = effects.Error();
    // 0x5306a0, after 0x549510: the embers, in the world.
    Weather::DrawLeaves(canvas.models, m_leaves);
    m_drawnSinceEntering = true;
}

// 0x50db57..0x510a7a with the login's character, after its body (GameCharacters.h, DrawCharacterTail): once per frame
// of the original that the clock counts (with several, only the last draws), and with zero once on copies. The light
// of what hangs (0x50db94) is the terrain's at its place (0x5f99e0, outside scene 4) plus +0xf4, the same as the
// body's, or the PKs' red. What it rolls (the glow of 0x509340's scepter) changes only in a frame of the original: an
// intermediate draw repeats the last one's sprites. DrawTheRaven draws the raven.
void LoginScene::DrawTheTail(Render::Frame::SceneCanvas& canvas, Kernel::ActorState& actor,
                             const Actors::Drawing::DrawnCharacter& character, Kernel::PoseBuffer& carry,
                             const Actors::FullSetScan& scan, int frames)
{
    Kernel::WorldState& world = CharacterWorld();
    Models::ClothWorld& cloth = *m_context.cloth;
    constexpr std::uint8_t RedPk = 6;
    actor.drawnLight = actor.pk >= RedPk ? glm::vec3{1.0f, 0.1f, 0.1f} : character.light;
    const bool originalFrame = frames > 0;
    // Between frames, the item globals on copies, like GameScene and SelectionWorld.
    std::optional<Actors::Drawing::GameItemRenderState> projectedItems;
    std::optional<Kernel::ObjectModelStepState> projectedModels;
    std::optional<Crt::CrtRandom> projectedRandom;
    Actors::Drawing::GameItemRenderState& items =
        originalFrame ? world.Runtime().itemRender : projectedItems.emplace(world.Runtime().itemRender);
    Kernel::ObjectModelStepState& modelState =
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
        const Actors::Drawing::ItemDrawResources itemResources{.frameClock = canvas.frameClock,
                                                               .random = itemRandom,
                                                               .itemRender = items,
                                                               .sprites = m_tailSprites,
                                                               .poseCarry = carry,
                                                               .modelState = modelState,
                                                               .joints = world.DrawTail().joints,
                                                               .effects = world.Map().effects,
                                                               .map = world.Runtime().map,
                                                               .originalFrame = originalFrame,
                                                               .glowDetail = world.Runtime().ui.glowDetail};
        Actors::Drawing::FillItemDrawContexts(special, itemContext, itemResources);
        Session::FillItemCreators(itemContext, itemResources);
        const Actors::Drawing::GameCharacterPass pass{.dynamicLighting = &DynamicLight().Lighting(),
                                                      .clothWorld = &cloth,
                                                      .frameClock = canvas.frameClock,
                                                      .sprites = &m_tailSprites,
                                                      .poseCarry = &carry,
                                                      .joints = world.DrawTail().joints,
                                                      .particles = &world.Particles(),
                                                      .effects = &world.Map().effects,
                                                      .itemContext = &itemContext,
                                                      .hooks = &m_characterDrawHooks,
                                                      .effectHooks = &m_effectHooks};
        if (auto drawn = Actors::Drawing::DrawCharacterTail(batch, m_models, world, actor, FrontEnd::LoginActorSlot,
                                                            character, pass, scan, originalFrame);
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
    for (Particles::Sprite& sprite : (originalFrame ? m_tailSprites : m_tailSpriteReplay).Sprites())
    {
        if (sprite.alive)
            static_cast<void>(m_sprites.Create(sprite.texture, sprite.position, sprite.scale, sprite.light,
                                               sprite.rotation, sprite.blend));
    }
}

void LoginScene::DrawTheRaven(Render::Frame::SceneCanvas& canvas, float alpha, Kernel::PoseBuffer* carry)
{
    Kernel::DarkSpirit& spirit = *CharacterWorld().Actors()[FrontEnd::LoginActorSlot].darkSpirit;
    if (!Actors::Drawing::DarkSpiritSeen(spirit, SharedTerrainPolygon()))
    {
        Actors::Drawing::MissTheView(spirit);
        return;
    }
    const Models::Skeleton* skeleton = m_models.SkeletonOf(Models::ModelId{Pets::DarkSpiritModel});
    const glm::vec3 light = TerrainLightAt(spirit.position);
    const Terrain::WorldTerrain* terrain = m_terrain.Terrain();
    Actors::Drawing::DrawnObject drawn = Actors::Drawing::DrawnDarkSpirit(spirit, skeleton, light, alpha);
    if (carry != nullptr)
    {
        if (auto result = Actors::Drawing::PublishPose(*carry, m_models, drawn, true); !result.HasValue())
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
    spirit.pose =
        Actors::Drawing::PosedWorldBones(m_models, Actors::Drawing::DrawnDarkSpirit(spirit, skeleton, light, 0.0f));
}

// 0x604ab0 with the 0xa2 in scene 2: the routes' light on the object and on the blendMesh, and the advance to +0xd8
// (not to the action's speed).
void LoginScene::StepTheLogo(Kernel::WorldObject& logo) const
{
    const float light = FrontEnd::LoginLogoLight(m_routes);
    logo.light = glm::vec3(light);
    logo.state.light = light;
    if (const Models::Skeleton* skeleton = m_logo.SkeletonOf(Models::ModelId{FrontEnd::LoginLogoModel});
        skeleton != nullptr && logo.animation.action < skeleton->actions.size())
    {
        Models::AdvanceAnimation(logo.animation, skeleton->actions[logo.animation.action], logo.speed,
                                 Models::AnimationScene::Other);
    }
}

glm::vec3 LoginScene::TerrainLightAt(const glm::vec3& position) const
{
    const Terrain::TerrainLighting* lighting = &DynamicLight().Lighting();
    return lighting != nullptr ? Terrain::LightAt(*lighting, position.x, position.y) : glm::vec3(0.0f);
}

void LoginScene::DrawTheMessages(Render::Frame::SceneCanvas& canvas)
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
    // The windows too: the account clears the password on closing (LoginWindow::Close).
    m_serverList.Close();
    m_menuBar.Close();
    m_login.Close();
    m_notice.Close();
    m_escape.Close();
    m_options.Close();
    m_credits.Close();
    m_ui.Clear();
    // 0x61c7c0 uploads 0x61ccb0's embers again. The common slot survives leaving the login.
    const Render::TextureSlot ember{Weather::EmberTexture};
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

std::optional<Kernel::SceneId> LoginScene::Next() const
{
    if (m_session != nullptr && m_session->State() == Session::SessionState::LoggedIn)
    {
        return Kernel::SceneId::CharacterSelect;
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
    // 0x6350b0 neither draws nor presents before the first step (0x7d3a13d); with the session, the code below covers
    // it: the step has not seen the state yet.
    if (m_session == nullptr)
    {
        return !m_steppedSinceEntering;
    }
    // Connecting, and also with what the step has not seen yet (since Enter, or a Poll that changed the state in a
    // frame without steps): a connect that fails right away comes out in that Poll, and the frame would be the login
    // without the notice.
    const Session::SessionState state = m_session->State();
    return IsConnecting(state) || m_seen != state;
}

void LoginScene::OnFrameHeld()
{
    // Waiting for Step to show a list or a notice is not equivalent to the original blocking connect.
    if (m_session != nullptr && (IsConnecting(m_session->State()) || (m_seen && IsConnecting(*m_seen))))
    {
        // What the original runs once per drawn frame runs once in the first draw after the connect (Q-C85), not the
        // frames it lasted: the object effects, 0x505820, the cloth and the scepter (08, P-C27 f).
        m_objectEffectFrames = {};
        m_fullSetScanFrames = {};
        m_characterCloth.RestartTheClock();
    }
}

const Kernel::CharacterHead& LoginScene::Head() const
{
    return CharacterWorld().Actors()[FrontEnd::LoginActorSlot].headTurn;
}

std::optional<std::string> LoginScene::Failure() const
{
    return m_failure;
}
} // namespace Client::Scenes
