#include "Network/Xor32Packet.h"
#include "Scenes/GameDialogSteps.h"

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
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/EventGlobals.h"
#include "Scenes/GameUi.h"
#include "Scenes/GuildState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/QuestState.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatInput.h"
#include "UI/ChatLog.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"

#include "support/SceneDataFiles.h"

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

// Los diálogos del juego en el paso (CL8 hito 6): el despacho de 0x623f78, el menú de Esc con sus salidas (el F3 30
// de 0x61c210, Macro.txt y el C3 F1 02), las opciones (0x93), las cajas con botones y el OK genérico. Textos
// inventados; el mundo, en Lorencia (0) y en Devias (2).
namespace
{
using Client::Crt::CrtRandom;
using Client::Input::InputState;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

class DialogPlayback final : public Client::Audio::IPlayback
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3>, bool) override
    {
        onSound(id);
    }
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}

    std::function<void(std::int32_t)> onSound;
};

// 0x61c210: el registro propio abierto y cerrado.
constexpr int OptionDataRandoms = 3848;
// El C3 F1 02: el relleno y el serial.
constexpr int LogoutRandoms = 2;
// 0x623fdf: la y de cada botón del menú, (6 + 3i) · 10, y una x adentro.
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

// Lo que piden los botones, en su orden.
struct Request
{
    std::string kind;
    std::vector<std::uint8_t> bytes;
    std::string text;
};

class RecordingRequests final : public Client::Scenes::IGameUiRequests
{
public:
    void SendOptionData(const Client::Protocol::OptionData::OptionBody& body) override
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
    // El paquete como sale (sin el XOR32) y si va en C3.
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
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.dialogs.current = Client::UI::EscapeMenuDialog;
        // Las teclas del elegido: el lugar 0 con la tecla 3 y el 2 con la 0; los demás sin tecla.
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
        return Client::Scenes::StepTheGameDialogs(GameUiContext{.world = world,
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
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true);
        return Dialogs(x, y);
    }

    WorldState world;
    CrtRandom random;
    InputState input;
    TypedLetters letters;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{ChaosBoxText, "caja abierta"}});
    RecordingRequests requests;
};

// 0x61c210: [tecla] el byte de la skill de su lugar, [10] las banderas, [11..13] Q, W y E, [14] el registro.
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

    const auto body = Client::Scenes::OptionBodyOf(tested.world, tested.random, std::uint8_t{ChosenSlot});

    ASSERT_TRUE(body.HasValue()) << body.Error();
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(runtime.ownRecordDepth, 0);
    const Client::Protocol::OptionData::OptionBody& bytes = body.Value();
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

// Q-C551: el registro sin su lugar va con 0 renglones, y la transparencia 0,7 que dejó un F3 30 vuelve como 6.
TEST(GameDialogStepsTest, TheChatWindowLosesItsRowsWithoutPlaceAndTruncatesTheAlpha)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.ui.chatModes.placement = 0;
    runtime.chat.log.rows = 9;
    runtime.chat.log.alpha = static_cast<float>(7.0 * static_cast<double>(0.1f));

    const auto body = Client::Scenes::OptionBodyOf(tested.world, tested.random, std::uint8_t{ChosenSlot});

    ASSERT_TRUE(body.HasValue()) << body.Error();
    EXPECT_EQ(body.Value()[14], 0x06);
    EXPECT_EQ(body.Value()[10], 0x01 | 0x08);
}

TEST(GameDialogStepsTest, TheOptionBodyCutsWithoutAChosenSlotOrWithAKeyOutsideTheStack)
{
    SteppingDialogs tested(Lorencia);
    const auto unchosen = Client::Scenes::OptionBodyOf(tested.world, tested.random, std::nullopt);
    ASSERT_FALSE(unchosen.HasValue());
    EXPECT_NE(unchosen.Error().find("0x61c2c6"), std::string::npos);
    tested.world.OwnCharacter().skillKeys[ChosenSlot][5] = 24;
    const auto outside = Client::Scenes::OptionBodyOf(tested.world, tested.random, std::uint8_t{ChosenSlot});
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x61c2e8"), std::string::npos);
}

// 0x624036: el log, el F3 30, Macro.txt y el C3 F1 02 con 0; 3.848 + 2 rand(), y el menú se cierra.
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
                  std::vector<std::uint8_t>{Client::Protocol::Connection::LogoutQuitsTheGame});
        EXPECT_EQ(tested.requests.Of("F3 30").bytes[3], 0x11);
        EXPECT_EQ(tested.requests.Of("Macro.txt").text, "hola\r\n\r\n\r\nchau\r\n\r\n\r\n\r\n\r\n\r\n\r\n");
        EXPECT_TRUE(runtime.ui.logoutRequested);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_EQ(runtime.lastSoundId, 0x19);
    }
}

// 0x6247ee: otro servidor pide la salida antes de guardar las opciones.
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
                  std::vector<std::uint8_t>{Client::Protocol::Connection::LogoutReturnsToServerList});
    }
}

// 0x624eec: otro personaje guarda las opciones y pide la vuelta a la selección.
TEST(GameDialogStepsTest, AnotherCharacterSavesTheOptionsAndAsksForTheSelection)
{
    SteppingDialogs tested(Devias);
    ASSERT_TRUE(tested.Click(MenuX, AnotherCharacterY).HasValue());
    EXPECT_EQ(tested.requests.Kinds(), (std::vector<std::string>{"log", "F3 30", "Macro.txt", "F1 02"}));
    EXPECT_EQ(tested.requests.Of("F1 02").bytes,
              std::vector<std::uint8_t>{Client::Protocol::Connection::LogoutReturnsToSelection});
}

// Con la caja del caos abierta, el 592 de error en vez del F1 02, sin sus dos rand().
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
        EXPECT_TRUE(std::ranges::any_of(runtime.chat.area.rows, [](const Client::UI::MessageAreaRow& row)
                                        { return row.text == "caja abierta" && row.type == 2; }));
        EXPECT_EQ(runtime.dialogs.current, 0);
    }
}

// 0x625538: las opciones reemplazan al menú.
TEST(GameDialogStepsTest, TheOptionsButtonOpensTheOptions)
{
    SteppingDialogs tested(Lorencia);
    ASSERT_TRUE(tested.Click(MenuX, OptionsY).HasValue());
    EXPECT_EQ(tested.world.Runtime().dialogs.current, Client::UI::OptionsDialog);
    EXPECT_EQ(tested.world.Runtime().dialogs.pending, 0);
    EXPECT_TRUE(tested.requests.requests.empty());
}

// 0x625563: las filas de las opciones cambian lo suyo con el clic.
TEST(GameDialogStepsTest, TheOptionRowsToggleTheirSettings)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::OptionsDialog;
    ASSERT_TRUE(tested.Click(0x104, 0x3c).HasValue());
    EXPECT_FALSE(runtime.ui.autoAttack);
    ASSERT_TRUE(tested.Click(0x17b, 0x5a + 0x15).HasValue());
    EXPECT_TRUE(runtime.chat.noticeSound);
    ASSERT_TRUE(tested.Click(0x110, 0x96).HasValue());
    EXPECT_FALSE(runtime.chat.slidesOn);
    EXPECT_EQ(runtime.lastSoundId, 0x19);
    // Fuera de las filas, nada.
    ASSERT_TRUE(tested.Click(0x17c, 0x3c).HasValue());
    EXPECT_FALSE(runtime.ui.autoAttack);
    // Cerrar pasa al que espera.
    runtime.dialogs.pending = 0x8b;
    ASSERT_TRUE(tested.Click(0x110, 0xe8).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x8b);
    EXPECT_EQ(runtime.dialogs.pending, 0);
}

// 0x6258e7..0x6258fa frente a 0x628280..0x6282c3: las opciones suenan antes de cerrar; el OK, después.
TEST(GameDialogStepsTest, OptionsAndGenericOkKeepTheirNativeSoundAndPendingDialogOrder)
{
    for (const bool options : {true, false})
    {
        SteppingDialogs tested(Lorencia);
        auto& runtime = tested.world.Runtime();
        runtime.dialogs.current = options ? Client::UI::OptionsDialog : 0x72;
        runtime.dialogs.pending = 0x8b;
        DialogPlayback playback;
        int sounds = 0;
        playback.onSound = [&](std::int32_t id)
        {
            ++sounds;
            EXPECT_EQ(id, 0x19);
            EXPECT_EQ(runtime.dialogs.current, options ? Client::UI::OptionsDialog : 0x8b);
            EXPECT_EQ(runtime.dialogs.pending, options ? 0x8b : 0);
            EXPECT_EQ(runtime.movement.mouseUpdateTime, 0);
            EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
        };
        runtime.audio = &playback;
        ASSERT_TRUE(tested.Click(options ? 0x110 : 0x161, options ? 0xe8 : 0x76).HasValue());
        EXPECT_EQ(sounds, 1);
        EXPECT_EQ(runtime.dialogs.current, 0x8b);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_FALSE(runtime.movement.pendingClick);
    }
}

// 0x628299 destruye el audio sincrónicamente antes del intento de sonido de 0x6282c3.
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
        playback.onSound = [&](std::int32_t) { ++sounds; };
        runtime.audio = &playback;
        runtime.ui.enterPressed = true;
        runtime.movement.mouseUpdateTime = 20;
        ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
        EXPECT_EQ(sounds, 0);
        EXPECT_TRUE(runtime.ui.destroyRequested);
        EXPECT_FALSE(runtime.ui.enterPressed);
        EXPECT_EQ(runtime.dialogs.current, 0x8b);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_EQ(runtime.movement.mouseUpdateTime, 0);
        EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
    }
}

// Q-C550: volver deja el menú esperando y las opciones abiertas, hasta cerrarlas.
TEST(GameDialogStepsTest, GoingBackLeavesTheMenuWaitingBehindTheOptions)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::OptionsDialog;
    ASSERT_TRUE(tested.Click(0x110, 0x1e).HasValue());
    EXPECT_EQ(runtime.dialogs.current, Client::UI::OptionsDialog);
    EXPECT_EQ(runtime.dialogs.pending, Client::UI::EscapeMenuDialog);
    ASSERT_TRUE(tested.Click(0x110, 0xe8).HasValue());
    EXPECT_EQ(runtime.dialogs.current, Client::UI::EscapeMenuDialog);
}

// 0x62568a: el clic en la barra la agarra y vuelve a prender el clic; la rueda suma y resta entre 0 y 9.
TEST(GameDialogStepsTest, TheVolumeBarAndTheWheelSetTheVolume)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::OptionsDialog;
    runtime.ui.volumeBarX = 0x140;
    runtime.ui.volumeBarY = 0x7f;

    ASSERT_TRUE(tested.Click(0x140 + 17, 0x80).HasValue());
    EXPECT_EQ(runtime.ui.volume, 3);
    EXPECT_EQ(runtime.ui.volumeDragging, 1);
    EXPECT_TRUE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.ui.volumeRequest, -1045);

    // Agarrada, el mouse fuera de su alto todavía la mueve; sin el clic, la suelta.
    ASSERT_TRUE(tested.Dialogs(0x140 + 0x31, 0x79).HasValue());
    EXPECT_EQ(runtime.ui.volume, 9);
    runtime.movement.pendingClick = false;
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
    // Con el mouse fuera de la fila, la rueda queda.
    runtime.ui.wheel = -1;
    ASSERT_TRUE(tested.Dialogs(0x110, 0x77).HasValue());
    EXPECT_EQ(runtime.ui.wheel, -1);
    runtime.ui.volume = 1;
    ASSERT_TRUE(tested.Dialogs(0x110, 0x78).HasValue());
    EXPECT_EQ(runtime.ui.volume, 0);
    EXPECT_EQ(runtime.ui.volumeRequest, -10000);
}

// 0x625856: el brillo toma la casilla con el clic y no lo gasta ni suena.
TEST(GameDialogStepsTest, TheGlowBoxesKeepTheClick)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = Client::UI::OptionsDialog;
    ASSERT_TRUE(tested.Click(0x10e + 0x14 + 0x12, 0xda).HasValue());
    EXPECT_EQ(runtime.ui.glowDetail, 1);
    EXPECT_TRUE(runtime.movement.pendingClick);
    EXPECT_FALSE(runtime.lastSoundId.has_value());
    ASSERT_TRUE(tested.Click(0x10e, 0xc8).HasValue());
    EXPECT_EQ(runtime.ui.glowDetail, 0);
}

// 0x6263df: el OK genérico cierra con su botón o con Enter, que se apaga; los que cierran el juego mandan WM_DESTROY.
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
    EXPECT_FALSE(runtime.movement.pendingClick);

    runtime.dialogs.current = Client::UI::DisconnectDialog;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_FALSE(runtime.ui.enterPressed);
    EXPECT_TRUE(runtime.ui.destroyRequested);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

// 0x62d756: las cajas con botones cierran con un botón o con Enter.
TEST(GameDialogStepsTest, TheBoxesCloseWithAButtonOrEnter)
{
    SteppingDialogs tested(Lorencia);
    auto& runtime = tested.world.Runtime();
    runtime.dialogs.current = 0;
    Client::UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, "aviso");
    ASSERT_TRUE(tested.Dialogs(0x47 + 0xd5, 0x8c + 0x3c).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x8a);
    EXPECT_EQ(runtime.dialogBox.buttons[0].state, 2);
    ASSERT_TRUE(tested.Click(0x47 + 0xd5, 0x8c + 0x3c).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.lastSoundId, 0x19);

    Client::UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, "aviso");
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_FALSE(runtime.ui.enterPressed);
}

// 0x62a7f7 y 0x62bb1d: el sí de la invitación a la party (0x77) gasta el clic, manda el C1 05 41 01 con la clave de
// 0x78aba4c en C3 (dos rand()) y cierra (0x62c4ac).
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
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(runtime.lastSoundId, 0x19);
}

// 0x62c50e y 0x62d4a2: el no del pedido de comercio (0x78) manda el C1 04 37 00 en claro, con el botón o con Esc.
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

    // El sí, con la invitación esperando: el C1 04 37 01 y la invitación queda abierta, sin que el no la mire.
    runtime.dialogs.current = 0x78;
    runtime.dialogs.pending = 0x77;
    ASSERT_TRUE(tested.Click(0xea + 0x45, 0x62).HasValue());
    ASSERT_EQ(tested.requests.requests.size(), 3u);
    EXPECT_EQ(tested.requests.requests[2].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x01}));
    EXPECT_EQ(runtime.dialogs.current, 0x77);
    // Fuera de los botones, nada.
    ASSERT_TRUE(tested.Click(0xea + 0x46, 0x62).HasValue());
    EXPECT_EQ(tested.requests.requests.size(), 3u);
}

// 0x623f17 → 0x657880(0x1b): Esc sin diálogo cierra la ventana abierta más baja y manda lo suyo por la sesión: las
// misiones (0x8) el C1 03 31 (0x419900), el comercio (0x20) el C1 03 3D en C3 (0x657fe1) y la tienda de otro (0x800,
// el mismo byte que 0x20000) el C1 10 3F 07 con la clave y el nombre del dueño (0x5eb17f). Ninguno abre el menú.
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
            tested->world.Runtime().panels.delegated.quest = Client::Scenes::QuestListWindow;
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
            Client::Scenes::PersonalShopState& shop = tested->world.Runtime().social.personalShop;
            tested->world.Runtime().panels.personalShop = 1;
            shop.mode = Client::Scenes::OtherShopView;
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
            EXPECT_EQ(shop.mode, Client::Scenes::NoShopView);
            EXPECT_EQ(shop.ownerKey, 0);
        }
    }
}

// 0x62c560: el sí del 0x78 con Esc y un diálogo de otro hito esperando lleva al tramo del no de ese diálogo, que corta
// con su dirección y su hito (08 P-C363), después del C1 04 37 01. En Lorencia y en Devias.
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
        // El 0x7d no está: su no es del hito 8 (LeaveNumberTest.NoClearsTheBufferButNotTheEdit).
        // Ni el 0x7f (WarRequestTest).
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

// El pedido de guerra (0x7f, el 61 de 0x4e4382): el sí (0x62b39c) y el no (0x62c5c2), con los botones de 0x46 del 0x77.
// El héroe (0) y el 5 son del gremio 1, el 6 del rival (2, marcado en 2 por el 62) y el 7 sin gremio. En Lorencia y en
// Devias.
class WarRequestTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    static constexpr std::int32_t YesX = 0xea;
    static constexpr std::int32_t NoX = 0x14e + 0x45;
    static constexpr std::int32_t ButtonY = 0x62;

    WarRequestTest() : m_tested(std::make_unique<SteppingDialogs>(GetParam()))
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        runtime.dialogs.current = 0x7f;
        Client::Scenes::GuildState& guild = runtime.guild;
        guild.warOn = 0;
        guild.rivalIndex = 2;
        guild.rival = {'T', 'e', 'r', 'o', 's', '\0', '\0', '\0', '\0'};
        const auto mark = [this](std::size_t index, std::uint16_t guildIndex, std::uint8_t byte374)
        {
            Client::Scenes::ActorState& actor = m_tested->world.Actors()[index];
            actor.alive = true;
            actor.word1f2 = guildIndex;
            actor.byte374 = byte374;
        };
        mark(0, 1, 1);
        mark(5, 1, 1);
        mark(6, 2, 2);
        mark(7, 0xffff, 0);
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_tested->world.Runtime();
    }
    std::uint8_t MarkOf(std::size_t index)
    {
        return m_tested->world.Actors()[index].byte374;
    }

    // El C1 04 61 de Protocol::Guild::WriteWarAnswer, como sale (con el XOR32).
    static std::vector<std::uint8_t> AnswerBytes(bool accept)
    {
        const Client::Protocol::Outgoing packet = Client::Protocol::Guild::WriteWarAnswer(accept);
        std::vector<std::uint8_t> bytes;
        for (const std::byte value : packet.packet.Bytes())
            bytes.push_back(std::to_integer<std::uint8_t>(value));
        return bytes;
    }

    // 0x4c1800 no corrió: el rival y las marcas, como estaban.
    void ExpectTheRivalKept()
    {
        EXPECT_EQ(Runtime().guild.rivalIndex, 2);
        EXPECT_EQ(Runtime().guild.rival[0], 'T');
        EXPECT_EQ(MarkOf(6), 2);
    }

    // 0x4c1800: sin guerra ni rival; +0x374 en 1 para el gremio del héroe y en 0 para los demás.
    void ExpectTheWarCleared()
    {
        const Client::Scenes::GuildState& guild = Runtime().guild;
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

// 0x62b39c: el sí gasta el clic, manda el C1 04 61 01 con el XOR32 y en claro (sin rand()) y cierra (0x62c4ac), sin
// tocar al rival: la guerra la empieza el 62 del GS.
TEST_P(WarRequestTest, YesSendsTheAcceptanceAndKeepsTheRival)
{
    ASSERT_TRUE(m_tested->Click(YesX, ButtonY).HasValue());

    ASSERT_EQ(m_tested->requests.requests.size(), 1u);
    EXPECT_EQ(m_tested->requests.requests[0].kind, "guild");
    EXPECT_EQ(m_tested->requests.requests[0].bytes, AnswerBytes(true));
    EXPECT_EQ(m_tested->random.State(), CrtRandom().State());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    ExpectTheRivalKept();
}

// 0x62c5c2..0x62c87b: el no manda el C1 04 61 00 y llama dos veces a 0x4c1800; después, el cierre (0x62da70) con el
// que espera.
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
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    ExpectTheWarCleared();
}

// Esc es el no; fuera de los botones y sin Esc, nada.
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

// El OK del 0x73 (0x626460), con Enter: el zen escrito para el comercio. Cada caso arranca de Seed en Lorencia y en
// Devias, con el comercio abierto (el byte de 0x20, un rand() en IsOpen) y 1.000 de zen propio.
class MoneyOkTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    static constexpr std::uint32_t Seed = 0x0c0ffee5u;
    // 0x40b4d0: el registro propio, al cerrarse.
    static constexpr int OwnRecordRandoms = 3848;

    MoneyOkTest() : m_tested(std::make_unique<SteppingDialogs>(GetParam()))
    {
        m_tested->random = CrtRandom(Seed);
        Client::Scenes::WorldRuntime& runtime = Runtime();
        runtime.dialogs.current = 0x73;
        runtime.panels.trade = 1;
        runtime.social.dialogFieldLimit = 8;
        runtime.social.moneyField = 1;
        runtime.chat.input.fieldCount = 1;
        m_tested->world.OwnCharacter().zen = 1000;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_tested->world.Runtime();
    }

    Client::Core::Expected<void, std::string> Enter(std::int32_t typed)
    {
        Runtime().social.typedMoney = typed;
        Runtime().ui.enterPressed = true;
        return m_tested->Dialogs(0, 0);
    }

    // 0x628215..0x6282c3: lo que deja el OK en todos los casos.
    void ExpectTheDialogFieldsReset()
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        EXPECT_FALSE(runtime.ui.enterPressed);
        EXPECT_EQ(runtime.social.dialogFieldLimit, 0x2a);
        EXPECT_EQ(runtime.chat.input.fieldCount, 2);
        EXPECT_EQ(runtime.social.moneyField, 0);
        EXPECT_FALSE(runtime.ui.chatOpen);
        EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
        EXPECT_EQ(runtime.lastSoundId, 0x19);
    }

    std::vector<Request>& Sent()
    {
        return m_tested->requests.requests;
    }

    std::unique_ptr<SteppingDialogs> m_tested;
};

// Más de 100.000.000 (con signo): el 0x75 en su lugar, sin mirar el comercio ni sacar rand().
TEST_P(MoneyOkTest, TooMuchMoneyOpensTheWarning)
{
    ASSERT_TRUE(Enter(100'000'001).HasValue());
    EXPECT_EQ(Runtime().dialogs.current, 0x75);
    EXPECT_EQ(Runtime().dialogs.pending, 0);
    EXPECT_TRUE(Sent().empty());
    EXPECT_EQ(m_tested->random.State(), CrtRandom(Seed).State());
    ExpectTheDialogFieldsReset();
}

// Más que el zen propio más el que ya está en el comercio (sin signo, así que también un negativo): el 0x74. El
// registro propio se abre y se cierra (3.848 rand() después del de IsOpen).
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

// Con el zen: el OK propio se apaga con el C1 04 3C 00 en C3 (dos rand() antes del cierre del registro), 0x78abac8
// en 0x96 con zen ya puesto, el zen en 0x57678ac y en el C1 08 3A 00 con el XOR32 y en claro, y el 0x73 se cierra
// hacia el que espera.
TEST_P(MoneyOkTest, TheMoneyGoesInTheThreeA)
{
    Client::Scenes::TradeState& trade = Runtime().social.trade;
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

    // Sin OK propio ni zen puesto: solo el 3A, y la espera queda.
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

// Sin el comercio, el zen va al almacén (0x6270ed, Scenes::MoveTheWarehouseMoney, CL9 hito 6): meter más zen que el
// del inventario abre el 0x74 sin mandar nada, después del rand() de IsOpen y con el registro propio abierto y cerrado;
// los campos vuelven como con el comercio.
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

// El diálogo 0x7d (CL9 hito 8): el número para echar a un miembro, disolver el gremio o salir de él. Cada caso arranca
// en Lorencia y en Devias con el EDIT del número ([0x5763508]) a la vista, con el foco y "1234567" escrito (lo que deja
// el dibujo, 0x630af0), el lugar 2 de 0x787a5e8 con un nombre inventado y 0x6d5e2c en 2 (0x64e553); el buffer
// 0x7798bf8 trae un mensaje viejo del chat.
class LeaveNumberTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    static constexpr std::int32_t LeaveDialog = 0x7d;
    static constexpr std::int32_t KickedSlot = 2;
    // 0x630ba1 y 0x630c24: el sí en [0x143, 0x16b) y el no en [0x175, 0x19d), los dos en [0x62, 0x77).
    static constexpr std::int32_t YesX = 0x143;
    static constexpr std::int32_t NoX = 0x175;
    static constexpr std::int32_t ButtonY = 0x62;

    LeaveNumberTest() : m_tested(std::make_unique<SteppingDialogs>(GetParam()))
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        runtime.dialogs.current = LeaveDialog;
        auto& slot = runtime.guild.members[KickedSlot];
        std::ranges::copy(std::string_view("Tala"), slot.name.begin());
        runtime.guild.kickMember = KickedSlot;
        Client::UI::ChatInputState& input = runtime.chat.input;
        Client::UI::CopyIntoTheBuffer(input.messageBuffer, "hola que tal");
        input.messageLength = 12;
        Client::UI::ShowTheNumberField(input, 0);
        Client::UI::FocusTheNumberField(input);
        input.numberField.filter = 9;
        input.numberField.text.SetLimit(7);
        Client::UI::TypeInTheChat(input, "1234567");
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_tested->world.Runtime();
    }
    Client::UI::ChatInputState& Input()
    {
        return Runtime().chat.input;
    }

    // El C1 17 53 de Protocol::Guild::WriteLeave con el nombre y los diez bytes, como sale (con el XOR32).
    static std::vector<std::uint8_t> LeaveBytes(std::string_view name, std::string_view number)
    {
        Client::Protocol::Guild::CharacterName characterName{};
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

// 0x62a878..0x62b38d: GetText del EDIT a 0x7798bf8 (sus 7 dígitos y el '\0'; detrás queda lo del chat) y SetText(0);
// el C1 17 53 con el nombre del lugar 2 y los diez bytes de 0x7798bf8, en claro y sin rand(); los diez bytes y el largo
// en 0, y el cierre (0x62c4ac) con el 0x19. El EDIT sigue a la vista y con el foco (Q-C1295).
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
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    // Los diez bytes y el largo en 0; lo de después, como estaba.
    for (std::size_t index = 0; index < 10; ++index)
        EXPECT_EQ(Input().messageBuffer[index], '\0') << index;
    EXPECT_EQ(Input().messageBuffer[10], 'a');
    EXPECT_EQ(Input().messageLength, 0);
    EXPECT_EQ(Input().numberField.text.Value(), "");
    EXPECT_EQ(Input().numberField.base.state, 0);
    EXPECT_EQ(Input().focus, Client::UI::ChatFocus::NumberField);
}

// El maestro que disuelve (0x64de93 deja 0x6d5e2c en 0): el nombre del lugar 0. Fuera de los 40 del sí angosto, nada.
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

// 0x62c567: el no (el botón o Esc) no manda nada; vacía los diez bytes y el largo, pero no el EDIT, y cierra
// (0x62da7a) con el 0x19.
TEST_P(LeaveNumberTest, NoClearsTheBufferButNotTheEdit)
{
    ASSERT_TRUE(m_tested->Click(NoX, ButtonY).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().lastSoundId, 0x19);
    for (std::size_t index = 0; index < 10; ++index)
        EXPECT_EQ(Input().messageBuffer[index], '\0') << index;
    EXPECT_EQ(Input().messageBuffer[10], 'a');
    EXPECT_EQ(Input().messageLength, 0);
    EXPECT_EQ(Input().numberField.text.Value(), "1234567");
    EXPECT_EQ(m_tested->random.State(), CrtRandom().State());

    Runtime().dialogs.current = LeaveDialog;
    Client::UI::CopyIntoTheBuffer(Input().messageBuffer, "otra");
    m_tested->input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(m_tested->Dialogs(0, 0).HasValue());
    EXPECT_TRUE(m_tested->requests.requests.empty());
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Client::UI::CStringOf(Input().messageBuffer), "");
}

// 0x62a91c: 0x6d5e2c en −1 (el de la .data) lee antes de 0x787a5e8 (Q-C1296), y sin quien mande corta.
TEST_P(LeaveNumberTest, WithoutAMemberOrRequestsItCuts)
{
    Runtime().guild.kickMember = -1;
    const auto outside = m_tested->Click(YesX, ButtonY);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x62a91c"), std::string::npos) << outside.Error();
    EXPECT_TRUE(m_tested->requests.requests.empty());

    // El cut deja el EDIT ya vacío (GetText y SetText(0) van antes del nombre).
    EXPECT_EQ(Input().numberField.text.Value(), "");
    Runtime().guild.kickMember = KickedSlot;
    Client::Scenes::LatchTheLeftButton(Runtime().movement, true, true);
    const auto withoutRequests =
        Client::Scenes::StepTheGameDialogs(GameUiContext{.world = m_tested->world,
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

// Dialog.bmd con el diálogo 3 (un renglón y dos respuestas: la primera lleva al 5 y la segunda a ninguno) y el 5, sin
// respuestas, que lleva al 3.
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

// 0x62d91d..0x62da57: con el texto del 3 en y = ((7 − 2 − 1) · 0x12) / 2 + 0x4c = 112, la primera respuesta va de 130 a
// 147 y la segunda de 148 a 165, a no más de 0x6a de x = 0x13f. El clic en una pasa el que espera a abierto y, con un
// diálogo siguiente, lo muestra; Enter no cierra el 0x8d.
TEST(GameDialogStepsTest, TheAnswersLeadToTheirNextDialog)
{
    const auto tested = std::make_unique<SteppingDialogs>(Lorencia);
    auto& runtime = tested->world.Runtime();
    runtime.dialogTable = AnswersTable();
    tested->texts = Client::Tests::MakeTextTable({{Client::UI::NoAnswersText, "nada mas"}});
    runtime.dialogs.current = 0;
    ASSERT_TRUE(
        Client::UI::OpenTheAnswersDialog(runtime.dialogBox, runtime.dialogs, &runtime.dialogTable, &tested->texts, 3)
            .HasValue());
    ASSERT_EQ(runtime.dialogs.current, Client::UI::AnswersDialog);
    ASSERT_EQ(runtime.dialogBox.lineCount, 1);
    ASSERT_EQ(runtime.dialogBox.answerCount, 2);

    runtime.ui.enterPressed = true;
    for (const auto& [x, y] : std::to_array<std::pair<std::int32_t, std::int32_t>>(
             {{0x13f, 129}, {0x13f, 166}, {0x13f + 0x6b, 140}, {0x13f - 0x6b, 140}}))
    {
        SCOPED_TRACE(std::format("{} {}", x, y));
        ASSERT_TRUE(tested->Click(x, y).HasValue());
        EXPECT_EQ(runtime.dialogs.current, Client::UI::AnswersDialog);
        EXPECT_EQ(runtime.dialogBox.dialogIndex, 3);
        EXPECT_TRUE(runtime.movement.pendingClick);
        runtime.movement.pendingClick = false;
    }
    EXPECT_TRUE(runtime.ui.enterPressed);
    EXPECT_FALSE(runtime.lastSoundId.has_value());

    // La primera, en el borde de la derecha: el 5, sin respuestas ("1) " y el 609), queda abierto.
    runtime.movement.mouseUpdateTime = 4;
    runtime.movement.mouseUpdateTimeMax = 12;
    ASSERT_TRUE(tested->Click(0x13f + 0x6a, 147).HasValue());
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.lastSoundId, 0x19);
    EXPECT_EQ(runtime.movement.mouseUpdateTime, 0);
    EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(runtime.dialogs.current, Client::UI::AnswersDialog);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 5);
    EXPECT_EQ(Client::UI::DialogAnswer(runtime.dialogBox, 0), "1) nada mas");

    // La cuenta 0 de Dialog.bmd deja elegir la primera (máx(0, 1)), que vuelve al 3; con otro esperando, el 3 espera.
    runtime.dialogs.pending = Client::UI::MessageBoxDialog;
    ASSERT_TRUE(tested->Click(0x13f, 131 + 9).HasValue());
    EXPECT_EQ(runtime.dialogs.current, Client::UI::MessageBoxDialog);
    EXPECT_EQ(runtime.dialogs.pending, Client::UI::AnswersDialog);
    EXPECT_EQ(runtime.dialogBox.dialogIndex, 3);

    // La segunda del 3 no lleva a ninguno: cierra.
    runtime.dialogs.current = Client::UI::AnswersDialog;
    runtime.dialogs.pending = 0;
    ASSERT_TRUE(tested->Click(0x13f, 150).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.dialogs.pending, 0);
}

// 0x62d9b8: una respuesta elegida sin el registro de Dialog.bmd corta.
TEST(GameDialogStepsTest, AnAnswerWithoutItsDialogIsCut)
{
    const auto tested = std::make_unique<SteppingDialogs>(Lorencia);
    auto& runtime = tested->world.Runtime();
    runtime.dialogs.current = Client::UI::AnswersDialog;
    runtime.dialogBox.answerCount = 1;
    const auto stepped = tested->Click(0x13f, ((7 - 1) * 0x12) / 2 + 0x4c);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x62d9b8"), std::string::npos) << stepped.Error();
}

// Los tramos de otras olas cortan con su dirección; un id que no está en la tabla va al OK genérico.
// 0x94 abierto por 0x633300(4, 0x173, 0x94), con los botones (1, 0x15, 0x5a, 0x46, 0x15) y (3, 0x78, 0x5a, 0x46, 0x15)
// y el aviso 3 de 0x78abb14 (el comercio).
void OpenTheWarning(Client::Scenes::WorldRuntime& runtime, std::int32_t id = 0x94)
{
    runtime.dialogs.current = id;
    runtime.dialogs.pending = 0;
    runtime.dialogBox.buttons = {Client::UI::DialogButton{1, 0x15, 0x5a, 0x46, 0x15},
                                 Client::UI::DialogButton{3, 0x78, 0x5a, 0x46, 0x15}};
    runtime.social.trade.warningKind = 3;
    runtime.social.trade.warningByte = 0xff;
}

// 0x6282dc..0x62a7e6: el primero ([0xea, 0x130] × [0xbe, 0xd3], con los bordes) responde 1 y el segundo ([0x14d,
// 0x193]) 2, los dos con el cierre y el sonido; sin rand(). Con el mouse encima, el estado es 2 o 4, y el clic se
// apaga siempre.
TEST(GameDialogStepsTest, TheWarningAnswersWithItsTwoButtons)
{
    for (const std::int32_t map : {Lorencia, Devias})
    {
        SCOPED_TRACE(map);
        SteppingDialogs tested(map);
        auto& runtime = tested.world.Runtime();
        OpenTheWarning(runtime);
        runtime.lastSoundId = 0;
        ASSERT_TRUE(tested.Click(0x15 + 0xd5, 0x5a + 0x64).HasValue());
        EXPECT_EQ(runtime.social.trade.warningByte, 1);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(runtime.lastSoundId, 0x19);
        EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_EQ(runtime.dialogBox.buttons[0].state, 2);
        EXPECT_EQ(runtime.dialogBox.buttons[1].state, 3);

        OpenTheWarning(runtime);
        runtime.dialogs.pending = 0x77;
        ASSERT_TRUE(tested.Click(0x78 + 0xd5 + 0x46, 0x5a + 0x64 + 0x15).HasValue());
        EXPECT_EQ(runtime.social.trade.warningByte, 2);
        EXPECT_EQ(runtime.dialogs.current, 0x77);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_EQ(runtime.dialogBox.buttons[1].state, 4);

        // Un píxel afuera: el clic se gasta igual y no hay respuesta.
        OpenTheWarning(runtime);
        ASSERT_TRUE(tested.Click(0x78 + 0xd5 + 0x47, 0x5a + 0x64).HasValue());
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_EQ(runtime.social.trade.warningByte, 0xff);
        EXPECT_EQ(runtime.dialogs.current, 0x94);
        EXPECT_EQ(runtime.dialogBox.buttons[0].state, 1);
        EXPECT_EQ(runtime.dialogBox.buttons[1].state, 3);
        EXPECT_EQ(tested.random.State(), CrtRandom().State());
        EXPECT_TRUE(tested.requests.requests.empty());
    }
}

// 0x62845d: el 0x99 del precio bajo va al mismo final que el 0x94: el primero responde 1 y Esc, 2.
TEST(GameDialogStepsTest, TheLowPriceWarningAnswersLikeTheWarning)
{
    SteppingDialogs tested(Devias);
    auto& runtime = tested.world.Runtime();
    OpenTheWarning(runtime, Client::UI::MoneyTextDialog);
    ASSERT_TRUE(tested.Click(0x15 + 0xd5 + 0x46, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.social.trade.warningByte, 1);
    EXPECT_EQ(runtime.dialogs.current, 0);

    OpenTheWarning(runtime, Client::UI::MoneyTextDialog);
    tested.input.Apply(
        Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown, .key = Client::Input::Key::Escape});
    ASSERT_TRUE(tested.Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.social.trade.warningByte, 2);
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(tested.random.State(), CrtRandom().State());
}

// 0x628409..0x628435: Esc elige el segundo botón, 2.
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
    for (const Client::Scenes::DialogRoute& route : Client::Scenes::DialogRoutes())
    {
        if (route.step != Client::Scenes::DialogStep::Cut)
            continue;
        runtime.dialogs.current = route.id;
        const auto stepped = tested.Dialogs(0, 0);
        ASSERT_FALSE(stepped.HasValue()) << route.id;
        EXPECT_NE(stepped.Error().find(route.address), std::string::npos) << stepped.Error();
        EXPECT_NE(stepped.Error().find(route.wave), std::string::npos) << stepped.Error();
    }
    EXPECT_EQ(Client::Scenes::DialogRouteOf(0x40).step, Client::Scenes::DialogStep::GenericOk);
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
    runtime.lastSoundId = 55;
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
    EXPECT_TRUE(runtime.movement.pendingClick);
    EXPECT_EQ(tested->random.State(), before.State());
    EXPECT_EQ(runtime.lastSoundId, 55);
    EXPECT_TRUE(tested->requests.requests.empty());
}
