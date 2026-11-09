#pragma once

#include "App/ServerSession.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Render/Models/Animation.h"
#include "Scenes/SceneTextures.h"
#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"
#include <vector>

#include <optional>
#include <string>

// Escena5: entrada desde el actor durable, paso de mundo y cámara/render desde el estado canónico.
// Las dependencias de aplicación y dibujo que faltan se propagan por Failure antes de Present.
// Dominio del caller Leaf: hero debe designar almacenamiento válido de WorldState.Actors durante
// sus lecturas. La identidad puede cambiar dentro de esa ventana; C3 la resuelve tres veces.
// El índice inicialmente fuera de ventana viola una precondición de seguridad del port: su rechazo
// previo a la composición no replica un fault nativo ni está cubierto por las garantías de prefijo530490.
namespace Client::Scenes
{
class IModelLibrary;
struct GameData;

class GameScene final : public IScene
{
public:
    // `session` tiene que vivir más; nullptr sin servidor.
    GameScene(const SceneContext& context, App::ServerSession* session, const IModelLibrary* models = nullptr,
              const GameData* data = nullptr);

    Core::Expected<void, std::string> Enter() override;
    void Step(const Input::InputState& input) override;
    void Render(SceneCanvas& canvas, float alpha) override;
    void PrepareFrame() override;
    [[nodiscard]] std::optional<std::string> Failure() const override;
    void Leave() override;
    // Ninguna: sin conexión, el juego sigue (Q-C70).
    [[nodiscard]] std::optional<SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;

private:
    void SendEntryRequest();
    void StepWorld(const Input::InputState& input);
    [[nodiscard]] bool ShouldStepLeaves() const;
    void StepLeaves();
    void StepParticles();
    void DrawParticles(SceneCanvas& canvas);
    void StepHeroPrefix(const Input::InputState& input);
    void ProcessHeroInput(const Input::InputState& input);
    void UpdateHeroHeadTarget(const Input::InputState& input);
    void CheckGates();
    void UseGroundClick(const Input::InputState& input);
    void RefreshHeroTerrainLayer();
    void RebuildWalls();
    void StepEffects();
    void DrawEffects(SceneCanvas& canvas, float alpha);
    SceneContext m_context;
    App::ServerSession* m_session;
    const IModelLibrary* m_models;
    const GameData* m_data;
    std::optional<std::string> m_failure;
    SceneTextures m_cursorTextures;
    Render::Models::OriginalFrameCarry m_cameraFrames;
    App::PixelSize m_screen{640, 480};
    World::TerrainRange m_range;
    World::TerrainPolygon m_polygon{.everything = true};
    std::vector<std::optional<int>> m_objectRandoms;
    bool m_holdFrame = true;
    bool m_sent = false;
    bool m_firstStep = true;
};
} // namespace Client::Scenes
