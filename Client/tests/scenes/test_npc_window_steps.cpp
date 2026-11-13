#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/GameDialogSteps.h"
#include "Scenes/GameUi.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/NpcWindowSteps.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Los pasos de las ventanas de los NPC (CL9 hito 6, fase 2; 04 § Las ventanas de los NPC): comprar, vender, reparar,
// guardar y sacar del almacén, el zen del almacén, la mezcla y los diálogos de sí y no, byte a byte y con los rand()
// del paso. La esquina de la ventana del NPC (0x78aba28) la pone su dibujo (Scenes/NpcWindowDrawing), que no corre en
// estos tests: la dejan en (0x104, 0), donde la pone el original. Los ítems y los textos son inventados.
namespace
{
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Render::Models::CrtRandom;
using Client::Scenes::EmptyOwnItemId;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::World::ItemState;

constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
// Los del paso con el inventario abierto y nada bajo el mouse (test_game_inventory_step.cpp): los del inventario y
// sus hermanas, antes de 0x5d8d50, y los de la barra de skills, la cadena de Q, W y E y la cola IsOpen(0), después.
constexpr int InventoryWindowRandoms = 8 + 13 * Own;
constexpr int QuietStepRandoms = InventoryWindowRandoms + Own + 2 + 2;
// Con el almacén abierto, la cadena de Q, W y E (0x5a343e) y IsOpen(0) (0x5a5511) paran en 0x40, antes del
// IsOpen(0x10000): uno menos cada una.
constexpr int WarehouseStepRandoms = QuietStepRandoms - 2;
// 0x5c3d20 con el mouse arriba de la grilla del inventario.
constexpr int EquipmentStepRandoms = 12 * Own;
// El rand() del relleno y el del número de serie de un C3.
constexpr int C3 = 2;
// 0x5d8d50 con un ítem en el cursor: IsOpen(0x20) de soltarlo en el comercio.
constexpr int CursorTradeQuery = 1;
// 0x5c5361 al levantar y 0x6264a6 en el OK del 0x73: IsOpen(0x20).
constexpr int TradeQuery = 1;
// 0x5cb3af y 0x5e7aa0: veinte cambios de dos teclas.
constexpr int KeypadRandoms = 40;
// Con la caja del caos abierta, IsOpen(0) (0x5a5511) para en 0x100, antes del IsOpen(0x10000): uno menos. La cadena
// de Q, W y E pasa por el 0x10000 antes del 0x100 y saca los mismos.
constexpr int ChaosBoxStepRandoms = QuietStepRandoms - 1;
constexpr std::int32_t InventoryX = 0x1c2;
constexpr std::int32_t NpcX = 0x104;
constexpr std::int32_t NpcGridLeft = NpcX + 0xf;
constexpr std::int32_t NpcGridTop = 0x32;
// El botón de mezclar (0x5fe690, de 0x2c × 0x20 en la esquina más (0x4b, 300)) y el precio del 3 que deja 0x5febf0.
constexpr std::int32_t MixButtonX = NpcX + 0x4b + 5;
constexpr std::int32_t MixButtonY = 300 + 5;
constexpr std::uint32_t UpgradePrice = 2'000'000;

// Un arma de 1 por 3 y un escudo de 2 por 2 con precio fijo (+0x40) y durabilidad (+0x2d), una joya de 1 por 1 y la
// fruta. Para la caja del caos, la joya de Soul, la del caos y el ojo y la llave del Devil Square, de 1 por 1 (los dos
// últimos con precio fijo).
constexpr std::uint16_t Sword = 0x0001;
constexpr std::uint16_t Shield = 0x0c00;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::uint16_t Soul = 0x1c0e;
constexpr std::uint16_t ChaosJewel = 0x180f;
constexpr std::uint16_t Eye = 0x1c11;
constexpr std::uint16_t Key = 0x1c12;
constexpr std::uint32_t EyePrice = 1000;
constexpr std::uint32_t SwordPrice = 15000;
constexpr std::uint32_t ShieldPrice = 9000;
constexpr std::uint8_t SwordDurability = 30;
constexpr std::uint8_t ShieldDurability = 20;

void PutDword(std::vector<std::byte>& plain, std::size_t at, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index)
        plain[at + index] = static_cast<std::byte>((value >> (8 * index)) & 0xffu);
}

Client::Data::Tables::ItemTable SyntheticItems()
{
    using Client::Data::Tables::ItemRecordSize;
    struct Shape
    {
        std::uint16_t type;
        std::uint8_t width;
        std::uint8_t height;
        std::uint32_t price;
        std::uint8_t durability;
    };
    constexpr std::array<Shape, 8> Shapes{{{Sword, 1, 3, SwordPrice, SwordDurability},
                                           {Shield, 2, 2, ShieldPrice, ShieldDurability},
                                           {Jewel, 1, 1, 0, 0},
                                           {Fruit, 1, 1, 0, 0},
                                           {Soul, 1, 1, 0, 0},
                                           {ChaosJewel, 1, 1, 0, 0},
                                           {Eye, 1, 1, EyePrice, 0},
                                           {Key, 1, 1, EyePrice, 0}}};
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (const Shape& shape : Shapes)
    {
        const std::size_t base = std::size_t{shape.type} * ItemRecordSize;
        plain[base + 0x24] = std::byte{shape.width};
        plain[base + 0x25] = std::byte{shape.height};
        plain[base + 0x2d] = std::byte{shape.durability};
        PutDword(plain, base + 0x40, shape.price);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

// `item` en la grilla `cells` de 8 columnas desde (column, row), con la copia en toda su huella y +0x3c solo en la
// esquina, como lo deja 0x5bc740.
void Place(std::span<ItemState> cells, ItemState item, std::int32_t column, std::int32_t row, std::int32_t width,
           std::int32_t height)
{
    item.gridColumn = static_cast<std::uint8_t>(column);
    item.gridRow = static_cast<std::uint8_t>(row);
    for (std::int32_t y = row; y < row + height; ++y)
    {
        for (std::int32_t x = column; x < column + width; ++x)
        {
            ItemState& cell = cells[static_cast<std::size_t>(y * 8 + x)];
            cell = item;
            cell.occupied = x == column && y == row ? 1u : 0u;
        }
    }
}

// Mide como el rasterizador falso: 6 columnas por letra y 13 filas.
class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

class NpcWindowStepsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        auto& runtime = world.Runtime();
        runtime.panels.inventory = 1;
        runtime.hud.inventoryX = InventoryX;
        runtime.hud.npcWindowX = NpcX;
        // El registro del chat donde lo deja el paso anterior (0x636f04, fuera de su ventana): en (0xba, 0x1a4), la
        // del primer paso, tapa la ventana del NPC y se queda con el mouse (0x5ee15a).
        runtime.hud.chatFrame.x = -10;
        runtime.hud.chatFrame.y = 0x65;
        for (ItemState& cell : runtime.npcWindows.grid)
            cell.id = EmptyOwnItemId;
        for (ItemState& cell : runtime.npcWindows.chaosBox)
            cell.id = EmptyOwnItemId;
        services.items = &items;
        services.dressTheHero = [] { return Client::Core::Expected<void, std::string>{}; };
        services.systemMessage = [this](std::size_t index, std::int32_t)
        {
            messages.push_back(index);
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendItemMove = [this](const Client::Protocol::Inventory::ItemMove& move)
        {
            sent.push_back(Client::Protocol::Inventory::WriteItemMove(move));
            return Client::Core::Expected<void, std::string>{};
        };
    }

    GameUiContext Context(std::int32_t x, std::int32_t y)
    {
        return GameUiContext{.world = world,
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
                             }};
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::StepTheGameUi(Context(x, y));
    }

    Client::Core::Expected<void, std::string> Dialogs(std::int32_t x, std::int32_t y)
    {
        return Client::Scenes::StepTheGameDialogs(Context(x, y));
    }

    void Click()
    {
        Client::Scenes::LatchTheLeftButton(world.Runtime().movement, true, true);
    }

    // El C1 del paquete `index` sin el XOR32, para compararlo byte a byte.
    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(sent.at(index).packet);
    }

    [[nodiscard]] Client::Scenes::NpcWindowState& Npc()
    {
        return world.Runtime().npcWindows;
    }

    // Lo que avisó 0x555190 con el tipo 2, en orden.
    [[nodiscard]] std::vector<std::string> Errors() const
    {
        std::vector<std::string> errors;
        for (const Client::UI::MessageAreaRow& row : world.Runtime().chat.area.rows)
        {
            if (row.type == 2 && !row.text.empty())
                errors.push_back(row.text);
        }
        return errors;
    }

    // La caja del 3: una espada +9 con suerte (0x54), una joya de Bless, una de Soul y una del caos.
    void FillTheUpgrade()
    {
        ItemState sword{.id = Sword, .scalar1Low = 9u << 3, .optionCount = 1};
        sword.optionBytes[0] = 0x54;
        Place(Npc().chaosBox, sword, 0, 0, 1, 3);
        Place(Npc().chaosBox, ItemState{.id = Jewel}, 2, 0, 1, 1);
        Place(Npc().chaosBox, ItemState{.id = Soul}, 3, 0, 1, 1);
        Place(Npc().chaosBox, ItemState{.id = ChaosJewel}, 4, 0, 1, 1);
    }

    WorldState world;
    CrtRandom random{0x2468aceu};
    InputState input;
    Client::Data::Tables::ItemTable items = SyntheticItems();
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable({{166, "luna"},
                                                                        {167, "sol"},
                                                                        {168, "cielo"},
                                                                        {169, "mar"},
                                                                        {0x178, "comer la fruta?"},
                                                                        {0x218, "vender esto"},
                                                                        {0x219, "que vale"},
                                                                        {0x21a, "mucho?"},
                                                                        {0x21b, "mezclar ya?"},
                                                                        {580, "no hay %s"},
                                                                        {581, "sin lugar"},
                                                                        {591, "mezcla"},
                                                                        {596, "sin zen"},
                                                                        {600, "no son iguales"},
                                                                        {1171, "desde el nivel %d"},
                                                                        {1900, "rio"},
                                                                        {1901, " y suerte"},
                                                                        {0x76e, "elegir"}});
    LetterMeasure measure;
    Client::Scenes::InventoryServices services;
    std::vector<Client::Protocol::Outgoing> sent;
    std::vector<std::size_t> messages;
};

// 0x5c4b64..0x5c510f, en Lorencia, Devias y Noria: el clic sobre la tercera casilla de un arma de la tienda la compra
// desde su esquina: el C1 04 32 con el lugar 82 (fila 10, columna 2), con el XOR32 y en C3, y el precio de 0x54d0f0
// en el modo 0 en 0x5767950. Con la compra esperando, el clic se gasta sin mandar nada. Las marcas, el ítem del
// cartel (0x78aba27 en 1) y 0x78abb28 en 6 con la casilla sin el 12.
TEST_F(NpcWindowStepsTest, TheShopBuysTheItemUnderTheMouseByteByByte)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        world.Runtime().map = map;
        world.Runtime().panels.npcShop = 1;
        Npc().buyLock = 0;
        Place(Npc().grid, ItemState{.id = Sword}, 2, 10, 1, 3);
        const std::int32_t x = NpcGridLeft + 2 * 20 + 5;
        const std::int32_t y = NpcGridTop + 11 * 20 + 5;
        const CrtRandom expected = Advanced(random, QuietStepRandoms + C3);
        Click();
        const auto stepped = Step(x, y);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x32, 82})));
        EXPECT_EQ(Npc().buyLock, static_cast<std::int32_t>(SwordPrice));
        EXPECT_FALSE(world.Runtime().movement.pendingClick);
        for (const std::size_t cell : {82u, 90u, 98u})
            EXPECT_EQ(Npc().grid[cell].warning, 2) << cell;
        const auto& ui = world.Runtime().ui;
        EXPECT_EQ(ui.hoveredItem.place, Client::UI::HoveredItem::Place::NpcGridCell);
        EXPECT_EQ(ui.hoveredItem.index, 90u);
        EXPECT_EQ(ui.field78aba27, 1);
        EXPECT_EQ(ui.field78abb28, 6);
        EXPECT_EQ(ui.field78abb2c, 90);
        EXPECT_EQ(ui.skillTooltipX, NpcGridLeft + 10 + 2 * 20);
        EXPECT_EQ(ui.skillTooltipY, NpcGridTop + 10 * 20);
        EXPECT_EQ(ui.mouseOnUi, 1);

        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(sent.size(), 1u);
        EXPECT_FALSE(world.Runtime().movement.pendingClick);
    }
}

// 0x5dee7f..0x5df416: el clic con un escudo en el cursor sobre la grilla de la tienda lo vende, C1 04 33 con su lugar
// (0x78a6758), con el XOR32 y en C3, después del IsOpen(0x10000) de 0x5decc1. En Lorencia, Devias y Noria.
TEST_F(NpcWindowStepsTest, SellingAPlainItemSendsItsPlace)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.npcShop = 1;
        runtime.cursorItem = ItemState{.id = Shield};
        runtime.cursorItemCount = 1;
        runtime.cursorGrid = Client::Scenes::CursorGrid::Inventory;
        runtime.slot78a6758 = 12 + 20;
        runtime.movement.mouseUpdateTimeMax = 0;
        const CrtRandom expected = Advanced(random, QuietStepRandoms + CursorTradeQuery + 1 + C3);
        Click();
        const auto stepped = Step(NpcX + 0x20, 300);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x33, 32})));
        EXPECT_EQ(Npc().cursorChoice, 0);
        EXPECT_EQ(Npc().dialogAnswer, 0xff);
        EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
    }
}

// 0x5df417: una joya pide confirmar con el 0x98 (los textos 0x218 a 0x21a) y 0x78abb14 en 1. El sí del 0x98
// (0x62a779, el botón de (0x15, 0x5a) corrido a (0xd5, 0x64)) deja 1 en 0x6c932e y, en el paso siguiente, 0x5d8d50
// manda el C1 04 33 y vuelve todo a 0 y 0xff. Con Esc es el no: la joya vuelve a su lugar.
TEST_F(NpcWindowStepsTest, AValuableItemIsSoldAfterTheConfirmation)
{
    auto& runtime = world.Runtime();
    runtime.panels.npcShop = 1;
    runtime.cursorItem = ItemState{.id = Jewel};
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Scenes::CursorGrid::Inventory;
    runtime.slot78a6758 = 12 + 3;
    const CrtRandom asked = Advanced(random, QuietStepRandoms + CursorTradeQuery + 1);
    Click();
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    EXPECT_EQ(random.State(), asked.State());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(runtime.dialogs.current, 0x98);
    EXPECT_EQ(Npc().cursorChoice, 1);
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "vender esto");
    EXPECT_EQ(runtime.dialogBox.lineCount, 3);

    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(Npc().dialogAnswer, 1);
    EXPECT_EQ(runtime.dialogBox.buttons[0].state, 2);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_FALSE(runtime.movement.leftHeld);
    EXPECT_EQ(runtime.lastSoundId, 0x19);

    const CrtRandom sold = Advanced(random, QuietStepRandoms + C3);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    EXPECT_EQ(random.State(), sold.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x33, 15})));
    EXPECT_EQ(Npc().cursorChoice, 0);
    EXPECT_EQ(Npc().dialogAnswer, 0xff);

    // El no, con Esc.
    Click();
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(runtime.dialogs.current, 0x98);
    input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = Key::Escape});
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(Npc().dialogAnswer, 2);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().cells[3].id, Jewel);
    EXPECT_EQ(Npc().cursorChoice, 0);
}

// 0x5ed960 con 0x78abb09: el botón de reparar todo manda C1 05 34 FF 00 en C3 sin gastar el clic; 0x5b1570 suma, con
// el registro propio abierto, lo de reparar el arma del equipo y el escudo del inventario gastados (0x5b13e0 sobre
// 0x54d0f0 en el modo 2). El botón de x + 0x19 prende el cursor de reparar.
TEST_F(NpcWindowStepsTest, RepairingEverythingSendsFfAndPricesTheWornItems)
{
    auto& runtime = world.Runtime();
    runtime.panels.npcShop = 1;
    runtime.panels.field78abb09 = 1;
    ItemState& sword = world.OwnCharacter().equipment[0];
    sword = ItemState{.id = Sword, .durability = 10};
    sword.occupied = 1;
    Place(world.Inventory().cells, ItemState{.id = Shield, .durability = 5}, 4, 1, 2, 2);
    const auto priceOf = [this](const ItemState& item, std::uint8_t durability)
    {
        const std::uint16_t maximum = Client::Scenes::MaxDurability(item, *items.Record(item.id), 0);
        EXPECT_EQ(maximum, durability);
        const auto price = Client::Scenes::ItemPrice(item, items, Client::Scenes::RepairPriceMode, std::nullopt);
        return Client::Scenes::RepairPriceOf(price.Value(), item.durability, maximum, 0).gold;
    };
    const std::int32_t total = priceOf(sword, SwordDurability) + priceOf(world.Inventory().cells[12], ShieldDurability);
    ASSERT_GT(total, 0);

    const CrtRandom expected = Advanced(random, QuietStepRandoms + C3 + Own);
    Click();
    const auto stepped = Step(NpcX + 0x73 + 5, 0x16d + 5);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0xff, 0x00})));
    EXPECT_TRUE(runtime.movement.pendingClick);
    EXPECT_EQ(Npc().repairAllPrice, total);

    runtime.movement.pendingClick = true;
    ASSERT_TRUE(Step(NpcX + 0x19, 0x16d).HasValue());
    EXPECT_EQ(runtime.panels.repairCursor, 2);
    EXPECT_EQ(runtime.panels.repairMode, 1);
    EXPECT_FALSE(runtime.movement.pendingClick);
}

// 0x5c2dd5..0x5c3594: con el modo de reparar, el clic sobre el arma del lugar 0 del equipo manda C1 05 34 00 y
// 0x78abb10, en C3 y con el registro de 0x5bec50 abierto.
TEST_F(NpcWindowStepsTest, RepairModeRepairsAnEquipmentPlace)
{
    auto& runtime = world.Runtime();
    world.OwnCharacter().equipment[0] = ItemState{.id = Sword};
    runtime.panels.repairMode = 1;
    runtime.field78abb10 = 1;
    const CrtRandom expected = Advanced(random, QuietStepRandoms + EquipmentStepRandoms + C3);
    Click();
    const auto stepped = Step(InventoryX + 15 + 5, 89 + 5);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0x00, 0x01})));
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(world.OwnCharacter().equipment[0].id, Sword);
}

// Guardar (0x5ca250 con el tipo 2): el arma levantada del inventario (el lugar 12) sobre la fila 8 y la columna 3 del
// almacén manda 0x449d80(0, 12, 2, 67), el C1 0E 24 en C3 con los siete bytes del ítem, y prende la traba del 24. En
// Lorencia, Devias y Noria, con los rand() del almacén abierto.
TEST_F(NpcWindowStepsTest, StoringAnItemInTheWarehouseSendsTheMove)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.cursorItemCount = 0;
        runtime.ui.field78abb39 = 0;
        runtime.itemMoveTarget = 0;
        runtime.panels.warehouse = 1;
        Place(world.Inventory().cells, ItemState{.id = Sword, .scalar1Low = 3u << 3, .durability = 25, .flags1b = 0x10},
              0, 0, 1, 3);
        Click();
        ASSERT_TRUE(Step(InventoryX + 15 + 5, 200 + 5).HasValue());
        ASSERT_EQ(runtime.cursorItemCount, 1);
        ASSERT_EQ(runtime.slot78a6758, 12);

        const CrtRandom expected = Advanced(random, WarehouseStepRandoms + CursorTradeQuery + C3);
        Click();
        const auto stepped = Step(NpcGridLeft + 3 * 20 + 10, NpcGridTop + 9 * 20 + 5);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(),
                              Bytes({0xc1, 0x0e, 0x24, 0x00, 12, 0x01, 0x18, 25, 0x10, 0x00, 0x00, 0x00, 0x02, 67})));
        EXPECT_EQ(runtime.itemMoveTarget, 67);
        EXPECT_EQ(runtime.ui.field78abb39, 1);
        EXPECT_FALSE(runtime.movement.pendingClick);
    }
}

// Sacar: el clic sobre el arma del almacén la levanta desde su esquina (el lugar 73, sin el 12) con el IsOpen(0x20)
// de 0x5c5361, y soltarla en la columna 4 y la fila 2 del inventario manda 0x449d80(2, 73, 0, 32). Con la traba del
// almacén (0x6c932f) y sin 0x78abb1c, en cambio, lo deja esperando la contraseña ({2, 73, 0, 32}), baraja el
// teclado y prende 0x78abb20 en 1 (0x5cb3af..0x5cb455), sin mandar nada.
TEST_F(NpcWindowStepsTest, TakingAnItemFromTheWarehouseSendsTheMoveOrWaitsForThePassword)
{
    for (const bool locked : {false, true})
    {
        SCOPED_TRACE(locked);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.ui.field78abb39 = 0;
        runtime.ui.field78abb20 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.warehouse = 1;
        Npc().warehouseLocked = locked ? 1 : 0;
        Npc().field78abb1c = 0;
        Place(Npc().grid, ItemState{.id = Sword, .durability = 7}, 1, 9, 1, 3);
        const CrtRandom picked = Advanced(random, WarehouseStepRandoms + TradeQuery + CursorTradeQuery);
        Click();
        ASSERT_TRUE(Step(NpcGridLeft + 20 + 5, NpcGridTop + 10 * 20 + 5).HasValue());
        EXPECT_EQ(random.State(), picked.State());
        ASSERT_EQ(runtime.cursorItemCount, 1);
        EXPECT_EQ(runtime.cursorGrid, Client::Scenes::CursorGrid::NpcGrid);
        EXPECT_EQ(runtime.slot78a6758, 73);
        for (const std::size_t cell : {73u, 81u, 89u})
            EXPECT_EQ(Npc().grid[cell].id, EmptyOwnItemId) << cell;
        EXPECT_EQ(runtime.lastSoundId, 0x1d);

        const CrtRandom beforeTheDrop = random;
        CrtRandom keys = Advanced(random, InventoryWindowRandoms);
        std::array<std::uint16_t, Client::Scenes::WarehouseKeyCount> keypad{};
        Client::Scenes::ShuffleTheKeypad(keys, keypad);
        Click();
        const auto dropped = Step(InventoryX + 15 + 4 * 20 + 5, 200 + 3 * 20 + 5);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        if (!locked)
        {
            EXPECT_EQ(random.State(), Advanced(beforeTheDrop, WarehouseStepRandoms + CursorTradeQuery + C3).State());
            ASSERT_EQ(sent.size(), 1u);
            EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x0e, 0x24, 0x02, 73, 0x01, 0x00, 7, 0x00, 0x00, 0x00,
                                                           0x00, 0x00, 32})));
            EXPECT_EQ(runtime.ui.field78abb20, 0);
            continue;
        }
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(random.State(),
                  Advanced(beforeTheDrop, WarehouseStepRandoms + KeypadRandoms + CursorTradeQuery).State());
        EXPECT_EQ(Npc().keypad, keypad);
        EXPECT_EQ(runtime.ui.field78abb20, 1);
        EXPECT_EQ(Npc().pendingMove.slot, 2);
        EXPECT_EQ(Npc().pendingMove.second, 73);
        EXPECT_EQ(Npc().pendingMove.third, 0);
        EXPECT_EQ(Npc().pendingMove.fourth, 32);
        // 0x5e7bf0: la caja de la contraseña se queda con el clic (0x5ee140 no sigue): la tecla de abajo a la
        // izquierda (0x5e7b50) agrega su cifra y el almacén no ve el clic.
        Click();
        const auto keypadStep = Step(0xdf + 5, 0x96 + 5);
        ASSERT_TRUE(keypadStep.HasValue()) << keypadStep.Error();
        EXPECT_EQ(Npc().typedNumber[0], '0' + keypad[0]);
        EXPECT_EQ(Npc().typedNumber[1], 0);
        EXPECT_EQ(Npc().keypadPressed, 1);
        EXPECT_EQ(runtime.ui.field78abb20, 1);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_TRUE(sent.empty());
    }
}

// 0x5ea060: meter (el botón de x + 0x1a) abre el 0x73 con la entrada numérica; su OK con Enter (0x6270ed) manda
// C1 08 81 00 y el zen, con el XOR32 y en claro, con el registro propio abierto. Más zen que el del inventario abre
// el 0x74; más de 100.000.000, el 0x75.
TEST_F(NpcWindowStepsTest, DepositingZenSends81FromTheMoneyDialog)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    world.OwnCharacter().zen = 5000;
    const CrtRandom opened = Advanced(random, WarehouseStepRandoms);
    Click();
    ASSERT_TRUE(Step(NpcX + 0x1a + 5, 0x186 + 5).HasValue());
    EXPECT_EQ(random.State(), opened.State());
    EXPECT_EQ(runtime.dialogs.current, 0x73);
    EXPECT_EQ(Npc().warehouseMoneyMode, 0);
    EXPECT_EQ(Npc().inputMode, 8);
    EXPECT_EQ(Npc().numberField, 1);
    EXPECT_EQ(runtime.chat.input.fieldCount, 1);
    EXPECT_EQ(runtime.lastSoundId, 0x19);
    EXPECT_FALSE(runtime.movement.pendingClick);

    Npc().typedMoney = 1000;
    runtime.ui.enterPressed = true;
    const CrtRandom confirmed = Advanced(random, TradeQuery + Own);
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(random.State(), confirmed.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x08, 0x81, 0x00, 0xe8, 0x03, 0x00, 0x00})));
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(Npc().inputMode, 0x2a);
    EXPECT_EQ(Npc().numberField, 0);
    EXPECT_EQ(runtime.chat.input.fieldCount, 2);

    runtime.dialogs.current = 0x73;
    Npc().typedMoney = 6000;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(runtime.dialogs.current, 0x74);

    runtime.dialogs.current = 0x73;
    Npc().typedMoney = 100'000'001;
    runtime.ui.enterPressed = true;
    const CrtRandom refused = Advanced(random, 0);
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(random.State(), refused.State());
    EXPECT_EQ(runtime.dialogs.current, 0x75);
}

// Sacar (el botón de x + 0x4c): C1 08 81 01 y el zen. Con la traba del almacén espera la contraseña: {−1, 1, zen,
// 0x77f3cc8} y 0x5e7aa0(1), que baraja el teclado con el registro todavía abierto (los 40 rand() antes de los 3.848
// del cierre). Más que el zen del almacén abre el 0x74.
TEST_F(NpcWindowStepsTest, WithdrawingZenSends81OrWaitsForThePassword)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    world.OwnCharacter().zen = 100;
    world.OwnCharacter().warehouseZen = 3000;
    Click();
    ASSERT_TRUE(Step(NpcX + 0x4c + 5, 0x186 + 5).HasValue());
    ASSERT_EQ(runtime.dialogs.current, 0x73);
    EXPECT_EQ(Npc().warehouseMoneyMode, 1);
    Npc().typedMoney = 2000;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x08, 0x81, 0x01, 0xd0, 0x07, 0x00, 0x00})));

    runtime.dialogs.current = 0x73;
    Npc().typedMoney = 3001;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x74);

    runtime.dialogs.current = 0x73;
    Npc().typedMoney = 2000;
    Npc().warehouseLocked = 1;
    runtime.itemMoveTarget = 9;
    runtime.ui.enterPressed = true;
    CrtRandom keys = Advanced(random, TradeQuery);
    std::array<std::uint16_t, Client::Scenes::WarehouseKeyCount> keypad{};
    Client::Scenes::ShuffleTheKeypad(keys, keypad);
    const CrtRandom expected = Advanced(random, TradeQuery + KeypadRandoms + Own);
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(Npc().keypad, keypad);
    EXPECT_EQ(runtime.ui.field78abb20, 1);
    EXPECT_EQ(Npc().pendingMove.slot, -1);
    EXPECT_EQ(Npc().pendingMove.second, 1);
    EXPECT_EQ(Npc().pendingMove.third, 2000);
    EXPECT_EQ(Npc().pendingMove.fourth, 9);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

// El botón 2 del almacén (0x5ea1aa) abre el teclado en 4, o en 3 con la traba; el 3 (0x5ea24d) cierra el almacén con
// el C1 03 82 en claro (0x658567). El OK del 0x8f (0x628276) abre el teclado en 6.
TEST_F(NpcWindowStepsTest, TheWarehouseButtonsOpenTheKeypadAndClose)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    Npc().warehouseLocked = 1;
    const CrtRandom expected = Advanced(random, WarehouseStepRandoms + KeypadRandoms);
    Click();
    ASSERT_TRUE(Step(NpcX + 0x7e + 5, 0x186 + 5).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.ui.field78abb20, 3);

    runtime.ui.field78abb20 = 0;
    runtime.dialogs.current = 0x8f;
    runtime.ui.enterPressed = true;
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.ui.field78abb20, 6);

    runtime.ui.field78abb20 = 0;
    Click();
    const auto closed = Step(NpcX + 0xd7 + 5, 0x18b + 5);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_EQ(runtime.panels.warehouse, 0);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x03, 0x82})));
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.movement.mouseUpdateTimeMax, 6);
}

// 0x5d934f..0x5d9633: con 0x78abb14 en 2 y el sí, la mezcla pedida (0x7d2dc80 en 1) y el C1 04 86 con el tipo, con
// el XOR32 y en claro, sin rand(); con el no, nada.
TEST_F(NpcWindowStepsTest, TheMixConfirmationSends86)
{
    auto& runtime = world.Runtime();
    Npc().cursorChoice = 2;
    Npc().dialogAnswer = 1;
    Npc().mixKind = 3;
    const CrtRandom expected = random;
    const auto chose = Client::Scenes::StepTheCursorChoice(Context(0, 0));
    ASSERT_TRUE(chose.HasValue()) << chose.Error();
    EXPECT_TRUE(chose.Value());
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x86, 0x03})));
    EXPECT_EQ(Npc().chaosMixState, 1);
    EXPECT_EQ(Npc().cursorChoice, 0);
    EXPECT_EQ(Npc().dialogAnswer, 0xff);

    Npc().cursorChoice = 2;
    Npc().dialogAnswer = 2;
    ASSERT_TRUE(Client::Scenes::StepTheCursorChoice(Context(0, 0)).HasValue());
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(Npc().cursorChoice, 0);
    EXPECT_EQ(runtime.dialogs.current, 0);
}

// 0x5d9b82..0x5d9c6b, en Lorencia, Devias y Noria: con 0x78abb14 en 4 (el panel 0x2000) y el sí, el teclado del
// almacén vuelve a 0..9 y se baraja con veinte pares de rand() % 10 (los 40 rand()), lo tecleado queda en 0 y
// 0x78abb20 en 6; con el no, 0x78abb08 en 0 y sin rand(); sin respuesta, nada. 0x5d9c75: el 5 (la tienda personal)
// corta hacia el hito 7.
TEST_F(NpcWindowStepsTest, TheKeypadChoiceShufflesTheWarehouseKeypadInSix)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        auto& runtime = world.Runtime();
        runtime.map = map;
        Npc().cursorChoice = 4;
        Npc().dialogAnswer = 0xff;
        const CrtRandom unchanged = random;
        ASSERT_TRUE(Client::Scenes::StepTheCursorChoice(Context(0, 0)).Value());
        EXPECT_EQ(Npc().cursorChoice, 4);
        EXPECT_EQ(random.State(), unchanged.State());

        Npc().dialogAnswer = 1;
        Npc().typedNumber.fill(7);
        Npc().keypad.fill(9);
        CrtRandom expectedRandom = random;
        std::array<std::uint16_t, 10> expectedKeys{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        for (int swap = 0; swap < 20; ++swap)
        {
            const auto first = static_cast<std::size_t>(expectedRandom.Next() % 10);
            const auto second = static_cast<std::size_t>(expectedRandom.Next() % 10);
            std::swap(expectedKeys[first], expectedKeys[second]);
        }
        const auto chose = Client::Scenes::StepTheCursorChoice(Context(0, 0));
        ASSERT_TRUE(chose.HasValue()) << chose.Error();
        EXPECT_TRUE(chose.Value());
        EXPECT_EQ(random.State(), expectedRandom.State());
        EXPECT_EQ(Npc().keypad, expectedKeys);
        EXPECT_EQ(Npc().typedNumber, (std::array<std::uint8_t, 11>{}));
        EXPECT_EQ(runtime.ui.field78abb20, 6);
        EXPECT_EQ(Npc().cursorChoice, 0);
        EXPECT_EQ(Npc().dialogAnswer, 0xff);
        EXPECT_TRUE(sent.empty());

        Npc().cursorChoice = 4;
        Npc().dialogAnswer = 2;
        runtime.panels.field78abb08 = 1;
        runtime.ui.field78abb20 = 0;
        const CrtRandom refused = random;
        ASSERT_TRUE(Client::Scenes::StepTheCursorChoice(Context(0, 0)).Value());
        EXPECT_EQ(runtime.panels.field78abb08, 0);
        EXPECT_EQ(runtime.ui.field78abb20, 0);
        EXPECT_EQ(Npc().cursorChoice, 0);
        EXPECT_EQ(Npc().dialogAnswer, 0xff);
        EXPECT_EQ(random.State(), refused.State());

        Npc().cursorChoice = 5;
        Npc().dialogAnswer = 1;
        const auto shop = Client::Scenes::StepTheCursorChoice(Context(0, 0));
        ASSERT_FALSE(shop.HasValue());
        EXPECT_NE(shop.Error().find("0x5d9c75"), std::string::npos) << shop.Error();
        EXPECT_NE(shop.Error().find("P-C339"), std::string::npos) << shop.Error();
    }
}

// 0x5d8db6..0x5d8dda y 0x5fe690..0x5fea82, en Lorencia, Devias y Noria (la del duende del caos): con la caja abierta,
// cada paso cuenta lo que vale (0x5fd120: 15.000 de la espada y los fijos de las joyas) y el tipo (0x5fd4f0): la
// espada +9 con suerte, Bless, Soul y la joya del caos son el 3, con la suerte. El clic en el botón de mezclar, con más
// zen que el precio del dibujo, abre y cierra el registro propio, pone 0x78abb14 en 2 y abre el 0x98 con el texto
// 0x21b, y suena. El sí y el paso siguiente mandan el C1 04 86 03 en claro, y la mezcla pedida apaga el botón.
TEST_F(NpcWindowStepsTest, TheMixButtonAsksAndTheYesSends86WithTheKind)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        sent.clear();
        auto& runtime = world.Runtime();
        runtime.map = map;
        runtime.panels.chaosBox = 1;
        runtime.dialogs.current = 0;
        runtime.lastSoundId = 0;
        Npc().chaosMixMode = 0;
        Npc().chaosMixState = 0;
        Npc().mixLuck = 0;
        FillTheUpgrade();
        world.OwnCharacter().zen = UpgradePrice + 1;

        const CrtRandom scanned = Advanced(random, ChaosBoxStepRandoms);
        const auto quiet = Step(NpcX + 0x20, 300);
        ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
        EXPECT_EQ(random.State(), scanned.State());
        EXPECT_EQ(Npc().chaosBoxValue, 15'000 + 100'000 + 70'000 + 40'000);
        EXPECT_EQ(Npc().mixKind, 3);
        EXPECT_EQ(Npc().mixLuck, 1);
        EXPECT_EQ(Npc().mixWidth, 1);
        EXPECT_EQ(Npc().mixHeight, 3);

        // El precio que deja el dibujo del 3 (0x5feec8).
        Npc().mixPrice = UpgradePrice;
        const CrtRandom asked = Advanced(random, ChaosBoxStepRandoms + Own);
        Click();
        const auto pressed = Step(MixButtonX, MixButtonY);
        ASSERT_TRUE(pressed.HasValue()) << pressed.Error();
        EXPECT_EQ(random.State(), asked.State());
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(runtime.dialogs.current, 0x98);
        EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "mezclar ya?");
        EXPECT_EQ(Npc().cursorChoice, 2);
        EXPECT_EQ(Npc().dialogAnswer, 0xff);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_EQ(runtime.lastSoundId, 0x19);
        EXPECT_TRUE(Errors().empty());

        Click();
        ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
        EXPECT_EQ(Npc().dialogAnswer, 1);
        const CrtRandom mixed = Advanced(random, ChaosBoxStepRandoms);
        ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
        EXPECT_EQ(random.State(), mixed.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x86, 0x03})));
        EXPECT_EQ(Npc().chaosMixState, 1);

        // Con la mezcla pedida, el botón no hace nada y el clic queda.
        Click();
        ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
        EXPECT_EQ(sent.size(), 1u);
        EXPECT_EQ(runtime.dialogs.current, 0);
        EXPECT_TRUE(runtime.movement.pendingClick);
    }
}

// 0x5fe982..0x5feb0e: los avisos del botón de mezclar (0x555190 con el tipo 2). Con el zen igual al precio, el 596 y
// sin sonido; con el ojo y la llave de niveles distintos (el −2), el 600; sin nada en la caja (el 0), el 580 con el
// 591; con el −0x18, el 1171 con 10 (el wsprintfA de 0x5feab6); con el inventario lleno, el 581. Cada uno gasta el
// clic y saca los 3.848 rand() del registro propio.
TEST_F(NpcWindowStepsTest, TheMixButtonWarnsWithoutZenRoomOrAMix)
{
    auto& runtime = world.Runtime();
    runtime.panels.chaosBox = 1;
    world.OwnCharacter().zen = UpgradePrice;
    FillTheUpgrade();
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, 3);
    Npc().mixPrice = UpgradePrice;
    runtime.lastSoundId = 0;
    const CrtRandom poor = Advanced(random, ChaosBoxStepRandoms + Own);
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(random.State(), poor.State());
    EXPECT_EQ(Errors(), std::vector<std::string>{"sin zen"});
    EXPECT_EQ(runtime.lastSoundId, 0);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(runtime.dialogs.current, 0);

    // El ojo +1 y la llave +2 con la joya del caos.
    for (ItemState& cell : Npc().chaosBox)
        cell = ItemState{.id = EmptyOwnItemId};
    Place(Npc().chaosBox, ItemState{.id = Eye, .scalar1Low = 1u << 3}, 0, 0, 1, 1);
    Place(Npc().chaosBox, ItemState{.id = Key, .scalar1Low = 2u << 3}, 1, 0, 1, 1);
    Place(Npc().chaosBox, ItemState{.id = ChaosJewel}, 2, 0, 1, 1);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, -2);
    Npc().mixPrice = 0;
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "no son iguales");
    EXPECT_EQ(runtime.lastSoundId, 0x19);

    for (ItemState& cell : Npc().chaosBox)
        cell = ItemState{.id = EmptyOwnItemId};
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, 0);
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "no hay mezcla");

    // El −0x18 lo deja 0x5fd4f0 con las alas y la pluma +1 sin el nivel 10. El botón (0x5ee1b1) corre antes que
    // 0x5d8d50 (0x5ee1de) y lee el tipo del paso anterior.
    Npc().mixKind = -0x18;
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "desde el nivel 10");
    EXPECT_EQ(Npc().mixKind, 0);

    // El 0xf (solo Bless) pide 1 × 1, y el inventario está lleno.
    for (ItemState& cell : world.Inventory().cells)
        cell = ItemState{.id = Jewel};
    Place(Npc().chaosBox, ItemState{.id = Jewel}, 0, 0, 1, 1);
    ASSERT_TRUE(Step(NpcX + 0x20, 300).HasValue());
    ASSERT_EQ(Npc().mixKind, 0xf);
    Click();
    ASSERT_TRUE(Step(MixButtonX, MixButtonY).HasValue());
    EXPECT_EQ(Errors().back(), "sin lugar");
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_TRUE(sent.empty());
}

// 0x633644..0x633739: el 0x96 nombra la fruta de nivel 2 (el texto 167) en "( %s )" y el sí (0x628b2d) abre el 0xa8
// con "( %s%s )" y el 1901, el texto 0x76e debajo y sus tres botones. Otro ítem va sin nombre.
TEST_F(NpcWindowStepsTest, TheFruitDialogsNameTheItem)
{
    auto& runtime = world.Runtime();
    runtime.dialogItem = ItemState{.id = Fruit, .scalar1Low = 2u << 3};
    ASSERT_TRUE(
        Client::UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 1, 0x178, 0x96, &runtime.dialogItem)
            .HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x96);
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "( sol )");
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 1), "comer la fruta?");
    EXPECT_EQ(runtime.dialogBox.lineCount, 2);

    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0xa8);
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "( sol y suerte )");
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 1), "elegir");
    EXPECT_EQ(runtime.dialogBox.buttons[0].x, 0x16);
    EXPECT_EQ(runtime.dialogBox.buttons[1].x, 0x52);
    EXPECT_EQ(runtime.dialogBox.thirdButton.state, 5);
    EXPECT_EQ(runtime.dialogBox.thirdButton.x, 0x8e);

    runtime.dialogs.current = 0;
    runtime.dialogItem.id = Sword;
    ASSERT_TRUE(
        Client::UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 1, 0x178, 0x96, &runtime.dialogItem)
            .HasValue());
    EXPECT_EQ(Client::UI::DialogRow(runtime.dialogBox, 0), "(  )");
    const auto unnamed = Client::UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, &texts, 1, 0x178, 0x96);
    ASSERT_FALSE(unnamed.HasValue());
    EXPECT_NE(unnamed.Error().find("0x63330f"), std::string::npos);
}

// 0x62a779: el sí del 0x94 deja 1 en 0x6c932e; el mouse fuera de los botones no elige nada y el clic se gasta igual.
// 0x629c41: el no del 0xa9 suelta la joya y la vuelve a su lugar; el sí con el almacén abierto avisa con el 474.
TEST_F(NpcWindowStepsTest, TheYesNoDialogsLeaveTheirAnswer)
{
    auto& runtime = world.Runtime();
    runtime.dialogs.current = 0x94;
    runtime.dialogBox.buttons[0] = {.state = 1, .x = 0x15, .y = 0x5a, .width = 0x46, .height = 0x15};
    runtime.dialogBox.buttons[1] = {.state = 3, .x = 0x78, .y = 0x5a, .width = 0x46, .height = 0x15};
    Click();
    ASSERT_TRUE(Dialogs(0, 0).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0x94);
    EXPECT_FALSE(runtime.movement.pendingClick);
    EXPECT_EQ(Npc().dialogAnswer, 0xff);
    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5 + 0x46, 0x5a + 0x64 + 0x15).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(Npc().dialogAnswer, 1);

    runtime.dialogs.current = 0xa9;
    runtime.dialogBox.jewelCell = 4;
    runtime.dialogBox.jewelTarget = 9;
    runtime.cursorItem = ItemState{.id = Jewel};
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Scenes::CursorGrid::Inventory;
    runtime.slot78a6758 = 12 + 4;
    Click();
    ASSERT_TRUE(Dialogs(0x78 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(runtime.dialogs.current, 0);
    EXPECT_EQ(runtime.dialogBox.jewelCell, 0);
    EXPECT_EQ(runtime.dialogBox.jewelTarget, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(world.Inventory().cells[4].id, Jewel);

    runtime.dialogs.current = 0xa9;
    runtime.panels.warehouse = 1;
    Click();
    ASSERT_TRUE(Dialogs(0x15 + 0xd5, 0x5a + 0x64).HasValue());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(runtime.dialogs.current, 0);
}
} // namespace
