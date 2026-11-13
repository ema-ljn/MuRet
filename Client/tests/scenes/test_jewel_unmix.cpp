#include "Scenes/GameDialogSteps.h"

#include "Core/Expected.h"
#include "Input/InputState.h"
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
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

// El paso del 0xa4, deshacer un paquete de joyas (0x625cf4, CL9 hito 6, fase 4a): las dos joyas con la lista de
// 0x6e5660 (0x402a10 y 0x402a20), el aviso sin paquetes, el cancelar sin joya y el corte de la lista. Ninguno saca
// rand(). Textos inventados; el mundo, en Lorencia (0), Devias (2) y Noria (3).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameUiContext;
using Client::Scenes::JewelMixState;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::World::ItemState;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint16_t EmptyItem = 0xffff;
constexpr std::int32_t UnmixDialog = 0xa4;
// 0x625d00..0x625d6f: las joyas en y de 160 a 185, la primera en x de 235 a 315 y la segunda de 315 a 395.
constexpr std::int32_t KindY = 170;
constexpr std::int32_t BlessX = 250;
constexpr std::int32_t SoulX = 330;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class UnmixingJewels
{
public:
    explicit UnmixingJewels(std::int32_t map)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.dialogs.current = UnmixDialog;
        runtime.npcWindows.jewels.mode = 1;
        for (ItemState& cell : world->Inventory().cells)
            cell.id = EmptyItem;
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

    // Un paquete `id` de nivel `level` en la casilla `cell`.
    void Put(std::uint16_t id, std::size_t cell, std::uint32_t level)
    {
        ItemState& item = world->Inventory().cells[cell];
        item.id = id;
        item.scalar1Low = (level << 3) | 2u;
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

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime() const
    {
        return world->Runtime();
    }
    [[nodiscard]] JewelMixState& Jewels() const
    {
        return world->Runtime().npcWindows.jewels;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x2468u};
    Client::Input::InputState input;
    LetterMeasure measure;
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{1818, "nada para deshacer"}});
    std::vector<Client::Protocol::Outgoing> sent;
};

std::vector<std::pair<std::int32_t, std::uint8_t>> RowsOf(const JewelMixState& jewels)
{
    std::vector<std::pair<std::int32_t, std::uint8_t>> rows;
    for (const JewelMixState::BundleRow& row : jewels.bundles)
        rows.emplace_back(row.cell, row.level);
    return rows;
}

// 0x625d73..0x625db1: sin paquetes de la joya, la lista queda vacía, el aviso 1818 de error y 0x402e50 la deja sin
// elegir y vuelve a abrir el 0xa4; el clic se apaga. Los paquetes de la otra joya no cuentan.
TEST(JewelUnmixTest, AKindWithoutBundlesSaysWhyAndGoesBack)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(SoulBundle, 4, 1);
        tested.Jewels().bundles.push_back({.cell = 9, .level = 9});
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Click(BlessX, KindY).HasValue());
        EXPECT_TRUE(tested.Jewels().bundles.empty());
        EXPECT_EQ(tested.Errors(), std::vector<std::string>{"nada para deshacer"});
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        EXPECT_EQ(tested.Runtime().dialogs.current, UnmixDialog);
        EXPECT_EQ(tested.Runtime().dialogs.pending, 0);
        EXPECT_FALSE(tested.Runtime().movement.pendingClick);
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x625d73..0x625dbe y 0x402a20: la joya del clic (x = 315 es de la primera) llena la lista con sus paquetes en el
// orden de las casillas, con el nivel de (+4 >> 3) & 0xf, y la lista corta en su paso (0x625dbe).
TEST(JewelUnmixTest, AKindFillsTheListAndTheListIsCut)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        for (const auto& [x, y, kind, rows] :
             std::to_array<std::tuple<std::int32_t, std::int32_t, std::uint8_t,
                                      std::vector<std::pair<std::int32_t, std::uint8_t>>>>({
                 {SoulX, 185, std::uint8_t{1}, {{5, std::uint8_t{3}}, {40, std::uint8_t{0}}}},
                 {315, 160, std::uint8_t{0}, {{12, std::uint8_t{15}}}},
                 {395, KindY, std::uint8_t{1}, {{5, std::uint8_t{3}}, {40, std::uint8_t{0}}}},
             }))
        {
            UnmixingJewels tested(map);
            tested.Put(SoulBundle, 40, 0);
            tested.Put(BlessBundle, 12, 15);
            tested.Put(SoulBundle, 5, 3);
            const CrtRandom before = tested.random;
            const auto stepped = tested.Click(x, y);
            ASSERT_FALSE(stepped.HasValue());
            EXPECT_NE(stepped.Error().find("0x625dbe"), std::string::npos) << stepped.Error();
            EXPECT_NE(stepped.Error().find("P-C338"), std::string::npos) << stepped.Error();
            EXPECT_EQ(tested.Jewels().kind, kind);
            EXPECT_EQ(RowsOf(tested.Jewels()), rows);
            EXPECT_FALSE(tested.Runtime().movement.pendingClick);
            EXPECT_TRUE(tested.Errors().empty());
            EXPECT_EQ(tested.random.State(), before.State());
        }
    }
}

// 0x625d00..0x625d6f y 0x625f4d..0x625f37: sin el clic, ni las joyas ni el cancelar; fuera de ellos, el clic queda. El
// cancelar (x de 279,5 a 359,5, y de 200 a 225) con el clic cierra todo con 0x402eb0 y el C1 03 31, sin apagarlo.
TEST(JewelUnmixTest, TheCancelClosesAllAndKeepsTheClick)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        UnmixingJewels tested(map);
        tested.Put(BlessBundle, 0, 2);
        auto& runtime = tested.Runtime();
        ASSERT_TRUE(tested.Step(BlessX, KindY).HasValue());
        EXPECT_EQ(tested.Jewels().kind, 0xff);
        ASSERT_TRUE(tested.Step(300, 210).HasValue());
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        ASSERT_TRUE(tested.Click(279, 210).HasValue());
        ASSERT_TRUE(tested.Click(BlessX, 186).HasValue());
        EXPECT_EQ(runtime.dialogs.current, UnmixDialog);
        EXPECT_TRUE(runtime.movement.pendingClick);
        EXPECT_TRUE(tested.sent.empty());

        tested.Jewels().cell = 3;
        ASSERT_TRUE(tested.Click(359, 225).HasValue());
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_EQ(tested.Jewels().mode, 0);
        EXPECT_EQ(tested.Jewels().cell, -1);
        EXPECT_TRUE(runtime.movement.pendingClick);
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(tested.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}
} // namespace
