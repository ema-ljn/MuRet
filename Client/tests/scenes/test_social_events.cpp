#include "Scenes/SocialEvents.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Party.h"
#include "Protocol/Trade.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GamePanels.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/ChatLog.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "World/GameCamera.h"
#include "World/ItemState.h"

#include "support/ItemFiles.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Scenes/SocialEvents (CL9 hito 7): lo que hacen el comercio (36..3D), la party (40..44) y el duelo (AA..AD) que
// llegan, contra el ASM de 0x4d4af0, 0x4d5570, 0x4d57f0, 0x5bc300, 0x5bc470, 0x5bc5e0, 0x4d67e0, 0x4e409b, 0x4dac90 y
// los casos de 0x4e2b10. Los nombres y los textos son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 1;
// Un tipo de Item.bmd de 1 por 2 y otro de 1 por 1 (sintéticos).
constexpr std::uint16_t TallType = 0x001;
constexpr std::uint16_t SmallType = 0x003;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

int DrawsSince(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 20'000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

template <std::size_t Size> std::array<std::byte, Size> NameOf(std::string_view text)
{
    std::array<std::byte, Size> name{};
    for (std::size_t at = 0; at < text.size() && at < Size; ++at)
        name[at] = static_cast<std::byte>(text[at]);
    return name;
}

template <std::size_t Size> std::string TextOf(const std::array<std::byte, Size>& name)
{
    std::string text;
    for (const std::byte b : name)
    {
        if (b == std::byte{0})
            break;
        text.push_back(static_cast<char>(b));
    }
    return text;
}

// Los siete bytes de un ítem del 39 (0x5bc740): el tipo en el byte 0, el bit 8 en el 0x80 del 3 y los de arriba en
// el 5.
Client::Protocol::Inventory::ItemData ItemBytes(std::uint16_t type, std::uint8_t level, std::uint8_t durability = 20)
{
    return {static_cast<std::byte>(type & 0xff),
            static_cast<std::byte>(level << 3),
            std::byte{durability},
            static_cast<std::byte>((type >> 1) & 0x80),
            std::byte{0},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
}

std::vector<std::uint8_t> PlainBytes(const Client::Protocol::Outgoing& sent)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(sent.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte b : plain.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(b));
    return bytes;
}

struct Stage
{
    // Como después de la entrada a la selección (0x5bbe10 desde 0x633f20): las grillas y la copia vacías.
    Stage()
    {
        world->Runtime().hero = 0;
        Client::Scenes::ClearTheTradeForSelection(world->Runtime().social.trade);
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .log = log,
                .textMeasure = &letters,
                .texts = &texts};
    }
    Client::Scenes::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }
    Client::Scenes::TradeState& Trade()
    {
        return world->Runtime().social.trade;
    }
    const Client::UI::ChatEntry& LastMessage()
    {
        return world->Runtime().chat.log.entries.front();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::Scenes::SelectionState selection{*world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Data::Tables::ItemTable items = Client::Tests::ItemTableOf({{TallType, 1, 2, 0}, {SmallType, 1, 1, 0}});
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{492, "rechazado"},
                                                                        {493, "ocupado"},
                                                                        {494, "cancelado"},
                                                                        {495, "sin lugar"},
                                                                        {496, "sin tiempo"},
                                                                        {497, "party 0"},
                                                                        {498, "party 1"},
                                                                        {499, "party 2"},
                                                                        {500, "party 3"},
                                                                        {501, "party 4"},
                                                                        {502, "dejaste la party"},
                                                                        {535, "party 5"},
                                                                        {912, "duelo con %s"},
                                                                        {913, "no quiso"},
                                                                        {914, "fin del duelo"}});
    LetterMeasure letters;
};

std::unique_ptr<Stage> MakeStage()
{
    return std::make_unique<Stage>();
}

// 0x4d4af0 y 0x4d5570 con el comercio abierto: el pedido (sin tiendas, sin rand()), la respuesta 1 (Open(0x20)).
void OpenTheTrade(Stage& stage)
{
    const Client::Protocol::Trade::TradeRequest request{NameOf<10>("Arbusto")};
    const auto asked = Client::Scenes::ApplyTradeRequest(request, stage.Resources());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_FALSE(asked.Value().has_value());
    // El sí del diálogo 0x78 lo cierra (0x62c4ac).
    stage.Runtime().dialogs.current = 0;
    const auto opened = Client::Scenes::ApplyTradeResponse({1, NameOf<10>("Arbusto"), 50, 7}, stage.Resources());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
}

// Del lado que pidió (sin el 36): el 37 trae un nombre nuevo y 0x5bbdf0 vacía la copia.
void OpenTheAskedTrade(Stage& stage)
{
    const auto opened = Client::Scenes::ApplyTradeResponse({1, NameOf<10>("Brezal"), 40, 2}, stage.Resources());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
}

TEST(SocialEventsTest, TheTradeRequestOpensTheDialogWithoutRandoms)
{
    auto stage = MakeStage();
    const auto asked = Client::Scenes::ApplyTradeRequest({NameOf<10>("Arbusto")}, stage->Resources());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    EXPECT_FALSE(asked.Value().has_value());
    EXPECT_EQ(stage->Runtime().dialogs.current, 0x78);
    EXPECT_EQ(TextOf(stage->Trade().otherName), "Arbusto");
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
}

TEST(SocialEventsTest, AnOpenPersonalShopRefusesTheTradeInClear)
{
    auto stage = MakeStage();
    stage->Runtime().panels.personalShop = 1;
    const auto asked = Client::Scenes::ApplyTradeRequest({NameOf<10>("Arbusto")}, stage->Resources());
    ASSERT_TRUE(asked.HasValue()) << asked.Error();
    ASSERT_TRUE(asked.Value().has_value());
    EXPECT_EQ(asked.Value()->delivery, Client::Network::Delivery::Clear);
    EXPECT_EQ(PlainBytes(*asked.Value()), (std::vector<std::uint8_t>{0xc1, 0x04, 0x37, 0x00}));
    EXPECT_EQ(stage->Runtime().dialogs.current, 0);
    EXPECT_TRUE(TextOf(stage->Trade().otherName).empty());
}

TEST(SocialEventsTest, AClearTradeRequestOnlyReportsItWithTwoRandoms)
{
    auto stage = MakeStage();
    const Client::Protocol::Outgoing report = Client::Scenes::ApplyClearTradeRequest(stage->cloth.random);
    EXPECT_EQ(report.delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(PlainBytes(report), (std::vector<std::uint8_t>{0xc1, 0x06, 0xf1, 0x03, 0x00, 0x36}));
    EXPECT_EQ(DrawsSince(stage->cloth.random), 2);
}

// 0x659e46: Open(0x20) con nada abierto saca 9: su IsOpen, el 0x10000 y el 0x20 de CanOpen, el 0x10000 de su
// CloseByMask, los cuatro de Open(inventario) y la escritura del byte protegido.
TEST(SocialEventsTest, TheAcceptedTradeOpensTheWindowAndTheInventory)
{
    auto stage = MakeStage();
    stage->Trade().otherMoney = 5;
    stage->Trade().otherOk = 1;
    stage->Trade().okWait = 3;
    ASSERT_TRUE(Client::Scenes::ApplyTradeResponse({1, NameOf<10>("Brezal"), 50, 7}, stage->Resources()).HasValue());
    const Client::UI::PanelState& panels = stage->Runtime().panels;
    EXPECT_EQ(panels.trade, 1);
    EXPECT_EQ(panels.inventory, 1);
    EXPECT_EQ(DrawsSince(stage->cloth.random), 9);
    const Client::Scenes::TradeState& trade = stage->Trade();
    EXPECT_EQ(TextOf(trade.otherName), "Brezal");
    EXPECT_EQ(trade.otherLevel, 50u);
    EXPECT_EQ(trade.otherGuild, 7);
    EXPECT_EQ(trade.otherMoney, 0u);
    EXPECT_EQ(trade.otherOk, 0);
    EXPECT_EQ(trade.okWait, 0);
    EXPECT_EQ(trade.cursorMoveRequested, 1);
}

TEST(SocialEventsTest, RefusedAndBusyTradesOnlySayIt)
{
    auto stage = MakeStage();
    ASSERT_TRUE(Client::Scenes::ApplyTradeResponse({0, {}, 0, 0}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "rechazado");
    EXPECT_EQ(stage->LastMessage().type, Client::UI::ErrorMessage);
    ASSERT_TRUE(Client::Scenes::ApplyTradeResponse({2, {}, 0, 0}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "ocupado");
    EXPECT_EQ(stage->Runtime().panels.trade, 0);
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
}

// Una vuelta entera: el pedido, la respuesta, un ítem del otro, el zen de los dos, los OK y el resultado.
TEST(SocialEventsTest, AWholeTradeRound)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    ASSERT_EQ(stage->Runtime().panels.trade, 1);

    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({9, ItemBytes(TallType, 3)}, stage->Resources()).HasValue());
    const Client::Scenes::TradeState& trade = stage->Trade();
    EXPECT_EQ(trade.otherGrid[9].id, TallType);
    EXPECT_EQ(trade.otherGrid[9].occupied, 1u);
    EXPECT_EQ(trade.otherGrid[17].id, TallType);
    EXPECT_EQ(trade.otherGrid[17].occupied, 0u);
    EXPECT_EQ(trade.otherGrid[9].gridColumn, 1);
    EXPECT_EQ(trade.otherGrid[9].gridRow, 1);
    EXPECT_EQ((trade.otherGrid[9].scalar1Low >> 3) & 0xf, 3u);
    EXPECT_EQ(stage->Runtime().lastSoundId, 0x1d);
    EXPECT_EQ(trade.changed, 0);

    stage->Trade().sentMoney = 1000;
    Client::Scenes::ApplyTradeMoneyResult({1}, *stage->world);
    EXPECT_EQ(trade.ownMoney, 1000u);
    Client::Scenes::ApplyTradeOtherMoney({2500}, *stage->world);
    EXPECT_EQ(trade.otherMoney, 2500u);
    Client::Scenes::ApplyTradeOkState({1}, *stage->world);
    EXPECT_EQ(trade.otherOk, 1);
    EXPECT_EQ(stage->Runtime().lastSoundId, 0x19);
    stage->Trade().ownOk = 1;
    Client::Scenes::ApplyTradeOkState({2}, *stage->world);
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(trade.otherOk, 1);
    Client::Scenes::ApplyTradeMoneyResult({0}, *stage->world);
    EXPECT_EQ(trade.ownMoney, 0u);

    // 0x4d57f0 con 1: sin texto, la cuenta del cursor en 0 y Close(0x20, 1): su IsOpen, el del CloseByMask y la
    // escritura, sin el 3D.
    const CrtRandom before = stage->cloth.random;
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({1, {0xc1, 0x04, 0x3d, 0x01}}, stage->Resources()).HasValue());
    CrtRandom expected = before;
    expected.Discard(3);
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->Runtime().panels.trade, 0);
    EXPECT_EQ(stage->Runtime().panels.inventory, 0);
    EXPECT_EQ(trade.otherGrid[9].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(trade.otherGrid[17].id, Client::Scenes::EmptyOwnItemId);
}

// 0x5bd750 guarda la copia de lo que el otro saca (0x5bc5e0) y 0x5bc470 marca con 99 lo que vuelve peor.
TEST(SocialEventsTest, TheOtherGridMarksAWorseItemThanItsCopy)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({0, ItemBytes(TallType, 5)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemRemoved({0}, stage->Resources()).HasValue());
    const Client::Scenes::TradeState& trade = stage->Trade();
    EXPECT_EQ(trade.backup[0].id, TallType);
    EXPECT_EQ((trade.backup[0].scalar1Low >> 3) & 0xf, 5u);
    EXPECT_EQ(trade.otherGrid[0].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(trade.otherGrid[8].id, Client::Scenes::EmptyOwnItemId);

    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({0, ItemBytes(TallType, 4)}, stage->Resources()).HasValue());
    EXPECT_EQ(trade.changed, 1);
    EXPECT_EQ(trade.otherGrid[0].warning, 99);
    EXPECT_EQ(trade.otherGrid[8].warning, 99);
    EXPECT_EQ(trade.otherGrid[1].warning, 1);

    // Uno igual a la copia pone la lista de 0x5bc470: 0x78abaa5 en 0 y los dos del tipo con 1.
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({2, ItemBytes(TallType, 5)}, stage->Resources()).HasValue());
    EXPECT_EQ(trade.changed, 0);
    EXPECT_EQ(trade.otherGrid[0].warning, 1);
    EXPECT_EQ(trade.otherGrid[2].warning, 1);
}

// 0x5bc5e0 no copia un ítem común (nivel 4 o menos, sin excelente).
TEST(SocialEventsTest, APlainItemIsNotWorthACopy)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({0, ItemBytes(SmallType, 4)}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemRemoved({0}, stage->Resources()).HasValue());
    // La copia queda como la dejó 0x5bbe10: el 36 ya había puesto el nombre y el 37 no corrió 0x5bbdf0.
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().backup[1].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().otherGrid[0].id, Client::Scenes::EmptyOwnItemId);
}

// 0x5bc52f: la lista de la pila de 0x5bc470 es de 10; el undécimo ítem igual a su copia escribiría pasado ella
// (Q-C961).
TEST(SocialEventsTest, ElevenItemsLikeTheirCopiesPassTheList)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    constexpr std::uint8_t Slots = 11;
    for (std::uint8_t slot = 0; slot < Slots; ++slot)
    {
        ASSERT_TRUE(
            Client::Scenes::ApplyTradeItemAdded({slot, ItemBytes(SmallType, 5)}, stage->Resources()).HasValue());
        ASSERT_TRUE(Client::Scenes::ApplyTradeItemRemoved({slot}, stage->Resources()).HasValue());
    }
    for (std::uint8_t slot = 0; slot + 1 < Slots; ++slot)
    {
        const auto added = Client::Scenes::ApplyTradeItemAdded({slot, ItemBytes(SmallType, 5)}, stage->Resources());
        ASSERT_TRUE(added.HasValue()) << added.Error();
    }
    const auto eleventh = Client::Scenes::ApplyTradeItemAdded({10, ItemBytes(SmallType, 5)}, stage->Resources());
    ASSERT_FALSE(eleventh.HasValue());
    EXPECT_NE(eleventh.Error().find("0x5bc52f"), std::string::npos) << eleventh.Error();
}

// Del lado que pidió, el 37 llega con un nombre nuevo y 0x5bbdf0 vacía la copia (tipo −1).
TEST(SocialEventsTest, ANewNameClearsTheCopy)
{
    auto stage = MakeStage();
    ASSERT_TRUE(Client::Scenes::ApplyTradeResponse({1, NameOf<10>("Brezal"), 50, 7}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().backup[31].id, Client::Scenes::EmptyOwnItemId);
}

// 0x5bc107..0x5bc13f: la entrada a la selección deja 0x78abaa5 en 0 y las dos grillas y la copia vacías, sin rand().
TEST(SocialEventsTest, TheSelectionEntryEmptiesTheTrade)
{
    Client::Scenes::TradeState trade;
    trade.changed = 1;
    trade.otherGrid[5].id = SmallType;
    trade.otherGrid[5].occupied = 1;
    trade.ownGrid[31].occupied = 1;
    Client::Scenes::ClearTheTradeForSelection(trade);
    EXPECT_EQ(trade.changed, 0);
    for (std::size_t slot = 0; slot < Client::Scenes::TradeGridCells; ++slot)
    {
        EXPECT_EQ(trade.otherGrid[slot].id, Client::Scenes::EmptyOwnItemId);
        EXPECT_EQ(trade.ownGrid[slot].id, Client::Scenes::EmptyOwnItemId);
        EXPECT_EQ(trade.backup[slot].id, Client::Scenes::EmptyOwnItemId);
        EXPECT_EQ(trade.otherGrid[slot].occupied, 0u);
        EXPECT_EQ(trade.ownGrid[slot].occupied, 0u);
    }
}

TEST(SocialEventsTest, TheTradeResultsSayWhatHappened)
{
    auto stage = MakeStage();
    OpenTheAskedTrade(*stage);
    ASSERT_TRUE(Client::Scenes::ApplyTradeItemAdded({0, ItemBytes(TallType, 6)}, stage->Resources()).HasValue());
    stage->Runtime().dialogs.current = 0x73;
    stage->Runtime().social.moneyField = 1;
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({0, {}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "cancelado");
    // El cancelado copia lo que tenía el otro.
    EXPECT_EQ(stage->Trade().backup[0].id, TallType);
    EXPECT_EQ(stage->Runtime().dialogs.current, 0);
    EXPECT_EQ(stage->Runtime().social.dialogFieldLimit, 0x2a);
    EXPECT_EQ(stage->Runtime().social.moneyField, 0);
    EXPECT_EQ(stage->Runtime().panels.trade, 0);

    OpenTheTrade(*stage);
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({2, {}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "sin lugar");
    OpenTheTrade(*stage);
    stage->Runtime().dialogs.pending = 0x6e;
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({3, {}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "sin tiempo");
    EXPECT_EQ(stage->Runtime().dialogs.current, 0x6e);
}

TEST(SocialEventsTest, TheItemMoveLockDefersTheFirstResult)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    stage->Runtime().ui.field78abb39 = 1;
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({0, {0xc1, 0x04, 0x3d, 0x00}}, stage->Resources()).HasValue());
    ASSERT_TRUE(Client::Scenes::ApplyTradeResult({3, {0xc1, 0x04, 0x3d, 0x03}}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Trade().resultDeferred, 1);
    EXPECT_EQ(stage->Trade().deferredHead, (std::array<std::uint8_t, 4>{0xc1, 0x04, 0x3d, 0x00}));
    EXPECT_EQ(stage->Runtime().panels.trade, 1);
}

// 0x657fb5 con aux en 0 (el cierre a mano): el 3D en C3 con sus dos rand(); con 1, nada sale. Las dos grillas quedan
// vacías y, con 0x78abaa5 en 1, la copia también.
TEST(SocialEventsTest, ClosingTheGridsSendsTheCancelOnlyByHand)
{
    auto stage = MakeStage();
    stage->Trade().ownGrid[3].id = SmallType;
    stage->Trade().otherGrid[4].id = SmallType;
    stage->Trade().backup[0].id = SmallType;
    stage->Trade().changed = 1;
    std::vector<Client::Protocol::Outgoing> sent;
    ASSERT_TRUE(Client::Scenes::CloseTheTradeGrids(*stage->world, stage->cloth.random, true,
                                                   [&sent](Client::Protocol::Outgoing out)
                                                   { sent.push_back(std::move(out)); })
                    .HasValue());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
    EXPECT_EQ(stage->Trade().ownGrid[3].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().otherGrid[4].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(stage->Trade().backup[0].id, Client::Scenes::EmptyOwnItemId);

    ASSERT_TRUE(Client::Scenes::CloseTheTradeGrids(*stage->world, stage->cloth.random, false,
                                                   [&sent](Client::Protocol::Outgoing out)
                                                   { sent.push_back(std::move(out)); })
                    .HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Client::Network::Delivery::Encrypted);
    EXPECT_EQ(PlainBytes(sent[0]), (std::vector<std::uint8_t>{0xc1, 0x03, 0x3d}));
    EXPECT_EQ(DrawsSince(stage->cloth.random), 2);

    const auto cut = Client::Scenes::CloseTheTradeGrids(*stage->world, stage->cloth.random, false, {});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x657fe1"), std::string::npos);
}

// Close(0x20) a mano con el comercio abierto y sin el cable (GamePanels sin LendTheTradeCancel): sus dos consultas y el
// corte antes del 3D, con el byte protegido todavía en 1.
TEST(SocialEventsTest, ClosingTheOpenTradeByHandWithoutTheCableCutsBeforeTheCancel)
{
    auto stage = MakeStage();
    OpenTheTrade(*stage);
    const CrtRandom before = stage->cloth.random;
    Client::Scenes::GamePanels panels(*stage->world, stage->cloth.random);
    const auto cut = panels.Registry().Close(Client::UI::TradePanel);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x657fe1"), std::string::npos);
    CrtRandom expected = before;
    expected.Discard(2);
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->Runtime().panels.trade, 1);
}

TEST(SocialEventsTest, CompareTradeItemsGoesFieldByField)
{
    ItemState a{};
    ItemState b{};
    a.id = TallType;
    b.id = SmallType;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 2);
    b.id = TallType;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 0);
    a.scalar1Low = 3 << 3;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 1);
    b.scalar1Low = 4 << 3;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), -1);
    b.scalar1Low = a.scalar1Low;
    a.flags1b = 0x01;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 1);
    b.flags1b = 0x41;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 0);
    a.optionCount = 1;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 1);
    b.optionCount = 1;
    b.optionBytes[0] = 0x50;
    b.optionBytes[Client::World::ItemOptionValueOffset] = 4;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), -1);
    a.optionBytes[0] = 0x53;
    a.optionBytes[Client::World::ItemOptionValueOffset] = 4;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), 0);
    a.durability = 9;
    b.durability = 10;
    EXPECT_EQ(Client::Scenes::CompareTradeItems(a, b), -1);
    EXPECT_EQ(Client::Scenes::CompareTradeItems(b, a), 1);
}

// Formar una party: la invitación (40), el sí (en Scenes/GameDialogSteps), la lista (42), las vidas (44) y la salida
// (43).
TEST(SocialEventsTest, APartyFormsAndBreaks)
{
    auto stage = MakeStage();
    Client::Scenes::ApplyPartyRequest({0x1234}, *stage->world);
    EXPECT_EQ(stage->Runtime().social.party.requestKey, 0x1234);
    EXPECT_EQ(stage->Runtime().dialogs.current, 0x77);

    stage->Runtime().dialogs.pending = 0;
    ASSERT_TRUE(Client::Scenes::ApplyPartyRequestResult({1}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "party 1");
    EXPECT_EQ(stage->Runtime().dialogs.current, 0);
    ASSERT_TRUE(Client::Scenes::ApplyPartyRequestResult({5}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->LastMessage().text, "party 5");
    const std::size_t messages = stage->Runtime().chat.log.entries.size();
    ASSERT_TRUE(Client::Scenes::ApplyPartyRequestResult({6}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Runtime().chat.log.entries.size(), messages);

    Client::Protocol::Party::PartyList list{.result = 1, .members = {}};
    list.members.push_back({NameOf<10>("Arbusto"), 0, 3, 120, 130, 900, 1000});
    list.members.push_back({NameOf<10>("Brezal"), 1, 3, 121, 131, 50, 600});
    ASSERT_TRUE(Client::Scenes::ApplyPartyList(list, *stage->world).HasValue());
    const Client::Scenes::PartyState& party = stage->Runtime().social.party;
    EXPECT_EQ(stage->Runtime().hud.partyCount, 2);
    EXPECT_EQ(TextOf(party.slots[1].name), "Brezal");
    EXPECT_EQ(party.slots[1].number, 1);
    EXPECT_EQ(party.slots[1].x, 121);
    EXPECT_EQ(party.slots[1].maxLife, 600u);

    ASSERT_TRUE(Client::Scenes::ApplyPartyLife({{0x07, 0x1f}}, *stage->world).HasValue());
    EXPECT_EQ(party.slots[0].tenths, 7);
    EXPECT_EQ(party.slots[1].tenths, 10);
    const auto outside = Client::Scenes::ApplyPartyLife({{0x53}}, *stage->world);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("Q-C960"), std::string::npos);

    stage->Runtime().social.party.followTarget = 1;
    ASSERT_TRUE(Client::Scenes::ApplyPartyLeft(stage->Resources()).HasValue());
    EXPECT_EQ(stage->Runtime().hud.partyCount, 0);
    EXPECT_EQ(stage->LastMessage().text, "dejaste la party");
    EXPECT_EQ(stage->Runtime().social.party.followTarget, -1);
}

TEST(SocialEventsTest, SixPartyMembersPassTheTable)
{
    auto stage = MakeStage();
    Client::Protocol::Party::PartyList list{.result = 1, .members = {}};
    for (std::uint8_t at = 0; at < 6; ++at)
        list.members.push_back({NameOf<10>("Arbusto"), at, 0, 0, 0, 1, 1});
    const auto applied = Client::Scenes::ApplyPartyList(list, *stage->world);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Q-C960"), std::string::npos);
}

// Un duelo: el pedido (AC), el comienzo (AA), los puntos (AD) y el fin (AB).
TEST(SocialEventsTest, ADuelFromRequestToEnd)
{
    auto stage = MakeStage();
    Client::Scenes::ApplyDuelRequest({0x0102, NameOf<10>("Brezal")}, *stage->world);
    const Client::Scenes::DuelState& duel = stage->Runtime().social.duel;
    EXPECT_EQ(stage->Runtime().ui.field78abad9, 1);
    EXPECT_EQ(duel.rivalKey, 0x0102);
    EXPECT_EQ(TextOf(duel.rivalName), "Brezal");
    EXPECT_EQ(stage->Runtime().lastSoundId, 0x8c);

    ASSERT_TRUE(Client::Scenes::ApplyDuelStart({1, 0x0102, NameOf<10>("Brezal")}, stage->Resources()).HasValue());
    EXPECT_EQ(duel.active, 1);
    EXPECT_EQ(stage->LastMessage().text, "duelo con Brezal");
    EXPECT_EQ(stage->Runtime().lastSoundId, 0x8d);

    Client::Scenes::ApplyDuelScore({0x0033, 0x0102, 2, 5}, *stage->world);
    EXPECT_EQ(duel.rivalScore, 5);
    EXPECT_EQ(duel.ownScore, 2);
    Client::Scenes::ApplyDuelScore({0x0102, 0x0033, 3, 4}, *stage->world);
    EXPECT_EQ(duel.rivalScore, 3);
    EXPECT_EQ(duel.ownScore, 4);
    Client::Scenes::ApplyDuelScore({0x0044, 0x0055, 9, 9}, *stage->world);
    EXPECT_EQ(duel.rivalScore, 3);

    stage->Runtime().social.duel.byte5767814 = 2;
    stage->Runtime().social.duel.word5767816 = 7;
    ASSERT_TRUE(Client::Scenes::ApplyDuelEnd({0x0102, NameOf<10>("Brezal")}, stage->Resources()).HasValue());
    EXPECT_EQ(duel.active, 0);
    EXPECT_EQ(duel.word5767816, 0);
    EXPECT_EQ(stage->LastMessage().text, "fin del duelo");
    EXPECT_EQ(DrawsSince(stage->cloth.random), 0);
}

TEST(SocialEventsTest, ARefusedDuelOnlyKeepsTheName)
{
    auto stage = MakeStage();
    ASSERT_TRUE(Client::Scenes::ApplyDuelStart({0, 0x0102, NameOf<10>("Brezal")}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->Runtime().social.duel.active, 0);
    EXPECT_EQ(stage->Runtime().social.duel.rivalKey, 0);
    EXPECT_EQ(TextOf(stage->Runtime().social.duel.rivalName), "Brezal");
    EXPECT_EQ(stage->LastMessage().text, "no quiso");

    // Con el diálogo 0x9a abierto, el AC no hace nada.
    stage->Runtime().dialogs.current = 0x9a;
    Client::Scenes::ApplyDuelRequest({0x0203, NameOf<10>("Arbusto")}, *stage->world);
    EXPECT_EQ(stage->Runtime().ui.field78abad9, 0);
    EXPECT_EQ(TextOf(stage->Runtime().social.duel.rivalName), "Brezal");
}
} // namespace
