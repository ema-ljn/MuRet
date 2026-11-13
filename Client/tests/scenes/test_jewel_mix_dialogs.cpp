#include "Scenes/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GameUi.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameDialogs.h"
#include "UI/MessageArea.h"
#include "World/ItemState.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// El modo de la caja del caos (0x8e) y la mezcla de joyas (0xa1, 0xa2, 0xa3 y 0xa6) en el paso de 0x623e70, con el
// BC y el 31 byte a byte (CL9 hito 6, fase 3b). Ninguno saca rand(). Textos inventados; el mundo, en Lorencia (0),
// Devias (2) y Noria (3), el mapa del NPC de las joyas.
namespace
{
using Client::Network::Crypto::Xor32Decode;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::World::ItemState;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr std::uint16_t BlessJewel = 0x1c0d;
constexpr std::uint16_t SoulJewel = 0x1c0e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint16_t EmptyItem = 0xffff;
// Dentro de los botones (con los bordes del original).
constexpr std::int32_t MixButtonX = 300;
constexpr std::int32_t MixY = 190;
constexpr std::int32_t UnmixY = 255;
constexpr std::int32_t MenuCloseX = 279;
constexpr std::int32_t MenuCloseY = 345;
constexpr std::int32_t BlessX = 245;
constexpr std::int32_t SoulX = 405;
constexpr std::int32_t KindY = 185;
constexpr std::int32_t TenY = 225;
constexpr std::int32_t TwentyY = 270;
constexpr std::int32_t ThirtyY = 350;
constexpr std::int32_t ChoiceCloseY = 390;
// 0x6282dc con el 0xa3: los botones (0x15 y 0x78, 0x5a) corridos (0xd5, 0xd2).
constexpr std::int32_t YesX = 0x15 + 0xd5;
constexpr std::int32_t NoX = 0x78 + 0xd5;
constexpr std::int32_t AnswerY = 0x5a + 0xd2;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class JewelMixDialogs
{
public:
    explicit JewelMixDialogs(std::int32_t map)
    {
        world->Runtime().map = map;
        for (ItemState& cell : world->Inventory().cells)
            cell.id = EmptyItem;
    }

    Client::Core::Expected<void, std::string> Click(std::int32_t x, std::int32_t y)
    {
        Client::Scenes::LatchTheLeftButton(world->Runtime().movement, true, true);
        return Client::Scenes::StepTheGameDialogs(
            GameUiContext{.world = *world,
                          .random = random,
                          .input = input,
                          .mouseX = x,
                          .mouseY = y,
                          .measure = &measure,
                          .texts = &texts,
                          .sendNpcWindow = [this](Client::Protocol::Outgoing packet)
                          {
                              sent.push_back(std::move(packet));
                              return Client::Core::Expected<void, std::string>{};
                          }});
    }

    // `count` joyas `id` desde la casilla `from`.
    void Fill(std::uint16_t id, std::size_t from, std::size_t count)
    {
        for (std::size_t cell = from; cell < from + count; ++cell)
            world->Inventory().cells[cell].id = id;
    }

    [[nodiscard]] std::vector<std::string> Errors() const
    {
        std::vector<std::string> errors;
        for (const Client::UI::MessageAreaRow& row : world->Runtime().chat.area.rows)
        {
            if (row.type == 2 && !row.text.empty())
                errors.push_back(row.text);
        }
        return errors;
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }
    [[nodiscard]] Client::Scenes::JewelMixState& Jewels()
    {
        return world->Runtime().npcWindows.jewels;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x1357u};
    Client::Input::InputState input;
    LetterMeasure measure;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{1806, "Joya Uno"},
                                                                        {1807, "Joya Dos"},
                                                                        {1809, "Juntar %s y %d?"},
                                                                        {1810, "Cuesta %d zen"},
                                                                        {1813, "Deshacer %s %d?"},
                                                                        {1814, "Deshacer cuesta %d"},
                                                                        {1817, "faltan joyas"},
                                                                        {1818, "no se puede deshacer"}});
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x625910..0x6259c5, en Lorencia, Devias y Noria: el clic en un botón deja su número en 0x7d3a114 y cierra, sin
// gastar el clic ni sonar; entre los botones, nada. El paso mira y desde 180 (el dibujo, desde 190: Q-C945).
TEST(JewelMixDialogsTest, TheChaosMixModeButtonsLeaveTheModeAndClose)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        runtime.dialogs.current = 0x8e;
        ASSERT_TRUE(tested.Click(MixButtonX, 240).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0x8e);
        ASSERT_TRUE(tested.Click(MixButtonX, 180 + 85).HasValue());
        EXPECT_EQ(runtime.npcWindows.chaosMixMode, 1);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_TRUE(runtime.movement.pendingClick);
        EXPECT_FALSE(runtime.lastSoundId.has_value());
        runtime.dialogs.current = 0x8e;
        runtime.dialogs.pending = 0x94;
        ASSERT_TRUE(tested.Click(395, 180).HasValue());
        EXPECT_EQ(runtime.npcWindows.chaosMixMode, 0);
        EXPECT_EQ(runtime.dialogs.current, 0x94);
        EXPECT_EQ(runtime.dialogs.pending, 0);
        EXPECT_EQ(tested.random.State(), before.State()) << map;
        EXPECT_TRUE(tested.sent.empty());
    }
}

// 0x6259c6..0x625b22: el primero abre el 0xa2 con 0x6e5794 en 0 y el segundo el 0xa4 con 1, con el clic apagado y el
// MouseUpdateTime en 0 y su máximo en 6; el cierre (0x402eb0) lo vuelve todo a su arranque y manda el C1 03 31 en
// claro.
TEST(JewelMixDialogsTest, TheMenuChoosesMixingOrUnmixingAndTheCloseSends31)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        runtime.dialogs.current = 0xa1;
        runtime.movement.mouseUpdateTime = 3;
        ASSERT_TRUE(tested.Click(MixButtonX, MixY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        EXPECT_EQ(tested.Jewels().mode, 0);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_EQ(runtime.movement.mouseUpdateTime, 0);
        EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
        runtime.dialogs.current = 0xa1;
        ASSERT_TRUE(tested.Click(395, UnmixY + 35).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa4);
        EXPECT_EQ(tested.Jewels().mode, 1);

        runtime.dialogs.current = 0xa1;
        tested.Jewels().kind = 1;
        tested.Jewels().cell = 7;
        runtime.npcWindows.field6e5798 = 1;
        ASSERT_TRUE(tested.Click(MenuCloseX, MenuCloseY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(tested.Jewels().mode, 0);
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(tested.Jewels().count, 0xff);
        EXPECT_EQ(tested.Jewels().cell, -1);
        EXPECT_EQ(tested.Jewels().price, -1);
        EXPECT_EQ(runtime.npcWindows.field6e5798, 0);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        EXPECT_EQ(tested.random.State(), before.State()) << map;
    }
}

// 0x625b23..0x625cf3 y 0x402bc0: sin joya elegida los paquetes no se miran; con la del alma y 20 en el inventario,
// el de 20 abre el 0xa3 con la pregunta (0x63340d) y el precio (20 · 50.000); el sí (0x62846f) abre el 0xa5 y manda
// C1 06 BC 00 01 01 en claro, una sola vez.
TEST(JewelMixDialogsTest, ChoosingTheSoulAndTwentyAsksAndTheYesSendsBc00)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        const CrtRandom before = tested.random;
        tested.Fill(BlessJewel, 0, 5);
        tested.Fill(SoulJewel, 10, 20);
        runtime.dialogs.current = 0xa2;
        ASSERT_TRUE(tested.Click(MixButtonX, TwentyY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        EXPECT_EQ(tested.Jewels().count, 0xff);
        ASSERT_TRUE(tested.Click(SoulX, KindY + 25).HasValue());
        EXPECT_EQ(tested.Jewels().kind, 1);
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        ASSERT_TRUE(tested.Click(MixButtonX, TwentyY).HasValue());
        EXPECT_EQ(tested.Jewels().count, 20);
        EXPECT_EQ(tested.Jewels().counted, 20);
        EXPECT_EQ(tested.Jewels().price, 1000000);
        EXPECT_EQ(tested.Jewels().field6e57a0, 100);
        EXPECT_EQ(runtime.dialogs.current, Client::UI::ServerTimeDialog);
        EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "Juntar Joya Dos y 20?");
        EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 1), "Cuesta 1000000 zen");
        EXPECT_EQ(runtime.dialogBox.lineCount, 2);

        ASSERT_TRUE(tested.Click(YesX, AnswerY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa5);
        EXPECT_EQ(runtime.npcWindows.dialogAnswer, 1);
        EXPECT_EQ(runtime.npcWindows.field6e5798, 1);
        EXPECT_EQ(runtime.lastSoundId, 0x19);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_EQ(tested.sent[0].delivery, Client::Network::Delivery::Clear);
        EXPECT_TRUE(SameBytes(Xor32Decode(tested.sent[0].packet).Bytes(), Bytes({0xc1, 0x06, 0xbc, 0x00, 0x01, 0x01})));
        EXPECT_EQ(tested.random.State(), before.State()) << map;
    }
}

// 0x402b70: con 30 elegidas y menos en el inventario, el aviso 1817 de error y la vuelta al 0xa2 (0x402e50) con la
// joya sin elegir; el no del 0xa3 deja 2 y vuelve igual; el cierre del 0xa2 manda el 31.
TEST(JewelMixDialogsTest, MissingJewelsWarnAndTheNoReturnsToTheChoice)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        tested.Fill(BlessJewel, 0, 12);
        runtime.dialogs.current = 0xa2;
        tested.Jewels().kind = 0;
        ASSERT_TRUE(tested.Click(MixButtonX, ThirtyY).HasValue());
        EXPECT_EQ(tested.Errors(), std::vector<std::string>{"faltan joyas"});
        EXPECT_EQ(tested.Jewels().refusal, 0);
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(tested.Jewels().counted, 12);
        EXPECT_EQ(runtime.dialogs.current, 0xa2);

        tested.Jewels().kind = 0;
        ASSERT_TRUE(tested.Click(BlessX, TenY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, Client::UI::ServerTimeDialog);
        EXPECT_EQ(tested.Jewels().price, 500000);
        ASSERT_TRUE(tested.Click(NoX, AnswerY).HasValue());
        EXPECT_EQ(runtime.npcWindows.dialogAnswer, 2);
        EXPECT_EQ(runtime.dialogs.current, 0xa2);
        EXPECT_TRUE(tested.sent.empty());

        ASSERT_TRUE(tested.Click(360, ChoiceCloseY).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}

// 0x4029e0 y 0x401f30: deshacer el paquete del alma de nivel 2 de la casilla 17 manda C1 07 BC 01 01 02 1D y deja la
// casilla como lugar (+12); el 0xa6 (0x625f98) vuelve al 0xa4 con 0x402e50.
TEST(JewelMixDialogsTest, UnmixingSendsBc01WithTheSlotAndTheResultReturns)
{
    for (const std::int32_t map : Maps)
    {
        JewelMixDialogs tested(map);
        auto& runtime = tested.Runtime();
        tested.world->Inventory().cells[17].id = SoulBundle;
        auto& jewels = tested.Jewels();
        jewels.mode = 1;
        jewels.kind = 1;
        jewels.cell = 17;
        jewels.level = 2;
        jewels.counted = 1;
        jewels.price = 1000000;
        // 0x402a90 abre el 0xa3 con 0x633300(0, 0, 0xa3), como lo haría la lista del 0xa4.
        const Client::UI::DialogTextArguments question{
            .jewelMode = 1, .jewelKind = 1, .jewelCount = 1, .jewelLevel = 2, .jewelPrice = 1000000};
        ASSERT_TRUE(Client::UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &tested.texts, 0, 0,
                                               Client::UI::ServerTimeDialog, nullptr, &question)
                        .HasValue());
        EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "Deshacer Joya Dos 3?");
        ASSERT_TRUE(tested.Click(YesX, AnswerY).HasValue());
        EXPECT_EQ(jewels.cell, 17 + 12);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(
            SameBytes(Xor32Decode(tested.sent[0].packet).Bytes(), Bytes({0xc1, 0x07, 0xbc, 0x01, 0x01, 0x02, 0x1d})));
        // Mandada, otro sí no vuelve a mandar.
        runtime.dialogs.current = Client::UI::ServerTimeDialog;
        ASSERT_TRUE(tested.Click(YesX, AnswerY).HasValue());
        EXPECT_EQ(tested.sent.size(), 1u);

        runtime.dialogs.current = 0xa6;
        ASSERT_TRUE(tested.Click(300, 180).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0xa4);
        EXPECT_EQ(runtime.npcWindows.field6e5798, 0);
        EXPECT_EQ(jewels.kind, 0xff);
    }
}

// Lo que sigue cortado de la mezcla de joyas: el paso del 0xa4, con la lista de 0x6e5660.
TEST(JewelMixDialogsTest, TheUnmixListIsCut)
{
    JewelMixDialogs tested(0);
    auto& runtime = tested.Runtime();
    runtime.dialogs.current = 0xa4;
    const auto list = tested.Click(0, 0);
    ASSERT_FALSE(list.HasValue());
    EXPECT_NE(list.Error().find("0x625cf4"), std::string::npos) << list.Error();
}
} // namespace
