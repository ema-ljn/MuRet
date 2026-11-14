#include "Scenes/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Protocol/Connection/Logout.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/OwnCharacterRecord.h"
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// Los diálogos del juego en el paso (CL8 hito 6): el despacho de 0x623f78, el menú de Esc con sus salidas (el F3 30
// de 0x61c210, Macro.txt y el C3 F1 02), las opciones (0x93), las cajas con botones y el OK genérico. Textos
// inventados; el mundo, en Lorencia (0) y en Devias (2).
namespace
{
using Client::Input::InputState;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;

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
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true, false);
        return Dialogs(x, y);
    }

    WorldState world;
    CrtRandom random;
    InputState input;
    TypedLetters letters;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{ChaosBoxText, "caja abierta"}});
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

// Los tramos de otras olas cortan con su dirección; un id que no está en la tabla va al OK genérico.
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
