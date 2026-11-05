#include "Scenes/LoginScene.h"

#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Data/Models/ModelFiles.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Textures/TextureSet.h"
#include "GameLogic/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
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
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/GameData.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SceneCameras.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTextures.h"
#include "Scenes/SceneWorlds.h"
#include "Scenes/TextureSets.h"
#include "Scenes/WorldModels.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "UI/GameText.h"
#include "UI/LoginWindow.h"
#include "UI/MenuBar.h"
#include "UI/Notices.h"
#include "UI/ServerListModel.h"
#include "UI/ServerListWindow.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/Leaves.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ratio>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
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

// El lugar de un grupo sin botón para el título: solo llega con --auto-login, que elige el primero
// de la lista aunque la ventana no lo muestre. Cualquiera que no sea el 4 da lo mismo (IsNonPvp).
constexpr int NoSlot = -1;

[[nodiscard]] bool IsWindowTexture(Render::TextureSlot slot)
{
    return std::ranges::any_of(WindowTextureSet, [slot](const Data::Textures::TextureSetEntry& entry)
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
class LoginObjectModels final : public IModelLibrary
{
public:
    LoginObjectModels(const IModelLibrary& world, const IModelLibrary& logo) : m_world(world), m_logo(logo) {}

    [[nodiscard]] const Data::Models::Model* Find(std::uint16_t model) const override
    {
        return Of(model).Find(model);
    }
    [[nodiscard]] const Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return Of(model).SkeletonOf(model);
    }
    [[nodiscard]] std::span<const Render::TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return Of(model).MeshTextures(model);
    }

private:
    [[nodiscard]] const IModelLibrary& Of(std::uint16_t model) const
    {
        return model == LoginLogoModel ? m_logo : m_world;
    }

    const IModelLibrary& m_world;
    const IModelLibrary& m_logo;
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

LoginScene::LoginScene(const SceneContext& context, const GameData& data, const IModelLibrary& models,
                       App::ServerSession* session, std::string_view version, std::mt19937::result_type seed,
                       const Render::Models::ModelTextureSlots* gameSlots)
    : m_context(context), m_models(models), m_session(session), m_texts(data.text ? &*data.text : nullptr),
      m_textures(context.textures, LoginTextureSet), m_windowTextures(context.textures, WindowTextureSet),
      m_world(WorldOf(SceneId::Login)), m_terrain(context.textures), m_gameSlots(gameSlots), m_random(seed),
      m_serverList([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_menuBar([this](Render::TextureSlot slot) { return BufferSize(slot); }),
      m_login([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts),
      m_notice([this](Render::TextureSlot slot) { return BufferSize(slot); }, m_texts)
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

Core::Expected<void, std::string> LoginScene::Enter()
{
    LoadTheWorld();
    auto loaded = m_textures.Load(m_context.dataRoot);
    if (!loaded.HasValue())
    {
        return loaded;
    }
    auto logo = LoadTheLogo();
    if (!logo.HasValue())
    {
        return logo;
    }
    auto windows = m_windowTextures.Load(m_context.dataRoot);
    if (!windows.HasValue())
    {
        return windows;
    }
    m_serverList.Close();
    m_menuBar.Close();
    m_login.Close();
    m_notice.Close();
    m_seen.reset();
    m_quit = false;
    if (m_session != nullptr)
    {
        m_session->Start();
    }
    PlaceTheModels();
    return {};
}

void LoginScene::LoadTheWorld()
{
    m_terrain.Release();
    m_lighting.reset();
    m_worldModels.Release();
    m_objectRecords.clear();
    if (!m_world)
    {
        return;
    }
    // 0x5fbb00 y 0x5fb570 con la cámara fija y el héroe del login: el rango de bloques (en el login, los
    // que empiezan en x 0..128 e y 120..248) y el trapecio, que no recorta ninguna celda (06 § El
    // terreno del login).
    const Render::Camera3D camera = LoginCamera();
    m_terrainRange =
        World::TerrainRangeOf(Render::MakePass3D(App::PixelSize{640, 480}, WorldViewport, camera).view, camera);
    m_terrainPolygon =
        World::TerrainPolygonOf(World::CullingScene::Login, m_world->map, LoginHeroPosition, camera.angles.z);
    LoadTheTerrain();
    LoadTheObjects();
}

void LoginScene::LoadTheTerrain()
{
    std::vector<std::string> skipped;
    auto loaded = m_terrain.Load(m_context.dataRoot, m_world->map, skipped);
    if (!loaded.HasValue())
    {
        m_context.log.Warning("el login va sin el terreno: {}", loaded.Error());
        // Sin el terreno, la luz de los modelos puede salir igual de sus dos archivos.
        auto lighting = World::LoadTerrainLighting(m_context.dataRoot, m_world->map);
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
    auto records = World::LoadWorldObjects(m_context.dataRoot, m_world->map);
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
    auto skeleton = Render::Models::MakeSkeleton(model.Value());
    if (!skeleton.HasValue())
    {
        m_context.log.Warning("el logo no tiene un esqueleto que se pueda posar y no se dibuja");
        return {};
    }
    // Las texturas apuntan a lo que piden los slots: se van antes que ellos.
    m_logoTextures.reset();
    // Sin un 0x60d530 antes: donde dejó el próximo el mundo (Q-C92), o LoginLogoFirstSlot sin él.
    if (const Render::Models::ModelTextureSlots* world = m_worldModels.TextureSlots())
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

// 0x6349a0 con 0x606660 (los objetos del .obj y después el logo), 0x517270 y el caballo
// (0x443fa0(0xbf, …), invisible), y la cuenta de las rutas en −1. La fase vuelve a 0 (el original no la
// vuelve: Q-C111).
void LoginScene::PlaceTheModels()
{
    m_objects = World::CreateWorldObjects(m_objectRecords, m_world ? m_world->map : 0, World::ObjectScene::Login);
    m_logoIndex = m_objects.size();
    m_objects.push_back(LogoWorldObject());
    // 0x6070a0 vacía la tabla de efectos, y en el mapa 55 el dueño del dragón (el 0xb4 que la carga del
    // mapa crea en (5.600; 23.000; 0)) lo crea con 0x518cf0 (06 § El dragón de fondo).
    m_effects.Clear();
    m_sprites.Clear();
    m_particles.Clear();
    m_leaves.Clear();
    m_drawnSinceEntering = false;
    if (m_world && m_world->map == DragonMap)
    {
        (void)m_effects.Create(World::DragonEffect, DragonOwnerPosition, glm::vec3(0.0f), glm::vec3(1.0f),
                               World::DragonScale);
    }
    m_routes = {};
    m_character = LoginCharacter();
    m_characterAnimation = {};
    m_horse = {};
    if (const Render::Models::Skeleton* skeleton = m_models.SkeletonOf(Data::Models::PlayerSkeletonModel))
    {
        Render::Models::SetAction(m_characterAnimation.body, *skeleton, LoginCharacterStandingAction);
    }
}

void LoginScene::Step(const Input::InputState& input)
{
    if (m_context.cloth != nullptr)
    {
        Render::Models::StepTheClothWorld(*m_context.cloth);
    }
    if (m_session == nullptr)
    {
        StepTheWorld();
        return;
    }
    // Lo que cambió con el Poll de este frame.
    FollowSession();
    // Con la orden de cerrar el juego, el paso no atiende nada, ni el mundo: el original no corre otro
    // después del WM_DESTROY (Q-C106), y un Esc en la cuenta volvería a conectar.
    if (m_session->State() == App::SessionState::QuitOrdered)
    {
        return;
    }
    StepTheWorld();
    // Sin aviso, la barra se atiende junto con la cuenta o la lista: sus botones no se pisan con
    // los de ellas (Q-C84).
    if (!m_notice.IsOpen())
    {
        if (const std::optional<UI::MenuBarClick> click = m_menuBar.Step(input, m_screen))
        {
            OnMenuBarClick(*click);
        }
    }
    if (m_notice.IsOpen())
    {
        if (const std::optional<UI::NoticeId> closed = m_notice.Step(input, m_screen))
        {
            OnNoticeClosed(*closed);
        }
    }
    else if (m_login.IsOpen())
    {
        if (const std::optional<UI::LoginRequest> request = m_login.Step(input, m_screen))
        {
            OnLoginRequest(*request);
        }
    }
    else if (m_serverList.IsOpen())
    {
        if (const std::optional<UI::ServerClick> click = m_serverList.Step(input, m_screen))
        {
            OnServerClick(*click);
        }
    }
    // Y lo que cambió con lo que la escena le pidió a la sesión: un login que se rechaza en el
    // próximo Poll vuelve a WaitingCredentials, y la escena tiene que haber visto que salió.
    FollowSession();
}

void LoginScene::StepTheWorld()
{
    // 0x5fbf60, lo primero del paso del login (0x634c60): la ola del agua.
    if (m_context.clock != nullptr)
    {
        m_terrain.Step(m_context.clock->Now(), m_terrainRange);
    }
    const Render::Models::Skeleton* player = m_models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (m_context.clock != nullptr && player != nullptr)
    {
        if (const std::optional<std::uint8_t> action = LoginCharacterActionAt(m_context.clock->Now(), m_random))
        {
            Render::Models::SetAction(m_characterAnimation.body, *player, *action);
        }
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
    if (const Render::Models::Skeleton* horse = m_models.SkeletonOf(DarkHorseModel))
    {
        // El aliento lee la pose del dibujo anterior: el caballo de antes de este paso (en 0x444010, el
        // avance de 0x4ec200 corre después del aliento; la acción que mira es la de este paso).
        const DarkHorse drawn = m_horse;
        StepDarkHorse(m_horse, *horse, m_characterAnimation.body.action);
        BreatheTheHorse(drawn);
    }
    // 0x530490, después de los ayudantes (0x634d0e): en el mapa 55, las 80 brasas, con el rand() de la CRT.
    // Nacen alrededor del héroe (el personaje 0), al que 0x6350b0 le escribe la posición recién en el
    // dibujo: antes del primero, en (0, 0, 0) (Q-C137). La y de la cámara que miran, igual: al entrar,
    // 0x6349a0 deja la cámara en (0, 0, 0) (0x634b80) y 0x635ae0 le escribe la de LoginCamera en el
    // dibujo (0x635f7e).
    if (const World::WorldTerrain* terrain = m_terrain.Terrain();
        m_context.cloth != nullptr && terrain != nullptr && m_world && m_world->map == EmberMap)
    {
        const World::EmberWorld embers{.hero = m_drawnSinceEntering ? LoginHeroPosition : glm::vec3(0.0f),
                                       .cameraY = m_drawnSinceEntering ? LoginCamera().position.y : 0.0f,
                                       .heights = terrain->heights.get(),
                                       .attributes = terrain->attributes.get()};
        World::StepLoginEmbers(m_leaves, embers, m_context.cloth->random);
    }
    // 0x508f00 → 0x4fcd00: lo que termina vuelve a la de parado (0x4fcb10 → 0x4f2250).
    if (StepCharacterAnimation(m_characterAnimation, m_character, m_models, Render::Models::AnimationScene::Other) &&
        player != nullptr)
    {
        Render::Models::SetAction(m_characterAnimation.body, *player, LoginCharacterStandingAction);
    }
    // 0x52d220, después de los objetos (0x606560), el caballo (0x445230) y los personajes (0x508f00): los
    // efectos con modelo, el dragón.
    StepModelEffects(m_effects.Effects(), m_worldModels);
    // 0x5446e0, al final (0x634d22): el viento y las partículas, con el rand() de la CRT.
    if (m_context.cloth != nullptr)
    {
        World::StepParticles(m_particles, m_context.cloth->random);
    }
    AdvanceRoutes(m_routes);
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
    Render::Models::CrtRandom& random = m_context.cloth->random;
    if (random.Next() % OneIn != 0)
    {
        return;
    }
    const float clock =
        m_context.clock != nullptr
            ? static_cast<float>(std::chrono::duration<double, std::milli>(m_context.clock->Now()).count())
            : 0.0f;
    (void)m_particles.Create(World::BreathParticle, GlobalPosePoint(BreathBone, BreathOffset, drawn),
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
    // (0x52d620). Los objetos del mapa 55 tienen menos de 28 huesos, y lo que cuelga del personaje va
    // antes que el caballo.
    constexpr float Drawn = 0.01f;
    std::vector<PosedInTheBuffer> posed;
    if (drawnHorse.alpha >= Drawn)
    {
        posed.push_back(
            PosedInTheBuffer{&m_models, DrawnDarkHorse(drawnHorse, m_models.SkeletonOf(DarkHorseModel),
                                                       LoginCharacterTransform(), DarkHorseLoginScale, 0.0f)});
    }
    for (const World::ModelEffect& effect : m_effects.Effects())
    {
        if (!effect.alive || effect.alpha < Drawn)
        {
            continue;
        }
        DrawnObject object;
        object.model = static_cast<std::uint16_t>(effect.type);
        object.animation = effect.animation;
        object.transform = effect.transform;
        posed.push_back(PosedInTheBuffer{&m_worldModels, object});
    }
    return LastPosedBonePoint(posed, bone, offset).value_or(glm::vec3(0.0f));
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
        m_login.Close();
        m_serverList.Open(std::move(groups));
        // Y la barra, después de la lista: va arriba de ella.
        m_menuBar.Open();
        return;
    }
    case App::SessionState::WaitingCredentials:
        m_serverList.Close();
        // Con un resultado, el login se rechazó: su aviso, y el OK abre la cuenta (0x643560). Sin
        // él, el F1 00 (0x4c1e20).
        if (const std::optional<std::uint8_t> result = m_session->LoginResult())
        {
            if (const std::optional<UI::NoticeId> notice = UI::NoticeOfLoginResult(*result))
            {
                m_notice.Open(*notice);
                return;
            }
        }
        OpenLogin(UI::LoginFocus::Account);
        return;
    case App::SessionState::Failed:
        m_serverList.Close();
        m_login.Close();
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
        m_serverList.Close();
        m_login.Close();
        return;
    case App::SessionState::QuitOrdered:
        // El juego se cierra al final de este frame (main.cpp), sin aviso: el original no dibuja
        // otro después del WM_DESTROY (Q-C106), así que las ventanas quedan como estaban.
        return;
    }
}

void LoginScene::OnServerClick(const UI::ServerClick& click)
{
    switch (click.kind)
    {
    case UI::ServerClick::Kind::Chosen:
        m_session->ChooseServer(click.code);
        return;
    case UI::ServerClick::Kind::Full:
        m_notice.Open(UI::ServerFullNotice);
        return;
    }
}

void LoginScene::OnLoginRequest(const UI::LoginRequest& request)
{
    switch (request.kind)
    {
    case UI::LoginRequest::Kind::Submit:
        m_session->SubmitCredentials(request.credentials);
        return;
    case UI::LoginRequest::Kind::EmptyAccount:
        m_notice.Open(UI::EmptyAccountNotice);
        return;
    case UI::LoginRequest::Kind::EmptyPassword:
        m_notice.Open(UI::EmptyPasswordNotice);
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
    // El original abre la ventana de Esc con el menú, y con los créditos manda el F4 06 al CS y
    // abre la de +0x4578; ninguna de las dos se leyó.
    m_context.log.Info("el botón {} de la barra todavía no hace nada (P-C13)",
                       click == UI::MenuBarClick::Menu ? "del menú" : "de los créditos");
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
}

void LoginScene::ShowFailure()
{
    // 0x4c1e20 abre el 0x6f con otra versión y el 0x70 cuando no conecta o el F1 00 no acepta. Con
    // una lista vacía, sin respuesta o una cuenta que no entra, MuRet abre el 0x70 también.
    const bool otherVersion = m_session->Failure() == App::SessionFailure::VersionMismatch;
    m_notice.Open(otherVersion ? UI::VersionMismatchNotice : UI::ConnectFailedNotice);
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

Scenes::TextureBufferSize LoginScene::BufferSize(Render::TextureSlot slot) const
{
    return IsWindowTexture(slot) ? m_windowTextures.BufferSize(slot) : m_textures.BufferSize(slot);
}

void LoginScene::Render(SceneCanvas& canvas, float alpha)
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
    // La cámara fija de la escena 2 (0x635ae0), en el viewport de 0x620db0(0, 25, 640, 430).
    const Render::Pass3D pass = Render::MakePass3D(canvas.screen, WorldViewport, LoginCamera());
    canvas.models.Begin(pass);
    // 0x5fc260(0): el terreno, antes de los personajes.
    m_terrain.DrawTerrain(canvas.models, canvas.frameClock, m_terrainRange, m_terrainPolygon);
    DrawTheModels(canvas, alpha);
    canvas.flushModels();
    // Los sprites del frame, los que pidió el dibujo del personaje (los 30 destellos de la tela, 0x455930,
    // y los cinco del cetro, 0x509340): 0x549510 y, con la modelview en la identidad (0x6213e0),
    // 0x549440(0), y las partículas (0x548550(0)). Sin ninguno, el pase de ojo no dibuja nada y no se abre.
    if (m_sprites.Any() || m_particles.Any())
    {
        m_sprites.MarkSeen();
        Render::Pass3D eye = pass;
        eye.view = glm::mat4(1.0f);
        canvas.models.Begin(eye);
        DrawFrameSprites(canvas.models, m_sprites, m_models, Render::ViewRows(pass.view));
        DrawParticles(canvas.models, m_particles, m_models, Render::ViewRows(pass.view));
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
    // 7: el fundido no dibuja nada (Q-C66). 8: la UI (CL5.5), cada ventana con la mezcla normal.
    m_serverList.Draw(sprites, canvas.text, canvas.screen);
    m_menuBar.Draw(sprites, canvas.screen);
    m_login.Draw(sprites, canvas.text, canvas.screen);
    m_notice.Draw(sprites, canvas.text, canvas.screen);
}

// El personaje (0x510d00 → 0x50ba30, con la luz del terreno en su lugar más la suya), el caballo
// (0x4453b0 → 0x445260, con la escala de la escena 2) y después los objetos del mundo con el logo
// (0x6046a0 → 0x602e90, con el color de 0x5ffce0).
void LoginScene::DrawTheModels(SceneCanvas& canvas, float alpha)
{
    DrawnCharacter character;
    character.appearance = m_character;
    character.body = BodyBetweenSteps(m_characterAnimation, m_models, alpha);
    character.pet = m_characterAnimation.pet.state;
    character.wings = m_characterAnimation.wings.state;
    character.object = LoginCharacterTransform();
    character.light = TerrainLightAt(character.object.position) + glm::vec3(LoginCharacterLight);
    character.cloth = &m_characterCloth;
    character.clothWorld = m_context.cloth;
    character.map = m_world ? m_world->map : 0;
    character.sprites = &m_sprites;
    DrawCharacter(canvas.models, m_models, character, canvas.frameClock);

    DrawDarkHorse(
        canvas.models, m_models,
        DrawnDarkHorse(m_horse, m_models.SkeletonOf(DarkHorseModel), character.object, DarkHorseLoginScale, alpha),
        canvas.frameClock);

    // 0x6046a0: los objetos del mundo y el logo, sector por sector.
    const LoginObjectModels objects(m_worldModels, m_logo);
    const World::TerrainLighting* lighting = m_terrain.IsLoaded() ? m_terrain.Lighting() : m_lighting.get();
    DrawWorldObjects(canvas.models, objects, m_objects, m_terrainPolygon, lighting, m_world ? m_world->map : 0,
                     canvas.frameClock, alpha);
    // 0x52d620(0): los efectos con modelo, después de los objetos.
    DrawModelEffects(canvas.models, m_worldModels, m_effects.Effects(), lighting, canvas.frameClock, alpha);
    // 0x5306a0, después de 0x549510: las brasas, en el mundo.
    DrawLeaves(canvas.models, m_leaves);
    m_drawnSinceEntering = true;
}

// 0x604ab0 con el 0xa2 en la escena 2: la luz de las rutas en el objeto y en el blendMesh, y el avance a
// +0xd8 (no a la velocidad de la acción).
void LoginScene::StepTheLogo(World::WorldObject& logo) const
{
    const float light = LoginLogoLight(m_routes);
    logo.light = glm::vec3(light);
    logo.state.light = light;
    if (const Render::Models::Skeleton* skeleton = m_logo.SkeletonOf(LoginLogoModel);
        skeleton != nullptr && logo.animation.action < skeleton->actions.size())
    {
        Render::Models::AdvanceAnimation(logo.animation, skeleton->actions[logo.animation.action], logo.speed,
                                         Render::Models::AnimationScene::Other);
    }
}

glm::vec3 LoginScene::TerrainLightAt(const glm::vec3& position) const
{
    const World::TerrainLighting* lighting = m_terrain.IsLoaded() ? m_terrain.Lighting() : m_lighting.get();
    return lighting != nullptr ? World::LightAt(*lighting, position.x, position.y) : glm::vec3(0.0f);
}

void LoginScene::DrawTexts(SceneCanvas& canvas) const
{
    ITextDrawer& text = canvas.text;
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
    m_textures.Release();
    m_windowTextures.Release();
    m_terrain.Release();
    m_worldModels.Release();
    m_objects.clear();
    m_effects.Clear();
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
    if (m_session == nullptr)
    {
        return false;
    }
    // Conectando, y también con lo que el paso todavía no vio (desde Enter, o un Poll que cambió el
    // estado en un frame sin pasos): un connect que falla enseguida sale en ese Poll, y el frame
    // sería el login sin el aviso.
    const App::SessionState state = m_session->State();
    return IsConnecting(state) || m_seen != state;
}
} // namespace Client::Scenes
