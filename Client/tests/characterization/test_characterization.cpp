#include "support/FrameDigest.h"

#include "App/AutoLogin.h"
#include "App/Credentials.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WindowState.h"
#include "Core/Environment.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Connection.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/GameData.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameScene.h"
#include "Scenes/IScene.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/LoginScene.h"
#include "Scenes/NpcNames.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/SceneTransitions.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/Slides.h"
#include "Scenes/WinMainLoop.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/PanelRegistry.h"
#include "UI/SlideBanner.h"
#include "World/GameCamera.h"
#include "support/AppPrinters.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/LetterMeasure.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The behavior golden (refactor plan, AR0.3): a scene script drives the whole client frame by frame, composed as
// main.cpp composes it, and each frame's FrameDigest must match golden/<scene>.tsv. README.md says how to write a
// scene and when a golden may change.
namespace Client::Tests
{
namespace
{
constexpr auto FrameTime = std::chrono::milliseconds{40};
// main.cpp takes these from the time of day and std::random_device.
constexpr std::uint32_t CrtSeed = 1;
constexpr std::uint32_t LoginSeed = 1;
constexpr std::uint32_t PaddingSeed = 1;
constexpr std::string_view Version = "0.0.0";
constexpr App::PixelSize Screen{.width = 640, .height = 480};

std::filesystem::path CharacterizationDir()
{
    return MURET_CHARACTERIZATION_DIR;
}

bool EnvironmentFlag(const char* name)
{
    return Core::ReadEnvironmentVariable(name).value_or("") == "1";
}

struct ScriptRow
{
    int frame = 0;
    std::string event;
    std::string value;
};

// A scene: what happens at each frame, until the frame of its `end` row.
struct Script
{
    std::vector<ScriptRow> rows;
    int frames = 0;
};

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::istringstream stream(line);
    for (std::string field; std::getline(stream, field, '\t');)
    {
        fields.push_back(field);
    }
    return fields;
}

Core::Expected<Script, std::string> ReadScript(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        return Core::Unexpected(std::format("{} does not open", path.string()));
    }
    Script script;
    std::string line;
    std::getline(file, line);
    for (int number = 2; std::getline(file, line); ++number)
    {
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }
        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 2)
        {
            return Core::Unexpected(std::format("{}:{}: frame and event are required", path.string(), number));
        }
        ScriptRow row{.frame = std::stoi(fields[0]), .event = fields[1], .value = fields.size() > 2 ? fields[2] : ""};
        if (!script.rows.empty() && row.frame < script.rows.back().frame)
        {
            return Core::Unexpected(std::format("{}:{}: frames go up", path.string(), number));
        }
        if (row.event == "end")
        {
            script.frames = row.frame;
        }
        script.rows.push_back(std::move(row));
    }
    if (script.frames == 0)
    {
        return Core::Unexpected(std::format("{} has no end row", path.string()));
    }
    return script;
}

Core::Expected<std::vector<std::byte>, std::string> BytesOfHex(std::string_view hex)
{
    std::string digits;
    for (const char digit : hex)
    {
        if (digit != ' ')
        {
            digits.push_back(digit);
        }
    }
    if (digits.size() % 2 != 0)
    {
        return Core::Unexpected(std::format("odd hex: {}", hex));
    }
    std::vector<std::byte> bytes;
    for (std::size_t index = 0; index < digits.size(); index += 2)
    {
        bytes.push_back(static_cast<std::byte>(std::stoi(digits.substr(index, 2), nullptr, 16)));
    }
    return bytes;
}

std::optional<Input::Key> KeyNamed(std::string_view name)
{
    const auto offset = [](Input::Key first, int count)
    { return static_cast<Input::Key>(static_cast<int>(first) + count); };
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
    {
        return offset(Input::Key::A, name[0] - 'A');
    }
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
    {
        return offset(Input::Key::Digit0, name[0] - '0');
    }
    static constexpr std::array<std::pair<std::string_view, Input::Key>, 22> Named{{
        {"Escape", Input::Key::Escape},
        {"Enter", Input::Key::Enter},
        {"Space", Input::Key::Space},
        {"Tab", Input::Key::Tab},
        {"Backspace", Input::Key::Backspace},
        {"Up", Input::Key::Up},
        {"Down", Input::Key::Down},
        {"Left", Input::Key::Left},
        {"Right", Input::Key::Right},
        {"PageUp", Input::Key::PageUp},
        {"PageDown", Input::Key::PageDown},
        {"Home", Input::Key::Home},
        {"End", Input::Key::End},
        {"Delete", Input::Key::Delete},
        {"ShiftLeft", Input::Key::ShiftLeft},
        {"CtrlLeft", Input::Key::CtrlLeft},
        {"AltLeft", Input::Key::AltLeft},
        {"F1", Input::Key::F1},
        {"F2", Input::Key::F2},
        {"F3", Input::Key::F3},
        {"F4", Input::Key::F4},
        {"F5", Input::Key::F5},
    }};
    for (const auto& [keyName, key] : Named)
    {
        if (keyName == name)
        {
            return key;
        }
    }
    return std::nullopt;
}

std::optional<Input::MouseButton> ButtonNamed(std::string_view name)
{
    if (name == "left")
    {
        return Input::MouseButton::Left;
    }
    if (name == "right")
    {
        return Input::MouseButton::Right;
    }
    if (name == "middle")
    {
        return Input::MouseButton::Middle;
    }
    return std::nullopt;
}

std::string HexOf(std::span<const std::byte> bytes)
{
    std::string hex;
    for (const std::byte value : bytes)
    {
        hex += std::format("{}{:02x}", hex.empty() ? "" : " ", std::to_integer<int>(value));
    }
    return hex;
}

// The client as RunScenes in main.cpp composes it, with fakes for what needs a device (the socket, the clock, GL and
// the fonts), fixed seeds, and AutoLogin choosing the first server and sending the account, as with --auto-login. It
// leaves out the audio device, the clipboard, the friend chat rooms, the typed-text switch and the screenshots.
class ScriptedClient
{
public:
    ScriptedClient(const std::filesystem::path& dataRoot, const Scenes::GameData& data)
        : m_dataRoot(dataRoot), m_data(data), m_resources{.world = m_world,
                                                          .selection = m_selection,
                                                          .dataRoot = dataRoot,
                                                          .cloth = m_cloth,
                                                          .camera = m_camera,
                                                          .cameraInput = m_cameraInput,
                                                          .models = m_models,
                                                          .items = data.items ? &data.items->table : nullptr,
                                                          .itemSetTables = data.itemSets ? &*data.itemSets : nullptr,
                                                          .log = m_log,
                                                          .clock = &m_clock,
                                                          .textMeasure = &m_measure,
                                                          .screen = [] { return Screen; },
                                                          .scene = [this] { return m_manager.Current(); },
                                                          .texts = data.text ? &*data.text : nullptr,
                                                          .skills = data.skills ? &*data.skills : nullptr},
          m_session(m_connection, m_clock, m_log, GameClientKeys(),
                    App::SessionSettings{.connectServer = App::ServerAddress{"10.0.0.1", 44405}}, m_applier),
          m_context{.dataRoot = dataRoot,
                    .textures = m_textures,
                    .log = m_log,
                    .clock = &m_clock,
                    .cloth = &m_cloth,
                    .input = &m_input,
                    .camera = &m_camera,
                    .cameraInput = &m_cameraInput,
                    .selection = &m_selection,
                    .world = &m_world,
                    .textMeasure = &m_measure,
                    .worldEntry = &m_resources},
          m_manager([this](Scenes::SceneId id) { return MakeScene(id); }, m_target, m_clock,
                    [this] { return PollNetwork(); }, m_log,
                    [this] { return std::exchange(m_world.Runtime().orderedScene, std::nullopt); },
                    [this](Core::Duration now) { BeforeSceneFrame(now); },
                    [this](Core::Duration now) { AfterSceneFrame(now); }, [this] { return AfterSceneStep(); })
    {
        Scenes::WorldRuntime& runtime = m_world.Runtime();
        runtime.music.SetEnabled(true);
        runtime.chat.input.macros = data.macros;
        runtime.moves = data.moves;
        runtime.dialogTable = data.dialogs;
        runtime.quest.records = data.quests;
        runtime.monsterSkills = data.monsterSkills;
        runtime.npcNames = Scenes::NpcNameTableOf(data.npcNames);
        if (data.slides)
        {
            UI::LoadTheSlides(runtime.hud.slides, *data.slides);
        }
        Scenes::InitializeGameItemRenderState(runtime.itemRender, m_cloth.random);
    }

    ScriptedClient(const ScriptedClient&) = delete;
    ScriptedClient& operator=(const ScriptedClient&) = delete;
    ~ScriptedClient() = default;

    Core::Expected<void, std::string> Start()
    {
        Scenes::StepTheWinMainLoop(m_winMain, m_cloth.random);
        return m_manager.Start(Scenes::FirstScene);
    }

    Core::Expected<void, std::string> Apply(const ScriptRow& row)
    {
        if (row.event == "gs" || row.event == "gs-encrypted")
        {
            auto bytes = BytesOfHex(row.value);
            if (!bytes.HasValue())
            {
                return Core::Unexpected(bytes.Error());
            }
            m_socket.reads.push_back(row.event == "gs" ? std::move(bytes).Value()
                                                       : FromTheGameServer(bytes.Value(), m_serverSerials));
            return {};
        }
        if (row.event == "key-down" || row.event == "key-up")
        {
            const std::optional<Input::Key> key = KeyNamed(row.value);
            if (!key)
            {
                return Core::Unexpected(std::format("unknown key {}", row.value));
            }
            m_input.Apply(Input::InputEvent{.kind = row.event == "key-down" ? Input::InputEvent::Kind::KeyDown
                                                                            : Input::InputEvent::Kind::KeyUp,
                                            .key = *key});
            return {};
        }
        if (row.event == "button-down" || row.event == "button-up")
        {
            const std::optional<Input::MouseButton> button = ButtonNamed(row.value);
            if (!button)
            {
                return Core::Unexpected(std::format("unknown button {}", row.value));
            }
            m_input.Apply(Input::InputEvent{.kind = row.event == "button-down" ? Input::InputEvent::Kind::ButtonDown
                                                                               : Input::InputEvent::Kind::ButtonUp,
                                            .button = *button});
            return {};
        }
        if (row.event == "mouse")
        {
            float x = 0.0f;
            float y = 0.0f;
            std::istringstream(row.value) >> x >> y;
            m_input.Apply(Input::InputEvent{.kind = Input::InputEvent::Kind::Motion, .x = x, .y = y});
            return {};
        }
        if (row.event == "text")
        {
            Input::InputEvent event{.kind = Input::InputEvent::Kind::Text};
            event.text = row.value;
            m_input.Apply(event);
            return {};
        }
        if (row.event == "end")
        {
            return {};
        }
        return Core::Unexpected(std::format("unknown event {}", row.event));
    }

    // One frame as GameLoop runs it with one step: the network, the step, the draw unless the scene holds the last
    // frame, and the end of the loop. Then the clock moves one step.
    FrameDigest RunFrame()
    {
        const std::size_t sentBefore = m_socket.sent.size();
        m_target.frames.clear();
        m_manager.Poll();
        m_manager.Step(m_input);
        m_input.ConsumeEdges();
        static_cast<void>(m_manager.TakeTheSceneChange());
        if (!m_manager.HoldsTheLastFrame())
        {
            m_manager.Render(0.0f);
        }
        m_manager.FinishTheLoop();
        static const RecordedFrame NoDraw{};
        const RecordedFrame& drawn = m_target.frames.empty() ? NoDraw : m_target.frames.back();
        m_sentThisFrame = std::span(m_socket.sent).subspan(sentBefore);
        const FrameDigest digest = DigestOf(drawn, m_sentThisFrame, m_cloth.random.State(), m_world);
        m_clock.Advance(FrameTime);
        return digest;
    }

    // What stops main.cpp's loop, as a test failure.
    [[nodiscard]] std::optional<std::string> Failure() const
    {
        if (m_manager.Failure())
        {
            return m_manager.Failure();
        }
        if (m_selection.Failure())
        {
            return m_selection.Failure();
        }
        if (m_world.Runtime().fatalDataMessage)
        {
            return m_world.Runtime().fatalDataMessage;
        }
        if (m_manager.WantsToQuit() || m_session.State() == App::SessionState::QuitOrdered)
        {
            return "the client quit";
        }
        return std::nullopt;
    }

    // The last frame's scene, session state and the packets the server read from it, to write a scene.
    std::string Trace()
    {
        std::string trace = std::format("scene {} session {}", ::testing::PrintToString(m_manager.Current()),
                                        ::testing::PrintToString(m_session.State()));
        for (const Network::Packet& packet : AsTheGameServerReceives(m_sentThisFrame, m_traceSerials).packets)
        {
            trace += "\n  sent " + HexOf(packet.Bytes());
        }
        if (m_manager.Current() == Scenes::SceneId::Game)
        {
            trace += WorldTrace();
        }
        return trace;
    }

private:
    // The state that FrameDigest hashes, in words.
    std::string WorldTrace()
    {
        const Scenes::WorldRuntime& runtime = m_world.Runtime();
        const Scenes::OwnCharacterRecord& own = m_world.OwnCharacter();
        const UI::PanelState& panels = runtime.panels;
        std::string trace =
            std::format("\n  map {} life {} mana {} zen {} panels inventory {} npc shop {} event {}", runtime.map,
                        own.life, own.mana, own.zen, panels.inventory, panels.npcShop, panels.eventNpc);
        const auto actors = m_world.Actors();
        for (std::size_t index = 0; index < actors.size(); ++index)
        {
            const Scenes::ActorState& actor = actors[index];
            if (actor.alive)
            {
                trace += std::format("\n  actor {}{} key {:04x} type {} cell ({}, {}) action {}", index,
                                     index == runtime.hero ? " (hero)" : "", actor.key, actor.type,
                                     static_cast<int>(actor.position.x / 100.0f),
                                     static_cast<int>(actor.position.y / 100.0f), actor.animation.action);
            }
        }
        if (!runtime.chat.log.entries.empty())
        {
            const UI::ChatEntry& newest = runtime.chat.log.entries.front();
            trace += std::format("\n  chat {} lines, the newest {}: {} ({})", runtime.chat.log.entries.size(),
                                 newest.name, newest.text, newest.type);
        }
        return trace;
    }

    std::unique_ptr<Scenes::IScene> MakeScene(Scenes::SceneId id)
    {
        switch (id)
        {
        case Scenes::SceneId::Loading:
            return std::make_unique<Scenes::LoadingScene>(m_context, &m_models);
        case Scenes::SceneId::Login:
            return std::make_unique<Scenes::LoginScene>(m_context, m_data, m_models, &m_session, Version, LoginSeed,
                                                        &m_models.TextureSlots());
        case Scenes::SceneId::CharacterSelect:
            return std::make_unique<Scenes::CharacterSelectScene>(m_context, m_data, m_models, &m_session);
        case Scenes::SceneId::WorldLoading:
            return std::make_unique<Scenes::WorldLoadingScene>(m_context);
        case Scenes::SceneId::Game:
            return std::make_unique<Scenes::GameScene>(
                m_context, &m_session, &m_models, m_data.items ? &m_data.items->table : nullptr,
                &m_models.TextureSlots(), m_data.gates ? &*m_data.gates : nullptr,
                m_data.skills ? &*m_data.skills : nullptr, m_data.text ? &*m_data.text : nullptr,
                Scenes::GameChatTables{.filter = m_data.filter,
                                       .filterName = m_data.filterName,
                                       .setOptions = m_data.itemSets ? &m_data.itemSets->options : nullptr,
                                       .itemSetTables = m_data.itemSets ? &*m_data.itemSets : nullptr});
        }
        return nullptr;
    }

    Core::Expected<void, std::string> PollNetwork()
    {
        const Scenes::OwnCharacterRecord& own = m_world.OwnCharacter();
        m_session.SetKeepaliveSpeeds(App::KeepaliveSpeeds{
            .attackSpeed = own.attackSpeed, .magicSpeed = own.magicSpeed, .characterFlags = own.speedAdjustBits});
        const auto polled = m_session.Poll();
        if (!polled.HasValue())
        {
            return polled;
        }
        m_autoLogin.Poll();
        return {};
    }

    [[nodiscard]] bool Connecting() const
    {
        return m_session.State() == App::SessionState::ConnectingConnectServer ||
               m_session.State() == App::SessionState::ConnectingGameServer;
    }

    void BeforeSceneFrame(Core::Duration now)
    {
        if (m_itemFrameCarry.FramesAt(now, Scenes::OriginalFramePaceOf(m_context.pacing)) == 0)
        {
            return;
        }
        const float worldClock = static_cast<float>(std::chrono::duration<double, std::milli>(now).count());
        Scenes::UpdateGameItemGlowPulse(m_world.Runtime().itemRender, worldClock);
    }

    void AfterSceneFrame(Core::Duration now)
    {
        Scenes::WorldRuntime& runtime = m_world.Runtime();
        runtime.ui.enterPressed = false;
        const int frames = m_winMainFrameCarry.FramesAt(now, Scenes::OriginalFramePaceOf(m_context.pacing));
        const auto systemTime =
            static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        const bool gameScene = m_manager.Current() == Scenes::SceneId::Game;
        for (int frame = 0; frame < frames; ++frame)
        {
            if (gameScene)
            {
                StepTheGameMusic(runtime);
            }
            Scenes::CloseTheFrameCounter(m_cloth.random);
            Scenes::RunTheSlideTimer(m_world, m_cloth.random, systemTime, gameScene);
            Scenes::StepTheWinMainLoop(m_winMain, m_cloth.random);
        }
    }

    void StepTheGameMusic(Scenes::WorldRuntime& runtime)
    {
        Scenes::StepWorldAmbience(runtime, m_cloth.random);
        Scenes::ActorState& hero = m_world.Actors()[runtime.hero];
        runtime.music.StepWorld(runtime.audio, runtime.map, hero.safeZone != 0, runtime.heroCellLayer,
                                [this, &hero] { return Scenes::InTheDeviasChurch(hero, m_cloth.random); });
        if (runtime.map == 34)
        {
            runtime.music.StepCrywolf(runtime.audio, runtime.crywolf.stage);
            if (runtime.crywolf.stage == 5)
            {
                runtime.RequestSound(runtime.crywolf.ui.dword6c73dc == 10 ? 0x245 : 0x244);
            }
        }
        else
        {
            runtime.music.StopCrywolfBase(runtime.audio);
        }
    }

    Core::Expected<void, std::string> AfterSceneStep()
    {
        if (Connecting())
        {
            return {};
        }
        return Scenes::StepTheMessageClocks(m_world.Runtime(), m_measure, Screen);
    }

    const std::filesystem::path m_dataRoot;
    const Scenes::GameData& m_data;
    RecordingSink m_sink;
    Core::Log m_log{m_sink, Core::LogLevel::Info};
    FakeClock m_clock;
    FakeTextureStore m_textures;
    RecordingFrameTarget m_target{Screen};
    LetterMeasure m_measure;
    Render::Models::ClothWorld m_cloth{.random = Render::Models::CrtRandom(CrtSeed)};
    Input::InputState m_input;
    World::GameCamera m_camera;
    Scenes::WorldState m_world;
    World::GameCameraInput m_cameraInput;
    Scenes::SelectionState m_selection{m_world};
    Scenes::GameModels m_models;
    Scenes::WorldEntryResources m_resources;
    Scenes::SessionEventApplier m_applier{m_resources};
    FakeSocket m_socket;
    Network::Connection m_connection{m_socket, Network::IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed};
    App::ServerSession m_session;
    App::AutoLogin m_autoLogin{m_session, App::Credentials{"user0001", "pass0002"}, m_log};
    Scenes::SceneContext m_context;
    Render::Models::OriginalFrameCarry m_itemFrameCarry;
    Scenes::WinMainCounters m_winMain;
    Render::Models::OriginalFrameCarry m_winMainFrameCarry;
    Scenes::SceneManager m_manager;
    Network::PacketSerials m_serverSerials;
    Network::PacketSerials m_traceSerials;
    std::span<const std::byte> m_sentThisFrame;
};

std::string GoldenText(const std::vector<FrameDigest>& digests)
{
    std::string text = "frame\tdraw\tsent\trand\tstate\n";
    for (std::size_t frame = 0; frame < digests.size(); ++frame)
    {
        const FrameDigest& digest = digests[frame];
        text += std::format("{}\t{:016x}\t{:016x}\t{:08x}\t{:016x}\n", frame, digest.draw, digest.sent,
                            digest.randState, digest.state);
    }
    return text;
}

Core::Expected<std::vector<FrameDigest>, std::string> ReadGolden(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        return Core::Unexpected(
            std::format("{} does not open: write it with MURET_CHARACTERIZATION_WRITE=1", path.string()));
    }
    std::vector<FrameDigest> digests;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() != 5)
        {
            return Core::Unexpected(std::format("{}: a row without five fields: {}", path.string(), line));
        }
        digests.push_back(FrameDigest{.draw = std::stoull(fields[1], nullptr, 16),
                                      .sent = std::stoull(fields[2], nullptr, 16),
                                      .randState = static_cast<std::uint32_t>(std::stoul(fields[3], nullptr, 16)),
                                      .state = std::stoull(fields[4], nullptr, 16)});
    }
    return digests;
}

// The first frame that differs and which of its fields, or nothing if both lists are the same.
std::optional<std::string> FirstDifference(const std::vector<FrameDigest>& expected,
                                           const std::vector<FrameDigest>& actual)
{
    for (std::size_t frame = 0; frame < std::min(expected.size(), actual.size()); ++frame)
    {
        const FrameDigest& was = expected[frame];
        const FrameDigest& is = actual[frame];
        if (was == is)
        {
            continue;
        }
        std::string fields;
        for (const auto& [name, differs] : {std::pair{"draw", was.draw != is.draw},
                                            {"sent", was.sent != is.sent},
                                            {"rand", was.randState != is.randState},
                                            {"state", was.state != is.state}})
        {
            if (differs)
            {
                fields += std::format("{}{}", fields.empty() ? "" : ", ", name);
            }
        }
        return std::format("frame {} differs in {}", frame, fields);
    }
    if (expected.size() != actual.size())
    {
        return std::format("{} frames instead of {}", actual.size(), expected.size());
    }
    return std::nullopt;
}

class CharacterizationTest : public GoldenDataTest
{
protected:
    void SetUp() override
    {
        ASSERT_NO_FATAL_FAILURE(GoldenDataTest::SetUp());
        auto data = Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data = std::make_unique<Scenes::GameData>(std::move(data).Value());
    }

    // The digest of every frame of `scene`, from 0 to its end row.
    std::vector<FrameDigest> Run(std::string_view scene)
    {
        const auto script = ReadScript(CharacterizationDir() / "scenes" / std::format("{}.tsv", scene));
        EXPECT_TRUE(script.HasValue()) << script.Error();
        if (!script.HasValue())
        {
            return {};
        }
        const bool trace = EnvironmentFlag("MURET_CHARACTERIZATION_TRACE");
        auto client = std::make_unique<ScriptedClient>(m_dataRoot, *m_data);
        const auto started = client->Start();
        EXPECT_TRUE(started.HasValue()) << started.Error();
        std::vector<FrameDigest> digests;
        auto row = script.Value().rows.begin();
        for (int frame = 0; frame <= script.Value().frames && started.HasValue(); ++frame)
        {
            for (; row != script.Value().rows.end() && row->frame == frame; ++row)
            {
                const auto applied = client->Apply(*row);
                EXPECT_TRUE(applied.HasValue()) << "frame " << frame << ": " << applied.Error();
            }
            digests.push_back(client->RunFrame());
            if (trace)
            {
                std::printf("frame %d: %s\n", frame, client->Trace().c_str());
            }
            if (const auto failure = client->Failure())
            {
                ADD_FAILURE() << "frame " << frame << ": " << *failure;
                break;
            }
        }
        return digests;
    }

    // Compares `scene` with its golden, or writes it with MURET_CHARACTERIZATION_WRITE=1.
    void ExpectTheGolden(std::string_view scene)
    {
        const std::vector<FrameDigest> digests = Run(scene);
        const std::filesystem::path golden = CharacterizationDir() / "golden" / std::format("{}.tsv", scene);
        if (EnvironmentFlag("MURET_CHARACTERIZATION_WRITE"))
        {
            std::ofstream(golden, std::ios::binary) << GoldenText(digests);
            std::printf("wrote %s\n", golden.string().c_str());
            return;
        }
        const auto expected = ReadGolden(golden);
        ASSERT_TRUE(expected.HasValue()) << expected.Error();
        const std::optional<std::string> difference = FirstDifference(expected.Value(), digests);
        EXPECT_FALSE(difference.has_value()) << scene << ": " << difference.value_or("");
    }

    std::unique_ptr<Scenes::GameData> m_data;
};

TEST_F(CharacterizationTest, LoginToLorencia)
{
    ExpectTheGolden("LoginToLorencia");
}

TEST_F(CharacterizationTest, WalkAndFight)
{
    ExpectTheGolden("WalkAndFight");
}

TEST_F(CharacterizationTest, InventoryShopAndChat)
{
    ExpectTheGolden("InventoryShopAndChat");
}

TEST_F(CharacterizationTest, EventEntry)
{
    ExpectTheGolden("EventEntry");
}

TEST_F(CharacterizationTest, IsDeterministic)
{
    const std::vector<FrameDigest> first = Run("LoginToLorencia");
    const std::vector<FrameDigest> second = Run("LoginToLorencia");
    ASSERT_FALSE(first.empty());
    const std::optional<std::string> difference = FirstDifference(first, second);
    EXPECT_FALSE(difference.has_value()) << difference.value_or("");
}
} // namespace
} // namespace Client::Tests
