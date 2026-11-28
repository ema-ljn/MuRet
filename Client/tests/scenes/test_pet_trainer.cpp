#include "Network/Xor32Packet.h"
#include "PetTrainer/PetTrainer.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Models/LogicalAccess.h"
#include "Hud/GamePanels.h"
#include "Ui/GameUiContext.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Items/ItemPrice.h"
#include "Npc/NpcWindowState.h"
#include "Npc/NpcWindows.h"
#include "Kernel/MouseState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

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

// The pet trainer (CL9 milestone 6): the menu (0x44b540), the recovery (0x44b910) with the 34 that recovers the horse
// or the raven byte by byte, the box (0x44d010, 0x44ae10) with the 86 of its two mixes, the closing 31, opening it
// through the registry (0x65be8b) and the rand() of the own record, of the C3 and of the panels' protected bytes.
// GameServer 0.97 does not have the trainer (NpcTalk does not handle its class): the tests go through Lorencia (0),
// Devias (2) and Noria (3). The items are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Ui::GameUiContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Network::Delivery;
using Client::Network::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr int Own = Client::Models::OwnRecordCloseRandoms;
constexpr int C3 = 2;
constexpr std::uint16_t Horse = 0x1a04;
constexpr std::uint16_t Raven = 0x1a05;
constexpr std::size_t HorseSlot = 8;
constexpr std::size_t RavenSlot = 1;
constexpr std::uint8_t PetDurability = 255;
constexpr std::uint32_t PlentyOfZen = 2'000'000'000;
// The buttons (0x103, 0x96 + 0x3c · i) and the close one (0x189, 0x109), at their corner.
constexpr std::int32_t ButtonX = 0x103;
constexpr std::int32_t HorseY = 0x96;
constexpr std::int32_t RavenY = 0x96 + 0x3c;
constexpr std::int32_t CloseX = 0x189;
constexpr std::int32_t CloseY = 0x109;
// The menu's (0xb4 + 0x1e · i), and the box's mix (0x14f, 0x12c) and close (0x118, 0x18b) buttons.
constexpr std::int32_t MenuY = 0xb4;
constexpr std::int32_t MenuStep = 0x1e;
constexpr std::int32_t MixX = 0x14f;
constexpr std::int32_t MixY = 0x12c;
constexpr std::int32_t BoxCloseX = 0x118;
constexpr std::int32_t BoxCloseY = 0x18b;
// What 0x6599a0 asks to open the inventory or the trainer: the shop's and the trade's protected bytes in CanOpen and
// in CloseByMask.
constexpr int PanelOpen = 4;
constexpr std::size_t BoxNotEmptyText = 1211;
constexpr std::uint8_t DarkLord = 4;
// What 0x44ae10 counts: the jewels and the spirit (level 0 the horse's, 1 the raven's).
constexpr std::uint16_t Bless = 0x1c0d;
constexpr std::uint16_t Soul = 0x1c0e;
constexpr std::uint16_t Creation = 0x1c16;
constexpr std::uint16_t Chaos = 0x180f;
constexpr std::uint16_t Spirit = 0x1a1f;
constexpr std::uint16_t Life = 0x1c10;

Client::Tables::ItemTable SyntheticItems()
{
    using Client::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (const std::uint16_t type : {Horse, Raven})
        plain[std::size_t{type} * ItemRecordSize + 0x2d] = std::byte{PetDurability};
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

class PetTrainer
{
public:
    explicit PetTrainer(std::int32_t map, std::int32_t mode = Client::PetTrainer::TrainerRecoveryMode)
    {
        auto& runtime = world->Runtime();
        runtime.map = map;
        runtime.panels.delegated.trainer = mode;
        runtime.panels.delegated.field724584 = 7;
        runtime.field78abb10 = 1;
        runtime.pets[Client::Kernel::DarkHorsePetStats].level = 3;
        runtime.pets[Client::Kernel::RavenPetStats].level = 2;
        world->OwnCharacter().ClearEquipmentForEntry();
        world->OwnCharacter().zen = PlentyOfZen;
        services.items = &items;
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.emplace_back(index, type);
            return Client::Core::Expected<void, std::string>{};
        };
    }

    [[nodiscard]] Client::Npc::NpcWindowSender Sender()
    {
        return [this](Client::Protocol::Outgoing packet)
        {
            sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
    }

    // The registry as the UI step builds it, with the NPC windows' wiring.
    template <typename Body> void WithThePanels(Body body)
    {
        Client::Hud::GamePanels panels(*world, random);
        panels.LendTheNpcWindows(*world, random, services, Sender());
        body(panels.Registry());
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click = true)
    {
        Client::Kernel::MouseState& mouse = world->Runtime().mouse;
        mouse.pendingClick = click;
        mouse.leftHeld = click;
        Client::Core::Expected<void, std::string> stepped{};
        WithThePanels(
            [&](Client::Ui::PanelRegistry& panels)
            {
                stepped = Client::PetTrainer::StepTheTrainer(GameUiContext{.world = *world,
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

    // The box with `count` items of `type` (level `level`) from cell `first`; returns the next free one.
    std::size_t Put(std::size_t first, std::uint16_t type, int count, std::uint32_t level = 0)
    {
        auto& box = world->Runtime().npcWindows.trainerBox;
        for (int i = 0; i < count; ++i, ++first)
            box.at(first) = ItemState{.id = type, .scalar1Low = level << 3, .occupied = 1};
        return first;
    }

    // What makes the horse mix (0x44aeb1): the level-0 spirit, 5 bless, 5 soul, one creation and one chaos.
    void FillTheHorseMix()
    {
        std::size_t next = Put(0, Spirit, 1, 0);
        next = Put(next, Bless, 5);
        next = Put(next, Soul, 5);
        next = Put(next, Creation, 1);
        Put(next, Chaos, 1);
    }

    // The raven's (0x44aed9): the level-1 spirit with 2, 2, 1 and 1.
    void FillTheRavenMix()
    {
        std::size_t next = Put(0, Chaos, 1);
        next = Put(next, Soul, 2);
        next = Put(next, Bless, 2);
        next = Put(next, Spirit, 1, 1);
        Put(next, Creation, 1);
    }

    // What recovering `pet` costs as 0x54d0f0 and 0x5b13e0 count it (with 0x78abb10 at 0), and double without
    // durability.
    [[nodiscard]] std::uint32_t PriceOf(const ItemState& pet) const
    {
        const std::uint16_t maximum = Client::Items::MaxDurability(pet, *items.Record(pet.id), 0);
        if (pet.durability >= maximum)
            return 0;
        const Client::Items::PetPriceLevels levels{.raven = world->Runtime().pets[Client::Kernel::RavenPetStats].level,
                                                   .horse =
                                                       world->Runtime().pets[Client::Kernel::DarkHorsePetStats].level};
        const auto price = Client::Items::ItemPrice(pet, items, Client::Items::RepairPriceMode, levels);
        const auto gold =
            static_cast<std::uint32_t>(Client::Items::RepairPriceOf(price.Value(), pet.durability, maximum, 0).gold);
        return pet.durability == 0 ? gold * 2 : gold;
    }

    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(sent[index].packet);
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random{0x2468u};
    Client::Input::InputState input{};
    Client::Tables::ItemTable items = SyntheticItems();
    Client::Items::InventoryServices services{};
    std::vector<Client::Protocol::Outgoing> sent;
    std::vector<std::pair<std::size_t, std::int32_t>> messages;
};

// 0x44b9e4..0x44c62e with button 0: the horse in slot 8, the 34 in C3 and the 31 in the clear. The rand(): 0x44b190's
// record close when counting, its text's and the step's, and the C3's two.
TEST(PetTrainerTest, TheHorseButtonSendsTheRepairOfThePetSlotAndClosesTheTrainer)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map);
        const ItemState horse{.id = Horse, .durability = 100, .occupied = 1};
        trainer.world->OwnCharacter().equipment[HorseSlot] = horse;
        trainer.Runtime().petTooltipQuery = {.key = 0x12345678, .type = Horse, .scalar = 0x3f};
        trainer.Runtime().pets[0].experience = 987;
        // Level 3 is worth 6000000 (0x54db28) and recovering it, 3 · √p · ∜p · (1 − 100 / 255) + 1, at 100: 221000
        // (0x48).
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
        EXPECT_EQ(runtime.petTooltipQuery.key, 0x12345678u);
        EXPECT_EQ(runtime.petTooltipQuery.type, 0);
        EXPECT_EQ(runtime.petTooltipQuery.scalar, 0u);
        EXPECT_EQ(runtime.pets[0].experience, 987u);
        EXPECT_EQ(runtime.ui.mouseOnUi, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_FALSE(runtime.mouse.leftHeld);
        EXPECT_EQ(runtime.ownRecordDepth, 0);
    }
}

// Button 1: the raven in slot 1, without durability (the price doubled, 0x44b36d).
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

// 0x44ba75: zen (unsigned) below the price sends nothing and does not close; the click is already spent.
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
        EXPECT_FALSE(trainer.Runtime().mouse.pendingClick);
    }
}

// 0x44b2b3..0x44b2a5: without the horse in slot 8 (even with the raven), −1: one close in 0x44b190 and the step's.
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

// 0x44b3be..0x44b4ba: with full durability the price is 0, which is enough: the 34 goes out with 0, without 0x44b190's
// second close.
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

// 0x44b99c..0x44b9ce: the close button, without the own record or rand().
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
        EXPECT_FALSE(trainer.Runtime().mouse.leftHeld);
    }
}

// Without a click, or outside the edges (x = 0x17b, y = 0x96 + 0x16), nothing; the mouse in the window belongs to the
// UI.
TEST(PetTrainerTest, WithoutTheClickOrOutsideTheButtonsNothingHappens)
{
    PetTrainer trainer(0);
    trainer.world->OwnCharacter().equipment[HorseSlot] = ItemState{.id = Horse, .durability = 1, .occupied = 1};
    const CrtRandom expected = trainer.random;
    ASSERT_TRUE(trainer.Step(ButtonX, HorseY, false).HasValue());
    EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 1);
    ASSERT_TRUE(trainer.Step(0x17b, HorseY).HasValue());
    ASSERT_TRUE(trainer.Step(ButtonX, HorseY + 0x16).HasValue());
    EXPECT_TRUE(trainer.Runtime().mouse.pendingClick);
    EXPECT_EQ(trainer.random.State(), expected.State());
    EXPECT_TRUE(trainer.sent.empty());
    EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 2);
    trainer.Runtime().ui.mouseOnUi = 0;
    ASSERT_TRUE(trainer.Step(0xd4, 0x5a, false).HasValue());
    EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 0);
}

// 0x44b4e0: with 0, 4 or outside 1..3 it does nothing, not even the UI's mouse.
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
        EXPECT_TRUE(trainer.Runtime().mouse.pendingClick) << mode;
    }
}

// 0x44b5e3..0x44b5f2: menu button 0 goes to mode 2 (0x449960), without rand() or packets.
TEST(PetTrainerTest, TheFirstMenuButtonGoesToTheRecovery)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerMenuMode);
        const CrtRandom expected = trainer.random;
        const auto stepped = trainer.Step(ButtonX + 0x77, MenuY + 0x15);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::PetTrainer::TrainerRecoveryMode);
        EXPECT_EQ(trainer.Runtime().ui.mouseOnUi, 1);
        EXPECT_FALSE(trainer.Runtime().mouse.pendingClick);
        EXPECT_FALSE(trainer.Runtime().mouse.leftHeld);
        EXPECT_TRUE(trainer.sent.empty());
    }
}

// 0x44b5f4..0x44b605: button 1 goes to mode 3 and opens the inventory (0x449970), which does not close the trainer
// (0x65c009): the shop's and the trade's 4 rand(), and the registry's sounds (0x1c the last, CL10).
TEST(PetTrainerTest, TheSecondMenuButtonOpensTheBoxWithTheInventory)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerMenuMode);
        const CrtRandom expected = Advanced(trainer.random, PanelOpen);
        const auto stepped = trainer.Step(ButtonX, MenuY + MenuStep);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        auto& runtime = trainer.Runtime();
        EXPECT_EQ(runtime.panels.delegated.trainer, Client::PetTrainer::TrainerBoxMode);
        EXPECT_EQ(runtime.panels.inventory, 1);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_TRUE(trainer.sent.empty());
        trainer.WithThePanels([](Client::Ui::PanelRegistry& panels)
                              { EXPECT_TRUE(panels.IsOpen(Client::Ui::TrainerPanel)); });
    }
}

// With the Dark Lord, opening the inventory asks for its pet (C1 A9, 0x65c039), where the registry still fails: mode 3
// is already written.
TEST(PetTrainerTest, TheDarkLordInventoryStillCuts)
{
    PetTrainer trainer(2, Client::PetTrainer::TrainerMenuMode);
    trainer.world->Hero().characterClass = DarkLord;
    const auto stepped = trainer.Step(ButtonX, MenuY + MenuStep);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x65c039"), std::string::npos);
    EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::PetTrainer::TrainerBoxMode);
}

// 0x44b57b..0x44b5a8 walks three buttons: the third, which the drawing does not show, only spends the click (Q-C957).
TEST(PetTrainerTest, TheHiddenThirdMenuButtonOnlyEatsTheClick)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerMenuMode);
        const CrtRandom expected = trainer.random;
        ASSERT_TRUE(trainer.Step(ButtonX, MenuY + 2 * MenuStep).HasValue());
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::PetTrainer::TrainerMenuMode);
        EXPECT_FALSE(trainer.Runtime().mouse.pendingClick);
        EXPECT_FALSE(trainer.Runtime().mouse.leftHeld);
        EXPECT_TRUE(trainer.sent.empty());
        // Below (0xb4 + 0x3c + 0x16) there is no button: the click stays.
        ASSERT_TRUE(trainer.Step(ButtonX, MenuY + 2 * MenuStep + 0x16).HasValue());
        EXPECT_TRUE(trainer.Runtime().mouse.pendingClick);
    }
}

// 0x44b5aa..0x44b5dc: the menu's close button, with the 31 in the clear; with something in the box, the 1211 and it
// stays open.
TEST(PetTrainerTest, TheMenuCloseButtonClosesUnlessTheBoxHasItems)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerMenuMode);
        trainer.Put(7, Bless, 1);
        ASSERT_TRUE(trainer.Step(CloseX, CloseY).HasValue());
        EXPECT_TRUE(trainer.sent.empty());
        ASSERT_EQ(trainer.messages.size(), 1u);
        EXPECT_EQ(trainer.messages[0], (std::pair<std::size_t, std::int32_t>{BoxNotEmptyText, 2}));
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::PetTrainer::TrainerMenuMode);
        EXPECT_FALSE(trainer.Runtime().mouse.pendingClick);

        trainer.Runtime().npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
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

// 0x44ae10 at the end of each mode-3 step: the horse mix (1) with the own record opened and closed, and 0x724585 at 1
// with the zen below 5000000 (Q-C956).
TEST(PetTrainerTest, TheBoxScanFindsTheHorseMixAndComparesTheZen)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerBoxMode);
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
        EXPECT_EQ(Client::PetTrainer::TrainerMixPrice(1), 5'000'000u);
    }
}

// The raven's (2), at 1000000; the zen is compared unsigned.
TEST(PetTrainerTest, TheBoxScanFindsTheRavenMix)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerBoxMode);
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
        EXPECT_EQ(Client::PetTrainer::TrainerMixPrice(2), 1'000'000u);
        EXPECT_EQ(Client::PetTrainer::TrainerMixPrice(0), 0u);
    }
}

// 0x44ae40..0x44af00: one jewel too many or too few, a spirit of another level or a cell with +0x3c at 0 do not mix,
// without opening the own record; the jewel of life and the other types do not count.
TEST(PetTrainerTest, TheBoxScanNeedsTheExactJewels)
{
    PetTrainer trainer(0, Client::PetTrainer::TrainerBoxMode);
    auto& runtime = trainer.Runtime();
    const CrtRandom unchanged = trainer.random;
    // One soul too many.
    trainer.FillTheHorseMix();
    trainer.Put(20, Soul, 1);
    runtime.panels.delegated.field724584 = 9;
    runtime.npcWindows.trainerZenShort = 1;
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 0);
    EXPECT_EQ(runtime.npcWindows.trainerZenShort, 0);
    // The level-2 spirit.
    runtime.npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
    trainer.FillTheHorseMix();
    runtime.npcWindows.trainerBox[0].scalar1Low = 2u << 3;
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 0);
    // The chaos jewel with +0x3c at 0.
    runtime.npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
    trainer.FillTheHorseMix();
    runtime.npcWindows.trainerBox[12].occupied = 0;
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 0);
    EXPECT_EQ(trainer.random.State(), unchanged.State());
    // The life jewel and a sword change nothing: the horse's stays.
    runtime.npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
    trainer.FillTheHorseMix();
    trainer.Put(20, Life, 3);
    trainer.Put(25, 0x0001, 1);
    ASSERT_TRUE(trainer.Step(0, 0, false).HasValue());
    EXPECT_EQ(runtime.panels.delegated.field724584, 1);
}

// 0x44d063..0x44d608: the mix button sends the 86 of the mix the previous step left (0x724584), with the XOR32 and in
// the clear, without checking the zen or 0x724585 (Q-C956); then 0x44ae10 counts it again with the current box.
TEST(PetTrainerTest, TheMixButtonSendsTheMixOfThePreviousStep)
{
    for (const std::int32_t map : Maps)
    {
        for (const auto& [kind, code] : {std::pair<int, int>{1, 0x0d}, std::pair<int, int>{2, 0x0e}})
        {
            PetTrainer trainer(map, Client::PetTrainer::TrainerBoxMode);
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
            EXPECT_FALSE(trainer.Runtime().mouse.pendingClick);
            EXPECT_FALSE(trainer.Runtime().mouse.leftHeld);
            EXPECT_EQ(trainer.Runtime().panels.delegated.field724584, 0);
            EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::PetTrainer::TrainerBoxMode);
        }
    }
}

// Without a mix (0x724584 at 0 or another value), the button spends the click and sends nothing.
TEST(PetTrainerTest, TheMixButtonWithoutAMixOnlyEatsTheClick)
{
    for (const std::uint8_t kind : {std::uint8_t{0}, std::uint8_t{3}})
    {
        PetTrainer trainer(2, Client::PetTrainer::TrainerBoxMode);
        trainer.Runtime().panels.delegated.field724584 = kind;
        ASSERT_TRUE(trainer.Step(MixX, MixY).HasValue());
        EXPECT_TRUE(trainer.sent.empty());
        EXPECT_FALSE(trainer.Runtime().mouse.pendingClick);
    }
}

// 0x44d61c..0x44d661: the box's close button closes the panel through the registry (0x65cae0(0x8000)): 0x4499b0's 31
// and the inventory, without rand(); with something in the box, 0x658de3's 1211 and both stay open.
TEST(PetTrainerTest, TheBoxCloseButtonClosesThePanelAndTheInventory)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerBoxMode);
        auto& runtime = trainer.Runtime();
        runtime.panels.inventory = 1;
        trainer.Put(3, Bless, 1);
        const CrtRandom expected = trainer.random;
        ASSERT_TRUE(trainer.Step(BoxCloseX, BoxCloseY).HasValue());
        EXPECT_TRUE(trainer.sent.empty());
        ASSERT_EQ(trainer.messages.size(), 1u);
        EXPECT_EQ(trainer.messages[0], (std::pair<std::size_t, std::int32_t>{BoxNotEmptyText, 2}));
        EXPECT_EQ(runtime.panels.delegated.trainer, Client::PetTrainer::TrainerBoxMode);
        EXPECT_EQ(runtime.panels.inventory, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick);

        runtime.npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
        runtime.lastSoundId = Client::Audio::SoundId{0};
        const auto closed = trainer.Step(BoxCloseX + 0x17, BoxCloseY + 0x17);
        ASSERT_TRUE(closed.HasValue()) << closed.Error();
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        ASSERT_EQ(trainer.sent.size(), 1u);
        EXPECT_EQ(trainer.sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(trainer.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
        EXPECT_EQ(runtime.panels.delegated.trainer, 0);
        EXPECT_EQ(runtime.panels.inventory, 0);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1c});
    }
}

// 0x65be77..0x65be93: opening panel 0x8000 closes the rest (the shop's and the trade's 4 rand()) and calls
// 0x449940(1): the menu, with the box empty, without opening the inventory.
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
            [](Client::Ui::PanelRegistry& panels)
            {
                const auto opened = panels.Open(Client::Ui::TrainerPanel);
                ASSERT_TRUE(opened.HasValue()) << opened.Error();
                EXPECT_TRUE(opened.Value());
            });
        EXPECT_EQ(trainer.random.State(), expected.State()) << map;
        EXPECT_EQ(runtime.panels.delegated.trainer, Client::PetTrainer::TrainerMenuMode);
        EXPECT_FALSE(Client::PetTrainer::TrainerBoxHasItems(*trainer.world));
        EXPECT_EQ(runtime.panels.character, 0);
        EXPECT_EQ(runtime.panels.inventory, 0);
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_TRUE(trainer.sent.empty());
    }
}

// 0x658dda..0x658dfd: closing everything with something in the box keeps the trainer with the 1211; empty, 0x4499b0
// and its 31.
TEST(PetTrainerTest, ClosingAllPanelsKeepsTheTrainerWithItemsInTheBox)
{
    for (const std::int32_t map : Maps)
    {
        PetTrainer trainer(map, Client::PetTrainer::TrainerMenuMode);
        trainer.Put(31, Chaos, 1);
        trainer.WithThePanels([](Client::Ui::PanelRegistry& panels) { ASSERT_TRUE(panels.CloseAll().HasValue()); });
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, Client::PetTrainer::TrainerMenuMode);
        ASSERT_EQ(trainer.messages.size(), 1u);
        EXPECT_EQ(trainer.messages[0].first, BoxNotEmptyText);
        EXPECT_TRUE(trainer.sent.empty());

        trainer.Runtime().npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
        trainer.WithThePanels([](Client::Ui::PanelRegistry& panels) { ASSERT_TRUE(panels.CloseAll().HasValue()); });
        EXPECT_EQ(trainer.Runtime().panels.delegated.trainer, 0);
        ASSERT_EQ(trainer.sent.size(), 1u);
        EXPECT_TRUE(SameBytes(trainer.sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    }
}
} // namespace
