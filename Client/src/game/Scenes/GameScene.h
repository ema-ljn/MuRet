#pragma once

#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"
#include "Session/ServerSession.h"
#include "Models/SkinningBuffers.h"
#include "Core/PixelSize.h"
#include "Tables/GateTable.h"
#include "Tables/ItemSetTables.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Pathfinding/PathCell.h"
#include "Terrain/SphereFrustum.h"
#include "Terrain/TerrainPickingState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Models/ModelTextureSlots.h"
#include "Render/Pass3D.h"
#include "Scenes/IScene.h"
#include "Npc/NpcWindows.h"
#include "Scenes/OriginalFramesOfTheSteps.h"
#include "Effects/GroundEffectFrame.h"
#include "Guild/GuildMarks.h"
#include "Kernel/PoseBuffer.h"
#include "Particles/Sprites.h"
#include "Effects/ModelEffects.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
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
struct WorldTerrain;
} // namespace Client::Terrain

namespace Client::Kernel
{
class KalimaWater;
} // namespace Client::Kernel

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

// Scene 5: the F303 entry, the gates wait and the logic/draw cycles over WorldState. The wait holds the presentation,
// but does not interrupt the phases after MoveHero. The poses are global across scenes; the intermediate frames use
// scratch and projected sprites. The services or families not ported yet return their boundary after keeping the
// prefix. Contract: research 06, «La entrada al mundo», «El paso del juego» and «El dibujo del juego».
namespace Client::Models
{
struct ClothWorld;
class IModelLibrary;
} // namespace Client::Models

namespace Client::Actors::Drawing
{
struct GameItemRenderState;
struct ItemDrawContext;
struct ItemSpecialDrawContext;
} // namespace Client::Actors::Drawing

namespace Client::Chat
{
struct GameChatContext;
} // namespace Client::Chat

namespace Client::Effects
{
struct ArrowStepContext;
} // namespace Client::Effects

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Kernel
{
struct GameDrawTailState;
struct GameProjection;
struct ObjectModelStepState;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct GameSceneFrameProjection;

// 0x620db0(0, 0, w, 432), the game's world pass (0x637410): 480 with the top view (0x7d36899), which never turns on.
inline constexpr Render::ViewportRect GameWorldViewport{0, 0, 640, 432};

// What the chat input reads from the load (GameData): the Filter.bmd words (0x622b40) and the ItemSetOption.bmd sets
// (the tail of 0x56c440's names). They must outlive the scene.
struct GameChatTables
{
    std::span<const std::string> filter{};
    // FilterName.bmd: what 0x622c70 looks at in the new guild's name (guild window 2, 0x654f80).
    std::span<const std::string> filterName{};
    const Tables::ItemSetOptionTable* setOptions = nullptr;
    // ItemSetType.bmd and ItemSetOption.bmd whole (0x40c600): 0x550d10 reads them on opening the character (0x65ca1e)
    // from the UI step. nullptr if they were not loaded: 0x550d10 stops at 0x40e1b0.
    const Tables::ItemSetTables* itemSetTables = nullptr;
};

class GameScene final : public IScene
{
public:
    // `session`, `models`, `items` and `gameSlots` must outlive it; nullptr without a server, without the models (the
    // dark horse stops with an error if one is alive, and the characters' body without Player.bmd, at 0x4f9580) or
    // without Item.bmd (*[0x7758550]: what reads it stops with an error). `gameSlots` is the load's slot table
    // (GameModels::TextureSlots), which the map's special models follow (WorldModels); without it, from 0x69. Without
    // Text.bmd (`texts`), the HUD stops with an error at the first text it draws. `chat` is what the chat input reads;
    // empty, without a filter and without the set names.
    GameScene(const SceneContext& context, App::ServerSession* session, const Models::IModelLibrary* models = nullptr,
              const Tables::ItemTable* items = nullptr, const Models::ModelTextureSlots* gameSlots = nullptr,
              const Tables::GateTable* gates = nullptr, const Tables::SkillTable* skills = nullptr,
              const Tables::TextTable* texts = nullptr, GameChatTables chat = {});

    ~GameScene() override;

    // Without `context.world` or `context.cloth`, the error: the wait lives in the first and its closes spend the
    // second's rand(). It uploads the cursors and the windows (WindowTextureSet) and the HUD sands (SandTextureSet); a
    // missing one, the error.
    Core::Expected<void, std::string> Enter() override;
    // 0x636290 and the cloth. After the wait it runs the characters (0x508f00) and stops with an error at the first
    // stage MuRet does not have; there the scene stays in Failure. After a cut, nothing.
    void Step(const Input::InputState& input) override;
    // 0x63723a onward, with the frame not held: what MuRet has, in the original's order, up to the first stage it does
    // not have. There the frame stops and the scene stays in Failure. Held, or after a cut, nothing.
    void Render(Render::Frame::SceneCanvas& canvas, float alpha) override;
    // 0x6371a6..0x63722e: without the world loaded it holds without reading; with it, it reads the counter and holds
    // with more than 30.
    void PrepareFrame() override;
    // Releases the terrain tiles, the object models the draw and the step uploaded, and the textures Enter uploaded.
    void Leave() override;
    // None: without a connection, the game goes on (Q-C70).
    [[nodiscard]] std::optional<Kernel::SceneId> Next() const override;
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    // What the last PrepareFrame decided; before the first, it holds.
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // The draw's cut, with the address of what is missing; empty while it does not stop.
    [[nodiscard]] std::optional<std::string> Failure() const override;

    // The mouse segment the last draw left (0x620480 into 0x7d366c0), for MoveHero's picking (CL7.4); empty before the
    // first.
    [[nodiscard]] const std::optional<Terrain::TerrainPickingSegment>& MouseRay() const;

private:
    // 0x6362b2..0x63693a: only the scene's first step.
    void StepEntry();
    // The WndProc's mouse buttons and 0x623e70, the dialogs, before the scene's logic (GameSceneUi.cpp).
    [[nodiscard]] Core::Expected<void, std::string> StepTheDialogs(const Input::InputState& input);
    // 0x636ded..0x63705b: the step's UI block.
    [[nodiscard]] Core::Expected<void, std::string> StepTheUi(const Input::InputState& input);
    // What the chat input reads and where it sends through (App::ServerSession::SendChat).
    [[nodiscard]] Chat::GameChatContext ChatContext();
    // What picking up, dropping and equipping ask for outside: those of SceneContext::worldEntry and sending the 24
    // (App::ServerSession::SendItemMove).
    [[nodiscard]] Items::InventoryServices InventoryContext();
    // 0x5fc260(1) and 0x5fac90 with the mouse ray (MoveHero and the C1 23): the terrain cell under the mouse or
    // nothing.
    [[nodiscard]] Core::Expected<std::optional<Pathfinding::PathCell>, std::string> GroundUnderTheMouse();
    // Sending the NPC windows through the session (App::ServerSession::SendNpcWindowPacket); without it, empty.
    [[nodiscard]] static Npc::NpcWindowSender NpcWindowSenderOf(App::ServerSession* session);
    // 0x636da3..0x63717f: the step with the world loaded, up to its first cut.
    [[nodiscard]] Core::Expected<void, std::string> StepWorld(const Input::InputState& input);
    [[nodiscard]] Core::Expected<void, std::string> StepTheFloorItems(float worldClock);
    // `controlDown`: Ctrl, for 0x558eb0 in the hero's skill (CL9 milestone 3).
    [[nodiscard]] Core::Expected<void, std::string> StepTheCharacters(float worldClock, bool controlDown);
    // `terrainHeight` is the 0x5f93c0 height the 54's butterfly 0xae asks for (0x520dad).
    [[nodiscard]] Core::Expected<void, std::string>
    StepTheModelEffects(float worldClock, const std::function<double(float, float)>& terrainHeight);
    [[nodiscard]] Core::Expected<void, std::string> StepTheEffects(float worldClock);
    // What an arrow's step and hit (0x5208a0, 0x520170) read from the frame.
    [[nodiscard]] Effects::ArrowStepContext ArrowContext(float worldClock,
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
    // 0x63706a: 0x606560 with the seen objects (StepGameWorldObjects); the other maps stop if there is one.
    [[nodiscard]] Core::Expected<void, std::string> StepTheObjects();

    // 0x63723a: a whole frame of the world, up to a missing dependency if one is reached.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheFrame(Render::Frame::SceneCanvas& canvas);
    // The object models of `map` (0x616ec0, WorldModels) and their textures, which the original reads in 0x61a560, on
    // loading; MuRet, the first time the draw or the step asks for them.
    [[nodiscard]] Core::Expected<void, std::string> LoadTheObjectModels(std::int32_t map);
    // The tiles and the grass of `map` on the GPU, which the original uploads in 0x61a560, on loading.
    [[nodiscard]] Core::Expected<void, std::string> UploadTheTiles(std::uint8_t map);
    // 0x635ae0, once per original frame, and the intermediate draws' query.
    [[nodiscard]] Render::Camera3D CameraOfTheFrame(Render::Frame::SceneCanvas& canvas, int frames);
    // 0x5fbb00 → 0x5fb570: the trapezoid around `target`, with its transition if a frame runs. With a frame both stay
    // in WorldRuntime (TerrainPolygonFrame.h); in an intermediate draw, nothing.
    [[nodiscard]] Core::Expected<Terrain::TerrainPolygon, std::string>
    PolygonOfTheFrame(const Render::Camera3D& camera, const glm::vec3& target, std::int32_t worldWidth, int frames);
    // 0x637410: terrain, objects, ground, birds and characters; then the frame's tail continues. `frames` are the
    // clock's original frames: with 0, the objects take no rand().
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
    // 0x637641..0x6376eb: the frame's picking and the 2D pass: the HUD, the overlays and the cursor. `projection` is
    // the world pass's 0x6203c0, which the bubbles read (0x5a9050); nullptr if it could not be built.
    [[nodiscard]] Core::Expected<void, std::string> DrawTheInterface(Render::Frame::SceneCanvas& canvas,
                                                                     const Kernel::GameProjection* projection);
    // The frame's 0x60b3d0 globals, shared by what hangs from the characters (0x509340) and the floor items (0x608a00):
    // `item` points at `special`.
    void FillTheItemContexts(Actors::Drawing::ItemSpecialDrawContext& special, Actors::Drawing::ItemDrawContext& item,
                             Core::Duration frameClock, int frames);
    [[nodiscard]] Kernel::PoseBuffer& FramePoseCarry();
    [[nodiscard]] Particles::FrameSprites& FrameSpritesForDraw();
    void PrepareDrawProjection(int frames);
    void ObserveMapLoad();
    [[nodiscard]] Kernel::GameDrawTailState& FrameTail();
    [[nodiscard]] Terrain::TerrainLighting& FrameLighting();
    [[nodiscard]] Models::ClothWorld& FrameCloth();
    [[nodiscard]] Kernel::ObjectModelStepState& FrameModelState();
    [[nodiscard]] Actors::Drawing::GameItemRenderState& FrameItemRender();
    [[nodiscard]] Effects::ModelEffects& FrameEffects();
    [[nodiscard]] Particles::Particles& FrameParticles();
    [[nodiscard]] std::span<Kernel::WorldObject> FrameObjects();
    [[nodiscard]] Kernel::KalimaWater* FrameKalima();
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
    // The scene's textures: cursors 2 and 3 with the windows, and the HUD sands.
    Assets::SceneTextures m_windowTextures;
    Assets::SceneTextures m_sandTextures;
    // The guild marks (0x5f16a0), each in its slot of the scene's table.
    Guild::GuildMarkTextures m_guildMarks;
    bool m_sent = false;
    bool m_firstStep = true;
    bool m_holdFrame = true;
    // The camera and what it reads, without main's (the tests that do not pass them).
    Camera::GameCamera m_localCamera;
    Camera::GameCameraInput m_localCameraInput;
    // The original frames, by the scene's steps (#307).
    OriginalFramesOfTheSteps m_cameraFrames;
    // 0x4eb620: an original frame starts with the first step after the draw that was one; the previous one's
    // timeGetTime(), for the skills' wait (CL9 milestone 4).
    bool m_frameStarts = true;
    std::optional<std::uint32_t> m_lastFrameStart;
    Effects::GroundEffectFrame m_groundEffects;
    Particles::FrameSprites m_sprites;
    Particles::FrameSprites m_spriteReplay;
    bool m_spriteReplayValid = false;
    Particles::FrameSprites m_projectedSprites;
    Particles::FrameSprites* m_frameSprites = nullptr;
    std::unique_ptr<GameSceneFrameProjection> m_projection;
    bool m_projectingFrame = false;
    Kernel::PoseBuffer m_projectedPoseCarry;
    Kernel::PoseBuffer* m_framePoseCarry = nullptr;
    Models::SkinningBuffers m_characterBuffers;
    Session::CharacterDrawHooks m_characterDrawHooks;
    Effects::ActorEffectHooks m_effectHooks;
    Terrain::SphereFrustum m_frustum;
    Render::Camera3D m_drawnCamera;
    Terrain::TerrainPickingState m_picking;
    Core::PixelSize m_screen{640, 480};
    Assets::TerrainTiles m_tiles;
    // The object models of the map that loaded them, if any.
    Assets::WorldModels m_worldModels;
    std::optional<std::int32_t> m_worldModelsMap;
    std::optional<std::uint64_t> m_loadedRevision;
    // The block range of the scene's last 0x5fbb00, which 0x5fbf60 reads in the step.
    Terrain::TerrainRange m_terrainRange;
    // The game's water shift (0x78ecbec) and waves (0x78acbd8).
    Terrain::TerrainWater m_water;
    std::optional<Terrain::TerrainPickingSegment> m_mouseRay;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
