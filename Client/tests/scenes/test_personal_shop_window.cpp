#include "Scenes/PersonalShopWindow.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/Inventory.h"
#include "Protocol/OptionData.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PersonalShop.h"
#include "Render/Models/Cloth.h"
#include "Render/Text/FontSet.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SocialState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/Dialogs.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include "support/ItemFiles.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La ventana de la tienda personal (CL9 hito 7, fase 5, 08 P-C208 b) en Lorencia (0) y Devias (2), contra el ASM: los
// botones de 0x5ea290 (el 3F 02, el 3F 03, el aviso 1119, la caja 0x9c y el diálogo del título), la caja 0x9c
// (0x4716e0, 0x471930 y 0x623750), el clic y el clic derecho sobre las grillas con una tienda abierta (0x5c5110 y
// 0x5c5242), soltar en la grilla de la tienda (0x5ca250 con 0x78ab0b8 y 4) y de ella al inventario (0x5cc2cf), y las
// respuestas de los avisos que mira 0x5d8d50 (el precio, la compra y el OK del comercio). Los títulos, los nombres y
// los ítems son inventados.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::EmptyOwnItemId;
using Client::World::ItemState;

constexpr std::uint32_t Seed = 0x5a0e;
// Un bastón de 1 × 3, una poción de 1 × 1 y uno que no se comercia (0x1c17), de 1 × 1.
constexpr std::uint16_t StaffType = 0x0a05;
constexpr std::uint16_t PotionType = 0x1c00;
constexpr std::uint16_t UntradableType = 0x1c17;
// La ventana del inventario en (0x1c2, 0), con su grilla en (x + 15, y + 200), y la de la tienda en (0x104, 0), con la
// suya en (x + 15, y + 0x6e).
constexpr std::int32_t InventoryX = 0x1c2;
constexpr std::int32_t InventoryGridLeft = InventoryX + 15;
constexpr std::int32_t InventoryGridTop = 200;
constexpr std::int32_t ShopX = 0x104;
constexpr std::int32_t ShopGridLeft = ShopX + 15;
constexpr std::int32_t ShopGridTop = 0x6e;
constexpr std::int32_t Cell = 20;
// El bastón en la casilla (1, 2) del inventario (lugar 12 + 17) y la poción en la (3, 0) de la tienda (lugar 0x4f).
constexpr std::size_t StaffCell = 17;
constexpr std::size_t PotionCell = 3;
// Los botones del pie de la ventana: abrir (0x122..0x15a) y cerrar (x + 0x69..x + 0xa1), en y 0x18c..0x1a4; el título
// en (x + 0x14..x + 0xaa, y + 0x41..y + 0x59).
constexpr std::int32_t OpenButtonX = 0x122;
constexpr std::int32_t CloseButtonX = ShopX + 0x69;
constexpr std::int32_t FootButtonY = 0x18c;
constexpr std::int32_t TitleBoxX = ShopX + 0x14;
constexpr std::int32_t TitleBoxY = 0x41;
// Un lugar sin nada: abajo a la izquierda, fuera de las dos ventanas.
constexpr std::int32_t NowhereX = 10;
constexpr std::int32_t NowhereY = 300;
// Los textos de Text.bmd que usa: la pregunta de la compra (1100), el aviso sin precio (1119), la pregunta de abrir
// (1131, de 47 bytes: dos renglones) y el precio bajo (1132).
constexpr std::size_t BuyText = 1100;
constexpr std::size_t MissingPriceText = 1119;
constexpr std::size_t OpenShopText = 1131;
constexpr std::size_t LowPriceText = 1132;

// El paso con el inventario y una tienda abiertos, el registro propio abierto (sin los 3.848 de sus cierres) y nada
// bajo el mouse: los 12 de test_game_inventory_step.cpp con la ventana sola menos uno, porque la búsqueda de cualquier
// panel abierto (0x5a5511, de 0x1 hacia arriba) se detiene en 0x800 (las dos tiendas comparten su estado) después de
// 0x20 y antes de 0x10000. Medido: la ventana de la tienda, su grilla y las respuestas no sacan más.
constexpr int QuietStepRandoms = 11;
// IsOpen(0x20) del paso del ítem del cursor (0x5deb11), con un ítem en el cursor.
constexpr int CursorQueryRandoms = 1;
// El relleno y el serial de un C3.
constexpr int C3Randoms = 2;
// El paso de los diálogos sin Esc (0x623e70): la consulta de la ventana de amigos (0x623e9b) no saca.
constexpr int DialogStepRandoms = 0;
// 0x657a90 consulta cada panel: 0x20 y 0x10000 sacan uno cada uno.
constexpr int CloseAllRandoms = 2;

constexpr std::uint8_t U8(std::size_t value)
{
    return static_cast<std::uint8_t>(value);
}

std::map<std::size_t, std::string> ShopTexts()
{
    return {{BuyText, "aviso compra"},
            {MissingPriceText, "aviso sin precio"},
            {OpenShopText, "abrir la tienda con estos precios y este titulo"},
            {LowPriceText, "aviso precio bajo"}};
}

struct Sent
{
    bool encrypted = false;
    std::vector<std::uint8_t> bytes;
};

Sent PlainOf(const Client::Protocol::Outgoing& outgoing)
{
    const auto plain = Client::Network::Crypto::Xor32Decode(outgoing.packet);
    Sent sent{.encrypted = outgoing.delivery == Client::Network::Delivery::Encrypted, .bytes = {}};
    for (const std::byte b : plain.Bytes())
        sent.bytes.push_back(std::to_integer<std::uint8_t>(b));
    return sent;
}

// Lo que sale por el cable de la UI (IGameUiRequests::SendGamePacket), en la misma lista que lo de InventoryServices.
class RecordingRequests final : public Client::Scenes::IGameUiRequests
{
public:
    explicit RecordingRequests(std::vector<Sent>& sent) : m_sent(sent) {}
    void SendOptionData(const Client::Protocol::OptionData::OptionBody& /*body*/) override {}
    void RequestLogout(std::uint8_t /*kind*/) override {}
    void RequestPartyList() override {}
    void SendGamePacket(Client::Protocol::Outgoing outgoing) override
    {
        m_sent.push_back(PlainOf(outgoing));
    }
    Client::Core::Expected<void, std::string> SaveMacros(std::string_view /*content*/) override
    {
        return {};
    }
    void Log(std::string_view /*line*/) override {}

private:
    std::vector<Sent>& m_sent;
};

// Los rand() que se sacaron desde `from` hasta `to` (hasta 100.000).
int Drawn(CrtRandom from, const CrtRandom& to)
{
    for (int count = 0; count <= 100'000; ++count)
    {
        if (from.State() == to.State())
            return count;
        static_cast<void>(from.Next());
    }
    return -1;
}

// El mundo del juego, en el heap.
struct ShopWorld
{
    Client::Scenes::WorldState world;
    Client::Data::Tables::ItemTable items =
        Client::Tests::ItemTableOf({{StaffType, 1, 3, 0}, {PotionType, 1, 1, 0xff}, {UntradableType, 1, 1, 0xff}});
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(ShopTexts());
};

class PersonalShopWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    void SetUp() override
    {
        shop = std::make_unique<ShopWorld>();
        Client::Scenes::WorldState& world = shop->world;
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        runtime.map = GetParam();
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        runtime.panels.inventory = 1;
        runtime.hud.inventoryX = InventoryX;
        runtime.hud.inventoryY = 0;
        // La esquina del registro del chat fuera de las ventanas, como en test_trade_grids.cpp.
        runtime.hud.chatFrame.x = -10;
        runtime.hud.chatFrame.y = 0x65;
        runtime.ownRecordDepth = 1;
        // La esquina de la ventana, que deja su dibujo (0x5f306e) antes del primer paso.
        runtime.social.personalShop.windowX = ShopX;
        runtime.social.personalShop.windowY = 0;
        services.items = &shop->items;
        services.dressTheHero = [] { return Client::Core::Expected<void, std::string>{}; };
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.emplace_back(index, type);
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendItemMove = [this](const Client::Protocol::Inventory::ItemMove& move)
        {
            sent.push_back(PlainOf(Client::Protocol::Inventory::WriteItemMove(move)));
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendGamePacket = [this](Client::Protocol::Outgoing packet) { sent.push_back(PlainOf(packet)); };
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return shop->world.Runtime();
    }

    Client::Scenes::PersonalShopState& Shops()
    {
        return Runtime().social.personalShop;
    }

    std::array<ItemState, Client::Scenes::SecondGridCellCount>& ShopCells()
    {
        return shop->world.Inventory().secondCells;
    }

    std::array<ItemState, Client::Scenes::InventoryCellCount>& InventoryCells()
    {
        return shop->world.Inventory().cells;
    }

    // Open(0x800) u Open(0x20000), como el botón del pie o el 3F 05.
    void OpenTheShop(Client::UI::PanelId id)
    {
        Client::Scenes::GamePanels panels(shop->world, random);
        panels.LendThePersonalShops(shop->world, random,
                                    [this](Client::Protocol::Outgoing packet) { sent.push_back(PlainOf(packet)); });
        const auto opened = panels.Registry().Open(id);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        sent.clear();
    }

    // El ítem `type` de nivel `level` y `height` casillas de alto en la casilla `index` de una grilla de 8 columnas:
    // la copia en cada casilla de su huella, con su columna y su fila, y +0x3c en 1 solo en la de arriba.
    static void Place(std::span<ItemState> cells, std::size_t index, std::uint16_t type, std::uint32_t level,
                      std::size_t height)
    {
        for (std::size_t row = 0; row < height; ++row)
        {
            ItemState& item = cells[index + row * 8];
            item = ItemState{};
            item.id = type;
            item.scalar1Low = level << 3;
            item.durability = 30;
            item.gridColumn = static_cast<std::uint8_t>(index % 8);
            item.gridRow = static_cast<std::uint8_t>(index / 8);
            item.occupied = row == 0 ? 1U : 0U;
        }
    }

    // Un paso de la UI con el mouse en (x, y) y, si `click`, el flanco del clic; con `rightClick`, el del derecho.
    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click, bool rightClick = false)
    {
        if (click)
            Client::Scenes::LatchTheLeftButton(Runtime().movement, true, true);
        if (rightClick)
            Runtime().ui.rightClick = true;
        mark = random;
        return Client::Scenes::StepTheGameUi(Context(x, y));
    }

    // El paso de los diálogos (0x623e70) con el mouse en (x, y) y el clic.
    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x, std::int32_t y, bool click)
    {
        if (click)
            Client::Scenes::LatchTheLeftButton(Runtime().movement, true, true);
        mark = random;
        return Client::Scenes::StepTheGameDialogs(Context(x, y));
    }

    Client::Scenes::GameUiContext Context(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::GameUiContext{.world = shop->world,
                                             .random = random,
                                             .input = input,
                                             .mouseX = x,
                                             .mouseY = y,
                                             .texts = &shop->texts,
                                             .requests = &requests,
                                             .inventory = &services};
    }

    // Los rand() del último paso.
    [[nodiscard]] int DrawnByTheStep() const
    {
        return Drawn(mark, random);
    }

    // La x y la y del centro de la casilla (column, row) de una grilla desde (left, top).
    static std::int32_t CenterOf(std::int32_t corner, std::size_t place)
    {
        return corner + static_cast<std::int32_t>(place) * Cell + Cell / 2;
    }

    // Lo que deja el paso del diálogo 0x91 (0x6260f6, que corta: su campo es el EDIT del hito 6) con el precio escrito
    // y el OK: el diálogo cerrado, 0x77f3b6c con el precio y 0x6c932e en 1.
    void ThePriceIsTyped(std::int32_t price)
    {
        Runtime().dialogs.current = 0;
        Runtime().social.typedMoney = price;
        Runtime().social.trade.warningByte = 1;
    }

    // El valor del ítem (0x54d0f0 con el modo 2).
    [[nodiscard]] std::uint32_t ValueOf(const ItemState& item) const
    {
        const auto value = Client::Scenes::ItemPrice(item, shop->items, 2, std::nullopt);
        EXPECT_TRUE(value.HasValue());
        return value.HasValue() ? value.Value() : 0;
    }

    std::unique_ptr<ShopWorld> shop;
    CrtRandom random{Seed};
    CrtRandom mark{Seed};
    Client::Input::InputState input;
    std::vector<Sent> sent;
    RecordingRequests requests{sent};
    Client::Scenes::InventoryServices services;
    std::vector<std::pair<std::size_t, std::int32_t>> messages;
};

std::vector<std::uint8_t> OpenShopBytes(std::string_view title)
{
    std::vector<std::uint8_t> bytes{0xc1, 0x28, 0x3f, 0x02};
    for (std::size_t at = 0; at < Client::Protocol::PersonalShop::TitleSize; ++at)
        bytes.push_back(at < title.size() ? static_cast<std::uint8_t>(title[at]) : std::uint8_t{0});
    return bytes;
}

const std::vector<std::uint8_t> CloseShopBytes{0xc1, 0x04, 0x3f, 0x03};

// 0x5ea290: la esquina en cada paso; el botón de abrir sin los precios, sin ítems o sin título da el aviso 1119 del
// tipo 2 y gasta el clic; con todo, la caja 0x9c con el 1000, los dos renglones del texto 1131 y dos botones. El sí
// (0x471930 → 0x623750) manda el C1 28 3F 02 con el título y los ceros detrás, en C3, y cierra los paneles (0x657a90).
TEST_P(PersonalShopWindowTest, TheOpenButtonAsksAndOpensTheShop)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Shops().windowX = 0;
    auto stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Shops().windowX, ShopX);
    EXPECT_EQ(Shops().windowY, 0);

    // Sin ítems, con un ítem sin precio, con un precio de 0 y sin título: el aviso.
    const auto press = [this] { return Step(OpenButtonX, FootButtonY, true); };
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    Shops().ownPrices[0x4c + static_cast<std::int32_t>(PotionCell)] = 0;
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    Shops().ownPrices[0x4c + static_cast<std::int32_t>(PotionCell)] = 500;
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    ASSERT_EQ(messages.size(), 4U);
    for (const auto& [text, type] : messages)
    {
        EXPECT_EQ(text, MissingPriceText);
        EXPECT_EQ(type, 2);
    }
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_TRUE(sent.empty());

    // Con el título: la caja.
    Shops().ownTitle = "Puesto Sur";
    stepped = press();
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(messages.size(), 4U);
    EXPECT_EQ(Runtime().dialogs.current, 0x9c);
    const Client::Scenes::ConfirmBoxState& box = Runtime().social.confirmBox;
    EXPECT_EQ(box.id, Client::Scenes::OpenShopConfirm);
    EXPECT_EQ(box.type, 2);
    EXPECT_EQ(box.lines, (std::vector<std::string>{"abrir la tienda con estos", " precios y este titulo"}));
    EXPECT_TRUE(sent.empty());

    // Fuera de los botones, sin el clic, nada.
    stepped = Dialogs(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), DialogStepRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x9c);

    // El sí: y de h + 140 a h + 156 con h = 2 · (el paso de la letra + 2), x de 0xff a 0x133.
    const std::int32_t height = 2 * (Client::Render::Text::FontStepFor(640) + 2);
    stepped = Dialogs(0xff, height + 140, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), DialogStepRandoms + C3Randoms + CloseAllRandoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, OpenShopBytes("Puesto Sur"));
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    EXPECT_EQ(Runtime().panels.personalShop, 0);
    EXPECT_EQ(Shops().mode, Client::Scenes::NoShopView);
    EXPECT_FALSE(Runtime().movement.pendingClick);
}

// Con la tienda abierta (0x78aba50): cerrar manda el C1 04 3F 03 en C3; abrir manda el 3F 02 otra vez sin la caja y
// cierra el inventario (0x65cae0(0x4000000)), que se lleva la tienda (0x11d9e0).
TEST_P(PersonalShopWindowTest, WithTheShopOpenTheButtonsSendAtOnce)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    Shops().ownPrices[0x4c + static_cast<std::int32_t>(PotionCell)] = 500;
    Shops().ownTitle = "Puesto Sur";
    Shops().ownOpen = 1;

    auto stepped = Step(CloseButtonX, FootButtonY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_FALSE(Runtime().movement.pendingClick);

    // Cerrada, el botón de cerrar solo gasta el clic.
    sent.clear();
    Shops().ownOpen = 0;
    stepped = Step(CloseButtonX + 0x37, FootButtonY + 0x17, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_FALSE(Runtime().movement.pendingClick);

    Shops().ownOpen = 1;
    stepped = Step(OpenButtonX + 0x37, FootButtonY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    // El C3, el cierre del inventario (0x11d9e0 consulta 0x20 y 0x10000, como 0x657a90) y la búsqueda de 0x5a5511, que
    // sin la tienda llega a 0x10000.
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms + CloseAllRandoms + 1);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, OpenShopBytes("Puesto Sur"));
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().panels.inventory, 0);
    EXPECT_EQ(Runtime().panels.personalShop, 0);
    EXPECT_EQ(Shops().mode, Client::Scenes::NoShopView);
}

// 0x5eafc8..0x5eb00b: el título (sin gastar el clic) abre el diálogo 0x91 en el modo 1, con el campo de 0x1c, sin el
// zen y sin el chat; respondido (0x6c932e en 1 o 2), el paso siguiente lo borra (0x5ea2f1).
TEST_P(PersonalShopWindowTest, TheTitleBoxOpensTheTitleDialog)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Runtime().ui.chatOpen = true;
    Runtime().social.moneyField = 1;
    auto stepped = Step(TitleBoxX, TitleBoxY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::Scenes::ShopTitleMode);
    EXPECT_EQ(Runtime().social.dialogFieldLimit, 0x1c);
    EXPECT_EQ(Runtime().social.moneyField, 0);
    EXPECT_FALSE(Runtime().ui.chatOpen);
    EXPECT_TRUE(sent.empty());

    // El 0x91 (que corta) deja la respuesta y se cierra.
    Runtime().dialogs.current = 0;
    Runtime().social.trade.warningByte = 2;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
}

// 0x5c5110 con la propia: el clic levanta el ítem de cualquiera de las dos grillas (el lugar + 12 o + 0x4c) sin el
// sonido ni el comercio, y borra el ítem bajo el mouse; 0x78abaa4 marca la grilla de la tienda.
TEST_P(PersonalShopWindowTest, TheClickPicksUpFromBothGridsWithTheOwnShop)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    Runtime().lastSoundId = 0;

    // El mouse sobre la poción de la tienda, sin el clic: 0x78abaa4 en 1 y 0x5aed40 con 0 y el lugar.
    const std::int32_t potionX = CenterOf(ShopGridLeft, PotionCell);
    const std::int32_t potionY = CenterOf(ShopGridTop, 0);
    auto stepped = Step(potionX, potionY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().ui.shopGridHovered, 1);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::UI::HoveredItem::Place::ShopCell);
    EXPECT_EQ(Runtime().ui.hoveredItem.index, PotionCell);
    EXPECT_EQ(Runtime().ui.field78abb28, 0);
    EXPECT_EQ(Runtime().ui.field78abb2c, 0x4c + static_cast<std::int32_t>(PotionCell));

    // El bastón desde la casilla de abajo de su huella: el lugar de arriba (12 + 17).
    const std::int32_t staffX = CenterOf(InventoryGridLeft, 1);
    const std::int32_t staffY = CenterOf(InventoryGridTop, 4);
    stepped = Step(staffX, staffY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    EXPECT_EQ(Runtime().ui.shopGridHovered, 0);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_EQ(Runtime().cursorItem.id, StaffType);
    EXPECT_EQ(Runtime().cursorGrid, Client::Scenes::CursorGrid::Inventory);
    EXPECT_EQ(Runtime().slot78a6758, 12 + static_cast<std::int32_t>(StaffCell));
    for (const std::size_t cell : {StaffCell, StaffCell + 8, StaffCell + 16})
        EXPECT_EQ(InventoryCells()[cell].id, EmptyOwnItemId);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::UI::HoveredItem::Place::None);
    EXPECT_EQ(Runtime().hoveredItemCopy.id, StaffType);
    EXPECT_EQ(Runtime().lastSoundId, 0);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_TRUE(sent.empty());

    // La poción de la tienda, con el cursor vacío otra vez.
    Runtime().cursorItemCount = 0;
    stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItem.id, PotionType);
    EXPECT_EQ(Runtime().cursorGrid, Client::Scenes::CursorGrid::PersonalShop);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(PotionCell));
    EXPECT_EQ(ShopCells()[PotionCell].id, EmptyOwnItemId);
    EXPECT_TRUE(sent.empty());
}

// 0x5c5110 con la de otro: el clic sobre su ítem lo copia a 0x78a59c8 con su lugar y abre el 0x94 con el texto 1100,
// que espera el aviso 5; el sí (0x5d9c82) manda el C1 11 3F 06 con la clave del dueño, su nombre y el lugar en C3. El
// no y el sí sin la clave solo lo borran. Uno del inventario se levanta.
TEST_P(PersonalShopWindowTest, TheOtherShopAsksBeforeBuying)
{
    OpenTheShop(Client::UI::OtherShopPanel);
    ASSERT_EQ(Shops().mode, Client::Scenes::OtherShopView);
    Place(ShopCells(), PotionCell + 2, PotionType, 0, 1);
    const std::int32_t potionX = CenterOf(ShopGridLeft, PotionCell + 2);
    const std::int32_t potionY = CenterOf(ShopGridTop, 0);
    auto stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x94);
    EXPECT_EQ(std::string_view(Runtime().dialogBox.rows.data()), "aviso compra");
    EXPECT_EQ(Runtime().dialogItem.id, PotionType);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(PotionCell) + 2);
    EXPECT_EQ(Runtime().social.trade.warningKind, 5);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(ShopCells()[PotionCell + 2].id, PotionType);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::UI::HoveredItem::Place::None);
    EXPECT_FALSE(Runtime().movement.pendingClick);

    // El 0x94 (0x6282dc, CL9.3) deja la respuesta y se cierra. Sin respuesta todavía, el aviso sigue esperando.
    Runtime().dialogs.current = 0;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().social.trade.warningKind, 5);
    Shops().ownerKey = 0x0456;
    Shops().ownerName = {std::byte{'V'}, std::byte{'e'}, std::byte{'n'}, std::byte{'d'}, std::byte{'e'},
                         std::byte{'d'}, std::byte{'o'}, std::byte{'r'}, std::byte{0},   std::byte{0}};
    Runtime().social.trade.warningByte = 1;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x11, 0x3f, 0x06, 0x04, 0x56, 'V', 'e', 'n', 'd', 'e',
                                                        'd', 'o', 'r', 0, 0, U8(0x4c + PotionCell + 2)}));
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_EQ(Runtime().social.trade.warningKind, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);

    // El no, y el sí sin la clave: nada.
    sent.clear();
    for (const auto& [answer, key] : {std::pair<std::uint8_t, std::uint16_t>{2, 0x0456}, {1, 0}})
    {
        Shops().ownerKey = key;
        Runtime().social.trade.warningKind = 5;
        Runtime().social.trade.warningByte = answer;
        stepped = Step(NowhereX, NowhereY, false);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
        EXPECT_EQ(Runtime().social.trade.warningKind, 0);
        EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);
    }
    EXPECT_TRUE(sent.empty());

    // Del inventario, se levanta.
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 2), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItem.id, StaffType);
    EXPECT_EQ(Runtime().slot78a6758, 12 + static_cast<std::int32_t>(StaffCell));
    EXPECT_EQ(Runtime().dialogs.current, 0);
}

// 0x5c5242: el clic derecho copia el ítem a 0x78a59c8 y pone su lugar + 0x4c en 0x78a6758 con cualquier grilla; con la
// propia y su grilla abre el diálogo del precio (0x5eba70(2): el campo de 8 y el zen). El ítem bajo el mouse queda.
TEST_P(PersonalShopWindowTest, TheRightClickOpensThePriceDialog)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    auto stepped = Step(CenterOf(ShopGridLeft, PotionCell), CenterOf(ShopGridTop, 0), false, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_FALSE(Runtime().ui.rightClick);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::Scenes::ShopPriceMode);
    EXPECT_EQ(Runtime().social.dialogFieldLimit, 8);
    EXPECT_EQ(Runtime().social.moneyField, 1);
    EXPECT_EQ(Runtime().dialogItem.id, PotionType);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(PotionCell));
    EXPECT_EQ(Runtime().cursorGrid, Client::Scenes::CursorGrid::PersonalShop);
    EXPECT_EQ(Runtime().ui.hoveredItem.place, Client::UI::HoveredItem::Place::ShopCell);
    EXPECT_EQ(ShopCells()[PotionCell].id, PotionType);

    // Sobre el bastón del inventario: el lugar con 0x4c igual (0x5c526a), sin diálogo.
    Runtime().dialogs.current = 0;
    Shops().dialogMode = 0;
    stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 3), false, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().dialogItem.id, StaffType);
    EXPECT_EQ(Runtime().slot78a6758, 0x4c + static_cast<std::int32_t>(StaffCell));
    EXPECT_EQ(Runtime().cursorGrid, Client::Scenes::CursorGrid::Inventory);
    EXPECT_EQ(InventoryCells()[StaffCell].id, StaffType);
    EXPECT_TRUE(sent.empty());
}

// 0x5d5046 desde el inventario: soltar el bastón en la tienda propia lo copia a 0x78a59c8 y abre el diálogo del precio
// con el destino en 0x77f3cc8. El precio escrito (0x5dc79e) no menor que su valor: el C1 09 3F 01 con el lugar de
// origen y el precio, el 24 de la grilla 0 a la 4 y el precio en el mapa en el destino; menor, el aviso 0x99 (que
// corta, CL9.3). El no vuelve el bastón a su lugar.
TEST_P(PersonalShopWindowTest, DroppingOnTheShopAsksForThePrice)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    auto stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 2), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorItem.id, StaffType);

    // Sin el clic, las marcas de la huella en la grilla de la tienda.
    const std::int32_t dropX = CenterOf(ShopGridLeft, 2);
    const std::int32_t dropY = CenterOf(ShopGridTop, 1);
    stepped = Step(dropX, dropY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    for (const std::size_t cell : {2U, 10U, 18U})
        EXPECT_EQ(ShopCells()[cell].warning, 2) << cell;

    stepped = Step(dropX, dropY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::Scenes::ShopPriceMode);
    EXPECT_EQ(Runtime().dialogItem.id, StaffType);
    EXPECT_EQ(Runtime().itemMoveTarget, 0x4c + 2);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_TRUE(sent.empty());

    // El precio, menor que el valor: el aviso 0x99 corta.
    const std::uint32_t value = ValueOf(Runtime().dialogItem);
    ASSERT_GT(value, 0U);
    ThePriceIsTyped(static_cast<std::int32_t>(value - 1));
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x63333d"), std::string::npos) << stepped.Error();
    EXPECT_EQ(Shops().dialogMode, 0);

    // El precio igual al valor, con la tienda abierta: el 3F 03, el 3F 01 y el 24.
    Shops().dialogMode = Client::Scenes::ShopPriceMode;
    Shops().ownOpen = 1;
    ThePriceIsTyped(static_cast<std::int32_t>(value));
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + 3 * C3Randoms);
    ASSERT_EQ(sent.size(), 3U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    const auto byte = [](std::uint32_t number, unsigned shift) { return static_cast<std::uint8_t>(number >> shift); };
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x09, 0x3f, 0x01, U8(12 + StaffCell), byte(value, 0),
                                                        byte(value, 8), byte(value, 16), byte(value, 24)}));
    EXPECT_TRUE(sent[1].encrypted);
    EXPECT_EQ(sent[2].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x00, U8(12 + StaffCell), 0x05, 2 << 3, 30,
                                                        0x00, 0x00, 0x50, 0x00, 0x04, U8(0x4c + 2)}));
    EXPECT_EQ(Shops().ownPrices.at(0x4c + 2), static_cast<std::int32_t>(value));
    EXPECT_EQ(Runtime().social.typedMoney, 0);
    EXPECT_EQ(Runtime().ui.field78abb39, 1);
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_EQ(Runtime().social.trade.warningByte, 0xff);

    // El no del diálogo: 0x5be150 vuelve el bastón.
    sent.clear();
    Runtime().ui.field78abb39 = 0;
    Shops().dialogMode = Client::Scenes::ShopPriceMode;
    Runtime().social.trade.warningByte = 2;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(InventoryCells()[StaffCell].id, StaffType);
    EXPECT_EQ(Runtime().lastSoundId, 0x1d);
    EXPECT_EQ(Shops().dialogMode, 0);
    EXPECT_TRUE(sent.empty());
}

// El aviso 6 (0x5dc7e9, el del precio bajo) con su respuesta: el no abre el diálogo del precio otra vez; el sí pone el
// precio (sin ítem en el cursor, el 3F 01 y el precio en el lugar de 0x78a6758).
TEST_P(PersonalShopWindowTest, TheLowPriceWarningAnswers)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Runtime().social.trade.warningKind = 6;
    Runtime().social.trade.warningByte = 2;
    auto stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0x91);
    EXPECT_EQ(Shops().dialogMode, Client::Scenes::ShopPriceMode);
    EXPECT_EQ(Runtime().social.trade.warningKind, 0);
    EXPECT_TRUE(sent.empty());

    Runtime().dialogs.current = 0;
    Runtime().slot78a6758 = 0x4c + 5;
    Runtime().social.typedMoney = 7000;
    Runtime().social.trade.warningKind = 6;
    Runtime().social.trade.warningByte = 1;
    Shops().dialogMode = 0;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x09, 0x3f, 0x01, 0x51, 0x58, 0x1b, 0x00, 0x00}));
    EXPECT_EQ(Shops().ownPrices.at(0x4c + 5), 7000);
    EXPECT_EQ(Runtime().social.typedMoney, 0);
}

// Desde la grilla de la tienda (0x5d5098 y 0x5cc2cf): al mismo lugar, 0x5be150; a otro de la tienda, con la tienda
// abierta, el 3F 03 y el 24 de la 4 a la 4; al inventario, el 3F 03 y el 24 de la 4 a la 0 con el lugar + 12.
TEST_P(PersonalShopWindowTest, MovingOutOfTheShopGridClosesTheShop)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Place(ShopCells(), PotionCell, PotionType, 0, 1);
    Shops().ownOpen = 1;
    const std::int32_t potionX = CenterOf(ShopGridLeft, PotionCell);
    const std::int32_t potionY = CenterOf(ShopGridTop, 0);
    auto stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorGrid, Client::Scenes::CursorGrid::PersonalShop);

    // Al mismo lugar.
    stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms);
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(ShopCells()[PotionCell].id, PotionType);
    EXPECT_EQ(Runtime().lastSoundId, 0x1d);
    EXPECT_TRUE(sent.empty());

    // A la casilla (6, 1) de la tienda.
    stepped = Step(potionX, potionY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    stepped = Step(CenterOf(ShopGridLeft, 6), CenterOf(ShopGridTop, 1), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms + 2 * C3Randoms);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x04, U8(0x4c + PotionCell), 0x00, 0x00, 30,
                                                        0x00, 0x00, 0xe0, 0x00, 0x04, U8(0x4c + 14)}));
    EXPECT_EQ(Runtime().itemMoveTarget, 0x4c + 14);

    // Al inventario, a la casilla (0, 0).
    sent.clear();
    Runtime().ui.field78abb39 = 0;
    stepped = Step(CenterOf(InventoryGridLeft, 0), CenterOf(InventoryGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + CursorQueryRandoms + 2 * C3Randoms);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].bytes, CloseShopBytes);
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x04, U8(0x4c + PotionCell), 0x00, 0x00, 30,
                                                        0x00, 0x00, 0xe0, 0x00, 0x00, 12}));
    EXPECT_EQ(Runtime().itemMoveTarget, 12);
}

// Soltar en la grilla de la tienda lo que no se comercia (0x5cf3c9) o mirando la de otro (0x5d506b) no hace nada más
// que gastar el clic dentro de la grilla.
TEST_P(PersonalShopWindowTest, SomeDropsOnTheShopDoNothing)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Place(InventoryCells(), 0, UntradableType, 0, 1);
    auto stepped = Step(CenterOf(InventoryGridLeft, 0), CenterOf(InventoryGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorItem.id, UntradableType);
    stepped = Step(CenterOf(ShopGridLeft, 0), CenterOf(ShopGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_FALSE(Runtime().movement.pendingClick);
    EXPECT_TRUE(sent.empty());

    Shops().mode = Client::Scenes::OtherShopView;
    Runtime().cursorItem.id = PotionType;
    stepped = Step(CenterOf(ShopGridLeft, 0), CenterOf(ShopGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().dialogs.current, 0);
    EXPECT_EQ(Runtime().itemMoveTarget, 0x4c);
    EXPECT_TRUE(sent.empty());
}

// Los avisos de 0x78abb14 van antes del ítem del cursor: el 3 (0x5d966b) con el sí da vuelta el OK propio y manda el
// C1 04 3C con el nuevo en C3; sin respuesta el paso termina sin soltar el ítem; los de las ventanas de NPC cortan.
TEST_P(PersonalShopWindowTest, TheWarningsAnswerBeforeTheCursorItem)
{
    OpenTheShop(Client::UI::PersonalShopPanel);
    Client::Scenes::TradeState& trade = Runtime().social.trade;
    trade.warningKind = 3;
    trade.warningByte = 1;
    auto stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms + C3Randoms);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x01}));
    EXPECT_EQ(trade.ownOk, 1);
    EXPECT_EQ(trade.changed, 1);
    EXPECT_EQ(trade.warningKind, 0);

    // Sin respuesta, el bastón del cursor no se suelta en el inventario.
    sent.clear();
    Place(InventoryCells(), StaffCell, StaffType, 2, 3);
    stepped = Step(CenterOf(InventoryGridLeft, 1), CenterOf(InventoryGridTop, 2), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    ASSERT_EQ(Runtime().cursorItemCount, 1);
    trade.warningKind = 3;
    trade.warningByte = 0xff;
    stepped = Step(CenterOf(InventoryGridLeft, 5), CenterOf(InventoryGridTop, 0), true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawnByTheStep(), QuietStepRandoms);
    EXPECT_EQ(Runtime().cursorItemCount, 1);
    EXPECT_TRUE(Runtime().movement.pendingClick);
    EXPECT_TRUE(sent.empty());

    // El 1 con el no vuelve el ítem; con el sí, el 2 con el sí y el 4 cortan.
    trade.warningKind = 1;
    trade.warningByte = 2;
    stepped = Step(NowhereX, NowhereY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Runtime().cursorItemCount, 0);
    EXPECT_EQ(InventoryCells()[StaffCell].id, StaffType);
    for (const auto& [kind, address] :
         {std::pair<std::int32_t, std::string_view>{1, "0x5d8e0d"}, {2, "0x5d934f"}, {4, "0x5d9b82"}})
    {
        trade.warningKind = kind;
        trade.warningByte = 1;
        stepped = Step(NowhereX, NowhereY, false);
        ASSERT_FALSE(stepped.HasValue()) << kind;
        EXPECT_NE(stepped.Error().find(address), std::string::npos) << stepped.Error();
    }
}

INSTANTIATE_TEST_SUITE_P(LorenciaAndDevias, PersonalShopWindowTest, ::testing::Values(0, 2));

// 0x4716e0: los renglones se cortan al pasar de 24 bytes (un byte de 0x80 o más cuenta dos, sin mirar el que sigue) o
// en el final justo; el pedazo que termina con un byte de 0x80 o más pasado del final se pierde (07 Q-C966). Con el
// 0x9c abierto, falso; con un tipo fuera de 1..2, falso con el diálogo pedido.
TEST(ConfirmBoxTest, TheTextIsCutIntoRows)
{
    Client::Scenes::ConfirmBoxState box;
    Client::UI::DialogState dialogs;
    ASSERT_TRUE(Client::Scenes::OpenTheConfirmBox(box, dialogs, 7, std::string(30, 'a'), 1));
    EXPECT_EQ(dialogs.current, 0x9c);
    EXPECT_EQ(box.id, 7);
    EXPECT_EQ(box.type, 1);
    EXPECT_EQ(box.lines, (std::vector<std::string>{std::string(25, 'a'), std::string(5, 'a')}));
    EXPECT_FALSE(Client::Scenes::OpenTheConfirmBox(box, dialogs, 8, "otro", 2));
    EXPECT_EQ(box.id, 7);

    // Un byte alto en el 25: el pedazo llega a 26, con el byte que lo sigue.
    dialogs = {};
    const std::string wide = std::string(24, 'b') + "\xb0\xa1" + "cc";
    ASSERT_TRUE(Client::Scenes::OpenTheConfirmBox(box, dialogs, 1, wide, 2));
    EXPECT_EQ(box.lines, (std::vector<std::string>{std::string(24, 'b') + "\xb0\xa1", "cc"}));

    // Uno alto en el último byte: el pedazo se pierde.
    dialogs = {};
    ASSERT_TRUE(Client::Scenes::OpenTheConfirmBox(box, dialogs, 1, std::string("dd") + "\xb0", 2));
    EXPECT_TRUE(box.lines.empty());

    // El tipo 3: el diálogo pedido y nada más.
    dialogs = {};
    EXPECT_FALSE(Client::Scenes::OpenTheConfirmBox(box, dialogs, 9, "x", 3));
    EXPECT_EQ(dialogs.current, 0x9c);
    EXPECT_EQ(box.id, 1);
}

// 0x471930 con un botón (0x127..0x15b) y el no de dos (0x14a..0x17e), con los bordes: el 1000 con el 2 solo vuelve al
// diálogo que esperaba; con otro id, 0x623750 no hace nada y la caja queda.
TEST(ConfirmBoxTest, TheButtonsAnswerTheBox)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::WorldRuntime& runtime = world->Runtime();
    CrtRandom random{Seed};
    Client::Input::InputState input;
    std::vector<Sent> sent;
    RecordingRequests requests{sent};
    const auto step = [&](std::int32_t x, std::int32_t y)
    {
        Client::Scenes::LatchTheLeftButton(runtime.movement, true, true);
        return Client::Scenes::StepTheGameDialogs(Client::Scenes::GameUiContext{
            .world = *world, .random = random, .input = input, .mouseX = x, .mouseY = y, .requests = &requests});
    };
    const std::int32_t row = Client::Render::Text::FontStepFor(640) + 2;

    // Dos botones, el no en el borde de abajo: el diálogo que esperaba (0x7d3a110) vuelve.
    ASSERT_TRUE(Client::Scenes::OpenTheConfirmBox(runtime.social.confirmBox, runtime.dialogs,
                                                  Client::Scenes::OpenShopConfirm, "pregunta", 2));
    runtime.dialogs.pending = 0x8c;
    auto stepped = step(0x17e, row + 156);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.dialogs.current, 0x8c);
    EXPECT_EQ(runtime.dialogs.pending, 0);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_TRUE(sent.empty());

    // Un botón, otro id: el clic se gasta y la caja queda.
    runtime.dialogs = {};
    ASSERT_TRUE(Client::Scenes::OpenTheConfirmBox(runtime.social.confirmBox, runtime.dialogs, 5, "aviso", 1));
    stepped = step(0x14a, row + 140);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(runtime.dialogs.current, 0x9c);
    EXPECT_FALSE(runtime.movement.pendingClick);
    // Fuera del botón (y + 157), el clic queda.
    stepped = step(0x14a, row + 157);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(runtime.movement.pendingClick);
    EXPECT_TRUE(sent.empty());
}
} // namespace
