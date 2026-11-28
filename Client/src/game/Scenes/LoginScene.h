#pragma once

#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Models/IModelLibrary.h"
#include "Render/Pass3D.h"
#include "Camera/GameCamera.h"

#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"
#include "Session/ServerSession.h"
#include "Session/SessionState.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Kernel/CharacterAppearance.h"
#include "Input/InputState.h"
#include "Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/CharacterHead.h"
#include "Kernel/HelperTable.h"
#include "Actors/FullSetScan.h"
#include "Tables/GameData.h"
#include "Scenes/IScene.h"
#include "Assets/LoadedWorld.h"
#include "Session/ModuleStates.h"
#include "Terrain/DynamicWorldLight.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainLighting.h"
#include "Weather/Leaves.h"
#include "Particles/ParticleDrawing.h"
#include "Particles/Sprites.h"
#include "Kernel/WorldObject.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Render/Frame/SceneWorld.h"
#include "FrontEnd/LoginModels.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"
#include "Assets/WorldModels.h"
#include "Kernel/WorldState.h"
#include "FrontEnd/CreditsWindow.h"
#include "FrontEnd/EscapeWindow.h"
#include "FrontEnd/LoginWindow.h"
#include "Ui/MenuBar.h"
#include "Ui/NewUi.h"
#include "Ui/NoticeWindow.h"
#include "Ui/Notices.h"
#include "FrontEnd/OptionsWindow.h"
#include "FrontEnd/ServerListWindow.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Kernel
{
struct PoseBuffer;
} // namespace Client::Kernel

// Scene 2: the login of 0x634c60, entered with 0x6349a0 and drawn with 0x6350b0 (06 § El login, al entrar and § El
// login, el dibujo), with the logo and the character (FrontEnd/LoginModels.h) and without the music. With the
// session, its UI: the server list while the session waits for the choice, with the bottom bar, which opens with it
// and stays until leaving; the account while it waits for account and password, and the notices of 06 § Los avisos.
// The session says which window goes: the scene checks it on each step and, with what the player asks, calls it.
//
// Each step handles only the top window (the notice, the account or the list): so the Enter that closes one does not
// reach the one that opens in the same step. Without a notice, also the bar, whose buttons do not overlap the
// account's or the list's (Q-C84). The OK of notices 0x1a, 0x1c, 0x6f and 0x70 closes the game (Q-C69): the scene
// says so in WantsToQuit.
namespace Client::Scenes
{
// The three bottom texts, by their index in Text.bmd.
inline constexpr std::size_t LoginLeftText = 454;
inline constexpr std::size_t LoginRightText = 455;
inline constexpr std::size_t LoginVersionText = 456;

// The 456 with the version, like 0x635464's sprintf but without using the text as a format (Q-C61): the first "%s"
// becomes `version` and the rest stays as is, other "%" too. Without "%s", the text as is.
[[nodiscard]] std::string PlaceVersion(std::string_view text, std::string_view version);

class LoginScene final : public IScene
{
public:
    // `data`, `models` and `session` must outlive the scene. `session` is nullptr without a server (Main without host):
    // the scene draws and does not connect. `version` is MuRet's (MU_VERSION). `models` has the character's
    // (GameModels). `seed` is the one of the rand() of the character's action (0x634c60) without the context's
    // ClothWorld, which brings the CRT's rand() that Main uses: the tests fix it. `gameSlots` is the loading's slot
    // table (GameModels::TextureSlots), which the world continues from 0x41c like the original's global; without it, an
    // empty one.
    LoginScene(const SceneContext& context, const Tables::GameData& data, const Models::IModelLibrary& models,
               App::ServerSession* session, std::string_view version, std::mt19937::result_type seed,
               const Models::ModelTextureSlots* gameSlots = nullptr);

    // The windows keep this (the size of the textures): it is neither copied nor moved.
    LoginScene(const LoginScene&) = delete;
    LoginScene& operator=(const LoginScene&) = delete;
    ~LoginScene() override;

    // Like 0x6349a0, on each entry: world 55's terrain (0x61a560, LoadedWorld; without it, the login goes without
    // terrain and the terrain light is 0), its objects (0x616ec0, WorldModels and the .obj; without them, only the
    // logo), LoginTextureSet (0x61c7c0), the logo and its textures (from the next slot the world leaves),
    // WindowTextureSet (from 0x61ccb0), the ConnectServer (0x4c1340, ServerSession::Start), the logo and the character
    // in place and the route count at −1. A missing texture stops before connecting; an unreadable Logo03.bmd does not
    // (0x60da30 skips it).
    Core::Expected<void, std::string> Enter() override;
    // 0x634c60: the character's action (with the context's clock), the objects and the logo (0x606560), the character
    // (0x508f00) and the routes (0x633920); then the UI in the step (Q-C81): what the session asks for, the top window
    // and, without a notice, the bar. The SceneManager runs the session before.
    void Step(const Input::InputState& input) override;
    // 0x6350b0 without the fade (Q-C66) or what is common to the scenes: in the 3D pass, the terrain, the character
    // (0x510d00), the horse and the objects with the logo (0x6046a0), except with the credits open; at the end the UI,
    // each window with the normal blend.
    void Render(Render::Frame::SceneCanvas& canvas, float alpha) override;
    // Closes the windows (the account's clears the password) and releases their slots, the world's, the logo's and the
    // credits' illustrations.
    void Leave() override;
    // The selection with the login accepted (SessionState::LoggedIn), like 0x634dce with the 0x14.
    [[nodiscard]] std::optional<Kernel::SceneId> Next() const override;
    // With the OK of a notice that closes the game (Q-C69).
    [[nodiscard]] bool WantsToQuit() const override;
    // With the account window open: it is the top one, because a notice closes it on opening.
    [[nodiscard]] bool WantsTextInput() const override;
    // While the session connects to the CS or the GS, and until a scene step saw the current state: the original uses a
    // blocking connect (0x4c1340, which calls 0x4e8570: to the CS from 0x6349a0, before the login's first frame, and
    // from cancel, 0x642837; to the GS from 0x4e3396), and the last frame stays on screen until it ends (Q-C85). So the
    // first frame drawn after it brings what the step opened. Until the step and not only while connecting: a connect
    // that fails right away comes out in the Poll, which can come without a step before the draw (after Enter, or in a
    // frame without steps).
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // During a connect, discards the undrawn time of the object effects, the cloth and the scepter: the next draw runs
    // one frame of each. Keeps the pool, the samples, the cloths and the other clocks.
    void OnFrameHeld() override;
    // The character's head as the last step left it (0x505de0), the one it draws.
    [[nodiscard]] const Kernel::CharacterHead& Head() const;
    [[nodiscard]] std::optional<std::string> Failure() const override;
    // The character the frame draws with `alpha` (0x510d00 → 0x50ba30): the body between steps, the step's head, the
    // terrain light at its place plus its own, and its cloth and the scene's sprite table, at the context's pace. What
    // hangs (the scepter with its glow) goes apart, with DrawCharacterTail.
    [[nodiscard]] Actors::Drawing::DrawnCharacter TheCharacterToDraw(float alpha);
    // 0x505820 with the drawn character, `frames` times (Actors/FullSetScan.h), on WorldState: what the last one
    // returns, which reads the full set from the effect tail.
    Actors::FullSetScan ScanTheCharacter(const Kernel::CharacterAppearance& appearance, int frames);

private:
    // 0x60da30 and 0x60d5f0 with the 0xa2 (LoginLogoFile), without a 0x60d530 before (Q-C92).
    [[nodiscard]] Core::Expected<void, std::string> LoadTheLogo();
    void LoadTheWorld();
    void LoadTheTerrain();
    void RetainTheTerrain();
    void LoadTheObjects();
    // 0x604ab0 with the 0xa2: the routes' light and the advance to +0xd8.
    void StepTheLogo(Kernel::WorldObject& logo) const;
    [[nodiscard]] Core::Expected<void, std::string> PlaceTheModels();
    void DetachTheDragonOwner();
    [[nodiscard]] Core::Expected<void, std::string> PlaceTheCharacter();
    [[nodiscard]] Core::Expected<void, std::string> StepTheCharacters();
    void StepTheWorld();
    // 0x52d220 (0x634d18): 0x44dc40(0, 0, 0) and 0x5208a0 with each live effect of the world's table, in its order,
    // with the CRT's rand(): the dragon through Effects::StepGameModelEffect and the 0x59's meteor 0xc2/9 through
    // Effects::StepWizardModelEffect.
    [[nodiscard]] Core::Expected<void, std::string> StepTheModelEffects();
    // 0x53c670 (0x634d1d), between the model effects and the particles: the step of the world's joints
    // (WorldState::DrawTail), with the game's globals, like the selection (SelectionWorld::StepEffects).
    [[nodiscard]] Core::Expected<void, std::string> StepTheJoints();
    // 0x5446e0 (0x634d22): the game scene's, with the owner of the particles that follow an effect (the meteor's trail
    // 0x5f2/0x24).
    [[nodiscard]] Core::Expected<void, std::string> StepTheParticles();
    // 0x5f93c0 on the login's terrain, or nothing without it.
    [[nodiscard]] std::function<double(float, float)> TerrainHeight();
    // 0x5767b88: the world clock in ms, or 0 without a clock.
    [[nodiscard]] float WorldClock() const;
    void StepTheScene(const Input::InputState& input, bool connecting);
    // 0x444010 (0x44453f..0x4445d9), after the horse's step: the breath, with `drawn`, the horse as the previous draw
    // left it (before this step).
    void BreatheTheHorse(const Kernel::DarkHorse& drawn);
    // Where `offset` lands on bone `bone` of the global pose buffer (0x68b181c) when the step arrives: left by the last
    // model posed without its own buffer in the previous draw that has that bone and is not dummy (LastPosedBonePoint),
    // in the world: the dragon (0x52d620) goes after the horse (0x4453b0), but its bone 27 is dummy, so the horse's
    // stays at
    // 27. Before the first draw, (0, 0, 0).
    [[nodiscard]] glm::vec3 GlobalPosePoint(std::size_t bone, const glm::vec3& offset,
                                            const Kernel::DarkHorse& drawnHorse) const;
    void DrawTheModels(Render::Frame::SceneCanvas& canvas, float alpha);
    // 0x4182f0: the raven, if it passes the trapezoid (if not, MissTheView), and its pose for the spark.
    void DrawTheRaven(Render::Frame::SceneCanvas& canvas, float alpha, Kernel::PoseBuffer* carry = nullptr);
    // 0x50db57..0x510a7a with the character, after its body, `frames` times (with zero, once between frames); an error
    // stays in m_failure.
    void DrawTheTail(Render::Frame::SceneCanvas& canvas, Kernel::ActorState& actor,
                     const Actors::Drawing::DrawnCharacter& character, Kernel::PoseBuffer& carry,
                     const Actors::FullSetScan& scan, int frames);
    // The particle pass's cut, in the log: once per entry.
    void ReportParticleFailure(const Particles::ParticleDrawingFailure& failure);
    // The terrain light at (x, y), or 0 without the world's.
    [[nodiscard]] glm::vec3 TerrainLightAt(const glm::vec3& position) const;
    void DrawTexts(Render::Frame::SceneCanvas& canvas) const;
    // 0x63552a..0x63553f: the notices and the old area, before the UI.
    void DrawTheMessages(Render::Frame::SceneCanvas& canvas);
    // What changed in the session since the last step: opens or closes the windows.
    void FollowSession();
    // The reply to F4 03 (0x4e3341), whether it connects or not: the 481 in the message area, once per address.
    void NoticeTheServerAddress();
    // 0x555190 with Text.bmd text `index` as system and without a name: 0x6e57a4's and 0x5767974's are at 0.
    // `arguments` go to the text's marks, which the original uses as a format (Q-C61).
    void AddTheMessage(std::size_t index, std::span<const std::string_view> arguments = {});
    void OnServerClick(const FrontEnd::ServerClick& click);
    void OnLoginRequest(const FrontEnd::LoginRequest& request);
    void OnNoticeClosed(Ui::NoticeId notice);
    // 0x6411b0: the menu opens the Esc one and the credit button, the credits.
    void OnMenuBarClick(Ui::MenuBarClick click);
    // 0x647d40: what happened in the Esc one.
    void OnEscapeEvent(const FrontEnd::EscapeWindowEvent& event);
    // 0x644730: what changed in the options, to the globals.
    void OnOptionsEvent(const FrontEnd::OptionsEvent& event);
    // 0x641203: the F4 06, the credits with 0x65f690 and what happened when opening them.
    void OpenTheCredits();
    // What happened in the credits on opening them or in a step: each close (0x63f9a0) sends the F4 06, and a new
    // illustration (0x63fc50) is uploaded.
    void FollowTheCredits(const FrontEnd::CreditsStep& step);
    void ReleaseTheIllustrations();
    // 0x65fc30 at the start of the step, like 0x637700 before 0x634c60: each window in its turn.
    void StepTheWindows(const Input::InputState& input);
    // 0x65f690: opens the window with its own and raises it with the focus.
    void OpenNotice(Ui::NoticeId notice);
    void OpenTheEscapeWindow();
    void OpenTheOptions();
    // The account window, with the chosen server's title.
    void OpenLogin(FrontEnd::LoginFocus focus);
    // The notice of a failed session: 0x6f with another version, 0x70 with the rest.
    void ShowFailure();
    [[nodiscard]] std::string LoginTitle() const;
    [[nodiscard]] Assets::TextureBufferSize BufferSize(Render::TextureSlot slot) const;

    [[nodiscard]] Camera::GameCamera& CameraState();
    [[nodiscard]] Camera::GameCameraInput& CameraInputState();
    // The global trapezoid of the last 0x5fb570 (Camera::CameraState::trapezoid), any scene's.
    [[nodiscard]] Terrain::TerrainPolygon& SharedTerrainPolygon();
    [[nodiscard]] Kernel::WorldState& CharacterWorld() const;
    // The dynamic light (Terrain::TerrainState::dynamicLight), which the selection and the game go on with.
    [[nodiscard]] Terrain::DynamicWorldLight& DynamicLight() const;
    // The context's module states, or the local ones of the tests without them.
    [[nodiscard]] Session::ModuleStates& States() const;
    [[nodiscard]] Models::ClothWorld& CharacterRandomWorld();

    SceneContext m_context;
    // View tests without main's globals use the same pool and step, with a local owner on the heap.
    std::unique_ptr<Kernel::WorldState> m_localWorld;
    std::unique_ptr<Session::ModuleStates> m_localStates;
    Models::ClothWorld m_localCloth;
    std::optional<std::string> m_failure;
    // Explicit fallback for tests without a full context; main always provides both globals.
    Camera::GameCamera m_localCamera;
    Camera::GameCameraInput m_localCameraInput;
    Models::OriginalFrameCarry m_cameraFrames;
    // 0x602ed0: one draw per object and original frame, with replay of the lantern's light between them.
    Models::OriginalFrameCarry m_objectEffectFrames;
    // The original's frames of 0x505820 from the character's draw (0x50c91e).
    Models::OriginalFrameCarry m_fullSetScanFrames;
    // Those of the notices and the old area (0x554810 and 0x5554e0): with zero, the draw goes on copies.
    Models::OriginalFrameCarry m_messageFrames;
    std::vector<std::optional<int>> m_objectEffectRandoms;
    const Models::IModelLibrary& m_models;
    App::ServerSession* m_session;
    const Tables::TextTable* m_texts;
    Assets::SceneTextures m_textures;
    Assets::SceneTextures m_windowTextures;
    // The one from SceneWorlds.h: the 55.
    std::optional<Render::Frame::SceneWorld> m_world;
    Assets::LoadedWorld m_terrain;
    // The terrain's block range with the login camera (0x5fbb00): the loading initializes it; each draw updates it for
    // the next step. The trapezoid (0x5fb570) is WorldRuntime's global.
    Terrain::TerrainRange m_terrainRange;
    Render::Camera3D m_drawnCamera;
    // The light alone, if the terrain could not be read and its light could (Terrain::LoadTerrainLighting).
    std::unique_ptr<Terrain::TerrainLighting> m_lighting;
    const Models::ModelTextureSlots* m_gameSlots;
    // The models of Object56\ and the records of EncTerrain56.obj, from each entry.
    Assets::WorldModels m_worldModels;
    std::vector<Data::Terrain::TerrainObject> m_objectRecords;
    // The world objects as 0x606660 creates them, and the logo at the end (0x61c7c0 creates it after the world load).
    std::vector<Kernel::WorldObject> m_objects;
    std::optional<std::size_t> m_logoIndex;
    // A frame's sprites (0x760f990): the cloth's sparkles and the scepter's glow.
    Particles::FrameSprites m_sprites;
    // The leaves (0x75e34b8): on map 55, the 80 embers.
    Weather::Leaves m_leaves;
    // Whether a frame was drawn since entering: before, the global pose buffer has nothing of the login (P-C26).
    bool m_drawnSinceEntering = false;
    // Whether a step ran since entering: before, 0x6350b0 does not draw (0x7d3a13d, which the step sets).
    bool m_steppedSinceEntering = false;
    bool m_particleFailureReported = false;
    // The sprites of what hangs and of the character's effects (the scepter's glow, 0x509340): those of the original's
    // last frame, which an intermediate draw repeats, like the game and the selection with the whole table.
    Particles::FrameSprites m_tailSprites;
    Particles::FrameSprites m_tailSpriteReplay;
    bool m_tailSpriteReplayValid = false;
    // The logo: its model, its meshes' slots from where the world ended and its textures.
    FrontEnd::SingleModelLibrary m_logo;
    std::optional<Models::ModelTextureSlots> m_logoSlots;
    std::optional<Assets::SceneTextures> m_logoTextures;
    FrontEnd::LoginRoutes m_routes;
    // The horse it rides (0x443fa0(0xbf, …) of 0x6349a0): it follows the character.
    Kernel::DarkHorse m_horse;
    // The Dark Lord's cloth (0x50ba30), between frames.
    Actors::Drawing::CharacterCloth m_characterCloth;
    // The three texts, or empty without Text.bmd. The version one, already built.
    std::optional<std::string_view> m_left;
    std::optional<std::string_view> m_right;
    std::optional<std::string> m_version;

    FrontEnd::ServerListWindow m_serverList;
    // The list opens it and it stays open until leaving (06 § Las ventanas de la UI nueva).
    Ui::MenuBar m_menuBar;
    FrontEnd::LoginWindow m_login;
    Ui::NoticeWindow m_notice;
    FrontEnd::EscapeWindow m_escape;
    // The options (+0x7e8) and the credits (+0x4578), with GameData's credit.bmd table.
    FrontEnd::OptionsWindow m_options;
    FrontEnd::CreditsWindow m_credits;
    // The credits' illustration pair (0x6f8 and 0x6f9) that the last 0x63fc50 uploaded.
    std::optional<Assets::SceneTextures> m_illustrations;
    // The new UI's manager: the windows' order, focus and mouse.
    Ui::NewUi m_ui;
    // The session state the last step saw, to open each window once.
    std::optional<Session::SessionState> m_seen;
    // Whether the last step saw the F4 03 address in the session.
    bool m_sawTheAddress = false;
    // The last frame's screen: the windows place themselves and read the mouse with it. Until the first, the reference
    // one.
    Core::PixelSize m_screen{640, 480};
    bool m_quit = false;
    // Who creates the joints of the character's draw (0x530860).
    Session::CharacterDrawHooks m_characterDrawHooks;
    Effects::ActorEffectHooks m_effectHooks;
};
} // namespace Client::Scenes
