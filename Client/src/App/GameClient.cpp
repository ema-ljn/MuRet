#include "App/GameClient.h"

#include "App/AutoLogin.h"
#include "App/ILoopClient.h"
#include "Session/ISessionEventSink.h"
#include "Session/ServerSession.h"
#include "App/SessionState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Crt/CrtRandom.h"
#include "Input/Key.h"
#include "Render/Frame/SceneCanvas.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameScene.h"
#include "Scenes/IScene.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/LoginScene.h"
#include "Scenes/NpcNames.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneTransitions.h"
#include "Scenes/Screenshots.h"
#include "Scenes/WinMainLoop.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/WorldRuntime.h"
#include "Tables/GameData.h"
#include "UI/SlideBanner.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <ratio>
#include <string>
#include <utility>

namespace Client::App
{
std::optional<GameExit> GameExitOf(const std::optional<std::string>& fatalDataMessage,
                                   const std::optional<std::string>& sceneFailure,
                                   const std::optional<std::string>& selectionFailure, bool serverOrderedTheQuit,
                                   bool sceneWantsToQuit)
{
    // Q-C180: the message box of a data error comes before anything else closes the game.
    if (fatalDataMessage)
    {
        return GameExit{.kind = GameExitKind::DataError, .message = *fatalDataMessage};
    }
    if (sceneFailure)
    {
        return GameExit{.kind = GameExitKind::SceneFailure, .message = *sceneFailure};
    }
    if (selectionFailure)
    {
        return GameExit{.kind = GameExitKind::SelectionFailure, .message = *selectionFailure};
    }
    // Q-C106: F1 02 with 0.
    if (serverOrderedTheQuit)
    {
        return GameExit{.kind = GameExitKind::ServerOrder, .message = {}};
    }
    // Q-C69: the OK of a notice that closes the game.
    if (sceneWantsToQuit)
    {
        return GameExit{.kind = GameExitKind::PlayerQuit, .message = {}};
    }
    return std::nullopt;
}

struct GameClient::Online
{
    Online(const GameClientServer& server, Core::IClock& clock, Core::Log& log, ISessionEventSink& eventSink)
        : connection(server.socket, server.obfuscation, clock, log, server.paddingSeed),
          session(connection, clock, log, server.keys, server.settings, eventSink)
    {
        if (server.autoLogin)
        {
            autoLogin.emplace(session, *server.autoLogin, log);
        }
        // 0x49de50: one socket per friend chat room.
        if (server.chatRooms)
        {
            session.EnableChatRooms(*server.chatRooms);
        }
    }

    Network::Connection connection;
    ServerSession session;
    std::optional<AutoLogin> autoLogin;
};

GameClient::GameClient(const Tables::GameData& data, const GameClientDevices& devices, GameClientOptions options,
                       std::optional<GameClientServer> server)
    : m_data(data), m_devices(devices), m_options(std::move(options)),
      m_cloth{.random = Crt::CrtRandom(m_options.crtSeed)}, m_selection(m_world),
      m_worldEntry{.world = m_world,
                   .selection = m_selection,
                   .dataRoot = m_options.dataRoot,
                   .cloth = m_cloth,
                   .camera = m_camera,
                   .cameraInput = m_cameraInput,
                   .models = m_models,
                   .items = data.items ? &data.items->table : nullptr,
                   .itemSetTables = data.itemSets ? &*data.itemSets : nullptr,
                   .log = devices.log,
                   .clock = &devices.clock,
                   .textMeasure = &devices.textMeasure,
                   .screen = devices.screen,
                   // 0x6d5dc8, the scene that 00 and 0D read.
                   .scene = [this] { return m_manager.Current(); },
                   .texts = data.text ? &*data.text : nullptr,
                   .skills = data.skills ? &*data.skills : nullptr},
      m_eventApplier(m_worldEntry),
      m_online(server ? std::make_unique<Online>(*server, devices.clock, devices.log, m_eventApplier) : nullptr),
      m_context{.dataRoot = m_options.dataRoot,
                .textures = devices.textures,
                .log = devices.log,
                .clock = &devices.clock,
                .cloth = &m_cloth,
                .input = &devices.input,
                .pacing = m_options.pacing,
                .camera = &m_camera,
                .cameraInput = &m_cameraInput,
                .selection = &m_selection,
                .world = &m_world,
                .textMeasure = &devices.textMeasure,
                .worldEntry = &m_worldEntry,
                .clipboard = devices.clipboard},
      m_manager([this](Scenes::SceneId id) { return MakeScene(id); }, devices.frame, devices.clock,
                [this] { return PollNetwork(); }, devices.log, [this] { return TakeSceneOrder(); },
                [this](Core::Duration now) { BeforeSceneFrame(now); }, [this](Core::Duration now)
                { AfterSceneFrame(now); }, [this] { return AfterSceneStep(); }, [this] { return BeforeSceneDraw(); },
                [this](Render::Frame::SceneCanvas& canvas) { return AfterSceneDraw(canvas); })
{
    Scenes::WorldRuntime& runtime = m_world.Runtime();
    runtime.music.SetEnabled(m_options.music);
    if (m_devices.playback != nullptr)
    {
        runtime.audio = m_devices.playback;
        runtime.SetSoundVolume(static_cast<std::uint8_t>(runtime.ui.volume));
    }
    // 0x61ee30 → 0x61c1c0: Data\Macro.txt in 0x77f1db0.
    runtime.chat.input.macros = data.macros;
    // 0x61ec8f: 0x448440 keeps movereq.bmd in the list of 0x723c68.
    runtime.moves = data.moves;
    // 0x61eb69 and 0x61eb7c: Dialog.bmd in 0x7688ac0, quest.bmd in the records of [0x710148].
    runtime.dialogTable = data.dialogs;
    runtime.quest.records = data.quests;
    // 0x61ecdf: 0x54a440 keeps MonsterSkill.bmd in [0x7437670].
    runtime.monsterSkills = data.monsterSkills;
    // 0x61ed05: 0x54e7f0 keeps NpcName(%s).txt in the records of 0x7681eb8.
    runtime.npcNames = Scenes::NpcNameTableOf(data.npcNames);
    // 0x61ed18: 0x46b3b0 keeps Slide.bmd in the banner of [0x5763528].
    if (data.slides)
    {
        UI::LoadTheSlides(runtime.hud.slides, *data.slides);
    }
    // 0x4bada2..0x4bade0: the angle table, once, with WinMain's CRT.
    Scenes::InitializeGameItemRenderState(runtime.itemRender, m_cloth.random);
}

GameClient::~GameClient() = default;

Core::Expected<void, std::string> GameClient::Start()
{
    Scenes::StepTheWinMainLoop(m_winMainLoop.counters, m_cloth.random);
    return m_manager.Start(Scenes::FirstScene);
}

ILoopClient& GameClient::Loop()
{
    return m_manager;
}

std::optional<Scenes::SceneId> GameClient::CurrentScene() const
{
    return m_manager.Current();
}

bool GameClient::WantsTextInput() const
{
    return m_manager.WantsTextInput();
}

std::optional<GameExit> GameClient::Exit() const
{
    const ServerSession* session = Session();
    return GameExitOf(m_world.Runtime().fatalDataMessage, m_manager.Failure(), m_selection.Failure(),
                      session != nullptr && session->State() == SessionState::QuitOrdered, m_manager.WantsToQuit());
}

Scenes::WorldState& GameClient::World()
{
    return m_world;
}

const Scenes::WorldState& GameClient::World() const
{
    return m_world;
}

std::uint32_t GameClient::RandomState() const
{
    return m_cloth.random.State();
}

const ServerSession* GameClient::Session() const
{
    return m_online ? &m_online->session : nullptr;
}

ServerSession* GameClient::MutableSession()
{
    return m_online ? &m_online->session : nullptr;
}

std::unique_ptr<Scenes::IScene> GameClient::MakeScene(Scenes::SceneId id)
{
    ServerSession* session = MutableSession();
    switch (id)
    {
    case Scenes::SceneId::Loading:
        return std::make_unique<Scenes::LoadingScene>(m_context, &m_models);
    case Scenes::SceneId::Login:
        return std::make_unique<Scenes::LoginScene>(m_context, m_data, m_models, session, m_options.version,
                                                    m_options.loginSeed ? m_options.loginSeed() : 0,
                                                    &m_models.TextureSlots());
    case Scenes::SceneId::CharacterSelect:
        return std::make_unique<Scenes::CharacterSelectScene>(m_context, m_data, m_models, session);
    case Scenes::SceneId::WorldLoading:
        return std::make_unique<Scenes::WorldLoadingScene>(m_context);
    case Scenes::SceneId::Game:
        return std::make_unique<Scenes::GameScene>(
            m_context, session, &m_models, m_data.items ? &m_data.items->table : nullptr, &m_models.TextureSlots(),
            m_data.gates ? &*m_data.gates : nullptr, m_data.skills ? &*m_data.skills : nullptr,
            m_data.text ? &*m_data.text : nullptr,
            Scenes::GameChatTables{.filter = m_data.filter,
                                   .filterName = m_data.filterName,
                                   .setOptions = m_data.itemSets ? &m_data.itemSets->options : nullptr,
                                   .itemSetTables = m_data.itemSets ? &*m_data.itemSets : nullptr});
    }
    return nullptr;
}

Core::Expected<void, std::string> GameClient::PollNetwork()
{
    if (!m_online)
    {
        return {};
    }
    // 0x4bee60 reads the own character's speeds ([0x7681eb0]: +0x3c, +0x4c and +0x58) each time it builds a keepalive.
    const Scenes::OwnCharacterRecord& own = m_world.OwnCharacter();
    m_online->session.SetKeepaliveSpeeds(KeepaliveSpeeds{
        .attackSpeed = own.attackSpeed, .magicSpeed = own.magicSpeed, .characterFlags = own.speedAdjustBits});
    const auto polled = m_online->session.Poll();
    if (!polled.HasValue())
    {
        return polled;
    }
    if (m_online->autoLogin)
    {
        m_online->autoLogin->Poll();
    }
    return {};
}

// 0x4c363f: the scene F1 02 with 1 ordered, once.
std::optional<Scenes::SceneId> GameClient::TakeSceneOrder()
{
    return std::exchange(m_world.Runtime().orderedScene, std::nullopt);
}

// Q-C85: the original runs no frame while it connects, because its connect blocks.
bool GameClient::Connecting() const
{
    const ServerSession* session = Session();
    return session != nullptr && (session->State() == SessionState::ConnectingConnectServer ||
                                  session->State() == SessionState::ConnectingGameServer);
}

void GameClient::BeforeSceneFrame(Core::Duration now)
{
    if (m_itemFrameCarry.FramesAt(now, Scenes::OriginalFramePaceOf(m_options.pacing)) == 0)
    {
        return;
    }
    const float worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(now).count());
    Scenes::UpdateGameItemGlowPulse(m_world.Runtime().itemRender, worldClock);
}

void GameClient::AfterSceneFrame(Core::Duration now)
{
    Scenes::FinishTheWinMainLoop(m_winMainLoop, m_world, m_cloth.random, now,
                                 Scenes::OriginalFramePaceOf(m_options.pacing),
                                 m_manager.Current() == Scenes::SceneId::Game);
    // P-C110: a held frame with the screenshot requested stops at the next step or draw.
    m_screenshots.FinishTheLoop(Connecting());
}

// 0x637cf3 and 0x637cf8: the message clocks, and 0x637cfd: Print Screen, after the step of every scene.
Core::Expected<void, std::string> GameClient::AfterSceneStep()
{
    if (Connecting())
    {
        return {};
    }
    if (auto clocks = Scenes::StepTheMessageClocks(m_world.Runtime(), m_devices.textMeasure, m_devices.screen());
        !clocks.HasValue())
    {
        return clocks;
    }
    return m_screenshots.StepTheKey(m_world.Runtime().ui, m_devices.input);
}

// 0x638303: Shift is GetAsyncKeyState(0x10).
Scenes::ScreenshotFrameContext GameClient::ScreenshotFrame()
{
    const ServerSession* session = Session();
    return Scenes::ScreenshotFrameContext{.world = m_world,
                                          .measure = m_devices.textMeasure,
                                          .screen = m_devices.screen(),
                                          .shiftDown = m_devices.input.IsDown(Input::Key::Shift),
                                          .localTime = m_devices.localTime,
                                          .texts = m_data.text ? &*m_data.text : nullptr,
                                          .chosenServer = session != nullptr ? session->ChosenServer() : std::nullopt,
                                          .folder = m_options.screenshotFolder};
}

// 0x6381bb..0x63835a and 0x638586..0x6385da: the screenshot around the scene's draw, in every scene (0x637700).
Core::Expected<void, std::string> GameClient::BeforeSceneDraw()
{
    return m_screenshots.BeforeTheDraw(ScreenshotFrame());
}

Core::Expected<void, std::string> GameClient::AfterSceneDraw(Render::Frame::SceneCanvas& canvas)
{
    return m_screenshots.AfterTheDraw(ScreenshotFrame(), canvas, m_devices.screenReader);
}
} // namespace Client::App
