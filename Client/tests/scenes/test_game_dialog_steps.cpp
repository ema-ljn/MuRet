#include "Network/Xor32Packet.h"
#include "Kernel/MouseState.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Hud/Dialogs/GameDialogSteps.h"

#include "Audio/Playback.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/DialogTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Session/Logout.h"
#include "Protocol/Guild/Guild.h"
#include "Protocol/Player/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Kernel/ActorState.h"
#include "Events/EventGlobals.h"
#include "Hud/GameUi.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Quests/QuestState.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/Dialogs.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Ui/MessageArea.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
#include "Sound/SoundRequests.h"

#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Npc/NpcWindowState.h"
#include "Items/HudItems.h"
#include "Ui/GameUiContext.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <array>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The game's dialogs in the step (CL8 milestone 6): the dispatch of 0x623f78, the Esc menu with its exits (the F3 30
// of 0x61c210, Macro.txt and the C3 F1 02), the options (0x93), the boxes with buttons and the generic OK. Made-up
// texts; the world in Lorencia (0) and in Devias (2).
namespace
{
using Client::Crt::CrtRandom;
using Client::Input::InputState;
using Client::Kernel::WorldState;
using Client::Ui::GameUiContext;

class DialogPlayback final : public Client::Audio::IPlayback
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3>, bool) override
    {
        onSound(Client::Audio::SoundId{id});
    }
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}

    std::function<void(Client::Audio::SoundId)> onSound;
};

// 0x61c210: the own record opened and closed.
constexpr int OptionDataRandoms = 3848;
// The C3 F1 02: the padding and the serial.
constexpr int LogoutRandoms = 2;
// 0x623fdf: the y of each menu button, (6 + 3i) · 10, and an x inside.
constexpr std::int32_t MenuX = 0x110;
constexpr std::int32_t ExitGameY = 65;
constexpr std::int32_t AnotherServerY = 95;
constexpr std::int32_t AnotherCharacterY = 125;
constexpr std::int32_t OptionsY = 155;
constexpr std::int32_t ChosenSlot = 2;
constexpr std::size_t ChaosBoxText = 592;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class TypedLetters final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

struct Request
{
    std::string kind;
    std::vector<std::uint8_t> bytes;
    std::string text;
};

class RecordingRequests final : public Client::Ui::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::Player::OptionBody& body) override
    {
        requests.push_back({"F3 30", {body.begin(), body.end()}, {}});
    }
    void RequestLogout(std::uint8_t kind) override
    {
        requests.push_back({"F1 02", {kind}, {}});
    }
    void RequestPartyList() override
    {
        requests.push_back({"42", {}, {}});
    }
    void SendGuildPacket(Client::Protocol::Outgoing packet) override
    {
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : packet.packet.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        requests.push_back({"guild", std::move(bytes), {}});
    }
    // The packet as it goes out (without the XOR32) and whether it goes in C3.
    void SendGamePacket(Client::Protocol::Outgoing outgoing) override
    {
        if (onGamePacket)
            onGamePacket();
        const Client::Network::Packet decoded = Client::Network::Xor32Decode(outgoing.packet);
        std::vector<std::uint8_t> bytes;
        for (const std::byte b : decoded.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(b));
        requests.push_back(
            {outgoing.delivery == Client::Network::Delivery::Encrypted ? "C3" : "C1", std::move(bytes), {}});
    }
    void SendWindowClose() override
    {
        requests.push_back({"31", {}, {}});
    }
    void SendSetQuest(std::uint8_t quest, std::uint8_t state) override
    {
        requests.push_back({"A2", {quest, state}, {}});
    }
    void SendFriendPacket(Client::Protocol::Outgoing packet) override
    {
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : packet.packet.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        requests.push_back({"amigos", std::move(bytes), {}});
    }
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view content) override
    {
        requests.push_back({"Macro.txt", {}, std::string(content)});
        return {};
    }
    void Log(std::string_view line) override
    {
        requests.push_back({"log", {}, std::string(line)});
    }

    [[nodiscard]] std::vector<std::string> Kinds() const
    {
        std::vector<std::string> kinds;
        for (const Request& request : requests)
            kinds.push_back(request.kind);
        return kinds;
    }
    [[nodiscard]] const Request& Of(std::string_view kind) const
    {
        return *std::ranges::find(requests, kind, &Request::kind);
    }

    std::vector<Request> requests;
    std::function<void()> onGamePacket;
};

class SteppingDialogs
{
public:
    explicit SteppingDialogs(std::int32_t map)
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.dialogs.current = Client::Ui::EscapeMenuDialog;
        auto& keys = world.OwnCharacter().skillKeys[ChosenSlot];
        keys.fill(0xff);
        keys[0] = 3;
        keys[2] = 0;
        world.OwnCharacter().words6c[0] = 0x44332211u;
        runtime.chat.input.macros[0] = "hola";
        runtime.chat.input.macros[3] = "chau";
    }

    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x, std::int32_t y)
    {
        return Client::Hud::StepTheGameDialogs(GameUiContext{.world = world,
                                                             .random = random,
                                                             .input = input,
                                                             .mouseX = x,
                                                             .mouseY = y,
                                                             .measure = &letters,
                                                             .texts = &texts,
                                                             .chosenSlot = std::uint8_t{ChosenSlot},
                                                             .requests = &requests});
    }
    Client::Core::Expected<void, std::string> Click(std::int32_t x, std::int32_t y)
    {
        Client::Kernel::LatchTheLeftButton(world.Runtime().mouse, true, true);
        return Dialogs(x, y);
    }

    WorldState world;
    CrtRandom random;
    InputState input;
    TypedLetters letters;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{ChaosBoxText, "caja abierta"}});
    RecordingRequests requests;
};

// 0x61c210: [key] the skill byte of its slot, [10] the flags, [11..13] Q, W and E, [14] the chat log.
TEST(GameDialogStepsTest, TheOptionBodyCarriesTheKeysTheFlagsTheSlotsAndTheChatWindow)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.ui.autoAttack = false;
    runtime.chat.noticeSound = true;
    runtime.chat.slidesOn = false;
    runtime.hud.quickSlots.types = {0x1c01, 0x1c05, 0x1c09};
    runtime.ui.chatModes.placement = 1;
    runtime.chat.log.rows = 9;
    runtime.chat.log.alpha = 0.5f;
    const CrtRandom expected = Advanced(tested.random, OptionDataRandoms);

    const auto body = Client::Hud::Dialogs::OptionBodyOf(tested.world, tested.random, std::uint8_t{ChosenSlot});

    ASSERT_TRUE(body.HasValue()) << body.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(runtime.ownRecordDepth, 0);
    const Client::Protocol::Player::OptionBody& bytes = body.Value();
    EXPECT_EQ(bytes[3], 0x11);
    EXPECT_EQ(bytes[0], 0x33);
    EXPECT_EQ(bytes[1], 0);
    EXPECT_EQ(bytes[10], 0x02 | 0x04 | 0x10);
    EXPECT_EQ(bytes[11], 0x01);
    EXPECT_EQ(bytes[12], 0x05);
    EXPECT_EQ(bytes[13], 0x09);
    EXPECT_EQ(bytes[14], 0x35);
    EXPECT_EQ(bytes[15], 0);
}

// Q-C551: the chat log without its place goes with 0 lines, and the 0.7 transparency an F3 30 left comes back as 6.
TEST(GameDialogStepsTest, TheChatWindowLosesItsRowsWithoutPlaceAndTruncatesTheAlpha)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.ui.chatModes.placement = 0;
    runtime.chat.log.rows = 9;
    runtime.chat.log.alpha = static_cast<float>(7.0 * static_cast<double>(0.1f));

    const auto body = Client::Hud::Dialogs::OptionBodyOf(tested.world, tested.random, std::uint8_t{ChosenSlot});

    ASSERT_TRUE(body.HasValue()) << body.Error();
    EXPECT_EQ(body.Value()[14], 0x06);
    EXPECT_EQ(body.Value()[10], 0x01 | 0x08);
}

TEST(GameDialogStepsTest, TheOptionBodyCutsWithoutAChosenSlotOrWithAKeyOutsideTheStack)
{
    SteppingDialogs tested(Lorencia);
    const auto unchosen = Client::Hud::Dialogs::OptionBodyOf(tested.world, tested.random, std::nullopt);
    ASSERT_FALSE(unchosen.HasValue());
    EXPECT_NE(unchosen.Error().find("0x61c2c6"), std::string::npos);
    tested.world.OwnCharacter().skillKeys[ChosenSlot][5] = 24;
    const auto outside = Client::Hud::Dialogs::OptionBodyOf(tested.world, tested.random, std::uint8_t{ChosenSlot});
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x61c2e8"), std::string::npos);
}

// 0x624036: the log, the F3 30, Macro.txt and the C3 F1 02 with 0; 3848 + 2 rand(), and the menu closes.
TEST(GameDialogStepsTest, ExitingTheGameSavesTheOptionsAndAsksToQuit)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SteppingDialogs tested(map);
        auto& runtime = tested.world.Runtime();
        const CrtRandom expected = Advanced(tested.random, OptionDataRandoms + LogoutRandoms);

        const auto stepped = tested.Click(MenuX, ExitGameY);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(tested.requests.Kinds(), (std::vector<std::string>{"log", "F3 30", "Macro.txt", "F1 02"}));
        EXPECT_EQ(tested.requests.Of("F1 02").bytes,
                  std::vector<std::uint8_t>{Client::Protocol::Session::LogoutQuitsTheGame});
        EXPECT_EQ(tested.requests.Of("F3 30").bytes[3], 0x11);
        EXPECT_EQ(tested.requests.Of("Macro.txt").text, "hola\r\n\r\n\r\nchau\r\n\r\n\r\n\r\n\r\n\r\n\r\n");
        EXPECT_TRUE(runtime.ui.logoutRequested);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    }
}

// 0x6247ee
TEST(GameDialogStepsTest, AnotherServerAsksToLeaveBeforeSavingTheOptions)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SteppingDialogs tested(map);
        const CrtRandom expected = Advanced(tested.random, OptionDataRandoms + LogoutRandoms);

        ASSERT_TRUE(tested.Click(MenuX, AnotherServerY).HasValue());

        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(tested.requests.Kinds(), (std::vector<std::string>{"log", "F1 02", "F3 30", "Macro.txt"}));
        EXPECT_EQ(tested.requests.Of("F1 02").bytes,
                  std::vector<std::uint8_t>{Client::Protocol::Session::LogoutReturnsToServerList});
    }
}

// 0x624eec
TEST(GameDialogStepsTest, AnotherCharacterSavesTheOptionsAndAsksForTheSelection)
{
    SteppingDialogs tested(Devias);
    ASSERT_TRUE(tested.Click(MenuX, AnotherCharacterY).HasValue());
    EXPECT_EQ(tested.requests.Kinds(), (std::vector<std::string>{"log", "F3 30", "Macro.txt", "F1 02"}));
    EXPECT_EQ(tested.requests.Of("F1 02").bytes,
              std::vector<std::uint8_t>{Client::Protocol::Session::LogoutReturnsToSelection});
}

// With the chaos box open, the error 592 instead of the F1 02, without its two rand().
TEST(GameDialogStepsTest, TheChaosBoxKeepsThePlayerInTheGame)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SteppingDialogs tested(map);
        auto& runtime = tested.world.Runtime();
        runtime.panels.chaosBox = 1;
        const CrtRandom expected = Advanced(tested.random, OptionDataRandoms);

        ASSERT_TRUE(tested.Click(MenuX, ExitGameY).HasValue());

        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(tested.requests.Kinds(), (std::vector<std::string>{"log", "F3 30", "Macro.txt"}));
        EXPECT_FALSE(runtime.ui.logoutRequested);
        EXPECT_TRUE(std::ranges::any_of(runtime.chat.area.rows, [](const Client::Ui::MessageAreaRow& row)
                                        { return row.text == "caja abierta" && row.type == 2; }));
        EXPECT_EQ(runtime.dialogs.current, 0);
    }
}

// 0x625538: the options replace the menu.
TEST(GameDialogStepsTest, TheOptionsButtonOpensTheOptions)
{
    SteppingDialogs tested(Lorencia);
    ASSERT_TRUE(tested.Click(MenuX, OptionsY).HasValue());
    EXPECT_EQ(tested.world.Runtime().dialogs.current, Client::Ui::OptionsDialog);
    EXPECT_EQ(tested.world.Runtime().dialogs.pending, 0);
    EXPECT_TRUE(tested.requests.requests.empty());
}

// 0x625563
TEST(GameDialogStepsTest, TheOptionRowsToggleTheirSettings)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::OptionsDialog;
    ASSERT_TRUE(tested.Click(0x104, 0x3c).HasValue());
    EXPECT_FALSE(runtime.ui.autoAttack);
    ASSERT_TRUE(tested.Click(0x17b, 0x5a + 0x15).HasValue());
    EXPECT_TRUE(runtime.chat.noticeSound);
    ASSERT_TRUE(tested.Click(0x110, 0x96).HasValue());
    EXPECT_FALSE(runtime.chat.slidesOn);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    // Outside the rows, nothing.
    ASSERT_TRUE(tested.Click(0x17c, 0x3c).HasValue());
    EXPECT_FALSE(runtime.ui.autoAttack);
    // Closing goes on to the pending one.
    runtime.dialogs.pending = 0x8b;
    ASSERT_TRUE(tested.Click(0x110, 0xe8).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x8b);
    EXPECT_EQ(runtime.dialogs.pending, 0);
}

// 0x6258e7..0x6258fa versus 0x628280..0x6282c3: the options sound before closing; the OK, after.
TEST(GameDialogStepsTest, OptionsAndGenericOkKeepTheirNativeSoundAndPendingDialogOrder)
{
    for (const bool options : {true, false})
    {
        SteppingDialogs tested(Lorencia);
        auto& runtime = tested.world.Runtime();
        runtime.dialogs.current = options ? Client::Ui::OptionsDialog : 0x72;
        runtime.dialogs.pending = 0x8b;
        DialogPlayback playback;
        int sounds = 0;
        playback.onSound = [&](Client::Audio::SoundId id)
        {
            ++sounds;
            EXPECT_EQ(id, Client::Audio::SoundId{0x19});
            EXPECT_EQ(runtime.dialogs.current, options ? Client::Ui::OptionsDialog : 0x8b);
            EXPECT_EQ(runtime.dialogs.pending, options ? 0x8b : 0);
            EXPECT_EQ(runtime.mouse.updateTime, 0);
            EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
        };
        runtime.audio = &playback;
        Client::Sound::SoundRequests playbackRequests(&playback);
        runtime.sounds = &playbackRequests;
        ASSERT_TRUE(tested.Click(options ? 0x110 : 0x161, options ? 0xe8 : 0x76).HasValue());
        EXPECT_EQ(sounds, 1);
        EXPECT_EQ(runtime.dialogs.current, 0x8b);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_FALSE(runtime.mouse.pendingClick);
    }
}

// 0x628299 destroys the audio synchronously before the sound attempt of 0x6282c3.
TEST(GameDialogStepsTest, QuittingOkDialogsKeepTheCloseStateWithoutAnAudibleClick)
{
    for (const std::int32_t id : {0x70, 0x6f, 0x1a, 0x1c})
    {
        SCOPED_TRACE(id);
        SteppingDialogs tested(Devias);
        auto& runtime = tested.world.Runtime();
        runtime.dialogs.current = id;
        runtime.dialogs.pending = 0x8b;
        DialogPlayback playback;
        int sounds = 0;
        playback.onSound = [&](Client::Audio::SoundId) { ++sounds; };
        runtime.audio = &playback;
        Client::Sound::SoundRequests playbackRequests(&playback);
        runtime.sounds = &playbackRequests;
        runtime.ui.enterPressed = true;
        runtime.mouse.updateTime = 20;
        ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
        EXPECT_EQ(sounds, 0);
        EXPECT_TRUE(runtime.ui.destroyRequested);
        EXPECT_FALSE(runtime.ui.enterPressed);
        EXPECT_EQ(runtime.dialogs.current, 0x8b);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_EQ(runtime.mouse.updateTime, 0);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
    }
}

// Q-C550: going back leaves the menu waiting and the options open, until they are closed.
TEST(GameDialogStepsTest, GoingBackLeavesTheMenuWaitingBehindTheOptions)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::OptionsDialog;
    ASSERT_TRUE(tested.Click(0x110, 0x1e).HasValue());
    EXPECT_EQ(runtime.dialogs.current, Client::Ui::OptionsDialog);
    EXPECT_EQ(runtime.dialogs.pending, Client::Ui::EscapeMenuDialog);
    ASSERT_TRUE(tested.Click(0x110, 0xe8).HasValue());
    EXPECT_EQ(runtime.dialogs.current, Client::Ui::EscapeMenuDialog);
}

// 0x62568a: the click on the bar grabs it and turns the click back on; the wheel adds and subtracts between 0 and 9.
TEST(GameDialogStepsTest, TheVolumeBarAndTheWheelSetTheVolume)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::OptionsDialog;
    runtime.ui.volumeBarX = 0x140;
    runtime.ui.volumeBarY = 0x7f;

    ASSERT_TRUE(tested.Click(0x140 + 17, 0x80).HasValue());
    EXPECT_EQ(runtime.ui.volume, 3);
    EXPECT_EQ(runtime.ui.volumeDragging, 1);
    EXPECT_TRUE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.ui.volumeRequest, -1045);

    // Grabbed, the mouse outside its height still moves it; without the click, it lets go.
    ASSERT_TRUE(tested.Dialogs(0x140 + 0x31, 0x79).HasValue());
    EXPECT_EQ(runtime.ui.volume, 9);
    runtime.mouse.pendingClick = false;
    ASSERT_TRUE(tested.Dialogs(0x140, 0x80).HasValue());
    EXPECT_EQ(runtime.ui.volumeDragging, 0);
    EXPECT_EQ(runtime.ui.volume, 9);

    runtime.ui.wheel = 2;
    ASSERT_TRUE(tested.Dialogs(0x110, 0x78).HasValue());
    EXPECT_EQ(runtime.ui.volume, 9);
    EXPECT_EQ(runtime.ui.wheel, 0);
    runtime.ui.wheel = -1;
    ASSERT_TRUE(tested.Dialogs(0x110, 0x78).HasValue());
    EXPECT_EQ(runtime.ui.volume, 8);
    EXPECT_EQ(runtime.ui.volumeRequest, -193);
    // With the mouse outside the row, the wheel stays.
    runtime.ui.wheel = -1;
    ASSERT_TRUE(tested.Dialogs(0x110, 0x77).HasValue());
    EXPECT_EQ(runtime.ui.wheel, -1);
    runtime.ui.volume = 1;
    ASSERT_TRUE(tested.Dialogs(0x110, 0x78).HasValue());
    EXPECT_EQ(runtime.ui.volume, 0);
    EXPECT_EQ(runtime.ui.volumeRequest, -10000);
}

// 0x625856: the glow takes the box on click and neither spends the click nor sounds.
TEST(GameDialogStepsTest, TheGlowBoxesKeepTheClick)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::Ui::OptionsDialog;
    ASSERT_TRUE(tested.Click(0x10e + 0x14 + 0x12, 0xda).HasValue());
    EXPECT_EQ(runtime.ui.glowDetail, 1);
    EXPECT_TRUE(runtime.mouse.pendingClick);
    EXPECT_FALSE(runtime.lastSoundId.has_value());
    ASSERT_TRUE(tested.Click(0x10e, 0xc8).HasValue());
    EXPECT_EQ(runtime.ui.glowDetail, 0);
}

// 0x6263df: the generic OK closes with its button or with Enter, which turns off; the ones that close the game send
// WM_DESTROY.
TEST(GameDialogStepsTest, TheGenericOkClosesWithItsButtonOrEnter)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = 0x72;
    ASSERT_TRUE(tested.Dialogs(0x11c, 0x62).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x72);
    ASSERT_TRUE(tested.Click(0x162, 0x62).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x72);
    ASSERT_TRUE(tested.Click(0x161, 0x76).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_FALSE(runtime.mouse.pendingClick);

    runtime.dialogs.current = Client::Ui::DisconnectDialog;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_FALSE(runtime.ui.enterPressed);
    EXPECT_TRUE(runtime.ui.destroyRequested);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

// 0x62d756
TEST(GameDialogStepsTest, TheBoxesCloseWithAButtonOrEnter)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = 0;
    Client::Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, "aviso");
    ASSERT_TRUE(tested.Dialogs(0x47 + 0xd5, 0x8c + 0x3c).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x8a);
    EXPECT_EQ(runtime.dialogBox.buttons[0].state, 2);
    ASSERT_TRUE(tested.Click(0x47 + 0xd5, 0x8c + 0x3c).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});

    Client::Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, "aviso");
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_FALSE(runtime.ui.enterPressed);
}

// 0x62a7f7 and 0x62bb1d: the yes to the party invitation (0x77) spends the click, sends the C1 05 41 01 with the key
// of 0x78aba4c in C3 (two rand()) and closes (0x62c4ac).
TEST(GameDialogStepsTest, YesToThePartyInvitationSendsTheAnswerInC3)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = 0x77;
    runtime.social.party.requestKey = 0x1234;
    const CrtRandom expected = Advanced(tested.random, 2);
    ASSERT_TRUE(tested.Click(0xea, 0x62).HasValue());
    ASSERT_EQ(tested.requests.requests.size(), 1u);
    EXPECT_EQ(tested.requests.requests[0].kind, "C3");
    EXPECT_EQ(tested.requests.requests[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x06, 0x41, 0x01, 0x12, 0x34}));
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
}

// 0x62c50e and 0x62d4a2: the no to the trade request (0x78) sends the C1 04 37 00 in clear, with the button or with
// Esc.
TEST(GameDialogStepsTest, NoToTheTradeRequestSendsTheRefusalInClear)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = 0x78;
    ASSERT_TRUE(tested.Click(0x14e + 0x45, 0x76).HasValue());
    ASSERT_EQ(tested.requests.requests.size(), 1u);
    EXPECT_EQ(tested.requests.requests[0].kind, "C1");
    EXPECT_EQ(tested.requests.requests[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x00}));
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(tested.random.State(), CrtRandom().State());

    runtime.dialogs.current = 0x78;
    tested.input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    ASSERT_EQ(tested.requests.requests.size(), 2u);
    EXPECT_EQ(tested.requests.requests[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x00}));
    EXPECT_EQ(runtime.dialogs.current, 0);

    // The yes, with the invitation pending: the C1 04 37 01, and the invitation stays open without the no looking at
    // it.
    runtime.dialogs.current = 0x78;
    runtime.dialogs.pending = 0x77;
    ASSERT_TRUE(tested.Click(0xea + 0x45, 0x62).HasValue());
    ASSERT_EQ(tested.requests.requests.size(), 3u);
    EXPECT_EQ(tested.requests.requests[2].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x01}));
    EXPECT_EQ(runtime.dialogs.current, 0x77);
    // Outside the buttons, nothing.
    ASSERT_TRUE(tested.Click(0xea + 0x46, 0x62).HasValue());
    EXPECT_EQ(tested.requests.requests.size(), 3u);
}

// 0x623f17 → 0x657880(0x1b): Esc with no dialog closes the lowest open window and sends its own through the session:
// the quests (0x8) the C1 03 31 (0x419900), the trade (0x20) the C1 03 3D in C3 (0x657fe1) and another's shop (0x800,
// the same byte as 0x20000) the C1 10 3F 07 with the owner's key and name (0x5eb17f). None opens the menu.
TEST(GameDialogStepsTest, EscapeClosesTheLowestWindowWithWhatItSends)
{
    const auto escape = [](SteppingDialogs& tested)
    {
        tested.world.Runtime().dialogs.current = 0;
        tested.input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown,
                                                     .key = Client::Input::Key::Escape});
        return tested.Dialogs(0, 0);
    };
    for (const std::int32_t map : {Lorencia, Devias})
    {
        {
            auto tested = std::make_unique<SteppingDialogs>(map);
            tested->world.Runtime().panels.delegated.quest = Client::Quests::QuestListWindow;
            tested->world.Runtime().panels.trade = 1;
            tested->requests.onGamePacket = [&tested]
            {
                EXPECT_EQ(tested->world.Runtime().panels.delegated.quest, 0);
                EXPECT_EQ(tested->world.Runtime().panels.trade, 1);
            };
            const auto expected = tested->random;
            const auto stepped = escape(*tested);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested->requests.Kinds(), (std::vector<std::string>{"C1"}));
            EXPECT_EQ(tested->requests.requests[0].bytes, (std::vector<std::uint8_t>{0xc1, 3, 0x31}));
            EXPECT_EQ(tested->world.Runtime().panels.delegated.quest, 0);
            EXPECT_EQ(tested->world.Runtime().panels.trade, 1);
            EXPECT_EQ(tested->world.Runtime().dialogs.current, 0);
            EXPECT_EQ(tested->random.State(), expected.State());
        }
        {
            auto tested = std::make_unique<SteppingDialogs>(map);
            tested->world.Runtime().panels.trade = 1;
            tested->world.Runtime().panels.inventory = 1;
            const auto stepped = escape(*tested);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested->requests.Kinds(), (std::vector<std::string>{"C3"}));
            EXPECT_EQ(tested->requests.requests[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x03, 0x3d}));
            EXPECT_EQ(tested->world.Runtime().panels.trade, 0);
            EXPECT_EQ(tested->world.Runtime().panels.inventory, 0);
            EXPECT_EQ(tested->world.Runtime().dialogs.current, 0);
        }
        {
            auto tested = std::make_unique<SteppingDialogs>(map);
            Client::Social::PersonalShopState& shop = tested->world.Runtime().social.personalShop;
            tested->world.Runtime().panels.personalShop = 1;
            shop.mode = Client::Social::OtherShopView;
            shop.ownerKey = 0x1234;
            shop.ownerName[0] = std::byte{'C'};
            shop.ownerName[1] = std::byte{'e'};
            const auto stepped = escape(*tested);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            ASSERT_EQ(tested->requests.Kinds(), (std::vector<std::string>{"C3"}));
            std::vector<std::uint8_t> expected{0xc1, 0x10, 0x3f, 0x07, 0x12, 0x34, 'C', 'e'};
            expected.resize(0x10);
            EXPECT_EQ(tested->requests.requests[0].bytes, expected);
            EXPECT_EQ(tested->world.Runtime().panels.personalShop, 0);
            EXPECT_EQ(shop.mode, Client::Social::NoShopView);
            EXPECT_EQ(shop.ownerKey, 0);
        }
    }
}

// 0x62c560: the yes of the 0x78 with Esc and a dialog of another milestone pending leads to that dialog's no stretch,
// which stops with its address and its milestone (08 P-C363), after the C1 04 37 01. In Lorencia and in Devias.
TEST(GameDialogStepsTest, TheNoOfTheOtherHitosDialogsCutsWithItsOwner)
{
    struct OtherNo
    {
        std::int32_t id;
        std::string_view address;
        std::string_view owner;
    };
    for (const std::int32_t map : {Lorencia, Devias})
    {
        // The 0x7d is not here: its no is of milestone 8 (LeaveNumberTest.NoClearsTheBufferButNotTheEdit).
        // Nor the 0x7f (WarRequestTest).
        for (const OtherNo& other : {OtherNo{0x71, "0x62c58f", "selección"}, OtherNo{0x76, "0x62c880", "no abre"}})
        {
            auto tested = std::make_unique<SteppingDialogs>(map);
            auto& runtime = tested->world.Runtime();
            runtime.dialogs.current = 0x78;
            runtime.dialogs.pending = other.id;
            tested->input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown,
                                                          .key = Client::Input::Key::Escape});
            const auto stepped = tested->Click(0xea + 0x45, 0x62);
            ASSERT_FALSE(stepped.HasValue()) << other.id;
            EXPECT_NE(stepped.Error().find(other.address), std::string::npos) << stepped.Error();
            EXPECT_NE(stepped.Error().find(other.owner), std::string::npos) << stepped.Error();
            EXPECT_NE(stepped.Error().find("P-C363"), std::string::npos) << stepped.Error();
            ASSERT_EQ(tested->requests.requests.size(), 1u);
            EXPECT_EQ(tested->requests.requests[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x01}));
            EXPECT_EQ(runtime.dialogs.current, other.id);
        }
    }
}

// The war request (0x7f, the 61 of 0x4e4382): the yes (0x62b39c) and the no (0x62c5c2), with the 0x46 buttons of the
// 0x77. The hero (0) and the 5 are of guild 1, the 6 of the rival (2, marked at 2 by the 62) and the 7 without a guild.
// In Lorencia and in Devias.
class WarRequestTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    static constexpr std::int32_t YesX = 0xea;
    static constexpr std::int32_t NoX = 0x14e + 0x45;
    static constexpr std::int32_t ButtonY = 0x62;

    WarRequestTest() : m_tested(std::make_unique<SteppingDialogs>(GetParam()))
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        runtime.dialogs.current = 0x7f;
        Client::Guild::GuildState& guild = runtime.guild;
        guild.warOn = 0;
        guild.rivalIndex = 2;
        guild.rival = {'T', 'e', 'r', 'o', 's', '\0', '\0', '\0', '\0'};
        const auto mark = [this](std::size_t index, std::uint16_t guildIndex, std::uint8_t byte374)
        {
            Client::Kernel::ActorState& actor = m_tested->world.Actors()[index];
            actor.alive = true;
            actor.word1f2 = guildIndex;
            actor.byte374 = byte374;
        };
        mark(0, 1, 1);
        mark(5, 1, 1);
        mark(6, 2, 2);
        mark(7, 0xffff, 0);
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_tested->world.Runtime();
    }
    std::uint8_t MarkOf(std::size_t index)
    {
        return m_tested->world.Actors()[index].byte374;
    }

    // The C1 04 61 of Protocol::Guild::WriteWarAnswer, as it goes out (with the XOR32).
    static std::vector<std::uint8_t> AnswerBytes(bool accept)
    {
        const Client::Protocol::Outgoing packet = Client::Protocol::Guild::WriteWarAnswer(accept);
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : packet.packet.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        return bytes;
    }

    // 0x4c1800 did not run: the rival and the marks, as they were.
    void ExpectTheRivalKept()
    {
        EXPECT_EQ(Runtime().guild.rivalIndex, 2);
        EXPECT_EQ(Runtime().guild.rival[0], 'T');
        EXPECT_EQ(MarkOf(6), 2);
    }

    // 0x4c1800: no war and no rival; +0x374 at 1 for the hero's guild and at 0 for the others.
    void ExpectTheWarCleared()
    {
        const Client::Guild::GuildState& guild = Runtime().guild;
        EXPECT_EQ(guild.warOn, 0);
        EXPECT_EQ(guild.rivalIndex, -1);
        EXPECT_EQ(guild.rival[0], '\0');
        EXPECT_EQ(guild.rival[1], 'e');
        EXPECT_EQ(MarkOf(0), 1);
        EXPECT_EQ(MarkOf(5), 1);
        EXPECT_EQ(MarkOf(6), 0);
        EXPECT_EQ(MarkOf(7), 0);
    }

    std::unique_ptr<SteppingDialogs> m_tested;
};

// 0x62b39c: the yes spends the click, sends the C1 04 61 01 with the XOR32 and in clear (no rand()) and closes
// (0x62c4ac), without touching the rival: the GS's 62 starts the war.
TEST_P(WarRequestTest, YesSendsTheAcceptanceAndKeepsTheRival)
{
    ASSERT_TRUE(m_tested->Click(YesX, ButtonY).HasValue());

    ASSERT_EQ(m_tested->requests.requests.size(), 1u);
    EXPECT_EQ(m_tested->requests.requests[0].kind, "guild");
    EXPECT_EQ(m_tested->requests.requests[0].bytes, AnswerBytes(true));
    EXPECT_EQ(m_tested->random.State(), CrtRandom().State());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Runtime().mouse.updateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    ExpectTheRivalKept();
}

// 0x62c5c2..0x62c87b: the no sends the C1 04 61 00 and calls 0x4c1800 twice; then the close (0x62da70) with the
// pending one.
TEST_P(WarRequestTest, NoSendsTheRefusalAndClearsTheWar)
{
    Runtime().dialogs.pending = 0x77;
    ASSERT_TRUE(m_tested->Click(NoX, ButtonY + 0x14).HasValue());

    ASSERT_EQ(m_tested->requests.requests.size(), 1u);
    EXPECT_EQ(m_tested->requests.requests[0].kind, "guild");
    EXPECT_EQ(m_tested->requests.requests[0].bytes, AnswerBytes(false));
    EXPECT_EQ(m_tested->random.State(), CrtRandom().State());
    EXPECT_EQ(Runtime().dialogs.current, 0x77);
    EXPECT_EQ(Runtime().dialogs.pending, 0);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    ExpectTheWarCleared();
}

TEST_P(WarRequestTest, EscapeIsTheNoAndOutsideTheButtonsNothing)
{
    ASSERT_TRUE(m_tested->Click(YesX + 0x46, ButtonY).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());
    EXPECT_EQ(Runtime().dialogs.current, 0x7f);
    ExpectTheRivalKept();

    m_tested->input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(m_tested->Dialogs(0, 0).HasValue());
    ASSERT_EQ(m_tested->requests.requests.size(), 1u);
    EXPECT_EQ(m_tested->requests.requests[0].bytes, AnswerBytes(false));
    EXPECT_EQ(Runtime().dialogs.current, 0);
    ExpectTheWarCleared();
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, WarRequestTest, ::testing::Values(Lorencia, Devias));

// The OK of the 0x73 (0x626460), with Enter: the zen typed for the trade. Each case starts from Seed in Lorencia and in
// Devias, with the trade open (the byte of 0x20, one rand() in IsOpen) and 1000 own zen.
class MoneyOkTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    static constexpr std::uint32_t Seed = 0x0c0ffee5u;
    // 0x40b4d0: the own record, on closing.
    static constexpr int OwnRecordRandoms = 3848;

    MoneyOkTest() : m_tested(std::make_unique<SteppingDialogs>(GetParam()))
    {
        m_tested->random = CrtRandom(Seed);
        Client::Kernel::WorldRuntime& runtime = Runtime();
        runtime.dialogs.current = 0x73;
        runtime.panels.trade = 1;
        runtime.social.dialogFieldLimit = 8;
        runtime.social.moneyField = 1;
        runtime.chat.input.fieldCount = 1;
        m_tested->world.OwnCharacter().zen = 1000;
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_tested->world.Runtime();
    }

    Client::Core::Expected<void, std::string> Enter(std::int32_t typed)
    {
        Runtime().social.typedMoney = typed;
        Runtime().ui.enterPressed = true;
        return m_tested->Dialogs(0, 0);
    }

    // 0x628215..0x6282c3: what the OK leaves in every case.
    void ExpectTheDialogFieldsReset()
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        EXPECT_FALSE(runtime.ui.enterPressed);
        EXPECT_EQ(runtime.social.dialogFieldLimit, 0x2a);
        EXPECT_EQ(runtime.chat.input.fieldCount, 2);
        EXPECT_EQ(runtime.social.moneyField, 0);
        EXPECT_FALSE(runtime.ui.chatOpen);
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    }

    std::vector<Request>& Sent()
    {
        return m_tested->requests.requests;
    }

    std::unique_ptr<SteppingDialogs> m_tested;
};

// More than 100000000 (signed): the 0x75 in its place, without looking at the trade or drawing rand().
TEST_P(MoneyOkTest, TooMuchMoneyOpensTheWarning)
{
    ASSERT_TRUE(Enter(100'000'001).HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x75);
    EXPECT_EQ(Runtime().dialogs.pending, 0);
    EXPECT_TRUE(Sent().empty());
    EXPECT_EQ(m_tested->random.State(), CrtRandom(Seed).State());
    ExpectTheDialogFieldsReset();
}

// More than the own zen plus what is already in the trade (unsigned, so also a negative): the 0x74. The own record
// opens and closes (3848 rand() after the one of IsOpen).
TEST_P(MoneyOkTest, MoreThanTheOwnMoneyOpensTheOtherWarning)
{
    Runtime().social.trade.ownMoney = 200;
    for (const std::int32_t typed : {1201, -1})
    {
        SCOPED_TRACE(typed);
        Runtime().dialogs.current = 0x73;
        const CrtRandom expected = Advanced(m_tested->random, 1 + OwnRecordRandoms);
        ASSERT_TRUE(Enter(typed).HasValue());
        EXPECT_EQ(Runtime().dialogs.current, 0x74);
        EXPECT_TRUE(Sent().empty());
        EXPECT_EQ(m_tested->random.State(), expected.State());
        ExpectTheDialogFieldsReset();
    }
}

// With the zen: the own OK turns off with the C1 04 3C 00 in C3 (two rand() before the record's close), 0x78abac8 at
// 0x96 with zen already set, the zen at 0x57678ac and in the C1 08 3A 00 with the XOR32 and in clear, and the 0x73
// closes towards the pending one.
TEST_P(MoneyOkTest, TheMoneyGoesInTheThreeA)
{
    Client::Social::TradeState& trade = Runtime().social.trade;
    trade.ownOk = 1;
    trade.ownMoney = 200;
    Runtime().dialogs.pending = 0x77;
    const CrtRandom expected = Advanced(m_tested->random, 1 + 2 + OwnRecordRandoms);
    ASSERT_TRUE(Enter(1200).HasValue());
    ASSERT_EQ(Sent().size(), 2u);
    EXPECT_EQ(Sent()[0].kind, "C3");
    EXPECT_EQ(Sent()[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
    EXPECT_EQ(Sent()[1].kind, "C1");
    EXPECT_EQ(Sent()[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x08, 0x3a, 0x00, 0xb0, 0x04, 0x00, 0x00}));
    EXPECT_EQ(m_tested->random.State(), expected.State());
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(trade.okWait, 0x96);
    EXPECT_EQ(trade.sentMoney, 1200u);
    EXPECT_EQ(Runtime().dialogs.current, 0x77);
    EXPECT_EQ(Runtime().dialogs.pending, 0);
    ExpectTheDialogFieldsReset();

    // Without own OK or zen set: only the 3A, and the wait stays.
    trade.ownMoney = 0;
    trade.okWait = 3;
    Runtime().dialogs.current = 0x73;
    const CrtRandom again = Advanced(m_tested->random, 1 + OwnRecordRandoms);
    ASSERT_TRUE(Enter(0).HasValue());
    ASSERT_EQ(Sent().size(), 3u);
    EXPECT_EQ(Sent()[2].bytes, (std::vector<std::uint8_t>{0xc1, 0x08, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(m_tested->random.State(), again.State());
    EXPECT_EQ(trade.okWait, 3);
    EXPECT_EQ(trade.sentMoney, 0u);
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// Without the trade, the zen goes to the warehouse (0x6270ed, Npc::MoveTheWarehouseMoney, CL9 milestone 6): putting in
// more zen than the inventory has opens the 0x74 without sending anything, after the rand() of IsOpen and with the own
// record opened and closed; the fields come back as with the trade.
TEST_P(MoneyOkTest, WithoutTheTradeTheMoneyGoesToTheWarehouse)
{
    Runtime().panels.trade = 0;
    Runtime().social.trade.field78abacc = 0;
    const CrtRandom expected = Advanced(m_tested->random, 1 + OwnRecordRandoms);
    const auto stepped = Enter(1001);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(Sent().empty());
    EXPECT_EQ(m_tested->random.State(), expected.State());
    EXPECT_EQ(Runtime().dialogs.current, 0x74);
    ExpectTheDialogFieldsReset();
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, MoneyOkTest, ::testing::Values(Lorencia, Devias));

// The dialog 0x7d (CL9 milestone 8): the number to kick a member, disband the guild or leave it. Each case starts in
// Lorencia and in Devias with the number EDIT ([0x5763508]) visible, focused and with "1234567" typed (what the drawing
// leaves, 0x630af0), slot 2 of 0x787a5e8 with a made-up name and 0x6d5e2c at 2 (0x64e553); the buffer 0x7798bf8 holds
// an old chat message.
class LeaveNumberTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    static constexpr std::int32_t LeaveDialog = 0x7d;
    static constexpr std::int32_t KickedSlot = 2;
    // 0x630ba1 and 0x630c24: the yes in [0x143, 0x16b) and the no in [0x175, 0x19d), both in [0x62, 0x77).
    static constexpr std::int32_t YesX = 0x143;
    static constexpr std::int32_t NoX = 0x175;
    static constexpr std::int32_t ButtonY = 0x62;

    LeaveNumberTest() : m_tested(std::make_unique<SteppingDialogs>(GetParam()))
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        runtime.dialogs.current = LeaveDialog;
        auto& slot = runtime.guild.members[KickedSlot];
        std::ranges::copy(std::string_view("Tala"), slot.name.begin());
        runtime.guild.kickMember = KickedSlot;
        Client::Ui::ChatInputState& input = runtime.chat.input;
        Client::Ui::CopyIntoTheBuffer(input.messageBuffer, "hola que tal");
        input.messageLength = 12;
        Client::Ui::ShowTheNumberField(input, 0);
        Client::Ui::FocusTheNumberField(input);
        input.numberField.filter = 9;
        input.numberField.text.SetLimit(7);
        Client::Ui::TypeInTheChat(input, "1234567");
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_tested->world.Runtime();
    }
    Client::Ui::ChatInputState& Input()
    {
        return Runtime().chat.input;
    }

    // The C1 17 53 of Protocol::Guild::WriteLeave with the name and the ten bytes, as it goes out (with the XOR32).
    static std::vector<std::uint8_t> LeaveBytes(std::string_view name, std::string_view number)
    {
        Client::Protocol::FrontEnd::NameBytes characterName{};
        for (std::size_t index = 0; index < name.size(); ++index)
            characterName[index] = static_cast<std::byte>(name[index]);
        Client::Protocol::Guild::LeaveNumber leaveNumber{};
        for (std::size_t index = 0; index < number.size(); ++index)
            leaveNumber[index] = static_cast<std::byte>(number[index]);
        const Client::Protocol::Outgoing packet = Client::Protocol::Guild::WriteLeave(characterName, leaveNumber);
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : packet.packet.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        return bytes;
    }

    std::unique_ptr<SteppingDialogs> m_tested;
};

// 0x62a878..0x62b38d: the EDIT's GetText to 0x7798bf8 (its 7 digits and the '\0'; the chat's stays behind) and
// SetText(0); the C1 17 53 with the name of slot 2 and the ten bytes of 0x7798bf8, in clear and without rand(); the
// ten bytes and the length at 0, and the close (0x62c4ac) with the 0x19. The EDIT stays visible and focused (Q-C1295).
TEST_P(LeaveNumberTest, YesSendsTheLeaveWithTheTypedNumber)
{
    ASSERT_TRUE(m_tested->Click(YesX, ButtonY).HasValue());

    ASSERT_EQ(m_tested->requests.requests.size(), 1u);
    EXPECT_EQ(m_tested->requests.requests[0].kind, "guild");
    EXPECT_EQ(m_tested->requests.requests[0].bytes, LeaveBytes("Tala", std::string_view("1234567\0 t", 10)));
    const std::vector<std::uint8_t>& sent = m_tested->requests.requests[0].bytes;
    ASSERT_EQ(sent.size(), 0x17u);
    EXPECT_EQ(sent[0], 0xc1);
    EXPECT_EQ(sent[1], 0x17);
    EXPECT_EQ(sent[2], 0x53);
    EXPECT_EQ(m_tested->random.State(), CrtRandom().State());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_FALSE(Runtime().mouse.pendingClick);
    // The ten bytes and the length at 0; what comes after, as it was.
    for (std::size_t index = 0; index < 10; ++index)
        EXPECT_EQ(Input().messageBuffer[index], '\0') << index;
    EXPECT_EQ(Input().messageBuffer[10], 'a');
    EXPECT_EQ(Input().messageLength, 0);
    EXPECT_EQ(Input().numberField.text.Value(), "");
    EXPECT_EQ(Input().numberField.base.state, 0);
    EXPECT_EQ(Input().focus, Client::Ui::ChatFocus::NumberField);
}

// The master who disbands (0x64de93 leaves 0x6d5e2c at 0): the name of slot 0. Outside the 40 of the narrow yes,
// nothing.
TEST_P(LeaveNumberTest, TheDisbandSendsTheFirstSlot)
{
    std::ranges::copy(std::string_view("Ceibal"), Runtime().guild.members[0].name.begin());
    Runtime().guild.kickMember = 0;
    ASSERT_TRUE(m_tested->Click(YesX + 0x28, ButtonY).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());
    EXPECT_EQ(Runtime().dialogs.current, LeaveDialog);
    ASSERT_TRUE(m_tested->Click(YesX - 1, ButtonY).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());

    ASSERT_TRUE(m_tested->Click(YesX + 0x27, ButtonY + 0x14).HasValue());
    ASSERT_EQ(m_tested->requests.requests.size(), 1u);
    EXPECT_EQ(m_tested->requests.requests[0].bytes, LeaveBytes("Ceibal", std::string_view("1234567\0 t", 10)));
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x62c567: the no (the button or Esc) sends nothing; it empties the ten bytes and the length, but not the EDIT, and
// closes (0x62da7a) with the 0x19.
TEST_P(LeaveNumberTest, NoClearsTheBufferButNotTheEdit)
{
    ASSERT_TRUE(m_tested->Click(NoX, ButtonY).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().lastSoundId, Client::Audio::SoundId{0x19});
    for (std::size_t index = 0; index < 10; ++index)
        EXPECT_EQ(Input().messageBuffer[index], '\0') << index;
    EXPECT_EQ(Input().messageBuffer[10], 'a');
    EXPECT_EQ(Input().messageLength, 0);
    EXPECT_EQ(Input().numberField.text.Value(), "1234567");
    EXPECT_EQ(m_tested->random.State(), CrtRandom().State());

    Runtime().dialogs.current = LeaveDialog;
    Client::Ui::CopyIntoTheBuffer(Input().messageBuffer, "otra");
    m_tested->input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(m_tested->Dialogs(0, 0).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Client::Ui::CStringOf(Input().messageBuffer), "");
}

// 0x62a91c: 0x6d5e2c at −1 (the .data one) reads before 0x787a5e8 (Q-C1296), and without a sender it stops.
TEST_P(LeaveNumberTest, WithoutAMemberOrRequestsItCuts)
{
    Runtime().guild.kickMember = -1;
    const auto outside = m_tested->Click(YesX, ButtonY);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x62a91c"), std::string::npos) << outside.Error();
    EXPECT_TRUE(m_tested->requests.requests.empty());

    // The stop leaves the EDIT already empty (GetText and SetText(0) go before the name).
    EXPECT_EQ(Input().numberField.text.Value(), "");
    Runtime().guild.kickMember = KickedSlot;
    Client::Kernel::LatchTheLeftButton(Runtime().mouse, true, true);
    const auto withoutRequests = Client::Hud::StepTheGameDialogs(GameUiContext{.world = m_tested->world,
                                                                               .random = m_tested->random,
                                                                               .input = m_tested->input,
                                                                               .mouseX = YesX,
                                                                               .mouseY = ButtonY,
                                                                               .measure = &m_tested->letters,
                                                                               .texts = &m_tested->texts,
                                                                               .chosenSlot = std::uint8_t{ChosenSlot},
                                                                               .requests = nullptr});
    ASSERT_FALSE(withoutRequests.HasValue());
    EXPECT_NE(withoutRequests.Error().find("0x62a878"), std::string::npos) << withoutRequests.Error();
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, LeaveNumberTest, ::testing::Values(Lorencia, Devias));

// Dialog.bmd with dialog 3 (one line and two answers: the first leads to 5 and the second to none) and 5, with no
// answers, which leads to 3.
Client::Tables::DialogTable AnswersTable()
{
    Client::Tables::DialogTable table(Client::Tables::DialogCount);
    const auto write = [](auto& field, std::string_view text) { std::ranges::copy(text, field.begin()); };
    write(table[3].text, "hola viajero");
    table[3].answerCount = 2;
    table[3].next = {5, -1};
    write(table[3].answers[0], "seguir");
    write(table[3].answers[1], "chau");
    write(table[5].text, "nada por ahora");
    table[5].next = {3};
    return table;
}

// 0x62d91d..0x62da57: with the text of 3 at y = ((7 − 2 − 1) · 0x12) / 2 + 0x4c = 112, the first answer goes from 130
// to 147 and the second from 148 to 165, no more than 0x6a from x = 0x13f. The click on one moves the pending one to
// open and, with a next dialog, shows it; Enter does not close the 0x8d.
TEST(GameDialogStepsTest, TheAnswersLeadToTheirNextDialog)
{
    const auto tested = std::make_unique<SteppingDialogs>(Lorencia);
    auto& runtime = tested->world.Runtime();
    runtime.dialogTable = AnswersTable();
    tested->texts = Client::Tests::MakeTextTable({{Client::Ui::NoAnswersText, "nada mas"}});
    runtime.dialogs.current = 0;
    ASSERT_TRUE(
        Client::Ui::OpenTheAnswersDialog(runtime.dialogBox, runtime.dialogs, &runtime.dialogTable, &tested->texts, 3)
            .HasValue());
    ASSERT_EQ(runtime.dialogs.current, Client::Ui::AnswersDialog);
    ASSERT_EQ(runtime.dialogBox.lineCount, 1);
    ASSERT_EQ(runtime.dialogBox.answerCount, 2);

    runtime.ui.enterPressed = true;
    for (const auto& [x, y] : std::to_array<std::pair<std::int32_t, std::int32_t>>(
             {{0x13f, 129}, {0x13f, 166}, {0x13f + 0x6b, 140}, {0x13f - 0x6b, 140}}))
    {
        SCOPED_TRACE(std::format("{} {}", x, y));
        ASSERT_TRUE(tested->Click(x, y).HasValue());
        EXPECT_EQ(runtime.dialogs.current, Client::Ui::AnswersDialog);
        EXPECT_EQ(runtime.dialogBox.dialogIndex, 3);
        EXPECT_TRUE(runtime.mouse.pendingClick);
        runtime.mouse.pendingClick = false;
    }
    EXPECT_TRUE(runtime.ui.enterPressed);
    EXPECT_FALSE(runtime.lastSoundId.has_value());

    // The first, on the right edge: the 5, with no answers ("1) " and the 609), stays open.
    runtime.mouse.updateTime = 4;
    runtime.mouse.updateTimeMax = 12;
    ASSERT_TRUE(tested->Click(0x13f + 0x6a, 147).HasValue());
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
    EXPECT_EQ(runtime.mouse.updateTime, 0);
    EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
    EXPECT_EQ(runtime.dialogs.current, Client::Ui::AnswersDialog);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 5);
    EXPECT_EQ(Client::Ui::DialogAnswer(runtime.dialogBox, 0), "1) nada mas");

    // Dialog.bmd's count 0 lets the first be chosen (max(0, 1)), which goes back to 3; with another pending, the 3
    // waits.
    runtime.dialogs.pending = Client::Ui::MessageBoxDialog;
    ASSERT_TRUE(tested->Click(0x13f, 131 + 9).HasValue());
    EXPECT_EQ(runtime.dialogs.current, Client::Ui::MessageBoxDialog);
    EXPECT_EQ(runtime.dialogs.pending, Client::Ui::AnswersDialog);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 3);

    // The second of 3 leads to none: it closes.
    runtime.dialogs.current = Client::Ui::AnswersDialog;
    runtime.dialogs.pending = 0;
    ASSERT_TRUE(tested->Click(0x13f, 150).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.dialogs.pending, 0);
}

// 0x62d9b8
TEST(GameDialogStepsTest, AnAnswerWithoutItsDialogIsCut)
{
    const auto tested = std::make_unique<SteppingDialogs>(Lorencia);
    auto& runtime = tested->world.Runtime();
    runtime.dialogs.current = Client::Ui::AnswersDialog;
    runtime.dialogBox.answerCount = 1;
    const auto stepped = tested->Click(0x13f, ((7 - 1) * 0x12) / 2 + 0x4c);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x62d9b8"), std::string::npos) << stepped.Error();
}

// The stretches of other waves stop with their address; an id not in the table goes to the generic OK.
// 0x94 opened by 0x633300(4, 0x173, 0x94), with the buttons (1, 0x15, 0x5a, 0x46, 0x15) and (3, 0x78, 0x5a, 0x46,
// 0x15) and notice 3 of 0x78abb14 (the trade).
void OpenTheWarning(Client::Kernel::WorldRuntime& runtime, std::int32_t id = 0x94)
{
    runtime.dialogs.current = id;
    runtime.dialogs.pending = 0;
    runtime.dialogBox.buttons = {Client::Ui::DialogButton{1, 0x15, 0x5a, 0x46, 0x15},
                                 Client::Ui::DialogButton{3, 0x78, 0x5a, 0x46, 0x15}};
    runtime.social.trade.warningKind = 3;
    runtime.social.trade.warningByte = 0xff;
}

// 0x6282dc..0x62a7e6: the first ([0xea, 0x130] × [0xbe, 0xd3], with the edges) answers 1 and the second ([0x14d,
// 0x193]) 2, both with the close and the sound; no rand(). With the mouse over it, the state is 2 or 4, and the click
// always turns off.
TEST(GameDialogStepsTest, TheWarningAnswersWithItsTwoButtons)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        SteppingDialogs tested(map);
        auto& runtime = tested.world.Runtime();
        OpenTheWarning(runtime);
        runtime.lastSoundId = Client::Audio::SoundId{0};
        ASSERT_TRUE(tested.Click(0x15 + 0xd5, 0x5a + 0x64).HasValue());
        EXPECT_EQ(runtime.social.trade.warningByte, 1);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x19});
        EXPECT_EQ(runtime.mouse.updateTimeMax, 6);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.dialogBox.buttons[0].state, 2);
        EXPECT_EQ(runtime.dialogBox.buttons[1].state, 3);

        OpenTheWarning(runtime);
        runtime.dialogs.pending = 0x77;
        ASSERT_TRUE(tested.Click(0x78 + 0xd5 + 0x46, 0x5a + 0x64 + 0x15).HasValue());
        EXPECT_EQ(runtime.social.trade.warningByte, 2);
        EXPECT_EQ(runtime.dialogs.current, 0x77);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_EQ(runtime.dialogBox.buttons[1].state, 4);

        // One pixel outside: the click is spent anyway and there is no answer.
        OpenTheWarning(runtime);
        ASSERT_TRUE(tested.Click(0x78 + 0xd5 + 0x47, 0x5a + 0x64).HasValue());
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.social.trade.warningByte, 0xff);
        EXPECT_EQ(runtime.dialogs.current, 0x94);
        EXPECT_EQ(runtime.dialogBox.buttons[0].state, 1);
        EXPECT_EQ(runtime.dialogBox.buttons[1].state, 3);
        EXPECT_EQ(tested.random.State(), CrtRandom().State());
        EXPECT_TRUE(tested.requests.requests.empty());
    }
}

// 0x62845d: the low price 0x99 goes to the same end as the 0x94: the first answers 1 and Esc, 2.
TEST(GameDialogStepsTest, TheLowPriceWarningAnswersLikeTheWarning)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    OpenTheWarning(runtime, Client::Ui::MoneyTextDialog);
    ASSERT_TRUE(tested.Click(0x15 + 0xd5 + 0x46, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.social.trade.warningByte, 1);
    EXPECT_EQ(runtime.dialogs.current, 0);

    OpenTheWarning(runtime, Client::Ui::MoneyTextDialog);
    tested.input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.social.trade.warningByte, 2);
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(tested.random.State(), CrtRandom().State());
}

// 0x628409..0x628435: Esc picks the second button, 2.
TEST(GameDialogStepsTest, EscapeAnswersNoToTheWarning)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    OpenTheWarning(runtime);
    tested.input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.social.trade.warningByte, 2);
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_TRUE(tested.requests.requests.empty());
}

TEST(GameDialogStepsTest, TheOtherWavesDialogsAreCut)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    for (const Client::Hud::Dialogs::DialogRoute& route : Client::Hud::Dialogs::DialogRoutes())
    {
        if (route.step != Client::Hud::Dialogs::DialogStep::Cut)
            continue;
        runtime.dialogs.current = route.id;
        const auto stepped = tested.Dialogs(0, 0);
        ASSERT_FALSE(stepped.HasValue()) << route.id;
        EXPECT_NE(stepped.Error().find(route.address), std::string::npos) << stepped.Error();
        EXPECT_NE(stepped.Error().find(route.wave), std::string::npos) << stepped.Error();
    }
    EXPECT_EQ(Client::Hud::Dialogs::DialogRouteOf(0x40).step, Client::Hud::Dialogs::DialogStep::GenericOk);
}
} // namespace

TEST(GameDialogStepsTest, TheWarehouseConfirmationSendsItsPasswordRequestAndBothAnswersClearTheNumber)
{
    for (const bool accept : {false, true})
    {
        auto tested = std::make_unique<SteppingDialogs>(Devias);
        auto& runtime = tested->world.Runtime();
        runtime.dialogs.current = 0x95;
        runtime.chat.input.messageBuffer[0] = '1';
        runtime.chat.input.messageBuffer[1] = '2';
        runtime.chat.input.messageLength = 2;
        runtime.npcWindows.typedNumber = {'4', '3', '2', '1'};
        const auto before = tested->random;
        const auto stepped = tested->Click(accept ? 0x143 : 0x175, 0x62);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(tested->random.State(), Advanced(before, accept ? 2 : 0).State());
        if (accept)
        {
            ASSERT_EQ(tested->requests.requests.size(), 1u);
            std::vector<std::uint8_t> expected{0xc1, 0x10, 0x83, 1, 0xe1, 0x10, '1', '2'};
            expected.resize(0x10);
            EXPECT_EQ(tested->requests.requests[0].bytes, expected);
            EXPECT_EQ(tested->requests.requests[0].kind, "C3");
            EXPECT_EQ(runtime.chat.input.messageLength, 0);
            EXPECT_EQ(runtime.chat.input.messageBuffer[0], 0);
        }
        else
        {
            EXPECT_TRUE(tested->requests.requests.empty());
            EXPECT_EQ(runtime.chat.input.messageLength, 0);
            EXPECT_EQ(runtime.chat.input.messageBuffer[0], 0);
        }
    }
}

TEST(GameDialogStepsTest, TheA7ClosesOnlyInsideItsInclusiveButtonWithoutConsumingTheClick)
{
    auto tested = std::make_unique<SteppingDialogs>(Lorencia);
    auto& runtime = tested->world.Runtime();
    runtime.dialogs.current = 0xa7;
    runtime.dialogs.pending = 0x70;
    runtime.events.guildWarOutcome.present = 2;
    runtime.events.guildWarOutcome.firstName.fill('A');
    runtime.events.guildWarOutcome.secondName.fill('B');
    runtime.events.guildWarOutcome.firstScore = 4;
    runtime.events.guildWarOutcome.secondScore = 7;
    runtime.events.timeValue = 300;
    runtime.lastSoundId = Client::Audio::SoundId{55};
    const auto before = tested->random;
    const auto outside = tested->Click(0x11c, 0x106);
    ASSERT_TRUE(outside.HasValue()) << outside.Error();
    EXPECT_EQ(runtime.dialogs.current, 0xa7);
    const auto inside = tested->Click(0x163, 0x11a);
    ASSERT_TRUE(inside.HasValue()) << inside.Error();
    EXPECT_EQ(runtime.dialogs.current, 0x70);
    EXPECT_EQ(runtime.events.guildWarOutcome.present, 0);
    EXPECT_EQ(runtime.events.guildWarOutcome.firstName.back(), 0);
    EXPECT_EQ(runtime.events.guildWarOutcome.secondName.back(), 0);
    EXPECT_EQ(runtime.events.guildWarOutcome.firstScore, 0);
    EXPECT_EQ(runtime.events.guildWarOutcome.secondScore, 0);
    EXPECT_EQ(runtime.events.timeValue, 0);
    EXPECT_EQ(runtime.dialogs.pending, 0);
    EXPECT_TRUE(runtime.mouse.pendingClick);
    EXPECT_EQ(tested->random.State(), before.State());
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{55});
    EXPECT_TRUE(tested->requests.requests.empty());
}
