#include "Network/Xor32Packet.h"
#include "Scenes/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Delivery.h"
#include "Protocol/Outgoing.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// Los pasos del 0x9a (el AF 01), del 0xa8 (con su tercer botón y Esc) y del 0xaa (el D0 00) en 0x623e70 (CL9 hito 6,
// fase 4a), con los paquetes byte a byte y los rand() de cada uno contra un paso sin nada elegido. El del 0x91 (el
// título o el precio de la tienda personal) es el del CL9 hito 7 (test_personal_shop_window.cpp). Textos y números
// inventados; el mundo, en Lorencia (0), Devias (2) y Noria (3).
namespace
{
using Client::Crt::CrtRandom;
using Client::Network::Delivery;
using Client::Network::Xor32Decode;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
// 0x6282dc: los botones de 0x633300 corridos (0xd5, 0x64): el sí, el no y, en el 0xa8, el tercero.
constexpr std::int32_t YesX = 0x15 + 0xd5;
constexpr std::int32_t NoX = 0x78 + 0xd5;
constexpr std::int32_t ButtonsY = 0x5a + 0x64;
constexpr std::int32_t ChaosCastleY = 0x78 + 0x64;
constexpr std::int32_t LuckYesX = 0x16 + 0xd5;
constexpr std::int32_t LuckNoX = 0x52 + 0xd5;
constexpr std::int32_t LuckThirdX = 0x8e + 0xd5;
constexpr std::int32_t LuckY = 0x5c + 0x64;
// Lo que deja un diálogo que no suena.
constexpr std::int32_t Silent = -1;
// 0x5af520: el almacén y la ventana 0x80 sin rand(); el comercio, un byte protegido, con uno (y solo si los dos
// primeros están cerrados).
constexpr int TradeQuery = 1;
// 0x628c11..0x629367 (y el sí): los dos rand() del envío en C3.
constexpr int SendRandoms = 2;
constexpr std::uint8_t LuckItemCell = 0x27;
constexpr std::uint8_t LuckItemSlot = LuckItemCell + 12;
constexpr std::int32_t UseWait = 10;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

class AnsweringDialogs
{
public:
    explicit AnsweringDialogs(std::int32_t map, std::int32_t id)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.dialogs.current = id;
        runtime.dialogs.pending = 0x8a;
        runtime.lastSoundId = Silent;
        runtime.movement.mouseUpdateTime = 3;
        runtime.movement.mouseUpdateTimeMax = 1;
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.emplace_back(index, type);
            return Client::Core::Expected<void, std::string>{};
        };
    }

    // Abre el diálogo con los botones de 0x633300 (los del 0xa8 y la y del 0x9a).
    void OpenWithTheButtons()
    {
        auto& box = world->Runtime().dialogBox;
        const std::int32_t id = world->Runtime().dialogs.current;
        box.buttons[0] = {.state = 1, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
        box.buttons[1] = {.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
        if (id == Client::UI::ChaosCastleDialog)
        {
            box.buttons[0].y = 0x78;
            box.buttons[1].y = 0x78;
        }
        if (id == Client::UI::ItemNameWithLuckDialog)
        {
            box.buttons[0] = {.state = 1, .x = 0x16, .y = 0x5c, .width = 0x31, .height = 0x10};
            box.buttons[1] = {.state = 3, .x = 0x52, .y = 0x5c, .width = 0x31, .height = 0x10};
            box.thirdButton = {.state = 5, .x = 0x8e, .y = 0x5c, .width = 0x31, .height = 0x10};
        }
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::StepTheGameDialogs(
            GameUiContext{.world = *world,
                          .random = random,
                          .input = input,
                          .mouseX = x,
                          .mouseY = y,
                          .measure = &measure,
                          .texts = &texts,
                          .inventory = &services,
                          .sendNpcWindow = [this](Client::Protocol::Outgoing packet)
                          {
                              sent.push_back(std::move(packet));
                              return Client::Core::Expected<void, std::string>{};
                          }});
    }

    Client::Core::Expected<void, std::string> Click(std::int32_t x, std::int32_t y)
    {
        Client::Scenes::LatchTheLeftButton(world->Runtime().movement, true, true);
        return Step(x, y);
    }

    void PressEscape()
    {
        input.Apply(Client::Input::InputEvent{.kind = Client::Input::InputEvent::Kind::KeyDown,
                                              .key = Client::Input::Key::Escape});
    }

    // Los rand() de un paso sin nada elegido, desde el estado de ahora (en una copia del mundo).
    [[nodiscard]] CrtRandom QuietStep() const
    {
        AnsweringDialogs quiet(world->Runtime().map, world->Runtime().dialogs.current);
        quiet.world->Runtime().panels = world->Runtime().panels;
        quiet.OpenWithTheButtons();
        quiet.random = random;
        static_cast<void>(quiet.Step(0, 0));
        return quiet.random;
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime() const
    {
        return world->Runtime();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x13579bdu};
    Client::Input::InputState input;
    LetterMeasure measure;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{474, "ahora no"}});
    Client::Scenes::InventoryServices services;
    std::vector<Client::Protocol::Outgoing> sent;
    std::vector<std::pair<std::size_t, std::int32_t>> messages;
};

// Lo que deja CloseTheDialog: el que esperaba abierto, MouseUpdateTime en 0 con su máximo en 6 y el sonido 0x19.
void ExpectClosedWithTheSound(const AnsweringDialogs& tested, std::int32_t sound = 0x19)
{
    auto& runtime = tested.Runtime();
    EXPECT_EQ(runtime.dialogs.current, 0x8a);
    EXPECT_EQ(runtime.dialogs.pending, 0);
    EXPECT_EQ(runtime.movement.mouseUpdateTime, 0);
    EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
    EXPECT_EQ(runtime.lastSoundId, sound);
}

// 0x628582..0x628aaf: el sí del 0x9a con el ítem de los diálogos (+8 en 0x78a59cc: el nivel 5) manda C1 06 AF 01 con
// el nivel y el byte bajo de 0x78a6758, con el XOR32 y en claro, sin rand(), y cierra con el sonido.
TEST(DialogAnswerStepsTest, TheChaosCastleYesSendsTheEntryWithTheItemLevelAndSlot)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs tested(map, Client::UI::ChaosCastleDialog);
        tested.OpenWithTheButtons();
        tested.Runtime().dialogItem.scalar1Low = (5u << 3) | 2u;
        tested.Runtime().slot78a6758 = 0x114;
        const CrtRandom quiet = tested.QuietStep();

        ASSERT_TRUE(tested.Click(YesX, ChaosCastleY).HasValue());
        EXPECT_EQ(tested.random.State(), quiet.State());
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_EQ(tested.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(Xor32Decode(tested.sent[0].packet).Bytes(), Bytes({0xc1, 0x06, 0xaf, 0x01, 0x05, 0x14})));
        ExpectClosedWithTheSound(tested);
    }
}

// 0x628582..0x62858e: sin el ítem (0x78a59cc en 0xff) el sí no hace nada y el diálogo sigue; 0x62854f: el no cierra
// con el sonido y no manda nada.
TEST(DialogAnswerStepsTest, TheChaosCastleWithoutItemStaysAndItsNoCloses)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs tested(map, Client::UI::ChaosCastleDialog);
        tested.OpenWithTheButtons();
        tested.Runtime().dialogItem.scalar1Low = 0xff;
        ASSERT_TRUE(tested.Click(YesX, ChaosCastleY).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_EQ(tested.Runtime().dialogs.current, Client::UI::ChaosCastleDialog);
        EXPECT_EQ(tested.Runtime().lastSoundId, Silent);
        EXPECT_FALSE(tested.Runtime().movement.pendingClick);

        ASSERT_TRUE(tested.Click(NoX, ChaosCastleY).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        ExpectClosedWithTheSound(tested);
    }
}

// 0x62a480..0x62a752: el sí del 0xaa manda C1 04 D0 00 con el XOR32, en claro, y cierra; el no (y Esc) solo cierra.
TEST(DialogAnswerStepsTest, TheNoticeYesSendsD0AndTheNoOnlyCloses)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs yes(map, Client::UI::NoticeQuestionDialog);
        yes.OpenWithTheButtons();
        const CrtRandom quiet = yes.QuietStep();
        ASSERT_TRUE(yes.Click(YesX, ButtonsY).HasValue());
        EXPECT_EQ(yes.random.State(), quiet.State());
        ASSERT_EQ(yes.sent.size(), 1u);
        EXPECT_EQ(yes.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(Xor32Decode(yes.sent[0].packet).Bytes(), Bytes({0xc1, 0x04, 0xd0, 0x00})));
        ExpectClosedWithTheSound(yes);

        AnsweringDialogs no(map, Client::UI::NoticeQuestionDialog);
        no.OpenWithTheButtons();
        ASSERT_TRUE(no.Click(NoX, ButtonsY).HasValue());
        EXPECT_TRUE(no.sent.empty());
        ExpectClosedWithTheSound(no);

        AnsweringDialogs escape(map, Client::UI::NoticeQuestionDialog);
        escape.OpenWithTheButtons();
        escape.PressEscape();
        ASSERT_TRUE(escape.Step(0, 0).HasValue());
        EXPECT_TRUE(escape.sent.empty());
        ExpectClosedWithTheSound(escape);
    }
}

// 0x6283a1..0x628409 y 0x628bb9..0x628bd7: el tercer botón del 0xa8 (sin estado) con el clic, o Esc (0x62841e),
// eligen 2: cierra con MouseUpdateTime, sin sonido y sin 0x5af520. Con Esc suena igual el 0x19 de 0x623f6a, que es de
// cualquier diálogo.
TEST(DialogAnswerStepsTest, TheLuckThirdButtonAndEscapeSkipTheItem)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        AnsweringDialogs third(map, Client::UI::ItemNameWithLuckDialog);
        third.OpenWithTheButtons();
        const CrtRandom quiet = third.QuietStep();
        ASSERT_TRUE(third.Click(LuckThirdX + 0x31, LuckY + 0x10).HasValue());
        EXPECT_EQ(third.random.State(), quiet.State());
        ExpectClosedWithTheSound(third, Silent);
        EXPECT_EQ(third.Runtime().dialogBox.thirdButton.state, 5);
        EXPECT_FALSE(third.Runtime().movement.pendingClick);

        AnsweringDialogs escape(map, Client::UI::ItemNameWithLuckDialog);
        escape.OpenWithTheButtons();
        const CrtRandom escapeQuiet = escape.QuietStep();
        escape.PressEscape();
        ASSERT_TRUE(escape.Step(0, 0).HasValue());
        EXPECT_EQ(escape.random.State(), escapeQuiet.State());
        ExpectClosedWithTheSound(escape);
    }
}

// 0x628bdc..0x628c00 y 0x6293b5..0x6293d9: el sí y el no del 0xa8 dejan 0 o 1 en 0x78abb26; con el almacén abierto
// (0x5af520 en falso con su primer IsOpen, sin rand()), el aviso 474 de error y el cierre con el sonido. Con la espera
// de 0x5767954 (los tres IsOpen, con el rand() del comercio) cierra sin mandar; sin ella, espera 10 y manda el
// C1 06 26 en C3 con la casilla del diálogo + 12, destino 0 y la elección, con dos rand() más. Ambos cierran.
TEST(DialogAnswerStepsTest, TheLuckAnswersUseTheItemOrSayWhyNot)
{
    for (const std::int32_t map : Maps)
    {
        for (const auto& [x, choice] : std::to_array<std::pair<std::int32_t, std::uint8_t>>(
                 {{LuckYesX, std::uint8_t{0}}, {LuckNoX, std::uint8_t{1}}}))
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", botón " << int{choice});
            AnsweringDialogs refused(map, Client::UI::ItemNameWithLuckDialog);
            refused.OpenWithTheButtons();
            refused.Runtime().ui.field78abb26 = 7;
            refused.Runtime().panels.warehouse = 1;
            const CrtRandom quiet = refused.QuietStep();
            ASSERT_TRUE(refused.Click(x, LuckY).HasValue());
            EXPECT_EQ(refused.random.State(), quiet.State());
            EXPECT_EQ(refused.Runtime().ui.field78abb26, choice);
            EXPECT_EQ(refused.messages, (std::vector<std::pair<std::size_t, std::int32_t>>{{474, 2}}));
            EXPECT_TRUE(refused.sent.empty());
            EXPECT_EQ(refused.Runtime().ui.field5767954, 0);
            ExpectClosedWithTheSound(refused);

            AnsweringDialogs waiting(map, Client::UI::ItemNameWithLuckDialog);
            waiting.OpenWithTheButtons();
            waiting.Runtime().ui.field5767954 = 4;
            const CrtRandom waitingQuiet = waiting.QuietStep();
            ASSERT_TRUE(waiting.Click(x, LuckY).HasValue());
            EXPECT_EQ(waiting.random.State(), Advanced(waitingQuiet, TradeQuery).State());
            EXPECT_EQ(waiting.Runtime().ui.field78abb26, choice);
            EXPECT_EQ(waiting.Runtime().ui.field5767954, 4);
            EXPECT_TRUE(waiting.sent.empty());
            EXPECT_TRUE(waiting.messages.empty());
            ExpectClosedWithTheSound(waiting);

            AnsweringDialogs used(map, Client::UI::ItemNameWithLuckDialog);
            used.OpenWithTheButtons();
            used.Runtime().dialogItemCell = LuckItemCell;
            used.Runtime().slot78a6758 = 0x73;
            used.Runtime().ui.field78abb26 = 7;
            const CrtRandom usedQuiet = used.QuietStep();
            ASSERT_TRUE(used.Click(x, LuckY).HasValue());
            EXPECT_EQ(used.random.State(), Advanced(usedQuiet, TradeQuery + SendRandoms).State());
            EXPECT_EQ(used.Runtime().ui.field78abb26, choice);
            EXPECT_EQ(used.Runtime().ui.field5767954, UseWait);
            EXPECT_TRUE(used.messages.empty());
            ASSERT_EQ(used.sent.size(), 1u);
            EXPECT_EQ(used.sent[0].delivery, Delivery::Encrypted);
            EXPECT_TRUE(SameBytes(Xor32Decode(used.sent[0].packet).Bytes(),
                                  Bytes({0xc1, 0x06, 0x26, LuckItemSlot, 0x00, choice})));
            ExpectClosedWithTheSound(used);
        }
    }
}
} // namespace
