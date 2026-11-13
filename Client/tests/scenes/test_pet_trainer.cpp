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
#include "Scenes/GameUi.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/ItemPrice.h"
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

// El entrenador de las mascotas con 0x724580 en 2 (0x44b910, CL9 hito 6, fase 3b): el 34 de recuperar el caballo o el
// cuervo byte a byte, el 31 de cerrar y los rand() del registro propio y del C3. El GameServer 0.97 no tiene al
// entrenador (NpcTalk no atiende su clase): los tests recorren Lorencia (0), Devias (2) y Noria (3). Los ítems son
// inventados.
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
    explicit PetTrainer(std::int32_t map)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.panels.delegated.trainer = 2;
        runtime.panels.delegated.field724584 = 7;
        runtime.field78abb10 = 1;
        runtime.darkHorseLevel = 3;
        runtime.ravenLevel = 2;
        world->OwnCharacter().ClearEquipmentForEntry();
        world->OwnCharacter().zen = PlentyOfZen;
        services.items = &items;
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click = true)
    {
        auto& movement = world->Runtime().movement;
        movement.pendingClick = click;
        movement.leftHeld = click;
        return Client::Scenes::StepTheTrainer(GameUiContext{.world = *world,
                                                            .random = random,
                                                            .input = input,
                                                            .mouseX = x,
                                                            .mouseY = y,
                                                            .inventory = &services,
                                                            .sendNpcWindow = [this](Client::Protocol::Outgoing packet)
                                                            {
                                                                sent.push_back(std::move(packet));
                                                                return Client::Core::Expected<void, std::string>{};
                                                            }});
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

// 0x44b4e0: 1 y 3 cortan con su dirección; 0, 4 y 5 no hacen nada.
TEST(PetTrainerTest, TheOtherModesStillCut)
{
    PetTrainer trainer(0);
    trainer.Runtime().panels.delegated.trainer = 1;
    const auto summary = trainer.Step(ButtonX, HorseY);
    ASSERT_FALSE(summary.HasValue());
    EXPECT_NE(summary.Error().find("0x44b540"), std::string::npos);
    trainer.Runtime().panels.delegated.trainer = 3;
    const auto box = trainer.Step(ButtonX, HorseY);
    ASSERT_FALSE(box.HasValue());
    EXPECT_NE(box.Error().find("0x44d010"), std::string::npos);
    for (const std::int32_t mode : {0, 4, 5})
    {
        trainer.Runtime().panels.delegated.trainer = mode;
        EXPECT_TRUE(trainer.Step(CloseX, CloseY).HasValue()) << mode;
    }
    EXPECT_TRUE(trainer.sent.empty());
}
} // namespace
