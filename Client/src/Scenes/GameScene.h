#pragma once

#include "App/ServerSession.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/Models/Animation.h"
#include "Render/Pass3D.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/TerrainTiles.h"
#include "World/GameCamera.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include "World/TerrainPickingSegment.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <string>

// La escena 5: en su primer paso manda el F3 03 (0x636290, ServerSession::EnterWorld) y después espera
// el mundo. Mientras el contador de la espera pasa de 30, el dibujo (0x6371a0) no dibuja ni presenta y
// queda en pantalla el mosaico de la escena 3; el paso corre con lo que dejó la selección y MoveHero
// baja el contador (0x59af01). La respuesta del F3 03 lo pone en 30 (T7). Con el contador en 30 o
// menos, el dibujo del juego hace la cámara, el rango y el trapecio, el pase del mundo, el rayo del
// mouse y el primer pase del terreno, y corta con error en la primera etapa que MuRet no tiene (T8);
// el paso, los personajes con la zona segura del héroe (0x508f00, T11), y corta igual.
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "La entrada al mundo", pasos 3, 4 y 6, "El
// paso del juego" y "El dibujo del juego".
namespace Client::Scenes
{
// 0x620db0(0, 0, w, 432), el pase del mundo del juego (0x637410): 480 con la vista de arriba
// (0x7d36899), que no se prende nunca.
inline constexpr Render::ViewportRect GameWorldViewport{0, 0, 640, 432};

class GameScene final : public IScene
{
public:
    // `session` tiene que vivir más; nullptr sin servidor.
    GameScene(const SceneContext& context, App::ServerSession* session);

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
    // Suelta los tiles del terreno que subió el dibujo.
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
    // 0x59ae8d..0x59afd6, lo de MoveHero con el contador: lo baja y lo vuelve a leer.
    [[nodiscard]] std::int32_t StepWaitCounter();

    // 0x63723a..0x6374ce: el dibujo hasta el corte. Devuelve siempre el error del corte.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheFrame(SceneCanvas& canvas);
    // Los tiles de `map` en la GPU, que el original sube en 0x61a560, al cargar.
    [[nodiscard]] Core::Expected<void, std::string> UploadTheTiles(std::uint8_t map);
    // 0x635ae0, una vez por frame original, y la consulta de los dibujos intermedios.
    [[nodiscard]] Render::Camera3D CameraOfTheFrame(SceneCanvas& canvas, int frames);
    // 0x5fbb00 → 0x5fb570: el trapecio alrededor de `target`, con su transición si corre un frame.
    [[nodiscard]] Core::Expected<World::TerrainPolygon, std::string>
    PolygonOfTheFrame(const Render::Camera3D& camera, const glm::vec3& target, int frames);
    // 0x637410..0x6374ce, dentro del pase del mundo: el rayo del mouse, el terreno y el corte.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheWorldPass(SceneCanvas& canvas, const Render::Pass3D& pass,
                                                                     const World::TerrainPolygon& polygon);

    World::GameCamera& CameraState();
    World::GameCameraInput& CameraInputState();

    SceneContext m_context;
    App::ServerSession* m_session;
    bool m_sent = false;
    bool m_firstStep = true;
    bool m_holdFrame = true;
    // La cámara y lo que lee, sin las de main (los tests que no las pasan).
    World::GameCamera m_localCamera;
    World::GameCameraInput m_localCameraInput;
    Render::Models::OriginalFrameCarry m_cameraFrames;
    TerrainTiles m_tiles;
    // 0x7b4ccb8, el nivel efectivo del trapecio, de un frame al otro.
    World::TerrainPolygonState m_polygonState;
    // El rango de bloques del último 0x5fbb00 de la escena, el que lee 0x5fbf60 en el paso.
    World::TerrainRange m_terrainRange;
    // El corrimiento del agua (0x78ecbec) y las olas (0x78acbd8) del juego.
    World::TerrainWater m_water;
    std::optional<World::TerrainPickingSegment> m_mouseRay;
    // El trapecio del último frame del juego que dibujó (0x5fb570), el que lee 0x5fba90 en el paso.
    std::optional<World::TerrainPolygon> m_lastPolygon;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
