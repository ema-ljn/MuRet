#pragma once

#include "Scenes/ILoopClient.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Input/InputState.h"
#include "Render/Frame/IFrameTarget.h"
#include "Scenes/IScene.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/SceneId.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// The scene machine: the client of Main's loop, which holds the current scene and changes it only by a transition of
// the original's table (SceneTransitions.h). An Enter that fails leaves the SceneManager without a scene, with the
// error in Failure, so main.cpp closes, and the failed scene does its Leave, which releases what it managed to load.
// A scene that fails in its step or its draw (IScene::Failure) stays, with the error in Failure, and does not run
// again: the cut frame is not presented.
//
// The network does not belong to a scene: the ServerSession lives on from the login to the selection and the game.
// The SceneManager runs it on each Poll, also minimized, with `pollNetwork`. An application error keeps the first
// diagnostic and blocks network, steps, transitions and draw until the normal close. What the network orders of the
// scene (the F1 02 with 1, which the original writes into DAT_006d5dc8 while reading it) is taken by `takeSceneOrder`
// after each Poll, and the next Step enters it before the step, through the same table as a scene's request.
namespace Client::Scenes
{
// Builds the scene of a SceneId: main.cpp with the real ones, the tests with doubles. nullptr if it does not have
// that scene.
using SceneFactory = std::function<std::unique_ptr<IScene>(Kernel::SceneId)>;

class SceneManager final : public Scenes::ILoopClient
{
public:
    // `pollNetwork` runs on each Poll: main.cpp propagates Online::Poll and, without a server, returns success without
    // network. `takeSceneOrder` runs after a pollNetwork that succeeded and returns the scene the network ordered, once
    // (main.cpp: WorldRuntime::orderedScene). Both can be empty. beforeSceneFrame publishes the global prefix even if
    // the scene holds the draw; the caller decides which frames are original. afterSceneFrame runs at the end of each
    // loop turn with the window not minimized (FinishTheLoop), drawn or held, with a scene and without an error: the
    // tail of 0x63945d and the start of WinMain's next turn (P-C61). afterSceneStep runs after each successful scene
    // step, before its request for another: what 0x637700 does in all scenes after their logic, like the message clocks
    // (0x637cf3 and 0x637cf8). Its error stays in Failure, like a step's. beforeSceneDraw runs on each Render before
    // BeginFrame, and afterSceneDraw after the scene's draw and before Present, with its canvas: what 0x637700 does in
    // all scenes around the draw (the screenshot, 0x6381bb..0x63835a and 0x638586..0x6385da). Their errors stay in
    // Failure and the frame is not presented. The factory, the frame, the clock and the log outlive it.
    SceneManager(SceneFactory factory, Render::Frame::IFrameTarget& frame, Core::IClock& clock,
                 std::function<Core::Expected<void, std::string>()> pollNetwork, Core::Log& log,
                 std::function<std::optional<Kernel::SceneId>()> takeSceneOrder = {},
                 std::function<void(Core::Duration)> beforeSceneFrame = {},
                 std::function<void(Core::Duration)> afterSceneFrame = {},
                 std::function<Core::Expected<void, std::string>()> afterSceneStep = {},
                 std::function<Core::Expected<void, std::string>()> beforeSceneDraw = {},
                 std::function<Core::Expected<void, std::string>(Render::Frame::SceneCanvas&)> afterSceneDraw = {});
    ~SceneManager() override;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // Enters `first` (main.cpp passes FirstScene, the loading). If its Enter fails, the error, which also stays in
    // Failure. With a scene already entered it is an error and the current one goes on: Request is for changing. A
    // previous Poll error is terminal, even before the first scene: it returns the first diagnostic and does not enter.
    // A failed Enter does allow retrying Start.
    Core::Expected<void, std::string> Start(Kernel::SceneId first);
    // The network, also minimized and before Start; only a Poll failure stops it.
    // A failed Enter does not cut the network while the host decides whether to retry Start.
    void Poll() override;
    // If the network ordered a scene, first Request to it, and the step already belongs to the new one. Then, the
    // scene's step and, if it asks for another (Next), Request. A step without a request ends the run of a rejected
    // request: if it comes back, it is reported again. If the scene failed (IScene::Failure), its error stays in
    // Failure and it asks for nothing.
    void Step(const Input::InputState& input) override;
    // BeginFrame with the clock, the scene's Render and Present, between beforeSceneDraw and afterSceneDraw. Without a
    // scene, nothing. Before, the frame's IScene::PrepareFrame if nobody ran it. It does not look at HoldsTheLastFrame:
    // the GameLoop does not call it with the frame held. If the draw failed (IScene::Failure), its error stays in
    // Failure and the frame is not presented.
    void Render(float alpha) override;

    // Changes to `next` if the current scene can go there (IsOriginalTransition): Leave, the factory and Enter. If not,
    // an error in the log (once per run of the same request) and nothing changes. Not called from inside a scene: the
    // scene asks with Next.
    void Request(Kernel::SceneId next);

    // The current scene; empty before Start or after a failed Enter.
    [[nodiscard]] std::optional<Kernel::SceneId> Current() const;
    // The current error of Enter, of the scene's step or draw, or the first terminal Poll failure (which wins over
    // Enter's), or empty.
    [[nodiscard]] const std::optional<std::string>& Failure() const;
    // IScene::WantsToQuit; without a scene, false.
    [[nodiscard]] bool WantsToQuit() const;
    // IScene::WantsTextInput; without a scene, false.
    [[nodiscard]] bool WantsTextInput() const;
    // With Failure it holds without notifying the scene; otherwise it queries IScene::HoldsTheLastFrame, after
    // IScene::PrepareFrame if this frame has not run it yet. A frame ends with Render, and Poll and Step start another:
    // asking twice in the same frame does not read twice.
    [[nodiscard]] bool HoldsTheLastFrame() const override;
    // Whether a Step entered another scene since the last time (Request); Start does not count, because it goes before
    // the loop.
    [[nodiscard]] bool TakeTheSceneChange() override;
    // afterSceneFrame with the clock, with a scene and without an error.
    void FinishTheLoop() override;

private:
    // The factory and the Enter of `id`; if it fails, it stays without a scene and with the error.
    Core::Expected<void, std::string> Enter(Kernel::SceneId id);
    // IScene::PrepareFrame, once per frame.
    void PrepareFrame() const;
    // If the scene failed in `where` (IScene::Failure): stores the error, logs it and returns true.
    bool TakeTheSceneFailure(const char* where);
    // If `done` is an error of what runs around the draw: stores it, logs it and returns true.
    bool TakeTheDrawFailure(const Core::Expected<void, std::string>& done);

    SceneFactory m_factory;
    Render::Frame::IFrameTarget& m_frame;
    Core::IClock& m_clock;
    std::function<Core::Expected<void, std::string>()> m_pollNetwork;
    Core::Log& m_log;
    std::function<std::optional<Kernel::SceneId>()> m_takeSceneOrder;
    // The draw's global prefix, once per frame, before the dispatch and the scene's guard.
    std::function<void(Core::Duration)> m_beforeSceneFrame;
    // The loop turn's work after the frame (FinishTheLoop).
    std::function<void(Core::Duration)> m_afterSceneFrame;
    // 0x637700's work after the step of any scene.
    std::function<Core::Expected<void, std::string>()> m_afterSceneStep;
    // 0x637700's work before and after the draw of any scene.
    std::function<Core::Expected<void, std::string>()> m_beforeSceneDraw;
    std::function<Core::Expected<void, std::string>(Render::Frame::SceneCanvas&)> m_afterSceneDraw;
    // The scene the network ordered that has not entered yet: the next Step takes it.
    std::optional<Kernel::SceneId> m_networkOrder;
    std::unique_ptr<IScene> m_scene;
    std::optional<Kernel::SceneId> m_current;
    std::optional<std::string> m_failure;
    // Tells Poll's fatal abort from the failed Enter that Start can retry.
    bool m_pollFailed = false;
    // The last rejected request, so the notice is not repeated on each step while it keeps being asked.
    std::optional<Kernel::SceneId> m_rejected;
    bool m_sceneChanged = false;
    // Whether the current scene already ran PrepareFrame in this frame. Mutable because the const query
    // HoldsTheLastFrame decides it, like the OnFrameHeld notice.
    mutable bool m_framePrepared = false;
};
} // namespace Client::Scenes
