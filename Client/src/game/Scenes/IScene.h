#pragma once

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Render/Frame/SceneCanvas.h"
#include "Kernel/SceneId.h"

#include <optional>
#include <string>

// A scene: in the original, a number in DAT_006d5dc8 and an `if` per scene in each step's logic and in the draw.
// What it loads on entering (its textures, its windows) lives as long as it does: Leave releases it. A SceneFactory
// (SceneManager.h) builds it on each entry, so entering again starts from zero, like the original with its "already
// entered" flags at 0.
namespace Client::Scenes
{
class IScene
{
public:
    virtual ~IScene() = default;
    // What the original does on entering (0x6349a0 in the login). An error, like a missing texture, stops: the
    // SceneManager does not go on and main.cpp closes, with the error in the log.
    virtual Core::Expected<void, std::string> Enter() = 0;
    // One logic step, 25 per second (02 § El ritmo).
    virtual void Step(const Input::InputState& input) = 0;
    // alpha in [0, 1), like ILoopClient::Render's.
    virtual void Render(Render::Frame::SceneCanvas& canvas, float alpha) = 0;
    // What the original's draw reads first to decide whether it draws and presents, even if it then does not: the game
    // reads the wait counter through the protection table (0x6371b7..0x637229) and above 30 returns 0. SceneManager
    // calls it once per frame: at the first HoldsTheLastFrame query, which only reads the decision, or in Render if
    // nobody asked. Minimized there is no frame and it is not called.
    virtual void PrepareFrame() {}
    // Releases what Enter loaded. Runs before the next scene's Enter.
    virtual void Leave() = 0;
    // Which scene it wants to go to; empty if it stays. The SceneManager checks it after each Step.
    [[nodiscard]] virtual std::optional<Kernel::SceneId> Next() const = 0;
    // Whether the player closed the game from the scene, like the OK of the login's connection notice, which in the
    // original posts WM_CLOSE (Q-C69). main.cpp checks it after each frame.
    [[nodiscard]] virtual bool WantsToQuit() const = 0;
    // Whether the scene has a text field on top that reads what is typed, like the login's account window. main.cpp
    // turns SDL text on only meanwhile (SDL_StartTextInput) and off when it stops asking, after each frame.
    [[nodiscard]] virtual bool WantsTextInput() const = 0;
    // Whether the current frame is not drawn and the previous one stays (ILoopClient::HoldsTheLastFrame): the login
    // while connecting, a blocking connect in the original (Q-C85), or the game while waiting for the world (what
    // PrepareFrame decided).
    [[nodiscard]] virtual bool HoldsTheLastFrame() const = 0;
    // SceneManager notifies when its HoldsTheLastFrame query returns true. The notice can repeat and must be
    // idempotent.
    virtual void OnFrameHeld() {}
    // What the original runs in the step or the draw and the scene lacks, with the address: the game reaching a stage
    // of its draw MuRet does not port (GameScene). SceneManager checks it after each Step and each Render, stores it as
    // its Failure and main.cpp closes with the error.
    [[nodiscard]] virtual std::optional<std::string> Failure() const
    {
        return std::nullopt;
    }
};
} // namespace Client::Scenes
