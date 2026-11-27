#pragma once

#include "Session/ServerSession.h"
#include "Core/PixelSize.h"
#include "Tables/GateTable.h"
#include "Tables/ItemSetTables.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Pathfinding/PathCell.h"
#include "Scenes/FaunaDrawing.h"
#include "Terrain/SphereFrustum.h"
#include "Terrain/TerrainPickingState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Models/ModelTextureSlots.h"
#include "Render/Pass3D.h"
#include "Scenes/IScene.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/OriginalFramesOfTheSteps.h"
#include "Scenes/GroundEffectFrame.h"
#include "Scenes/GuildMarks.h"
#include "World/Sprites.h"
#include "World/ModelEffects.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Assets/SceneTextures.h"
#include "Assets/TerrainTiles.h"
#include "Assets/WorldModels.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainPickingSegment.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <span>

namespace Client::Assets
{
class FaunaModels;
} // namespace Client::Assets

namespace Client::Terrain
{
class DynamicWorldLight;
struct TerrainLighting;
} // namespace Client::Terrain

// Escena5: entrada F303, espera de gates y ciclos de lógica/dibujo sobre WorldState.
// La espera retiene la presentación, pero no interrumpe las fases posteriores a MoveHero.
// Las poses son globales entre escenas; los cuadros intermedios usan scratch y sprites proyectados.
// Los servicios o familias aún no portados devuelven su frontera después de conservar el prefijo.
// Contrato: research06, «La entrada al mundo», «El paso del juego» y «El dibujo del juego».
namespace Client::World
{
class KalimaWorld;
class Particles;
struct ObjectModelStepState;
struct WorldObject;
} // namespace Client::World

namespace Client::Models
{
class IModelLibrary;
}

namespace Client::Actors::Drawing
{
struct GameItemRenderState;
struct ItemDrawContext;
struct ItemSpecialDrawContext;
} // namespace Client::Actors::Drawing

namespace Client::Scenes
{
struct ArrowStepContext;
struct GameProjection;
struct GameSceneFrameProjection;
struct GameDrawTailState;

// 0x620db0(0, 0, w, 432), el pase del mundo del juego (0x637410): 480 con la vista de arriba
// (0x7d36899), que no se prende nunca.
inline constexpr Render::ViewportRect GameWorldViewport{0, 0, 640, 432};

// Lo que la entrada del chat lee de la carga (GameData): las palabras de Filter.bmd (0x622b40) y los conjuntos de
// ItemSetOption.bmd (la cola de los nombres de 0x56c440). Tienen que vivir más que la escena.
struct GameChatTables
{
    std::span<const std::string> filter{};
    // FilterName.bmd: lo que mira 0x622c70 en el nombre del gremio nuevo (la ventana 2 del gremio, 0x654f80).
    std::span<const std::string> filterName{};
    const Tables::ItemSetOptionTable* setOptions = nullptr;
    // ItemSetType.bmd e ItemSetOption.bmd enteras (0x40c600): las lee 0x550d10 al abrir el personaje (0x65ca1e) desde
    // el paso de la UI. nullptr si no se cargaron: 0x550d10 corta en 0x40e1b0.
    const Tables::ItemSetTables* itemSetTables = nullptr;
};
struct GameChatContext;
struct InventoryServices;

class GameScene final : public IScene
{
public:
    // `session`, `models`, `items` y `gameSlots` tienen que vivir más; nullptr sin servidor, sin los modelos
    // (el caballo oscuro corta con error si hay uno vivo, y el cuerpo de los personajes sin Player.bmd, en
    // 0x4f9580) o sin Item.bmd (*[0x7758550]: lo que lo lee corta con error). `gameSlots` es la tabla de los
    // slots de la carga (GameModels::TextureSlots), que siguen los modelos especiales del mapa (WorldModels);
    // sin ella, desde 0x69. Sin Text.bmd (`texts`), el HUD corta con error en el primer texto que dibuja. `chat` es lo
    // que lee la entrada del chat; vacío, sin filtro y sin los nombres de los conjuntos.
    GameScene(const SceneContext& context, App::ServerSession* session, const Models::IModelLibrary* models = nullptr,
              const Tables::ItemTable* items = nullptr, const Models::ModelTextureSlots* gameSlots = nullptr,
              const Tables::GateTable* gates = nullptr, const Tables::SkillTable* skills = nullptr,
              const Tables::TextTable* texts = nullptr, GameChatTables chat = {});

    ~GameScene() override;

    // Sin `context.world` o `context.cloth`, el error: la espera vive en el primero y sus cierres
    // gastan el rand() del segundo. Sube los cursores y las ventanas (WindowTextureSet) y las arenas del HUD
    // (SandTextureSet); una que falta, el error.
    Core::Expected<void, std::string> Enter() override;
    // 0x636290 y la tela. Después de la espera corre los personajes (0x508f00) y corta con error en la
    // primera etapa que MuRet no tiene; ahí la escena queda en Failure. Después de un corte, nada.
    void Step(const Input::InputState& input) override;
    // 0x63723a en adelante, con el frame no retenido: lo que MuRet tiene, en el orden del original,
    // hasta la primera etapa que no tiene. Ahí el frame se corta y la escena queda en Failure. Retenido,
    // o después de un corte, nada.
    void Render(Render::Frame::SceneCanvas& canvas, float alpha) override;
    // 0x6371a6..0x63722e: sin el mundo cargado retiene sin leer; con él, lee el contador y retiene
    // con más de 30.
    void PrepareFrame() override;
    // Suelta los tiles del terreno, los modelos de los objetos que subieron el dibujo y el paso, y las texturas
    // que subió Enter.
    void Leave() override;
    // Ninguna: sin conexión, el juego sigue (Q-C70).
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    // Lo que decidió el último PrepareFrame; antes del primero, retiene.
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // El corte del dibujo, con la dirección de lo que falta; vacío mientras no corte.
    [[nodiscard]] std::optional<std::string> Failure() const override;

    // El segmento del mouse que dejó el último dibujo (0x620480 en 0x7d366c0), para el picking de
    // MoveHero (CL7.4); vacío antes del primero.
    [[nodiscard]] const std::optional<Terrain::TerrainPickingSegment>& MouseRay() const;

private:
    // 0x6362b2..0x63693a: solo el primer paso de la escena.
    void StepEntry();
    // Los botones del mouse del WndProc y 0x623e70, los diálogos, antes de la lógica de la escena (GameSceneUi.cpp).
    [[nodiscard]] Core::Expected<void, std::string> StepTheDialogs(const Input::InputState& input);
    // 0x636ded..0x63705b: el bloque de la UI del paso.
    [[nodiscard]] Core::Expected<void, std::string> StepTheUi(const Input::InputState& input);
    // Lo que la entrada del chat lee y por dónde manda (App::ServerSession::SendChat).
    [[nodiscard]] GameChatContext ChatContext();
    // Lo que levantar, soltar y equipar piden afuera: los de SceneContext::worldEntry y el envío del 24
    // (App::ServerSession::SendItemMove).
    [[nodiscard]] InventoryServices InventoryContext();
    // 0x5fc260(1) y 0x5fac90 con el rayo del mouse (MoveHero y el C1 23): la celda del terreno bajo el mouse o nada.
    [[nodiscard]] Core::Expected<std::optional<Pathfinding::PathCell>, std::string> GroundUnderTheMouse();
    // El envío de las ventanas de los NPC por la sesión (App::ServerSession::SendNpcWindowPacket); sin ella, vacío.
    [[nodiscard]] static NpcWindowSender NpcWindowSenderOf(App::ServerSession* session);
    // 0x636da3..0x63717f: el paso con el mundo cargado, hasta su primer corte.
    [[nodiscard]] Core::Expected<void, std::string> StepWorld(const Input::InputState& input);
    [[nodiscard]] Core::Expected<void, std::string> StepTheFloorItems(float worldClock);
    // `controlDown`: Ctrl, para 0x558eb0 en la habilidad del héroe (CL9 hito 3).
    [[nodiscard]] Core::Expected<void, std::string> StepTheCharacters(float worldClock, bool controlDown);
    // `terrainHeight` es la altura de 0x5f93c0 que pide la mariposa 0xae del 54 (0x520dad).
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheModelEffects(float worldClock, const std::function<double(float, float)>& terrainHeight);
    [[nodiscard]] Core::Expected<void, std::string> StepTheEffects(float worldClock);
    // Lo que el paso y el choque de una flecha (0x5208a0, 0x520170) leen del frame.
    [[nodiscard]] ArrowStepContext ArrowContext(float worldClock,
                                                const std::function<double(float, float)>& terrainHeight);
    [[nodiscard]] Core::Expected<void, std::string> StepTheHero(const Input::InputState& input);
    [[nodiscard]] Core::Expected<void, std::string> StepTheFauna();
    [[nodiscard]] Core::Expected<void, std::string> StepTheLeaves();
    [[nodiscard]] const Terrain::WorldTerrain& StepTerrain() const;
    // The current map's terrain (Terrain::TerrainState::current); StepTerrain adds the selection's in map 54.
    [[nodiscard]] const Terrain::WorldTerrain& MapTerrain() const;
    // The dynamic light (Terrain::TerrainState::dynamicLight), the one the login and the selection leave.
    [[nodiscard]] Terrain::DynamicWorldLight& DynamicLight() const;
    // The models a creation loads (Assets::AssetsState::fauna), which outlive the map.
    [[nodiscard]] Assets::FaunaModels& LoadedFaunaModels() const;
    // 0x63706a: 0x606560 con los objetos vistos (StepGameWorldObjects); los demás mapas cortan si hay alguno.
    [[nodiscard]] Core::Expected<void, std::string> StepTheObjects();

    // 0x63723a: un frame completo del mundo, hasta una dependencia ausente si se alcanza.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheFrame(Render::Frame::SceneCanvas& canvas);
    // Los modelos de los objetos de `map` (0x616ec0, WorldModels) y sus texturas, que el original lee en
    // 0x61a560, al cargar; MuRet, la primera vez que el dibujo o el paso los piden.
    [[nodiscard]] Core::Expected<void, std::string> LoadTheObjectModels(std::int32_t map);
    // Los tiles y el pasto de `map` en la GPU, que el original sube en 0x61a560, al cargar.
    [[nodiscard]] Core::Expected<void, std::string> UploadTheTiles(std::uint8_t map);
    // 0x635ae0, una vez por frame original, y la consulta de los dibujos intermedios.
    [[nodiscard]] Render::Camera3D CameraOfTheFrame(Render::Frame::SceneCanvas& canvas, int frames);
    // 0x5fbb00 → 0x5fb570: el trapecio alrededor de `target`, con su transición si corre un frame. Con un
    // frame los dos quedan en WorldRuntime (TerrainPolygonFrame.h); en un dibujo intermedio, nada.
    [[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
    PolygonOfTheFrame(const Render::Camera3D& camera, const glm::vec3& target, std::int32_t worldWidth, int frames);
    // 0x637410: terreno, objetos, suelo, aves y personajes; después continúa la cola del frame.
    // `frames` son los frames del original del reloj: con 0, los objetos no sacan rand().
    [[nodiscard]] Core::Expected<void, std::string> DrawTheWorldPass(Render::Frame::SceneCanvas& canvas,
                                                                     const Render::Pass3D& pass,
                                                                     const Terrain::TerrainPolygon& polygon,
                                                                     int frames);

    [[nodiscard]] Core::Expected<void, std::string>
    DrawTheCharacters(Render::Frame::SceneCanvas& canvas, const Terrain::TerrainPolygon& polygon, int frames);
    [[nodiscard]] Core::Expected<void, std::string> DrawTheBirds(Render::Frame::SceneCanvas& canvas,
                                                                 const Terrain::TerrainPolygon& polygon, int frames);
    [[nodiscard]] Core::Expected<void, std::string> DrawTheTail(Render::Frame::SceneCanvas& canvas,
                                                                const Render::Pass3D& pass,
                                                                const Terrain::TerrainPolygon& polygon, int frames);
    // 0x637641..0x6376eb: el picking del frame y la pasada 2D: el HUD, las superposiciones y el cursor. `projection`
    // es 0x6203c0 del pase del mundo, que leen los globos (0x5a9050); nullptr si no se pudo armar.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheInterface(Render::Frame::SceneCanvas& canvas,
                                                                     const GameProjection* projection);
    // Los globales de 0x60b3d0 del frame, que comparten lo que cuelga de los personajes (0x509340) y los
    // ítems del piso (0x608a00): `item` apunta a `special`.
    void FillTheItemContexts(Actors::Drawing::ItemSpecialDrawContext& special, Actors::Drawing::ItemDrawContext& item,
                             Core::Duration frameClock, int frames);
    [[nodiscard]] FaunaDrawCarry& FramePoseCarry();
    [[nodiscard]] World::FrameSprites& FrameSpritesForDraw();
    void PrepareDrawProjection(int frames);
    void ObserveMapLoad();
    [[nodiscard]] GameDrawTailState& FrameTail();
    [[nodiscard]] Terrain::TerrainLighting& FrameLighting();
    [[nodiscard]] Models::ClothWorld& FrameCloth();
    [[nodiscard]] World::ObjectModelStepState& FrameModelState();
    [[nodiscard]] Actors::Drawing::GameItemRenderState& FrameItemRender();
    [[nodiscard]] World::ModelEffects& FrameEffects();
    [[nodiscard]] World::Particles& FrameParticles();
    [[nodiscard]] std::span<World::WorldObject> FrameObjects();
    [[nodiscard]] World::KalimaWorld* FrameKalima();
    [[nodiscard]] std::uint8_t ReadFrameHeroCell(bool x);
    [[nodiscard]] Core::Expected<void, std::string> DrawKalimaWater(Render::Frame::SceneCanvas& canvas);

    Camera::GameCamera& CameraState();
    Camera::GameCameraInput& CameraInputState();

    SceneContext m_context;
    App::ServerSession* m_session;
    const Models::IModelLibrary* m_models;
    const Tables::ItemTable* m_items;
    const Models::ModelTextureSlots* m_gameSlots;
    const Tables::GateTable* m_gates;
    const Tables::SkillTable* m_skills;
    const Tables::TextTable* m_texts;
    GameChatTables m_chatTables;
    // Las texturas de la escena: los cursores 2 y 3 con las ventanas, y las arenas del HUD.
    Assets::SceneTextures m_windowTextures;
    Assets::SceneTextures m_sandTextures;
    // Las marcas de los gremios (0x5f16a0), cada una en su slot de la tabla de la escena.
    GuildMarkTextures m_guildMarks;
    bool m_sent = false;
    bool m_firstStep = true;
    bool m_holdFrame = true;
    // La cámara y lo que lee, sin las de main (los tests que no las pasan).
    Camera::GameCamera m_localCamera;
    Camera::GameCameraInput m_localCameraInput;
    // Los frames del original, por los pasos de la escena (#307).
    OriginalFramesOfTheSteps m_cameraFrames;
    // 0x4eb620: un frame del original empieza con el primer paso después del dibujo que lo fue; el timeGetTime() del
    // anterior, para la espera de las skills (CL9 hito 4).
    bool m_frameStarts = true;
    std::optional<std::uint32_t> m_lastFrameStart;
    GroundEffectFrame m_groundEffects;
    World::FrameSprites m_sprites;
    World::FrameSprites m_spriteReplay;
    bool m_spriteReplayValid = false;
    World::FrameSprites m_projectedSprites;
    World::FrameSprites* m_frameSprites = nullptr;
    std::unique_ptr<GameSceneFrameProjection> m_projection;
    bool m_projectingFrame = false;
    FaunaDrawCarry m_projectedPoseCarry;
    FaunaDrawCarry* m_framePoseCarry = nullptr;
    Terrain::SphereFrustum m_frustum;
    Render::Camera3D m_drawnCamera;
    Terrain::TerrainPickingState m_picking;
    Core::PixelSize m_screen{640, 480};
    Assets::TerrainTiles m_tiles;
    // Los modelos de los objetos del mapa que los cargó, si alguno.
    Assets::WorldModels m_worldModels;
    std::optional<std::int32_t> m_worldModelsMap;
    std::optional<std::uint64_t> m_loadedRevision;
    // El rango de bloques del último 0x5fbb00 de la escena, el que lee 0x5fbf60 en el paso.
    Terrain::TerrainRange m_terrainRange;
    // El corrimiento del agua (0x78ecbec) y las olas (0x78acbd8) del juego.
    Terrain::TerrainWater m_water;
    std::optional<Terrain::TerrainPickingSegment> m_mouseRay;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
