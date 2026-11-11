#pragma once

#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/SkillTable.h"
#include "Scenes/FaunaDrawing.h"
#include "World/SphereFrustum.h"
#include "World/TerrainPickingState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Render/Models/Animation.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Pass3D.h"
#include "Scenes/IScene.h"
#include "Scenes/GroundEffectFrame.h"
#include "Scenes/CharacterCloth.h"
#include "World/Sprites.h"
#include "World/ModelEffects.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/WorldModels.h"
#include "World/GameCamera.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainPickingSegment.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <span>
#include <vector>

// Escena5: entrada F303, espera de gates y ciclos de lógica/dibujo sobre WorldState.
// La espera retiene la presentación, pero no interrumpe las fases posteriores a MoveHero.
// Las poses son globales entre escenas; los cuadros intermedios usan scratch y sprites proyectados.
// Los servicios o familias aún no portados devuelven su frontera después de conservar el prefijo.
// Contrato: research06, «La entrada al mundo», «El paso del juego» y «El dibujo del juego».
namespace Client::World
{
class KalimaWorld;
class Particles;
struct TerrainLighting;
struct ObjectModelStepState;
struct WorldObject;
} // namespace Client::World

namespace Client::Scenes
{
class IModelLibrary;
struct GameSceneFrameProjection;
struct GameDrawTailState;
struct GameItemRenderState;
struct ItemDrawContext;
struct ItemSpecialDrawContext;

// 0x620db0(0, 0, w, 432), el pase del mundo del juego (0x637410): 480 con la vista de arriba
// (0x7d36899), que no se prende nunca.
inline constexpr Render::ViewportRect GameWorldViewport{0, 0, 640, 432};

class GameScene final : public IScene
{
public:
    // `session`, `models`, `items` y `gameSlots` tienen que vivir más; nullptr sin servidor, sin los modelos
    // (el caballo oscuro corta con error si hay uno vivo, y el cuerpo de los personajes sin Player.bmd, en
    // 0x4f9580) o sin Item.bmd (*[0x7758550]: lo que lo lee corta con error). `gameSlots` es la tabla de los
    // slots de la carga (GameModels::TextureSlots), que siguen los modelos especiales del mapa (WorldModels);
    // sin ella, desde 0x69.
    GameScene(const SceneContext& context, App::ServerSession* session, const IModelLibrary* models = nullptr,
              const Data::Tables::ItemTable* items = nullptr,
              const Render::Models::ModelTextureSlots* gameSlots = nullptr,
              const Data::Tables::GateTable* gates = nullptr, const Data::Tables::SkillTable* skills = nullptr);

    ~GameScene() override;

    // Sin `context.world` o `context.cloth`, el error: la espera vive en el primero y sus cierres
    // gastan el rand() del segundo.
    Core::Expected<void, std::string> Enter() override;
    // 0x636290 y la tela. Después de la espera corre los personajes (0x508f00) y corta con error en la
    // primera etapa que MuRet no tiene; ahí la escena queda en Failure. Después de un corte, nada.
    void Step(const Input::InputState& input) override;
    // 0x63723a en adelante, con el frame no retenido: lo que MuRet tiene, en el orden del original,
    // hasta la primera etapa que no tiene. Ahí el frame se corta y la escena queda en Failure. Retenido,
    // o después de un corte, nada.
    void Render(SceneCanvas& canvas, float alpha) override;
    // 0x6371a6..0x63722e: sin el mundo cargado retiene sin leer; con él, lee el contador y retiene
    // con más de 30.
    void PrepareFrame() override;
    // Suelta los tiles del terreno y los modelos de los objetos que subieron el dibujo y el paso.
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
    [[nodiscard]] const std::optional<World::TerrainPickingSegment>& MouseRay() const;

private:
    // 0x6362b2..0x63693a: solo el primer paso de la escena.
    void StepEntry();
    // 0x636da3..0x63717f: el paso con el mundo cargado, hasta su primer corte.
    [[nodiscard]] Core::Expected<void, std::string> StepWorld(const Input::InputState& input);
    [[nodiscard]] Core::Expected<void, std::string> StepTheFloorItems(float worldClock);
    [[nodiscard]] Core::Expected<void, std::string> StepTheCharacters(float worldClock);
    // `terrainHeight` es la altura de 0x5f93c0 que pide la mariposa 0xae del 54 (0x520dad).
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheModelEffects(float worldClock, const std::function<double(float, float)>& terrainHeight);
    [[nodiscard]] Core::Expected<void, std::string> StepTheEffects(float worldClock);
    [[nodiscard]] Core::Expected<void, std::string> StepTheHero(const Input::InputState& input);
    [[nodiscard]] Core::Expected<void, std::string> StepTheFauna();
    [[nodiscard]] Core::Expected<void, std::string> StepTheLeaves();
    [[nodiscard]] const World::WorldTerrain& StepTerrain() const;
    // 0x63706a: 0x606560 con los objetos vistos (StepGameWorldObjects); los demás mapas cortan si hay alguno.
    [[nodiscard]] Core::Expected<void, std::string> StepTheObjects();

    // 0x63723a: un frame completo del mundo, hasta una dependencia ausente si se alcanza.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheFrame(SceneCanvas& canvas);
    // Los modelos de los objetos de `map` (0x616ec0, WorldModels) y sus texturas, que el original lee en
    // 0x61a560, al cargar; MuRet, la primera vez que el dibujo o el paso los piden.
    [[nodiscard]] Core::Expected<void, std::string> LoadTheObjectModels(std::int32_t map);
    // Los tiles y el pasto de `map` en la GPU, que el original sube en 0x61a560, al cargar.
    [[nodiscard]] Core::Expected<void, std::string> UploadTheTiles(std::uint8_t map);
    // 0x635ae0, una vez por frame original, y la consulta de los dibujos intermedios.
    [[nodiscard]] Render::Camera3D CameraOfTheFrame(SceneCanvas& canvas, int frames);
    // 0x5fbb00 → 0x5fb570: el trapecio alrededor de `target`, con su transición si corre un frame. Con un
    // frame los dos quedan en WorldRuntime (TerrainPolygonFrame.h); en un dibujo intermedio, nada.
    [[nodiscard]] Core::Expected<World::TerrainPolygon, std::string>
    PolygonOfTheFrame(const Render::Camera3D& camera, const glm::vec3& target, int frames);
    // 0x637410: terreno, objetos, suelo, aves y personajes; después continúa la cola del frame.
    // `frames` son los frames del original del reloj: con 0, los objetos no sacan rand().
    [[nodiscard]] Core::Expected<void, std::string> DrawTheWorldPass(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                                     const World::TerrainPolygon& polygon, int frames);

    [[nodiscard]] Core::Expected<void, std::string> DrawTheCharacters(SceneCanvas& canvas, int frames);
    [[nodiscard]] Core::Expected<void, std::string> DrawTheBirds(SceneCanvas& canvas,
                                                                 const World::TerrainPolygon& polygon, int frames);
    [[nodiscard]] Core::Expected<void, std::string> DrawTheTail(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                                const World::TerrainPolygon& polygon, int frames);
    // Los globales de 0x60b3d0 del frame, que comparten lo que cuelga de los personajes (0x509340) y los
    // ítems del piso (0x608a00): `item` apunta a `special`.
    void FillTheItemContexts(ItemSpecialDrawContext& special, ItemDrawContext& item, Core::Duration frameClock,
                             int frames);
    [[nodiscard]] FaunaDrawCarry& FramePoseCarry();
    [[nodiscard]] World::FrameSprites& FrameSpritesForDraw();
    void PrepareDrawProjection(int frames);
    void ObserveMapLoad();
    [[nodiscard]] GameDrawTailState& FrameTail();
    [[nodiscard]] World::TerrainLighting& FrameLighting();
    [[nodiscard]] Render::Models::ClothWorld& FrameCloth();
    [[nodiscard]] World::ObjectModelStepState& FrameModelState();
    [[nodiscard]] GameItemRenderState& FrameItemRender();
    [[nodiscard]] World::ModelEffects& FrameEffects();
    [[nodiscard]] World::Particles& FrameParticles();
    [[nodiscard]] std::span<World::WorldObject> FrameObjects();
    [[nodiscard]] World::KalimaWorld* FrameKalima();
    [[nodiscard]] std::uint8_t ReadFrameHeroCell(bool x);
    [[nodiscard]] Core::Expected<void, std::string> DrawKalimaWater(SceneCanvas& canvas);

    World::GameCamera& CameraState();
    World::GameCameraInput& CameraInputState();

    SceneContext m_context;
    App::ServerSession* m_session;
    const IModelLibrary* m_models;
    const Data::Tables::ItemTable* m_items;
    const Render::Models::ModelTextureSlots* m_gameSlots;
    const Data::Tables::GateTable* m_gates;
    const Data::Tables::SkillTable* m_skills;
    bool m_sent = false;
    bool m_firstStep = true;
    bool m_holdFrame = true;
    // La cámara y lo que lee, sin las de main (los tests que no las pasan).
    World::GameCamera m_localCamera;
    World::GameCameraInput m_localCameraInput;
    Render::Models::OriginalFrameCarry m_cameraFrames;
    GroundEffectFrame m_groundEffects;
    std::vector<CharacterCloth> m_characterCloths;
    World::FrameSprites m_sprites;
    World::FrameSprites m_spriteReplay;
    bool m_spriteReplayValid = false;
    World::FrameSprites m_projectedSprites;
    World::FrameSprites* m_frameSprites = nullptr;
    std::unique_ptr<GameSceneFrameProjection> m_projection;
    bool m_projectingFrame = false;
    FaunaDrawCarry m_projectedPoseCarry;
    FaunaDrawCarry* m_framePoseCarry = nullptr;
    World::SphereFrustum m_frustum;
    Render::Camera3D m_drawnCamera;
    World::TerrainPickingState m_picking;
    App::PixelSize m_screen{640, 480};
    TerrainTiles m_tiles;
    // Los modelos de los objetos del mapa que los cargó, si alguno.
    WorldModels m_worldModels;
    std::optional<std::int32_t> m_worldModelsMap;
    std::optional<std::uint64_t> m_loadedRevision;
    // El rango de bloques del último 0x5fbb00 de la escena, el que lee 0x5fbf60 en el paso.
    World::TerrainRange m_terrainRange;
    // El corrimiento del agua (0x78ecbec) y las olas (0x78acbd8) del juego.
    World::TerrainWater m_water;
    std::optional<World::TerrainPickingSegment> m_mouseRay;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
