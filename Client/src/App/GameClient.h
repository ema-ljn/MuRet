#pragma once

#include "Session/Credentials.h"
#include "App/ILoopClient.h"
#include "App/PacingMode.h"
#include "Session/ServerSession.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/LocalTime.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Models/Animation.h"
#include "Models/Cloth.h"
#include "Network/Connection.h"
#include "Network/IObfuscation.h"
#include "Network/ISocket.h"
#include "Render/Frame/IFrameTarget.h"
#include "Render/Frame/IScreenReader.h"
#include "Render/Frame/SceneCanvas.h"
#include "Render/ITextureStore.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Assets/GameModels.h"
#include "Scenes/IScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/Screenshots.h"
#include "Scenes/SelectionState.h"
#include "Session/SessionEventApplier.h"
#include "Scenes/WinMainLoop.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Sound/SoundRequests.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Client::Audio
{
class IPlayback;
} // namespace Client::Audio

namespace Client::Platform
{
class IClipboard;
} // namespace Client::Platform

namespace Client::Tables
{
struct GameData;
} // namespace Client::Tables

// The composition root: the pieces of the game, built and destroyed in WinMain's order. main.cpp gives it SDL, GL and
// Winsock; the behavior golden gives it doubles.
namespace Client::App
{
// Everything here outlives the client.
struct GameClientDevices
{
    Render::Frame::IFrameTarget& frame;
    Render::ITextureStore& textures;
    const Render::Text::ITextMeasure& textMeasure;
    Render::Frame::IScreenReader& screenReader;
    Core::IClock& clock;
    Core::Log& log;
    const Input::InputState& input;
    std::function<Core::PixelSize()> screen{};
    std::function<Core::Expected<Core::LocalTime, std::string>()> localTime{};
    Audio::IPlayback* playback = nullptr;
    Platform::IClipboard* clipboard = nullptr;
};

// Only with a host. The socket and the obfuscation outlive the client.
struct GameClientServer
{
    Network::ISocket& socket;
    const Network::IObfuscation& obfuscation;
    Network::ConnectionKeys keys;
    SessionSettings settings{};
    std::optional<Credentials> autoLogin{};
    std::uint32_t paddingSeed = 0;
    std::optional<ChatRoomSockets> chatRooms{};
};

struct GameClientOptions
{
    std::filesystem::path dataRoot{};
    std::string version{};
    PacingMode pacing = PacingMode::Decoupled;
    bool music = true;
    // 0x4bad9a: srand(time(NULL)).
    std::uint32_t crtSeed = 0;
    // Called once per login scene.
    std::function<std::uint32_t()> loginSeed{};
    // Empty: the working directory, as the bare file name of 0x63b150's fopen.
    std::filesystem::path screenshotFolder{};
};

enum class GameExitKind : std::uint8_t
{
    DataError,
    SceneFailure,
    SelectionFailure,
    ServerOrder,
    PlayerQuit,
};

struct GameExit
{
    GameExitKind kind;
    std::string message;
};

// The first reason that holds, in this order.
[[nodiscard]] std::optional<GameExit> GameExitOf(const std::optional<std::string>& fatalDataMessage,
                                                 const std::optional<std::string>& sceneFailure,
                                                 const std::optional<std::string>& selectionFailure,
                                                 bool serverOrderedTheQuit, bool sceneWantsToQuit);

class GameClient
{
public:
    GameClient(const Tables::GameData& data, const GameClientDevices& devices, GameClientOptions options,
               std::optional<GameClientServer> server);
    ~GameClient();
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    // 0x63945d: the first loop turn, then the first scene.
    [[nodiscard]] Core::Expected<void, std::string> Start();
    [[nodiscard]] ILoopClient& Loop();
    [[nodiscard]] std::optional<Scenes::SceneId> CurrentScene() const;
    [[nodiscard]] bool WantsTextInput() const;
    [[nodiscard]] std::optional<GameExit> Exit() const;

    [[nodiscard]] Kernel::WorldState& World();
    [[nodiscard]] const Kernel::WorldState& World() const;
    // The CRT rand() state.
    [[nodiscard]] std::uint32_t RandomState() const;
    // nullptr without a host.
    [[nodiscard]] const ServerSession* Session() const;

private:
    struct Online;

    [[nodiscard]] ServerSession* MutableSession();
    [[nodiscard]] std::unique_ptr<Scenes::IScene> MakeScene(Scenes::SceneId id);
    [[nodiscard]] Core::Expected<void, std::string> PollNetwork();
    [[nodiscard]] std::optional<Scenes::SceneId> TakeSceneOrder();
    [[nodiscard]] bool Connecting() const;
    void BeforeSceneFrame(Core::Duration now);
    void AfterSceneFrame(Core::Duration now);
    [[nodiscard]] Core::Expected<void, std::string> AfterSceneStep();
    [[nodiscard]] Scenes::ScreenshotFrameContext ScreenshotFrame();
    [[nodiscard]] Core::Expected<void, std::string> BeforeSceneDraw();
    [[nodiscard]] Core::Expected<void, std::string> AfterSceneDraw(Render::Frame::SceneCanvas& canvas);

    const Tables::GameData& m_data;
    GameClientDevices m_devices;
    Sound::SoundRequests m_sounds;
    GameClientOptions m_options;
    Models::ClothWorld m_cloth;
    Camera::GameCamera m_camera;
    // 0x4bafa6..0x4bafc2: WinMain zeroes the pool of 401 characters and the own record; 0x4bafad..0x4bafdf: the hero
    // is character 0.
    Kernel::WorldState m_world;
    Session::ModuleStates m_states;
    Camera::GameCameraInput m_cameraInput;
    Scenes::SelectionState m_selection;
    // 0x61ccb0: the loading scene reads them.
    Assets::GameModels m_models;
    Scenes::WorldEntryResources m_worldEntry;
    Scenes::SessionEventApplier m_eventApplier;
    std::unique_ptr<Online> m_online;
    Scenes::SceneContext m_context;
    Models::OriginalFrameCarry m_itemFrameCarry;
    Scenes::WinMainLoopState m_winMainLoop;
    Scenes::Screenshots m_screenshots;
    Scenes::SceneManager m_manager;
};
} // namespace Client::App
