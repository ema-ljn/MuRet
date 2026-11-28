#pragma once

#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Camera/GameCamera.h"

#include "Effects/ActorEffectHooks.h"
#include "Session/CharacterDrawHooks.h"
#include "Session/CharacterStepHooks.h"
#include "Session/ServerSession.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/PixelSize.h"
#include "Tables/ItemTable.h"
#include "Actors/CharacterStep.h"
#include "Tables/TextTable.h"
#include "Kernel/CharacterAppearance.h"
#include "Input/DoubleClick.h"
#include "Input/InputState.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Models/ModelTextureSlots.h"
#include "Models/Pose.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/CharacterAnimation.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "FrontEnd/CreatePreview.h"
#include "Actors/FullSetScan.h"
#include "Tables/GameData.h"
#include "Scenes/IScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/SceneContext.h"
#include "Kernel/SceneId.h"
#include "Assets/SceneTextures.h"
#include "FrontEnd/SelectionWorld.h"
#include "FrontEnd/CharacterBalloons.h"
#include "FrontEnd/CreateCharacterWindow.h"
#include "FrontEnd/EscapeWindow.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/NewUi.h"
#include "Ui/NoticeWindow.h"
#include "Ui/Notices.h"
#include "FrontEnd/OptionsWindow.h"
#include "FrontEnd/SelectionBar.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainCulling.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Assets
{
class GameModels;
} // namespace Client::Assets

namespace Client::Kernel
{
struct ActorState;
struct PoseBuffer;
} // namespace Client::Kernel

// Scene 4, the character selection (0x633f20 on entering, 0x6342c0 the logic and 0x634680 the draw): the list's
// characters at their slots, animated, the click that chooses the one under the mouse, and Enter or the double click
// that enters the world with the chosen one. On top, the selection bar (FrontEnd::SelectionBar) with connect, create
// and delete, the create window (FrontEnd::CreateCharacterWindow) and the notices. With the common step of the pool
// and the NewFace and world 54 (SelectionWorld: the terrain, the objects and their effects, the light and the chosen
// one's two 0x729); without the menu and the options (CL8), the characters' signs or the message area.
// Contract: Client/docs/research/06-render-escenas-y-ui.md, "La selección de personaje", and the CL6 plan (CL6.3,
// "Contrato").
namespace Client::Scenes
{

class CharacterSelectScene final : public IScene
{
public:
    // `data`, `models` and `session` must outlive it. `session` is nullptr without a server: the scene draws the
    // backgrounds and asks for nothing. `gameSlots` is the load's slot table (GameModels::TextureSlots), which the
    // create window's preview follows from 0x6f8; nullptr, an empty table. It must outlive it.
    CharacterSelectScene(const SceneContext& context, const Tables::GameData& data, const Models::IModelLibrary& models,
                         App::ServerSession* session, const Models::ModelTextureSlots* gameSlots = nullptr);
    CharacterSelectScene(const SceneContext& context, const Tables::GameData& data, Assets::GameModels& models,
                         App::ServerSession* session);
    CharacterSelectScene(const SceneContext& context, const Tables::GameData& data, Assets::GameModels& models,
                         App::ServerSession* session, const Models::ModelTextureSlots* gameSlots);

    // Like 0x633f20: its texture set and its models (0x61ca30: Logo05 and the preview's NewFace, SelectionModels), the
    // windows' one, none chosen, and the F3 00 (ServerSession::RequestCharacterList).
    Core::Expected<void, std::string> Enter() override;
    // 0x6342c0: nothing until the list; then, the list's characters and their advance, the delete and create replies,
    // and the UI from top to bottom: the notice if open (with the create window below, which keeps taking what is
    // typed); otherwise, the create window, which is modal; otherwise, the bar, the double click that enters, the click
    // that chooses and Enter. With the mouse over the bar or an open window (+0x8de8), neither the click nor the double
    // click, and the outline stays on the last one; a notice that opens in the step is handled from the next one.
    void Step(const Input::InputState& input) override;
    // 0x634680 without the terrain, the objects or the effects: with a session, nothing until the list; the one under
    // the mouse, with the outline. Then, the UI: the bar, the create window (its base, the preview in its own 3D pass,
    // which is not darkened, and the rest) and the notice. On each original frame, the camera and 0x5fbb00's trapezoid
    // (0x63475c), which it leaves in WorldRuntime for the game's first step; if the trapezoid errors, the frame stops
    // and the scene stays in Failure.
    void Render(Render::Frame::SceneCanvas& canvas, float alpha) override;
    // Releases its slots.
    void Leave() override;
    // WorldLoading after entering. If the session fails, the scene does not leave: it opens notice 0x70.
    [[nodiscard]] std::optional<Kernel::SceneId> Next() const override;
    // With the OK of notice 0x70, which closes the game (Q-C69).
    [[nodiscard]] bool WantsToQuit() const override;
    [[nodiscard]] bool WantsTextInput() const override;
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // 0x5fb570's error in the draw (ADR 0009), with the address; empty while it does not stop.
    [[nodiscard]] std::optional<std::string> Failure() const override;
    [[nodiscard]] const Kernel::ActorState& PreviewActor() const;

private:
    // A list character at its slot, with the entry it came from.
    struct Character
    {
        std::uint8_t slot = 0;
        std::uint64_t revision = 0;
        Protocol::FrontEnd::CharacterEntry entry;
        Kernel::CharacterAppearance appearance;
        Actors::CharacterAnimation animation;
        // 0x50ba30 builds it from the first frame it is drawn.
        Actors::Drawing::CharacterCloth cloth;
    };

    // Whether the scene still waits for the list: with a session, until ChoosingCharacter.
    [[nodiscard]] bool WaitingForTheList() const;
    // The session's list to the characters: the first time, all of them, and the bar's +0x543; then, each construction
    // received recreates its visual, even if it has the same bytes as the previous one.
    void SyncTheCharacters();
    [[nodiscard]] Character MakeCharacter(const Protocol::FrontEnd::CharacterEntry& entry) const;
    [[nodiscard]] bool PrepareTheCharacters();
    // 0x63420a: with either of the list's two pkt[5] numbers, 1240 in the message area.
    [[nodiscard]] Core::Expected<void, std::string> AddTheAccountNotice();
    [[nodiscard]] Actors::CharacterStepContext CharacterContext();
    // The selection's terrain or, before it lends one, the current map's.
    [[nodiscard]] const Terrain::WorldTerrain* StepTerrain() const;
    void StepCharacters();
    void StepTheScene(const Input::InputState& input);
    // Whether it opened notice 0x70 in this step.
    bool ShowTheFailure();
    // With the F3 01 reply (0x4c2290): with 1, 0x5d closes; with 0 or 2, the create window opens again with its notice
    // on top. Whether it opened one in this step.
    bool TakeTheCreationResult();
    // 0x63d880 with the window open and no notice on top.
    void StepTheCreateWindow(const Input::InputState& input, Ui::NewUiTurn turn);
    // 0x65f690: opens the window with its own and moves it on top with the focus.
    void OpenTheCreateWindow();
    void OpenNotice(Ui::NoticeId notice, std::string_view argument = {});
    void OpenTheEscapeWindow();
    // 0x647e75: 0x65f690 with the options.
    void OpenTheOptions();
    // 0x65fc30 at the start of the step, like 0x637700 before 0x6342c0: each window with its turn.
    void StepTheWindows(const Input::InputState& input);
    // 0x647d40: what happened in the Esc one.
    void OnEscapeEvent(const FrontEnd::EscapeWindowEvent& event);
    // The options globals (Hud/OptionsSettings.h), or the scene's without the world.
    [[nodiscard]] FrontEnd::OptionsSettings CurrentOptions() const;
    // 0x644730: what changed in the options, to the globals.
    void OnOptionsEvent(const FrontEnd::OptionsEvent& event);
    // 0x647ef0: 380 with what is left, to the messages.
    void AddTheCountdown(int seconds);
    // The Esc one's 1 (0x647e08): the C3 F1 02 02 with its two rand() calls, and it and the bar close.
    void JoinAnotherServer();
    // 0x63d990: the name checks, and the F3 01 and 0x5d, or the name notice.
    void SubmitTheCreation();
    // 0x63e430 at the end of the window's step, which only runs with it on top: the window's class and the extra
    // advance: with a notice on top it only advances from 0x508ea0.
    void StepThePreview();
    void DrawTheCreateWindow(Render::Frame::SceneCanvas& canvas, float alpha);
    // 0x505820 with a character being drawn, `frames` times (Actors/FullSetScan.h), on WorldState: what the last one
    // returns, which reads the effects tail's full set.
    Actors::FullSetScan ScanTheCharacter(const Kernel::CharacterAppearance& appearance, int frames);
    // 0x635ae0 (0x6346ca) and 0x5fbb00 (0x63475c) on each original frame the clock counts, one after the other; an
    // intermediate draw only queries the camera. With a trapezoid error it leaves m_failure.
    [[nodiscard]] Render::Camera3D StepTheCameraAndTheTrapezoid(const Camera::GameCameraInput& input,
                                                                Core::Duration clock);
    // Whether world 54 runs: with WorldState, the cloth, the selection initialized and map 54 current.
    [[nodiscard]] bool HasTheWorld() const;
    // The terrain 54 height (0x5f93c0) for the butterfly and the particles; empty without terrain.
    [[nodiscard]] std::function<double(float, float)> TerrainHeight();
    // 0x634680 from 0x6347e9 to 0x6213f0: the chosen one's light, the world, the characters, the tail, the chosen one's
    // two 0x729 and the eye pass. Without the world, only the characters.
    void DrawTheWorldAndTheCharacters(Render::Frame::SceneCanvas& canvas, float alpha, const Render::Pass3D& pass,
                                      const Render::Camera3D& camera, std::uint8_t map, Kernel::PoseBuffer* poseCarry,
                                      int scanFrames);
    void DrawTheCharacters(Render::Frame::SceneCanvas& canvas, float alpha, std::uint8_t map,
                           Kernel::PoseBuffer* poseCarry, int scanFrames);
    // Whether Player.bmd has its skeleton with actions: without them, 0x50ba30 leaves early (0x50ba87).
    [[nodiscard]] bool HasTheSkeleton() const;
    // 0x50db57..0x510a7a with a list character, after its body; an error stays in m_failure.
    void DrawTheTail(Render::Frame::SceneCanvas& canvas, Kernel::ActorState& actor, std::size_t slot,
                     const Actors::Drawing::DrawnCharacter& drawn, Kernel::PoseBuffer& poseCarry,
                     const Actors::FullSetScan& scan, bool originalFrame);
    // The game cursor (0x5ac2d0) at the end of the UI, also with the notice before the list.
    void DrawTheCursor(Render::Frame::SceneCanvas& canvas) const;
    // 0x634949..0x63495f: the notices and the old area, before the UI.
    void DrawTheMessages(Render::Frame::SceneCanvas& canvas);
    // The pool's five records as 0x63cc60 reads them. MuRet removes the deleted slot from the session's list without
    // lowering its record's +0 (0x643374, Q-C104): one not in the list goes as dead. Without the world, all five dead.
    [[nodiscard]] std::array<FrontEnd::BalloonRecord, FrontEnd::BalloonCount> BalloonRecords() const;
    // 0x63cfb0: the balloons with the current records.
    void UpdateTheBalloons();
    // 0x65fe00 → 0x63cf80, before the windows: each character at z + 280 with the world pass's projection (0x6205b0
    // with 0x620db0's cache at 0x634785). An error stays in m_failure.
    void DrawTheBalloons(Render::Frame::SceneCanvas& canvas, const Render::Pass3D& pass, float fieldOfView);
    // With the F3 02 reply, its notice instead of 0x5d (0x4e2fe9), unless the session has dropped. Whether it opened
    // one in this step.
    bool TakeTheDeletionResult();
    void StepTheNotice(const Input::InputState& input, Ui::NewUiTurn turn);
    // 0x63eb40: create with a free slot among the five; connect and delete with one chosen.
    void EnableTheBar();
    void OnTheBar(FrontEnd::SelectionBarClick click);
    // 0x63edf0: the delete checks with the chosen one, and their notice.
    void AskToDelete();
    // 0x643740: the F3 02 with notice 0x5f's code, and 0x5d.
    void SendTheDeletion();
    // The slot of the character under the mouse, with the last drawn pass's ray.
    [[nodiscard]] std::optional<std::uint8_t> UnderTheMouse(const Input::InputState& input) const;
    void Choose(std::optional<std::uint8_t> slot);
    // Enter with one chosen: notice 0x84 if it is blocked, or the world.
    void TryToEnter();
    [[nodiscard]] Models::ObjectTransform ObjectOf(const Character& character) const;
    // The buffer of a windows or scene slot.
    [[nodiscard]] Assets::TextureBufferSize BufferSize(Render::TextureSlot slot) const;

    [[nodiscard]] Camera::GameCamera& CameraState();
    [[nodiscard]] Camera::GameCameraInput& CameraInputState();
    [[nodiscard]] FrontEnd::CreatePreview& Preview();
    [[nodiscard]] const FrontEnd::CreatePreview& Preview() const;

    SceneContext m_context;
    // An explicit fallback for tests without a full context; main always provides both globals.
    Camera::GameCamera m_localCamera;
    Camera::GameCameraInput m_localCameraInput;
    Models::OriginalFrameCarry m_cameraFrames;
    // 0x505820's original frames since the draw (0x50c91e), like the camera's.
    Models::OriginalFrameCarry m_fullSetScanFrames;
    Models::OriginalFrameCarry m_previewDrawFrames;
    // World 54's original frames since the draw: with zero, the draw goes on copies.
    Models::OriginalFrameCarry m_worldFrames;
    // Those of the notices and the old area (0x554810 and 0x5554e0), the same.
    Models::OriginalFrameCarry m_messageFrames;
    Render::Camera3D m_previewCamera;
    Session::CharacterStepHooks m_stepHooks;
    Session::CharacterDrawHooks m_characterDrawHooks;
    Effects::ActorEffectHooks m_effectHooks;
    const Tables::TextTable* m_texts;
    const Tables::ItemTable* m_items;
    const Models::IModelLibrary& m_models;
    App::ServerSession* m_session;
    Assets::SceneTextures m_textures;
    Assets::SceneTextures m_windowTextures;
    // World 54 (P-C50): the tiles, the Object55 models, the sprites and the intermediate draws' copies.
    FrontEnd::SelectionWorld m_world;
    // The last frame's trapezoid and range, those of the terrain and objects pass.
    Terrain::TerrainPolygon m_polygon{};
    Terrain::TerrainRange m_terrainRange{};
    Ui::NoticeWindow m_notice;
    FrontEnd::SelectionBar m_bar;
    // The five slots' balloons (+0x8dc8 of the new UI, 06 § Los globos).
    FrontEnd::CharacterBalloons m_balloons;
    // The FilterName.bmd and Filter.bmd words from `data`, and texts 457 and 458.
    Ui::NameFilters m_nameFilters;
    FrontEnd::CreateCharacterWindow m_create;
    FrontEnd::EscapeWindow m_escape;
    // The options (+0x7e8), and what they keep when the scene has no world (the tests).
    FrontEnd::OptionsWindow m_options;
    FrontEnd::OptionsSettings m_localOptions;
    // The new UI's manager (CL8, milestone 8): the order, the focus and the mouse of the windows.
    Ui::NewUi m_ui;
    // Whether the bar was already opened with the list (0x65f4b0).
    bool m_openedTheBar = false;
    const Models::ModelTextureSlots* m_gameSlots;
    Assets::GameModels* m_startupModels = nullptr;
    FrontEnd::SelectionModels m_selectionModels;
    // An adapter without SelectionState: main keeps the NewFace in the durable selection state.
    FrontEnd::CreatePreview m_preview;
    bool m_previewAdvanced = false;
    bool m_charactersPrepared = false;
    Input::DoubleClick m_doubleClick;
    std::vector<Character> m_characters;
    bool m_tookTheList = false;
    // From when the F3 02 (or the F3 01) was sent until its reply.
    bool m_waitingForTheDeletion = false;
    bool m_waitingForTheCreation = false;
    std::optional<Render::Pass3D> m_lastPass;
    // The one under the mouse on the last step, which carries the outline: like the DAT_006c8e30 that 0x59ec80 leaves
    // for the next frame.
    std::optional<std::uint8_t> m_hovered;
    Core::PixelSize m_screen{640, 480};
    // The steps since entering: the double click's clock.
    std::uint64_t m_steps = 0;
    std::optional<Kernel::SceneId> m_next;
    bool m_failureShown = false;
    bool m_quit = false;
    std::optional<std::string> m_failure;
};
} // namespace Client::Scenes
