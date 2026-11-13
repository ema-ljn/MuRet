#include "Scenes/PetTrainer.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/GamePanels.h"
#include "Scenes/GameUi.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/NpcWindows.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// El entrenador de las mascotas (CL9 hito 6): el menú (0x44b540), la recuperación (0x44b910) con el 34 de recuperar el
// caballo o el cuervo byte a byte, la caja (0x44d010, 0x44ae10) con el 86 de sus dos mezclas, el 31 de cerrar, abrirlo
// por el registro (0x65be8b) y los rand() del registro propio, del C3 y de los bytes protegidos de los paneles. El
// GameServer 0.97 no tiene al entrenador (NpcTalk no atiende su clase): los tests recorren Lorencia (0), Devias (2) y
// Noria (3). Los ítems son inventados.
namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameUiContext;
using Client::Scenes::WorldState;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
using Client::World::ItemState;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
constexpr int C3 = 2;
constexpr std::uint16_t Horse = 0x1a04;
constexpr std::uint16_t Raven = 0x1a05;
constexpr std::size_t HorseSlot = 8;
constexpr std::size_t RavenSlot = 1;
constexpr std::uint8_t PetDurability = 255;
constexpr std::uint32_t PlentyOfZen = 2'000'000'000;
// Los botones (0x103, 0x96 + 0x3c · i) y el de cerrar (0x189, 0x109), en su esquina.
constexpr std::int32_t ButtonX = 0x103;
constexpr std::int32_t HorseY = 0x96;
constexpr std::int32_t RavenY = 0x96 + 0x3c;
constexpr std::int32_t CloseX = 0x189;
constexpr std::int32_t CloseY = 0x109;
// Los del menú (0xb4 + 0x1e · i), y el de mezclar (0x14f, 0x12c) y el de cerrar (0x118, 0x18b) de la caja.
constexpr std::int32_t MenuY = 0xb4;
constexpr std::int32_t MenuStep = 0x1e;
constexpr std::int32_t MixX = 0x14f;
constexpr std::int32_t MixY = 0x12c;
constexpr std::int32_t BoxCloseX = 0x118;
constexpr std::int32_t BoxCloseY = 0x18b;
// Lo que pide 0x6599a0 para abrir el inventario o el entrenador: los bytes protegidos de la tienda y del comercio en
// CanOpen y en CloseByMask.
constexpr int PanelOpen = 4;
constexpr std::size_t BoxNotEmptyText = 1211;
constexpr std::uint8_t DarkLord = 4;
// Lo que cuenta 0x44ae10: las joyas y el espíritu (nivel 0 el del caballo, 1 el del cuervo).
constexpr std::uint16_t Bless = 0x1c0d;
constexpr std::uint16_t Soul = 0x1c0e;
constexpr std::uint16_t Creation = 0x1c16;
constexpr std::uint16_t Chaos = 0x180f;
constexpr std::uint16_t Spirit = 0x1a1f;
constexpr std::uint16_t Life = 0x1c10;

Client::Data::Tables::ItemTable SyntheticItems()
{
    using Client::Data::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (const std::uint16_t type : {Horse, Raven})
        plain[std::size_t{type} * ItemRecordSize + 0x2d] = std::byte{PetDurability};
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

class PetTrainer
{
public:
    explicit PetTrainer(std::int32_t map, std::int32_t mode = Client::Scenes::TrainerRecoveryMode)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.panels.delegated.trainer = mode;
        runtime.panels.delegated.field724584 = 7;
        runtime.field78abb10 = 1;
        runtime.darkHorseLevel = 3;
        runtime.ravenLevel = 2;
        world->OwnCharacter().ClearEquipmentForEntry();
        world->OwnCharacter().zen = PlentyOfZen;
        services.items = &items;
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.emplace_back(index, type);
            return Client::Core::Expected<void, std::string>{};
        };
    }

    [[nodiscard]] Client::Scenes::NpcWindowSender Sender()
    {
        return [this](Client::Protocol::Outgoing packet)
        {
            sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
    }

    // El registro como lo arma el paso de la UI, con los cables de las ventanas de los NPC.
    template <typename Body> void WithThePanels(Body body)
    {
        Client::Scenes::GamePanels panels(*world, random);
        panels.LendTheNpcWindows(*world, random, services, Sender());
        body(panels.Registry());
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click = true)
    {
        auto& movement = world->Runtime().movement;
        movement.pendingClick = click;
        movement.leftHeld = click;
        Client::Core::Expected<void, std::string> stepped{};
        WithThePanels(
            [&](Client::UI::PanelRegistry& panels)
            {
                stepped = Client::Scenes::StepTheTrainer(GameUiContext{.world = *world,
                                                                       .random = random,
                                                                       .input = input,
                                                                       .mouseX = x,
                                                                       .mouseY = y,
                                                                       .inventory = &services,
                                                                       .sendNpcWindow = Sender()},
                                                         panels);
            });
        return stepped;
    }

    // La caja con `count` ítems de `type` (nivel `level`) desde la casilla `first`; devuelve la siguiente libre.
    std::size_t Put(std::size_t first, std::uint16_t type, int count, std::uint32_t level = 0)
    {
        auto& box = world->Runtime().npcWindows.trainerBox;
        for (int i = 0; i < count; ++i, ++first)
            box.at(first) = ItemState{.id = type, .scalar1Low = level << 3, .occupied = 1};
        return first;
    }

    // Lo que deja la mezcla del caballo (0x44aeb1): el espíritu de nivel 0, 5 de bendición, 5 del alma, una de la
    // creación y una del caos.
    void FillTheHorseMix()
    {
        std::size_t next = Put(0, Spirit, 1, 0);
        next = Put(next, Bless, 5);
        next = Put(next, Soul, 5);
        next = Put(next, Creation, 1);
        Put(next, Chaos, 1);
    }

    // La del cuervo (0x44aed9): el espíritu de nivel 1 con 2, 2, 1 y 1.
    void FillTheRavenMix()
    {
        std::size_t next = Put(0, Chaos, 1);
        next = Put(next, Soul, 2);
        next = Put(next, Bless, 2);
        next = Put(next, Spirit, 1, 1);
        Put(next, Creation, 1);
    }

    // Lo que cuesta recuperar `pet` como lo cuentan 0x54d0f0 y 0x5b13e0 (con 0x78abb10 en 0), y el doble sin
    // durabilidad.
    [[nodiscard]] std::uint32_t PriceOf(const ItemState& pet) const
    {
        const std::uint16_t maximum = Client::Scenes::MaxDurability(pet, *items.Record(pet.id), 0);
        if (pet.durability >= maximum)
            return 0;
        const Client::Scenes::PetPriceLevels levels{.raven = world->Runtime().ravenLevel,
                                                    .horse = world->Runtime().darkHorseLevel};
        const auto price = Client::Scenes::ItemPrice(pet, items, Client::Scenes::RepairPriceMode, levels);
        const auto gold =
            static_cast<std::uint32_t>(Client::Scenes::RepairPriceOf(price.Value(), pet.durability, maximum, 0).gold);
        return pet.durability == 0 ? gold * 2 : gold;
    }

    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(sent[index].packet);
    }

    [[nodiscard]] Client::Scenes::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x2468u};
    Client::Input::InputState input{};
    Client::Data::Tables::ItemTable items = SyntheticItems();
    Client::Scenes::InventoryServices services{};
    std::vector<Client::Protocol::Outgoing> sent;
    std::vector<std::pair<std::size_t, std::int32_t>> messages;
};

// 0x44b9e4..0x44c62e con el botón 0: el caballo del lugar 8, el 34 en C3 y el 31 en claro. Los rand(): el cierre del
// registro de 0x44b190 al contar, el de su texto y el del paso, y los dos del C3.
TEST(PetTrainerTest, TheHorseButtonSendsTheRepairOfThePetSlotAndClosesTheTrainer)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        const ItemState horse{.id = Horse, .durability = 100, .occupied = 1};
        trainer.world->OwnCharacter().equipment[HorseSlot] = horse;
        // El nivel 3 vale 6.000.000 (0x54db28) y recuperarlo, 3 · √p · ∜p · (1 − 100 / 255) + 1, a 100: 221.000 (0x48).
        const std::uint32_t price = trainer.PriceOf(horse);
        ASSERT_EQ(price, 221'000u);
        const CrtRandom expected = Advanced(trainer.random, 3 * Own + C3);
        const auto stepped = trainer.Step(ButtonX, HorseY);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 2u);
        EXPECT_EQ(trainer.sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(
            SameBytes(trainer.Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0x08, static_cast<int>(price & 0xffu)})));
        EXPECT_EQ(trainer.sent[1].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(trainer.sent[1].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        auto& runtime = trainer.Runtime();
        EXPECT_EQ(runtime.panels.delegated.trainer, 0);
        EXPECT_EQ(runtime.panels.delegated.field724584, 0);
        EXPECT_EQ(runtime.field78abb10, 0);
        EXPECT_EQ(runtime.ui.mouseOnUi, 1);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_FALSE(runtime.movement.leftHeld);
        EXPECT_EQ(runtime.ownRecordDepth, 0);
    }
}

// El botón 1: el cuervo del lugar 1, sin durabilidad (el precio doble, 0x44b36d).
TEST(PetTrainerTest, TheRavenButtonSendsTheLeftHandAndPaysDoubleWithoutDurability)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        const ItemState raven{.id = Raven, .durability = 0, .occupied = 1};
        trainer.world->OwnCharacter().equipment[RavenSlot] = raven;
        const std::uint32_t price = trainer.PriceOf(raven);
        ASSERT_GT(price, 0u);
        const CrtRandom expected = Advanced(trainer.random, 3 * Own + C3);
        const auto stepped = trainer.Step(ButtonX + 0x77, RavenY + 0x15);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 2u);
        EXPECT_TRUE(
            SameBytes(trainer.Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0x01, static_cast<int>(price & 0xffu)})));
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 0);
    }
}

// 0x44ba75: el zen (sin signo) menor que el precio no manda nada ni cierra; el clic ya se gastó.
TEST(PetTrainerTest, WithoutEnoughZenNothingIsSent)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        const ItemState horse{.id = Horse, .durability = 10, .occupied = 1};
        trainer.world->OwnCharacter().equipment[HorseSlot] = horse;
        trainer.world->OwnCharacter().zen = trainer.PriceOf(horse) - 1;
        const CrtRandom expected = Advanced(trainer.random, 3 * Own);
        const auto stepped = trainer.Step(ButtonX, HorseY);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_TRUE(trainer.sent.empty());
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 2);
        EXPECT_FALSE(trainer.Runtime().movement.pendingClick);
    }
}

// 0x44b2b3..0x44b2a5: sin el caballo en el lugar 8 (aunque esté el cuervo), −1: un cierre en 0x44b190 y el del paso.
TEST(PetTrainerTest, WithoutThePetTheButtonOnlySpendsTheRecordCloses)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        trainer.world->OwnCharacter().equipment[RavenSlot] = ItemState{.id = Raven, .durability = 1, .occupied = 1};
        trainer.world->OwnCharacter().zen = 0xffffffffu;
        const CrtRandom expected = Advanced(trainer.random, 2 * Own);
        const auto stepped = trainer.Step(ButtonX, HorseY);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_TRUE(trainer.sent.empty());
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 2);
    }
}

// 0x44b3be..0x44b4ba: con la durabilidad entera el precio es 0, que alcanza: sale el 34 con 0, sin el segundo cierre de
// 0x44b190.
TEST(PetTrainerTest, AFullPetSendsAZeroPrice)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        trainer.world->OwnCharacter().equipment[HorseSlot] =
            ItemState{.id = Horse, .durability = PetDurability, .occupied = 1};
        trainer.world->OwnCharacter().zen = 0;
        const CrtRandom expected = Advanced(trainer.random, 2 * Own + C3);
        const auto stepped = trainer.Step(ButtonX, HorseY);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 2u);
        EXPECT_TRUE(SameBytes(trainer.Plain(0).Bytes(), Bytes({0xc1, 0x05, 0x34, 0x08, 0x00})));
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 0);
    }
}

// 0x44b99c..0x44b9ce: el botón de cerrar, sin el registro propio ni rand().
TEST(PetTrainerTest, TheCloseButtonClosesWithTheClearWindowClose)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        const CrtRandom expected = trainer.random;
        const auto stepped = trainer.Step(CloseX + 0x17, CloseY + 0x17);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 1u);
        EXPECT_EQ(trainer.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(trainer.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 0);
        EXPECT_EQ(trainer.Runtime().panels.delegated.field724584, 0);
        EXPECT_FALSE(trainer.Runtime().movement.leftHeld);
    }
}

// Sin clic, o fuera de los bordes (x = 0x17b, y = 0x96 + 0x16), nada; el mouse en la ventana es de la UI.
TEST(PetTrainerTest, WithoutTheClickOrOutsideTheButtonsNothingHappens)
{
    PetTrainer trainer(0);
    trainer.world->OwnCharacter().equipment[HorseSlot] = ItemState{.id = Horse, .durability = 1, .occupied = 1};
    const CrtRandom expected = trainer.random;
    ASSERT_TRUE(trainer.Step(ButtonX, HorseY, false).HasValue());
    EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 1);
    ASSERT_TRUE(trainer.Step(0x17b, HorseY).HasValue());
    ASSERT_TRUE(trainer.Step(ButtonX, HorseY + 0x16).HasValue());
    EXPECT_TRUE(trainer.Runtime().movement.pendingClick);
    EXPECT_EQ(trainer.random.State(), expected.State());
    EXPECT_TRUE(trainer.sent.empty());
    EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 2);
    trainer.Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(trainer.Step(0xd4, 0x5a, false).HasValue());
    EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 0);
}

// 0x44b4e0: con 0, 4 o fuera de 1..3 no hace nada, ni el mouse de la UI.
TEST(PetTrainerTest, TheIdleModesDoNothing)
{
    for (const std::int32_t mode : {0, 4, 5, -1})
    {
        PetTrainer trainer(0, mode);
        const CrtRandom expected = trainer.random;
        ASSERT_TRUE(trainer.Step(CloseX, CloseY).HasValue()) << mode;
        ASSERT_TRUE(trainer.Step(MixX, MixY).HasValue()) << mode;
        EXPECT_EQ(trainer.random.State(), expected.State()) << mode;
        EXPECT_TRUE(trainer.sent.empty()) << mode;
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, mode);
        EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 0) << mode;
        EXPECT_TRUE(trainer.Runtime().movement.pendingClick) << mode;
    }
}

// 0x44b5e3..0x44b5f2: el botón 0 del menú pasa al modo 2 (0x449960), sin rand() ni paquetes.
TEST(PetTrainerTest, TheFirstMenuButtonGoesToTheRecovery)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerMenuMode);
        const CrtRandom expected = trainer.random;
        const auto stepped = trainer.Step(ButtonX + 0x77, MenuY + 0x15);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::Scenes::TrainerRecoveryMode);
        EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 1);
        EXPECT_FALSE(trainer.Runtime().movement.pendingClick);
        EXPECT_FALSE(trainer.Runtime().movement.leftHeld);
        EXPECT_TRUE(trainer.sent.empty());
    }
}

// 0x44b5f4..0x44b605: el botón 1 pasa al modo 3 y abre el inventario (0x449970), que no cierra al entrenador
// (0x65c009): los 4 rand() de la tienda y del comercio, y los sonidos del registro (0x1c el último, CL10).
TEST(PetTrainerTest, TheSecondMenuButtonOpensTheBoxWithTheInventory)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerMenuMode);
        const CrtRandom expected = Advanced(trainer.random, PanelOpen);
        const auto stepped = trainer.Step(ButtonX, MenuY + MenuStep);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        auto& runtime = trainer.Runtime();
        EXPECT_EQ(runtime.panels.delegated.trainer, Client::Scenes::TrainerBoxMode);
        EXPECT_EQ(runtime.panels.inventory, 1);
        EXPECT_EQ(runtime.lastSoundId, 0x1c);
        EXPECT_FALSE(runtime.movement.pendingClick);
        EXPECT_TRUE(trainer.sent.empty());
        trainer.WithThePanels([](Client::UI::PanelRegistry& panels)
                              { EXPECT_TRUE(panels.IsOpen(Client::UI::TrainerPanel)); });
    }
}

// Con el Dark Lord, abrir el inventario pide su mascota (C1 A9, 0x65c039), que el registro todavía corta: el modo 3 ya
// quedó escrito.
TEST(PetTrainerTest, TheDarkLordInventoryStillCuts)
{
    PetTrainer trainer(2, Client::Scenes::TrainerMenuMode);
    trainer.world->Actors()[trainer.Runtime().hero].characterClass = DarkLord;
    const auto stepped = trainer.Step(ButtonX, MenuY + MenuStep);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x65c039"), std::string::npos);
    EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::Scenes::TrainerBoxMode);
}

// 0x44b57b..0x44b5a8 recorre tres botones: el tercero, que el dibujo no muestra, solo gasta el clic (Q-C957).
TEST(PetTrainerTest, TheHiddenThirdMenuButtonOnlyEatsTheClick)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerMenuMode);
        const CrtRandom expected = trainer.random;
        ASSERT_TRUE(trainer.Step(ButtonX, MenuY + 2 * MenuStep).HasValue());
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::Scenes::TrainerMenuMode);
        EXPECT_FALSE(trainer.Runtime().movement.pendingClick);
        EXPECT_FALSE(trainer.Runtime().movement.leftHeld);
        EXPECT_TRUE(trainer.sent.empty());
        // Debajo (0xb4 + 0x3c + 0x16) no hay botón: el clic sigue.
        ASSERT_TRUE(trainer.Step(ButtonX, MenuY + 2 * MenuStep + 0x16).HasValue());
        EXPECT_TRUE(trainer.Runtime().movement.pendingClick);
    }
}

// 0x44b5aa..0x44b5dc: el botón de cerrar del menú, con el 31 en claro; con algo en la caja, el 1211 y sigue abierto.
TEST(PetTrainerTest, TheMenuCloseButtonClosesUnlessTheBoxHasItems)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerMenuMode);
        trainer.Put(7, Bless, 1);
        ASSERT_TRUE(trainer.Step(CloseX, CloseY).HasValue());
        EXPECT_TRUE(trainer.sent.empty());
        ASSERT_EQ(trainer.messages.size(), 1u);
        EXPECT_EQ(trainer.messages[0], (std::pair<std::size_t, std::int32_t>{BoxNotEmptyText, 2}));
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::Scenes::TrainerMenuMode);
        EXPECT_FALSE(trainer.Runtime().movement.pendingClick);

        trainer.Runtime().npcWindows.trainerBox = Client::Scenes::EmptyTrainerBox();
        const CrtRandom expected = trainer.random;
        ASSERT_TRUE(trainer.Step(CloseX + 0x17, CloseY + 0x17).HasValue());
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 1u);
        EXPECT_EQ(trainer.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(trainer.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 0);
        EXPECT_EQ(trainer.Runtime().panels.delegated.field724584, 0);
    }
}

// 0x44ae10 al final de cada paso del modo 3: la mezcla del caballo (1) con el registro propio abierto y cerrado, y
// 0x724585 en 1 con el zen por debajo de 5.000.000 (Q-C956).
TEST(PetTrainerTest, TheBoxScanFindsTheHorseMixAndComparesTheZen)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerBoxMode);
        trainer.FillTheHorseMix();
        trainer.world->OwnCharacter().zen = 5'000'000;
        const CrtRandom expected = Advanced(trainer.random, Own);
        ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        auto& runtime = trainer.Runtime();
        EXPECT_EQ(runtime.panels.delegated.field724584, 1);
        EXPECT_EQ(runtime.npcWindows.trainerZenShort, 0);
        EXPECT_EQ(runtime.ownRecordDepth, 0);
        EXPECT_EQ(runtime.ui.mouseOnUi, 0);

        trainer.world->OwnCharacter().zen = 4'999'999;
        ASSERT_TRUE(trainer.Step(0x104, 0, false).HasValue());
        EXPECT_EQ(runtime.panels.delegated.field724584, 1);
        EXPECT_EQ(runtime.npcWindows.trainerZenShort, 1);
        EXPECT_EQ(runtime.ui.mouseOnUi, 1);
        EXPECT_EQ(Client::Scenes::TrainerMixPrice(1), 5'000'000u);
    }
}

// La del cuervo (2), a 1.000.000; el zen se compara sin signo.
TEST(PetTrainerTest, TheBoxScanFindsTheRavenMix)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerBoxMode);
        trainer.FillTheRavenMix();
        trainer.world->OwnCharacter().zen = 999'999;
        const CrtRandom expected = Advanced(trainer.random, Own);
        ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(trainer.Runtime().panels.delegated.field724584, 2);
        EXPECT_EQ(trainer.Runtime().npcWindows.trainerZenShort, 1);

        trainer.world->OwnCharacter().zen = 0x80000000u;
        ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
        EXPECT_EQ(trainer.Runtime().npcWindows.trainerZenShort, 0);
        EXPECT_EQ(Client::Scenes::TrainerMixPrice(2), 1'000'000u);
        EXPECT_EQ(Client::Scenes::TrainerMixPrice(0), 0u);
    }
}

// 0x44ae40..0x44af00: una joya de más o de menos, el espíritu de otro nivel o una casilla con +0x3c en 0 no mezclan,
// sin abrir el registro propio; la joya de la vida y los demás tipos no cuentan.
TEST(PetTrainerTest, TheBoxScanNeedsTheExactJewels)
{
    PetTrainer trainer(0, Client::Scenes::TrainerBoxMode);
    auto& runtime = trainer.Runtime();
    const CrtRandom unchanged = trainer.random;
    // Una del alma de más.
    trainer.FillTheHorseMix();
    trainer.Put(20, Soul, 1);
    runtime.panels.delegated.field724584 = 9;
    runtime.npcWindows.trainerZenShort = 1;
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 0);
    EXPECT_EQ(runtime.npcWindows.trainerZenShort, 0);
    // El espíritu de nivel 2.
    runtime.npcWindows.trainerBox = Client::Scenes::EmptyTrainerBox();
    trainer.FillTheHorseMix();
    runtime.npcWindows.trainerBox[0].scalar1Low = 2u << 3;
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 0);
    // La joya del caos con +0x3c en 0.
    runtime.npcWindows.trainerBox = Client::Scenes::EmptyTrainerBox();
    trainer.FillTheHorseMix();
    runtime.npcWindows.trainerBox[12].occupied = 0;
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 0);
    EXPECT_EQ(trainer.random.State(), unchanged.State());
    // La de la vida y una espada no cambian nada: sigue la del caballo.
    runtime.npcWindows.trainerBox = Client::Scenes::EmptyTrainerBox();
    trainer.FillTheHorseMix();
    trainer.Put(20, Life, 3);
    trainer.Put(25, 0x0001, 1);
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 1);
}

// 0x44d063..0x44d608: el botón de mezclar manda el 86 de la mezcla que dejó el paso anterior (0x724584), con el XOR32 y
// en claro, sin mirar el zen ni 0x724585 (Q-C956); después, 0x44ae10 la vuelve a contar con la caja de ahora.
TEST(PetTrainerTest, TheMixButtonSendsTheMixOfThePreviousStep)
{
    for (const std::int32_t map : Maps)
    {
        for (const auto& [kind, code] : {std::pair<int, int>{1, 0x0d}, std::pair<int, int>{2, 0x0e}})
        {
            PetTrainer trainer(map, Client::Scenes::TrainerBoxMode);
            trainer.world->OwnCharacter().zen = 0;
            trainer.Runtime().panels.delegated.field724584 = static_cast<std::uint8_t>(kind);
            trainer.Runtime().npcWindows.trainerZenShort = 1;
            const CrtRandom expected = trainer.random;
            const auto stepped = trainer.Step(MixX + 0x2b, MixY + 0x1f);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(trainer.random.State(), expected.State()) << map;
            ASSERT_EQ(trainer.sent.size(), 1u);
            EXPECT_EQ(trainer.sent[0].delivery, Delivery::Clear);
            EXPECT_TRUE(SameBytes(trainer.Plain(0).Bytes(), Bytes({0xc1, 0x04, 0x86, code})));
            EXPECT_FALSE(trainer.Runtime().movement.pendingClick);
            EXPECT_FALSE(trainer.Runtime().movement.leftHeld);
            EXPECT_EQ(trainer.Runtime().panels.delegated.field724584, 0);
            EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::Scenes::TrainerBoxMode);
        }
    }
}

// Sin mezcla (0x724584 en 0 u otro valor), el botón gasta el clic y no manda nada.
TEST(PetTrainerTest, TheMixButtonWithoutAMixOnlyEatsTheClick)
{
    for (const std::uint8_t kind : {std::uint8_t{0}, std::uint8_t{3}})
    {
        PetTrainer trainer(2, Client::Scenes::TrainerBoxMode);
        trainer.Runtime().panels.delegated.field724584 = kind;
        ASSERT_TRUE(trainer.Step(MixX, MixY).HasValue());
        EXPECT_TRUE(trainer.sent.empty());
        EXPECT_FALSE(trainer.Runtime().movement.pendingClick);
    }
}

// 0x44d61c..0x44d661: el botón de cerrar de la caja cierra el panel por el registro (0x65cae0(0x8000)): el 31 de
// 0x4499b0 y el inventario, sin rand(); con algo en la caja, el 1211 de 0x658de3 y los dos siguen abiertos.
TEST(PetTrainerTest, TheBoxCloseButtonClosesThePanelAndTheInventory)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerBoxMode);
        auto& runtime = trainer.Runtime();
        runtime.panels.inventory = 1;
        trainer.Put(3, Bless, 1);
        const CrtRandom expected = trainer.random;
        ASSERT_TRUE(trainer.Step(BoxCloseX, BoxCloseY).HasValue());
        EXPECT_TRUE(trainer.sent.empty());
        ASSERT_EQ(trainer.messages.size(), 1u);
        EXPECT_EQ(trainer.messages[0], (std::pair<std::size_t, std::int32_t>{BoxNotEmptyText, 2}));
        EXPECT_EQ(runtime.panels.delegated.trainer, Client::Scenes::TrainerBoxMode);
        EXPECT_EQ(runtime.panels.inventory, 1);
        EXPECT_FALSE(runtime.movement.pendingClick);

        runtime.npcWindows.trainerBox = Client::Scenes::EmptyTrainerBox();
        runtime.lastSoundId = 0;
        const auto closed = trainer.Step(BoxCloseX + 0x17, BoxCloseY + 0x17);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 1u);
        EXPECT_EQ(trainer.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(trainer.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        EXPECT_EQ(runtime.panels.delegated.trainer, 0);
        EXPECT_EQ(runtime.panels.inventory, 0);
        EXPECT_EQ(runtime.lastSoundId, 0x1c);
    }
}

// 0x65be77..0x65be93: abrir el panel 0x8000 cierra lo demás (los 4 rand() de la tienda y del comercio) y llama a
// 0x449940(1): el menú, con la caja vacía, sin abrir el inventario.
TEST(PetTrainerTest, OpeningThePanelStartsTheMenuWithAnEmptyBox)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, 0);
        auto& runtime = trainer.Runtime();
        trainer.Put(0, Bless, 4);
        runtime.panels.character = 1;
        const CrtRandom expected = Advanced(trainer.random, PanelOpen);
        trainer.WithThePanels(
            [](Client::UI::PanelRegistry& panels)
            {
                const auto opened = panels.Open(Client::UI::TrainerPanel);
                ASSERT_TRUE(opened.HasValue()) << opened.Error();
                EXPECT_TRUE(opened.Value());
            });
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(runtime.panels.delegated.trainer, Client::Scenes::TrainerMenuMode);
        EXPECT_FALSE(Client::Scenes::TrainerBoxHasItems(*trainer.world));
        EXPECT_EQ(runtime.panels.character, 0);
        EXPECT_EQ(runtime.panels.inventory, 0);
        EXPECT_EQ(runtime.lastSoundId, 0x1c);
        EXPECT_TRUE(trainer.sent.empty());
    }
}

// 0x658dda..0x658dfd: cerrar todo con algo en la caja deja al entrenador con el 1211; vacía, 0x4499b0 y su 31.
TEST(PetTrainerTest, ClosingAllPanelsKeepsTheTrainerWithItemsInTheBox)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::Scenes::TrainerMenuMode);
        trainer.Put(31, Chaos, 1);
        trainer.WithThePanels([](Client::UI::PanelRegistry& panels) { ASSERT_TRUE(panels.CloseAll().HasValue()); });
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::Scenes::TrainerMenuMode);
        ASSERT_EQ(trainer.messages.size(), 1u);
        EXPECT_EQ(trainer.messages[0].first, BoxNotEmptyText);
        EXPECT_TRUE(trainer.sent.empty());

        trainer.Runtime().npcWindows.trainerBox = Client::Scenes::EmptyTrainerBox();
        trainer.WithThePanels([](Client::UI::PanelRegistry& panels) { ASSERT_TRUE(panels.CloseAll().HasValue()); });
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 0);
        ASSERT_EQ(trainer.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(trainer.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}
} // namespace
