#include "Scenes/PersonalShopLists.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/ActorState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/SelectionState.h"
#include "Scenes/SocialEvents.h"
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
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// Scenes/PersonalShopLists (CL9 hito 7, fase 4): abrir y cerrar las tiendas personales (0x6599a0 con 0x800 y 0x20000,
// 0x5eb020) y las listas del 3F (0x4db120, 0x4db890, 0x4dbcb0 y 0x4dbb40), contra el ASM. El GameServer 0.97 no tiene
// el 3F: los paquetes salen del cliente. Nombres, títulos y textos inventados; cada caso en Lorencia y en Devias.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::WorldState;
using Client::World::ItemState;
namespace Shop = Client::Protocol::PersonalShop;

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::size_t Hero = 0;
constexpr std::size_t Seller = 7;
constexpr std::uint16_t HeroKey = 0x0101;
constexpr std::uint16_t SellerKey = 0x0234;
// Un tipo de Item.bmd de 1 por 2 y otro de 1 por 1 (sintéticos).
constexpr std::uint16_t TallType = 0x001;
constexpr std::uint16_t SmallType = 0x003;
constexpr std::uint8_t FirstShopSlot = 0x4c;
// Las consultas de los bytes protegidos de 0x10000 y 0x20 (un rand() cada una) y el relleno y el serial de un C3.
constexpr std::uint32_t C3Randoms = 2;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind /*font*/,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

Client::Protocol::Inventory::ItemData ItemBytes(std::uint16_t type, std::uint8_t level = 0)
{
    return {static_cast<std::byte>(type & 0xff),
            static_cast<std::byte>(level << 3),
            std::byte{20},
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

std::array<std::byte, Shop::TitleSize> TitleOf(std::string_view text)
{
    std::array<std::byte, Shop::TitleSize> title{};
    for (std::size_t at = 0; at < text.size() && at < title.size(); ++at)
        title[at] = static_cast<std::byte>(text[at]);
    return title;
}

class PersonalShopListsTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    PersonalShopListsTest()
    {
        WorldState& world = *m_world;
        world.Runtime().map = GetParam();
        world.Runtime().hero = Hero;
        Place(Hero, HeroKey, "Tejon");
        Place(Seller, SellerKey, "Brezal");
        // 0x5be150 viste al héroe al final (0x512a30), que lee las acciones de Player.bmd.
        m_models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
        // El registro propio abierto alrededor de todo: 0x5bc740 con el último argumento en 0 lo abre y lo cierra en
        // cada ítem (3.848 rand() si queda sin accesos), y acá solo cuentan los paneles y los envíos.
        world.Runtime().ownRecordDepth = 1;
        // El inventario abierto (0x7d53717), como lo pide Open(0x800) (0x6598a0).
        world.Runtime().panels.inventory = 1;
        for (ItemState& cell : world.Inventory().cells)
            cell.id = Client::Scenes::EmptyOwnItemId;
        for (ItemState& cell : world.Inventory().secondCells)
            cell.id = Client::Scenes::EmptyOwnItemId;
        // 0x78a6758 fuera del equipo: 0x5bc740 no abre el registro por las velocidades.
        world.Runtime().slot78a6758 = FirstShopSlot;
    }

    void Place(std::size_t index, std::uint16_t key, std::string_view name)
    {
        Client::Scenes::ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.key = key;
        actor.byte88 = 1;
        for (std::size_t at = 0; at < name.size(); ++at)
            actor.name[at] = static_cast<std::byte>(name[at]);
    }

    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = *m_world,
                .selection = m_selection,
                .dataRoot = {},
                .cloth = m_cloth,
                .camera = m_camera,
                .cameraInput = m_input,
                .models = m_models,
                .items = &m_items,
                .log = m_log,
                .textMeasure = &m_letters,
                .texts = &m_texts};
    }

    Client::Scenes::GamePacketSender Sender()
    {
        return [this](Client::Protocol::Outgoing outgoing) { m_sent.push_back(std::move(outgoing)); };
    }

    Client::Scenes::PersonalShopState& Shops()
    {
        return m_world->Runtime().social.personalShop;
    }

    // Open o Close de un panel como lo llama el original (0x6599a0 y 0x65cae0), con el cable.
    void OpenPanel(Client::UI::PanelId id)
    {
        Client::Scenes::GamePanels panels(*m_world, m_cloth.random);
        panels.LendThePersonalShops(*m_world, m_cloth.random, Sender());
        const auto opened = panels.Registry().Open(id);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        EXPECT_TRUE(opened.Value());
    }
    void ClosePanel(Client::UI::PanelId id)
    {
        Client::Scenes::GamePanels panels(*m_world, m_cloth.random);
        panels.LendThePersonalShops(*m_world, m_cloth.random, Sender());
        const auto closed = panels.Registry().Close(id);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
    }

    // Los rand() desde `before`.
    void ExpectDraws(const CrtRandom& before, std::uint32_t draws)
    {
        CrtRandom expected = before;
        expected.Discard(draws);
        EXPECT_EQ(m_cloth.random.State(), expected.State()) << "se esperaban " << draws << " rand()";
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    Client::Scenes::SelectionState m_selection{*m_world};
    Client::World::GameCamera m_camera;
    Client::World::GameCameraInput m_input;
    Client::Render::Models::ClothWorld m_cloth;
    Client::Scenes::SingleModelLibrary m_models;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Data::Tables::ItemTable m_items = Client::Tests::ItemTableOf({{TallType, 1, 2, 0}, {SmallType, 1, 1, 0}});
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable({{375, "no alcanza el zen"},
                                                                          {423, "no hay lugar"},
                                                                          {1120, "la tienda no se puede ver"},
                                                                          {1166, "la tienda ya no esta"}});
    LetterMeasure m_letters;
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// Abrir la propia, el precio que no se puso, cerrarla y mirar la de otro: comprar, la lista que se renueva y cerrarla.
TEST_P(PersonalShopListsTest, OpenTheOwnShopAndViewAnother)
{
    WorldState& world = *m_world;
    Client::Scenes::WorldRuntime& runtime = world.Runtime();
    Client::Scenes::PersonalShopState& shops = Shops();
    // Un ítem propio en la primera casilla de la tienda (el lugar 0x4c) y el cartel del héroe (3F 02).
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(FirstShopSlot, ItemBytes(SmallType), Resources()).HasValue());
    shops.titles[Hero] = Client::Scenes::ShopTitle{.key = HeroKey, .name = "Tejon", .title = "Mi puesto"};

    // 0x6599a0(0x800): CanOpen consulta 0x10000 y 0x20 (0x6598a0) y CloseByMask, con todo menos la tienda, el
    // inventario y los amigos, los consulta otra vez (0x657ab0). Open(0x4000000) ya está abierto. 0x6c931c en −1: sin
    // el F3 10.
    CrtRandom before = m_cloth.random;
    OpenPanel(Client::UI::PersonalShopPanel);
    ExpectDraws(before, 4);
    EXPECT_EQ(runtime.panels.personalShop, 1);
    EXPECT_EQ(runtime.panels.inventory, 1);
    EXPECT_EQ(shops.mode, Client::Scenes::OwnShopView);
    EXPECT_TRUE(shops.pricesBuilt);
    EXPECT_TRUE(m_sent.empty());

    // 0x4db120 (3F 01) con 1: nada. Con 0 y 0x6c931c en 1: el lugar sale del mapa propio y el F3 10 en C3.
    shops.ownPrices[FirstShopSlot] = 500;
    before = m_cloth.random;
    ASSERT_TRUE(
        Client::Scenes::ApplyPriceResult({.result = 1, .slot = FirstShopSlot}, Resources(), Sender()).HasValue());
    EXPECT_EQ(shops.ownPrices.size(), 1u);
    ASSERT_TRUE(
        Client::Scenes::ApplyPriceResult({.result = 0, .slot = FirstShopSlot}, Resources(), Sender()).HasValue());
    ExpectDraws(before, C3Randoms);
    EXPECT_TRUE(shops.ownPrices.empty());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(PlainBytes(m_sent[0]), (std::vector<std::uint8_t>{0xc1, 0x04, 0xf3, 0x10}));
    EXPECT_EQ(m_sent[0].delivery, Client::Network::Delivery::Encrypted);

    // 0x65cae0(0x800): 0x5eb020 con 0x6c931c en 1 solo lo vuelve a −1; sin rand().
    before = m_cloth.random;
    ClosePanel(Client::UI::PersonalShopPanel);
    ExpectDraws(before, 0);
    EXPECT_EQ(runtime.panels.personalShop, 0);
    EXPECT_EQ(shops.mode, Client::Scenes::NoShopView);
    EXPECT_EQ(runtime.panels.inventory, 1);

    // 0x4db890 (3F 05) del vendedor, con dos ítems. Close(0x4000000): sus compañeros (0x657ab0 con 0x11d9e0, que
    // consulta 0x10000 y 0x20) y el inventario; Open(0x20000): CanOpen y CloseByMask (cuatro consultas) y
    // Open(0x4000000) con las suyas (otras cuatro). 0x6c931c en −1: sin el F3 10.
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.title = TitleOf("Cosas baratas");
    list.entries = {{.slot = FirstShopSlot + 9, .item = ItemBytes(TallType, 2), .price = 1500},
                    {.slot = FirstShopSlot + 3, .item = ItemBytes(SmallType), .price = 200}};
    before = m_cloth.random;
    const auto viewed = Client::Scenes::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(viewed.HasValue()) << viewed.Error();
    ExpectDraws(before, 10);
    EXPECT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(runtime.panels.personalShop, 1);
    EXPECT_EQ(runtime.panels.inventory, 1);
    EXPECT_EQ(shops.mode, Client::Scenes::OtherShopView);
    EXPECT_EQ(shops.ownTitle, "Cosas baratas");
    EXPECT_EQ(shops.ownGridBackup[0].id, SmallType);
    EXPECT_EQ(shops.ownGridBackup[0].occupied, 1u);
    const auto& grid = world.Inventory().secondCells;
    EXPECT_EQ(grid[0].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(grid[9].id, TallType);
    EXPECT_EQ(grid[9].occupied, 1u);
    EXPECT_EQ(grid[17].id, TallType);
    EXPECT_EQ(grid[17].occupied, 0u);
    EXPECT_EQ(grid[3].id, SmallType);
    EXPECT_EQ(shops.otherPrices,
              (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 3, 200}, {FirstShopSlot + 9, 1500}}));
    EXPECT_EQ(shops.ownerKey, SellerKey);
    EXPECT_EQ(shops.ownerName[0], std::byte{'B'});
    EXPECT_EQ(shops.ownerName[5], std::byte{'l'});

    // 0x4dbcb0 (3F 06) con 1 y 0x6c931c en 2: el lugar elegido (0x78a6758) sale del mapa y de la grilla y lo comprado
    // va a su lugar del inventario (12 + 5).
    runtime.slot78a6758 = FirstShopSlot + 3;
    before = m_cloth.random;
    const auto bought = Client::Scenes::ApplyShopPurchase({.result = 1, .item = ItemBytes(SmallType), .slot = 12 + 5},
                                                          Resources(), Sender());
    ASSERT_TRUE(bought.HasValue()) << bought.Error();
    ExpectDraws(before, 0);
    EXPECT_EQ(grid[3].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(world.Inventory().cells[5].id, SmallType);
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 9, 1500}}));

    // 0x4dbb40 (3F 0x13): la grilla y el mapa de nuevo, sin mirar el precio.
    const Shop::ShopRefresh refresh{.result = Shop::ListedResult,
                                    .entries = {{.slot = FirstShopSlot + 1, .item = ItemBytes(SmallType), .price = 0}}};
    ASSERT_TRUE(Client::Scenes::ApplyShopRefresh(refresh, Resources()).HasValue());
    EXPECT_EQ(grid[9].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(grid[1].id, SmallType);
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 1, 0}}));

    // 0x65cae0(0x20000): 0x5eb020 con 0x6c931c en 2 devuelve la grilla propia y el título del cartel del héroe, y manda
    // el 3F 07 con la clave y el nombre del dueño en C3.
    before = m_cloth.random;
    ClosePanel(Client::UI::OtherShopPanel);
    ExpectDraws(before, C3Randoms);
    EXPECT_EQ(runtime.panels.personalShop, 0);
    EXPECT_EQ(shops.mode, Client::Scenes::NoShopView);
    EXPECT_EQ(grid[0].id, SmallType);
    EXPECT_EQ(grid[1].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(shops.ownTitle, "Mi puesto");
    EXPECT_EQ(shops.ownerKey, 0);
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(PlainBytes(m_sent[1]), (std::vector<std::uint8_t>{0xc1, 0x10, 0x3f, 0x07, 0x02, 0x34, 'B', 'r', 'e', 'z',
                                                                'a', 'l', 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(m_sent[1].delivery, Client::Network::Delivery::Encrypted);
}

// 0x4db890: un precio de 0 o menos cierra las dos y sale sin la copia del dueño; con 3, el texto 1120 con el tipo 2.
TEST_P(PersonalShopListsTest, ABadListClosesTheShopOrSaysIt)
{
    // Un ítem propio en la casilla 5 de la tienda: 0x5eb020 devuelve la grilla de la copia al cerrar.
    ASSERT_TRUE(Client::Scenes::PlaceInventoryItem(FirstShopSlot + 5, ItemBytes(SmallType), Resources()).HasValue());
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 100},
                    {.slot = FirstShopSlot + 2, .item = ItemBytes(SmallType), .price = 0},
                    {.slot = FirstShopSlot + 4, .item = ItemBytes(SmallType), .price = 300}};
    const auto closed = Client::Scenes::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    // Lo puesto antes del precio en 0 se va con la grilla de la copia.
    const auto& grid = m_world->Inventory().secondCells;
    EXPECT_EQ(grid[0].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(grid[5].id, SmallType);
    EXPECT_EQ(Shops().otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot, 100}}));
    EXPECT_EQ(m_world->Runtime().panels.personalShop, 0);
    EXPECT_EQ(Shops().mode, Client::Scenes::NoShopView);
    // El dueño nunca se copió: 0x5eb020 no manda el 3F 07.
    EXPECT_TRUE(m_sent.empty());

    list.result = 3;
    ASSERT_TRUE(Client::Scenes::ApplyShopItemList(list, Resources(), Sender()).HasValue());
    const auto& entries = m_world->Runtime().chat.log.entries;
    ASSERT_FALSE(entries.empty());
    EXPECT_EQ(entries.front().text, "la tienda no se puede ver");
    EXPECT_EQ(entries.front().type, Client::UI::ErrorMessage);
}

// 07 Q-C962: strncpy(0x78aba64, pkt + 0x12, 0x24) no termina un título de 36 bytes sin un 0, y el original lo sigue
// con lo que haya en la .bss desde 0x78aba88. MuRet se queda con los 36.
TEST_P(PersonalShopListsTest, AFullTitleKeepsItsThirtySixBytes)
{
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.title.fill(std::byte{'x'});
    list.entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 100}};
    const auto viewed = Client::Scenes::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(viewed.HasValue()) << viewed.Error();
    EXPECT_EQ(Shops().ownTitle, std::string(Shop::TitleSize, 'x'));
}

// 0x4dbcb0: 6 avisa y cierra; 7 y 8 avisan y vuelven el ítem del cursor; otro solo lo vuelve.
TEST_P(PersonalShopListsTest, TheFailedPurchasesSayWhy)
{
    const auto& entries = m_world->Runtime().chat.log.entries;
    ASSERT_TRUE(Client::Scenes::ApplyShopPurchase({.result = 7}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.front().text, "no hay lugar");
    ASSERT_TRUE(Client::Scenes::ApplyShopPurchase({.result = 8}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.front().text, "no alcanza el zen");
    const std::size_t said = entries.size();
    ASSERT_TRUE(Client::Scenes::ApplyShopPurchase({.result = 2}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.size(), said);
    ASSERT_TRUE(Client::Scenes::ApplyShopPurchase({.result = 6}, Resources(), Sender()).HasValue());
    EXPECT_EQ(entries.front().text, "la tienda ya no esta");
    EXPECT_EQ(entries.front().type, Client::UI::ErrorMessage);
}

// 0x4dbb40 fuera de 0x6c931c en 2 y 0x4db120 fuera de 1 no tocan nada; antes del primer Open, los mapas no existen y
// el 3F 05 pone los ítems sin los precios (Open(0x20000) no abre con el comercio abierto).
TEST_P(PersonalShopListsTest, WithoutTheMapsThePricesAreLost)
{
    const Shop::ShopRefresh refresh{.result = Shop::ListedResult,
                                    .entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 50}}};
    ASSERT_TRUE(Client::Scenes::ApplyShopRefresh(refresh, Resources()).HasValue());
    EXPECT_EQ(m_world->Inventory().secondCells[0].id, Client::Scenes::EmptyOwnItemId);
    ASSERT_TRUE(Client::Scenes::ApplyPriceResult({.result = 0, .slot = FirstShopSlot}, Resources(), {}).HasValue());

    m_world->Runtime().panels.trade = 1;
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.entries = {{.slot = FirstShopSlot + 6, .item = ItemBytes(SmallType), .price = 70}};
    // Con el comercio abierto (y el inventario cerrado), CanOpen no deja abrir 0x20000.
    m_world->Runtime().panels.inventory = 0;
    const auto listed = Client::Scenes::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    EXPECT_EQ(m_world->Inventory().secondCells[6].id, SmallType);
    EXPECT_FALSE(Shops().pricesBuilt);
    EXPECT_TRUE(Shops().otherPrices.empty());
    EXPECT_EQ(Shops().mode, Client::Scenes::NoShopView);
    EXPECT_EQ(Shops().ownerKey, SellerKey);
}

// 0x4c5ec0 (28) desde el 0x4c: con 0x6c931c en 2, la copia de la grilla propia; si no, el ítem del cursor levantado de
// esa casilla vuelve antes (0x5be150 con 0x78ab0b8) y la casilla queda vacía.
TEST_P(PersonalShopListsTest, TheDeleteOfAShopSlotLooksAtTheMode)
{
    WorldState& world = *m_world;
    Client::Scenes::WorldRuntime& runtime = world.Runtime();
    Client::Scenes::PersonalShopState& shops = Shops();
    shops.ownGridBackup[10].id = TallType;
    shops.ownGridBackup[10].gridColumn = 2;
    shops.ownGridBackup[10].gridRow = 1;
    shops.ownGridBackup[18] = shops.ownGridBackup[10];
    shops.mode = Client::Scenes::OtherShopView;
    namespace Inventory = Client::Protocol::Inventory;
    ASSERT_TRUE(Client::Scenes::ApplyItemDelete(Inventory::ItemDelete{.slot = FirstShopSlot + 10, .endsTheWait = 0},
                                                Resources())
                    .HasValue());
    EXPECT_EQ(shops.ownGridBackup[10].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(shops.ownGridBackup[18].id, Client::Scenes::EmptyOwnItemId);

    // Levantado de la casilla 4 de la tienda: vuelve (0x5be3eb) y el 28 la vacía.
    shops.mode = Client::Scenes::NoShopView;
    runtime.cursorItem.id = SmallType;
    runtime.cursorItem.gridColumn = 4;
    runtime.cursorItem.gridRow = 0;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Scenes::CursorGrid::PersonalShop;
    runtime.slot78a6758 = FirstShopSlot + 4;
    const auto deleted = Client::Scenes::ApplyItemDelete(
        Inventory::ItemDelete{.slot = FirstShopSlot + 4, .endsTheWait = 1}, Resources());
    ASSERT_TRUE(deleted.HasValue()) << deleted.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().secondCells[4].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(runtime.lastSoundId, 0x1d);
}

// 0x5be150 con la grilla de la tienda (0x5be3eb, desde el lugar − 0x4c) y la propia del comercio (0x5be191).
TEST_P(PersonalShopListsTest, TheCursorItemGoesBackToItsGrid)
{
    WorldState& world = *m_world;
    Client::Scenes::WorldRuntime& runtime = world.Runtime();
    // Los servicios guardan referencias a los recursos: que vivan toda la prueba.
    const Client::Scenes::WorldEntryResources resources = Resources();
    const auto services = Client::Scenes::InventoryServicesOf(resources);
    const Client::Scenes::InventoryMoveContext context{.world = world, .random = m_cloth.random, .services = services};
    runtime.cursorItem.id = TallType;
    runtime.cursorItem.gridColumn = 5;
    runtime.cursorItem.gridRow = 2;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Scenes::CursorGrid::PersonalShop;
    runtime.slot78a6758 = FirstShopSlot + 21;
    ASSERT_TRUE(Client::Scenes::ReturnTheCursorItem(context).HasValue());
    EXPECT_EQ(world.Inventory().secondCells[21].id, TallType);
    EXPECT_EQ(world.Inventory().secondCells[21].occupied, 1u);
    EXPECT_EQ(world.Inventory().secondCells[29].occupied, 0u);
    EXPECT_EQ(runtime.cursorItemCount, 0);

    Client::Scenes::TradeState& trade = runtime.social.trade;
    for (ItemState& cell : trade.ownGrid)
        cell.id = Client::Scenes::EmptyOwnItemId;
    runtime.cursorItem.gridColumn = 1;
    runtime.cursorItem.gridRow = 0;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Scenes::CursorGrid::OwnTrade;
    runtime.slot78a6758 = 1;
    ASSERT_TRUE(Client::Scenes::ReturnTheCursorItem(context).HasValue());
    EXPECT_EQ(trade.ownGrid[1].id, TallType);
    EXPECT_EQ(trade.ownGrid[1].occupied, 1u);
    EXPECT_EQ(trade.ownGrid[9].id, TallType);
    EXPECT_EQ(trade.ownGrid[9].occupied, 0u);
}

// 0x5bc740 con 0x78a9bb0 escribe la huella sin tope (Q-C200): un 3F 05 con un ítem de 1 por 2 en la última fila de la
// grilla de la tienda se descarta entero, antes de cerrar o abrir nada y sin rand(); lo de detrás de un precio de 0 no
// cuenta. Lo mismo un 3F 06 que pasa la casilla 63 del inventario y un 3F 0x13 que pasa la 31 de la tienda, o que
// empieza en el lugar 116.
TEST_P(PersonalShopListsTest, AnEntryPastItsGridDiscardsThePacket)
{
    WorldState& world = *m_world;
    Shop::ShopItemList list{};
    list.result = Shop::ListedResult;
    list.key = SellerKey;
    list.title = TitleOf("Cosas raras");
    list.entries = {{.slot = FirstShopSlot, .item = ItemBytes(SmallType), .price = 100},
                    {.slot = FirstShopSlot + 28, .item = ItemBytes(TallType), .price = 100}};
    CrtRandom before = m_cloth.random;
    const auto listed = Client::Scenes::ApplyShopItemList(list, Resources(), Sender());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    ExpectDraws(before, 0);
    EXPECT_EQ(world.Runtime().panels.personalShop, 0);
    EXPECT_EQ(world.Runtime().panels.inventory, 1);
    EXPECT_EQ(Shops().mode, Client::Scenes::NoShopView);
    EXPECT_TRUE(Shops().ownTitle.empty());
    EXPECT_EQ(world.Inventory().secondCells[0].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_TRUE(m_sent.empty());
    ASSERT_FALSE(m_sink.lines.empty());
    EXPECT_NE(m_sink.lines.back().find("3F 05"), std::string::npos) << m_sink.lines.back();
    EXPECT_NE(m_sink.lines.back().find("casilla 36"), std::string::npos) << m_sink.lines.back();
    EXPECT_NE(m_sink.lines.back().find("Q-C200"), std::string::npos) << m_sink.lines.back();

    // Detrás de un precio de 0 no se pone: la lista se aplica (los Open y Close de los paneles sacan sus rand()) y
    // cierra las dos.
    std::size_t warnings = m_sink.lines.size();
    list.entries[0].price = 0;
    before = m_cloth.random;
    ASSERT_TRUE(Client::Scenes::ApplyShopItemList(list, Resources(), Sender()).HasValue());
    EXPECT_EQ(m_sink.lines.size(), warnings);
    EXPECT_NE(m_cloth.random.State(), before.State());
    EXPECT_EQ(Shops().mode, Client::Scenes::NoShopView);

    // 3F 06 a la casilla 60 del inventario (el lugar 72): la 68 pasa las 64. Ni el mapa se vacía.
    Shops().pricesBuilt = true;
    Shops().otherPrices = {{FirstShopSlot + 3, 5}};
    before = m_cloth.random;
    const auto bought = Client::Scenes::ApplyShopPurchase({.result = 1, .item = ItemBytes(TallType), .slot = 12 + 60},
                                                          Resources(), Sender());
    ASSERT_TRUE(bought.HasValue()) << bought.Error();
    ExpectDraws(before, 0);
    EXPECT_EQ(world.Inventory().cells[60].id, Client::Scenes::EmptyOwnItemId);
    EXPECT_EQ(Shops().otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 3, 5}}));
    EXPECT_NE(m_sink.lines.back().find("3F 06"), std::string::npos) << m_sink.lines.back();
    EXPECT_NE(m_sink.lines.back().find("0x78a9bb0"), std::string::npos) << m_sink.lines.back();

    // 3F 0x13 con la de otro a la vista: ni la grilla ni el mapa se vacían.
    Shops().mode = Client::Scenes::OtherShopView;
    world.Inventory().secondCells[1].id = SmallType;
    for (const std::uint8_t slot : {static_cast<std::uint8_t>(FirstShopSlot + 31), std::uint8_t{116}})
    {
        warnings = m_sink.lines.size();
        const std::uint16_t type = slot == 116 ? SmallType : TallType;
        const Shop::ShopRefresh refresh{.result = Shop::ListedResult,
                                        .entries = {{.slot = slot, .item = ItemBytes(type), .price = 9}}};
        ASSERT_TRUE(Client::Scenes::ApplyShopRefresh(refresh, Resources()).HasValue());
        EXPECT_EQ(world.Inventory().secondCells[1].id, SmallType);
        EXPECT_EQ(Shops().otherPrices, (std::map<std::int32_t, std::int32_t>{{FirstShopSlot + 3, 5}}));
        ASSERT_EQ(m_sink.lines.size(), warnings + 1);
        EXPECT_NE(m_sink.lines.back().find("3F 0x13"), std::string::npos) << m_sink.lines.back();
        EXPECT_NE(m_sink.lines.back().find("0x78ab0b8"), std::string::npos) << m_sink.lines.back();
    }
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, PersonalShopListsTest, ::testing::Values(Lorencia, Devias));
} // namespace
