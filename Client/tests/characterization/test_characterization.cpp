#include "support/FrameDigest.h"

#include "Session/Credentials.h"
#include "App/GameClient.h"
#include "Scenes/ILoopClient.h"
#include "Session/ServerAddress.h"
#include "Audio/IMusicStream.h"
#include "Audio/Playback.h"
#include "Session/ServerSession.h"
#include "Core/Environment.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/LocalTime.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/IdentityObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Kernel/ActorState.h"
#include "Tables/GameData.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatLog.h"
#include "Ui/PanelRegistry.h"
#include "support/AppPrinters.h"
#include "support/BlackScreenReader.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/LetterMeasure.h"
#include "support/RecordingFrameTarget.h"
#include "support/RecordingSink.h"
#include "support/ScenePrinters.h"
#include "support/TemporaryFolder.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The behavior golden (refactor plan, AR0.3): a scene script drives the whole client, App::GameClient, frame by
// frame, and each frame's FrameDigest must match golden/<scene>.tsv. README.md says how to write a scene and when a
// golden may change.
namespace Client::Tests
{
namespace
{
constexpr auto FrameTime = std::chrono::milliseconds{40};
// main.cpp takes these from the time of day, std::random_device and the system's local time.
constexpr std::uint32_t CrtSeed = 1;
constexpr std::uint32_t LoginSeed = 1;
constexpr std::uint32_t PaddingSeed = 1;
constexpr Core::LocalTime FixedLocalTime{.month = 11, .day = 26, .hour = 12, .minute = 0};
constexpr std::string_view Version = "0.0.0";
constexpr Core::PixelSize Screen{.width = 640, .height = 480};

std::filesystem::path CharacterizationDir()
{
    return MU_CHARACTERIZATION_DIR;
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
    static constexpr std::array<std::pair<std::string_view, Input::Key>, 23> Named{{
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
        {"PrintScreen", Input::Key::PrintScreen},
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

std::string PositionOf(glm::vec3 position)
{
    return std::format("{} {} {}", position.x, position.y, position.z);
}

// The audio devices of the harness: one row per call, with the frame that made it ("start" before the first frame).
class RecordingPlayback final : public Audio::IPlayback, public Audio::IMusicStream
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop) override
    {
        Note("play-sound", std::format("{:#04x}{}{}", id, loop ? " loop" : "",
                                       origin ? " at " + PositionOf(*origin) : std::string()));
    }
    void StopSound(std::int32_t id, bool rewind) override
    {
        Note("stop-sound", std::format("{:#04x}{}", id, rewind ? " rewind" : ""));
    }
    void Play(std::string_view relativePath) override
    {
        Note("play-music", relativePath);
    }
    void Stop() override
    {
        Note("stop-music", "");
    }
    [[nodiscard]] int ProgressPercent() const override
    {
        return 0;
    }
    void SetEffectsVolume(std::uint8_t level) override
    {
        Note("volume", std::to_string(level));
    }
    void SetListener(glm::vec3 position) override
    {
        Note("listener", PositionOf(position));
    }
    void RegisterSound(std::int32_t id, std::string_view path) override
    {
        Note("register", std::format("{:#04x} {}", id, path));
    }
    void ReleaseWorldSounds() override
    {
        Note("release", "");
    }

    void SetFrame(int frame)
    {
        m_frame = std::to_string(frame);
    }
    [[nodiscard]] const std::vector<std::string>& Rows() const
    {
        return m_rows;
    }

private:
    void Note(std::string_view event, std::string_view value)
    {
        m_rows.push_back(std::format("{}\t{}\t{}", m_frame, event, value));
    }

    std::string m_frame = "start";
    std::vector<std::string> m_rows;
};

// The client of App::GameClient, with doubles for what needs a device (the socket, the clock, GL, the fonts, the
// screen reader and the audio), fixed seeds and a fixed local time, and AutoLogin choosing the first server and
// sending the account, as with --auto-login. No clipboard or friend chat rooms.
class ScriptedClient
{
public:
    ScriptedClient(const std::filesystem::path& dataRoot, const Tables::GameData& data,
                   const std::filesystem::path& screenshotFolder = {})
        : m_client(data,
                   App::GameClientDevices{.frame = m_target,
                                          .textures = m_textures,
                                          .textMeasure = m_measure,
                                          .screenReader = m_screenReader,
                                          .clock = m_clock,
                                          .log = m_log,
                                          .input = m_input,
                                          .screen = [] { return Screen; },
                                          .localTime = []() -> Core::Expected<Core::LocalTime, std::string>
                                          { return FixedLocalTime; },
                                          .playback = &m_audio,
                                          .music = &m_audio},
                   App::GameClientOptions{.dataRoot = dataRoot,
                                          .version = std::string(Version),
                                          .crtSeed = CrtSeed,
                                          .loginSeed = [] { return LoginSeed; },
                                          .screenshotFolder = screenshotFolder},
                   App::GameClientServer{
                       .socket = m_socket,
                       .obfuscation = Network::IdentityObfuscation::Shared(),
                       .keys = GameClientKeys(),
                       .settings = App::SessionSettings{.connectServer = Session::ServerAddress{"10.0.0.1", 44405}},
                       .autoLogin = App::Credentials{"user0001", "pass0002"},
                       .paddingSeed = PaddingSeed})
    {
    }

    ScriptedClient(const ScriptedClient&) = delete;
    ScriptedClient& operator=(const ScriptedClient&) = delete;
    ~ScriptedClient() = default;

    Core::Expected<void, std::string> Start()
    {
        return m_client.Start();
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
        m_audio.SetFrame(m_frame++);
        m_audioBefore = m_audio.Rows().size();
        const std::size_t sentBefore = m_socket.sent.size();
        m_target.frames.clear();
        Scenes::ILoopClient& loop = m_client.Loop();
        loop.Poll();
        loop.Step(m_input);
        m_input.ConsumeEdges();
        static_cast<void>(loop.TakeTheSceneChange());
        if (!loop.HoldsTheLastFrame())
        {
            loop.Render(0.0f);
        }
        loop.FinishTheLoop();
        static const RecordedFrame NoDraw{};
        const RecordedFrame& drawn = m_target.frames.empty() ? NoDraw : m_target.frames.back();
        m_sentThisFrame = std::span(m_socket.sent).subspan(sentBefore);
        const FrameDigest digest = DigestOf(drawn, m_sentThisFrame, m_client.RandomState(), m_client.World());
        m_clock.Advance(FrameTime);
        return digest;
    }

    // What stops main.cpp's loop, as a test failure.
    [[nodiscard]] std::optional<std::string> Failure() const
    {
        const std::optional<App::GameExit> exit = m_client.Exit();
        if (!exit)
        {
            return std::nullopt;
        }
        return exit->message.empty() ? std::string("the client quit") : exit->message;
    }

    [[nodiscard]] std::optional<Kernel::SceneId> CurrentScene() const
    {
        return m_client.CurrentScene();
    }

    [[nodiscard]] int ScreenReads() const
    {
        return m_screenReader.reads;
    }

    [[nodiscard]] const std::vector<std::string>& AudioRows() const
    {
        return m_audio.Rows();
    }

    // The last frame's scene, session state and the packets the server read from it, to write a scene.
    std::string Trace()
    {
        std::string trace = std::format("scene {} session {}", ::testing::PrintToString(m_client.CurrentScene()),
                                        ::testing::PrintToString(m_client.Session()->State()));
        for (const Network::Packet& packet : AsTheGameServerReceives(m_sentThisFrame, m_traceSerials).packets)
        {
            trace += "\n  sent " + HexOf(packet.Bytes());
        }
        for (const std::string& row : std::span(m_audio.Rows()).subspan(m_audioBefore))
        {
            const std::vector<std::string> fields = SplitTabs(row);
            trace += "\n  audio " + fields[1] + (fields.size() > 2 ? " " + fields[2] : "");
        }
        if (m_client.CurrentScene() == Kernel::SceneId::Game)
        {
            trace += WorldTrace();
        }
        return trace;
    }

private:
    // The state that FrameDigest hashes, in words.
    std::string WorldTrace()
    {
        const Kernel::WorldState& world = m_client.World();
        const Kernel::WorldRuntime& runtime = world.Runtime();
        const Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        const Ui::PanelState& panels = runtime.panels;
        std::string trace =
            std::format("\n  map {} life {} mana {} zen {} panels inventory {} npc shop {} event {}", runtime.map,
                        own.life, own.mana, own.zen, panels.inventory, panels.npcShop, panels.eventNpc);
        const auto actors = world.Actors();
        for (std::size_t index = 0; index < actors.size(); ++index)
        {
            const Kernel::ActorState& actor = actors[index];
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
            const Chat::ChatEntry& newest = runtime.chat.log.entries.front();
            trace += std::format("\n  chat {} lines, the newest {}: {} ({})", runtime.chat.log.entries.size(),
                                 newest.name, newest.text, newest.type);
        }
        return trace;
    }

    RecordingSink m_sink;
    Core::Log m_log{m_sink, Core::LogLevel::Info};
    FakeClock m_clock;
    FakeTextureStore m_textures;
    RecordingFrameTarget m_target{Screen};
    LetterMeasure m_measure;
    BlackScreenReader m_screenReader;
    Input::InputState m_input;
    FakeSocket m_socket;
    RecordingPlayback m_audio;
    App::GameClient m_client;
    Network::PacketSerials m_serverSerials;
    Network::PacketSerials m_traceSerials;
    std::span<const std::byte> m_sentThisFrame;
    std::size_t m_audioBefore = 0;
    int m_frame = 0;
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
            std::format("{} does not open: write it with MU_CHARACTERIZATION_WRITE=1", path.string()));
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

std::string AudioText(const std::vector<std::string>& rows)
{
    std::string text = "frame\tevent\tvalue\n";
    for (const std::string& row : rows)
    {
        text += row + "\n";
    }
    return text;
}

Core::Expected<std::vector<std::string>, std::string> ReadAudioGolden(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        return Core::Unexpected(
            std::format("{} does not open: write it with MU_CHARACTERIZATION_WRITE=1", path.string()));
    }
    std::vector<std::string> rows;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        rows.push_back(line);
    }
    return rows;
}

std::optional<std::string> FirstAudioDifference(const std::vector<std::string>& expected,
                                                const std::vector<std::string>& actual)
{
    for (std::size_t row = 0; row < std::min(expected.size(), actual.size()); ++row)
    {
        if (expected[row] != actual[row])
        {
            return std::format("audio row {} is \"{}\" instead of \"{}\"", row + 1, actual[row], expected[row]);
        }
    }
    if (expected.size() != actual.size())
    {
        return std::format("{} audio rows instead of {}", actual.size(), expected.size());
    }
    return std::nullopt;
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

struct SceneRun
{
    std::vector<FrameDigest> digests;
    std::vector<std::string> audio;
};

class CharacterizationTest : public GoldenDataTest
{
protected:
    void SetUp() override
    {
        ASSERT_NO_FATAL_FAILURE(GoldenDataTest::SetUp());
        auto data = Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data = std::make_unique<Tables::GameData>(std::move(data).Value());
    }

    // The digest of every frame of `scene`, from 0 to its end row, and every call to the audio device.
    SceneRun Run(std::string_view scene)
    {
        const auto script = ReadScript(CharacterizationDir() / "scenes" / std::format("{}.tsv", scene));
        EXPECT_TRUE(script.HasValue()) << script.Error();
        if (!script.HasValue())
        {
            return {};
        }
        const bool trace = EnvironmentFlag("MU_CHARACTERIZATION_TRACE");
        auto client = std::make_unique<ScriptedClient>(m_dataRoot, *m_data);
        const auto started = client->Start();
        EXPECT_TRUE(started.HasValue()) << started.Error();
        SceneRun run;
        auto row = script.Value().rows.begin();
        for (int frame = 0; frame <= script.Value().frames && started.HasValue(); ++frame)
        {
            for (; row != script.Value().rows.end() && row->frame == frame; ++row)
            {
                const auto applied = client->Apply(*row);
                EXPECT_TRUE(applied.HasValue()) << "frame " << frame << ": " << applied.Error();
            }
            run.digests.push_back(client->RunFrame());
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
        run.audio = client->AudioRows();
        return run;
    }

    // Compares `scene` with its goldens, or writes them with MU_CHARACTERIZATION_WRITE=1.
    void ExpectTheGolden(std::string_view scene)
    {
        const SceneRun run = Run(scene);
        const std::filesystem::path golden = CharacterizationDir() / "golden" / std::format("{}.tsv", scene);
        const std::filesystem::path audioGolden = CharacterizationDir() / "golden" / std::format("{}.audio.tsv", scene);
        if (EnvironmentFlag("MU_CHARACTERIZATION_WRITE"))
        {
            std::ofstream(golden, std::ios::binary) << GoldenText(run.digests);
            std::ofstream(audioGolden, std::ios::binary) << AudioText(run.audio);
            std::printf("wrote %s and %s\n", golden.string().c_str(), audioGolden.string().c_str());
            return;
        }
        const auto expected = ReadGolden(golden);
        ASSERT_TRUE(expected.HasValue()) << expected.Error();
        const std::optional<std::string> difference = FirstDifference(expected.Value(), run.digests);
        EXPECT_FALSE(difference.has_value()) << scene << ": " << difference.value_or("");
        const auto expectedAudio = ReadAudioGolden(audioGolden);
        ASSERT_TRUE(expectedAudio.HasValue()) << expectedAudio.Error();
        const std::optional<std::string> audioDifference = FirstAudioDifference(expectedAudio.Value(), run.audio);
        EXPECT_FALSE(audioDifference.has_value()) << scene << ": " << audioDifference.value_or("");
    }

    std::unique_ptr<Tables::GameData> m_data;
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

TEST_F(CharacterizationTest, IdleKeepalive)
{
    ExpectTheGolden("IdleKeepalive");
}

// Not a golden scene: the JPEG goes to a file. The ConnectServer stays silent, so the session waits for its hello.
TEST_F(CharacterizationTest, PrintScreenReadsTheScreenOnce)
{
    const TemporaryFolder folder("print-screen");
    ScriptedClient client(m_dataRoot, *m_data, folder.Path());
    const auto started = client.Start();
    ASSERT_TRUE(started.HasValue()) << started.Error();
    for (int frame = 0; frame < 12; ++frame)
    {
        static_cast<void>(client.RunFrame());
    }
    ASSERT_EQ(client.CurrentScene(), Kernel::SceneId::Login);
    for (const char* event : {"key-down", "key-up", "end", "end"})
    {
        const auto applied = client.Apply(ScriptRow{.frame = 0, .event = event, .value = "PrintScreen"});
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        static_cast<void>(client.RunFrame());
    }
    EXPECT_EQ(client.Failure(), std::nullopt);
    EXPECT_EQ(client.ScreenReads(), 1);
    EXPECT_TRUE(std::filesystem::exists(folder.Path() / "Screen(11_26-12_00)-0000.jpg"));
}

TEST_F(CharacterizationTest, IsDeterministic)
{
    const SceneRun first = Run("LoginToLorencia");
    const SceneRun second = Run("LoginToLorencia");
    ASSERT_FALSE(first.digests.empty());
    ASSERT_FALSE(first.audio.empty());
    const std::optional<std::string> difference = FirstDifference(first.digests, second.digests);
    EXPECT_FALSE(difference.has_value()) << difference.value_or("");
    const std::optional<std::string> audioDifference = FirstAudioDifference(first.audio, second.audio);
    EXPECT_FALSE(audioDifference.has_value()) << audioDifference.value_or("");
}
} // namespace
} // namespace Client::Tests
