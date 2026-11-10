#pragma once

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Render/Models/Animation.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/Pass3D.h"
#include "Scenes/IScene.h"
#include "Scenes/GroundEffectFrame.h"
#include "Scenes/CharacterCloth.h"
#include "World/ScepterGlow.h"
#include "World/Sprites.h"
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
#include <optional>
#include <string>
#include <vector>

// La escena 5: en su primer paso manda el F3 03 (0x636290, ServerSession::EnterWorld) y después espera
// el mundo. Mientras el contador de la espera pasa de 30, el dibujo (0x6371a0) no dibuja ni presenta y
// queda en pantalla el mosaico de la escena 3; el paso corre con lo que dejó la selección y MoveHero
// baja el contador (0x59af01). La respuesta del F3 03 lo pone en 30 (T7). Con el contador en 30 o
// menos, el dibujo del juego hace la cámara, el rango y el trapecio, el pase del mundo, el rayo del
// mouse, el terreno y, en el 0 y el 2, los objetos (0x6374ce, CL7 hito 3), los efectos del suelo
// (0x6374d3), los pájaros (0x6374d8, sin tabla) y los personajes (0x6374dd, con el TerrainWall de
// 0x42da60), y corta con error en la primera etapa que MuRet no tiene (T8): a lo sumo, en los ítems del
// piso (0x6374fe); el paso, los objetos que vio el dibujo (0x606560), el caballo oscuro (0x445230, también
// en la espera) y los personajes con su cuerpo (0x4fd5f0) y la zona segura del héroe (0x508f00, T11), y
// corta igual. El dibujo del caballo (0x637508) está portado (HelperTable.h, DrawHelpers) y queda detrás
// del corte de los ítems del piso. El trapecio que leen los personajes es el global de WorldRuntime: en el
// primer paso después de la espera, el que dejó el último dibujo de la selección (T11b).
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La entrada al mundo", pasos 3, 4 y 6, "El
// paso del juego", "El dibujo del juego" y "Los objetos del juego".
namespace Client::Scenes
{
class IModelLibrary;

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
              const Render::Models::ModelTextureSlots* gameSlots = nullptr);

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
    [[nodiscard]] Core::Expected<void, std::string> StepWorld();
    // 0x63706a: 0x606560 con los objetos vistos (StepGameWorldObjects); los demás mapas cortan si hay alguno.
    [[nodiscard]] Core::Expected<void, std::string> StepTheObjects();
    // 0x59ae8d..0x59afd6, lo de MoveHero con el contador: lo baja y lo vuelve a leer.
    [[nodiscard]] std::int32_t StepWaitCounter();

    // 0x63723a..0x6374fe: el dibujo hasta el corte. Devuelve siempre el error del corte.
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
    // 0x637410..0x6374fe, dentro del pase del mundo: el rayo del mouse, el terreno, los objetos, los efectos
    // del suelo, los personajes y el corte.
    // `frames` son los frames del original del reloj: con 0, los objetos no sacan rand().
    [[nodiscard]] Core::Expected<void, std::string> DrawTheWorldPass(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                                     const World::TerrainPolygon& polygon, int frames);

    [[nodiscard]] Core::Expected<void, std::string> DrawTheCharacters(SceneCanvas& canvas, int frames);

    World::GameCamera& CameraState();
    World::GameCameraInput& CameraInputState();

    SceneContext m_context;
    App::ServerSession* m_session;
    const IModelLibrary* m_models;
    const Data::Tables::ItemTable* m_items;
    const Render::Models::ModelTextureSlots* m_gameSlots;
    bool m_sent = false;
    bool m_firstStep = true;
    bool m_holdFrame = true;
    // La cámara y lo que lee, sin las de main (los tests que no las pasan).
    World::GameCamera m_localCamera;
    World::GameCameraInput m_localCameraInput;
    Render::Models::OriginalFrameCarry m_cameraFrames;
    GroundEffectFrame m_groundEffects;
    std::vector<CharacterCloth> m_characterCloths;
    std::vector<World::ScepterGlowCarry> m_scepters;
    World::FrameSprites m_sprites;
    TerrainTiles m_tiles;
    // Los modelos de los objetos del mapa que los cargó, si alguno.
    WorldModels m_worldModels;
    std::optional<std::int32_t> m_worldModelsMap;
    // El rango de bloques del último 0x5fbb00 de la escena, el que lee 0x5fbf60 en el paso.
    World::TerrainRange m_terrainRange;
    // El corrimiento del agua (0x78ecbec) y las olas (0x78acbd8) del juego.
    World::TerrainWater m_water;
    std::optional<World::TerrainPickingSegment> m_mouseRay;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
