#include "Core/Expected.h"
#include "Kernel/MouseState.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Protocol/Player/LevelUpPoint.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pets/PetInfo.h"
#include "Models/LogicalAccess.h"
#include "Actors/FullSetScan.h"
#include "Chat/GameChat.h"
#include "Hud/GameUi.h"
#include "Items/InventoryMoves.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/ItemSetFiles.h"
#include "support/SameBytes.h"
#include "support/TableFiles.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The character window in the UI step (CL8 milestone 5; 06 § HUD y ventanas, "La ventana del personaje"): opening it
// with C (0x65c94b..0x65ca72, with 0x550d10), its step (0x5e4510, the point buttons and the close one) and the dark
// horse's request (0x424130 → 0x424230). The rand() counts come from the ASM and the CRT model; none from a run of the
// original. Each case runs in Lorencia and in Devias.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameUiContext;
using Client::Input::InputEvent;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Protocol::Outgoing;
using Client::Tests::SameBytes;

// 0x5a05e5..0x5a10e2: the skill bar's own record; 0x5a343e..0x5a3587: the trade and NPC shop queries of Q, W and E;
// 0x5a5511: IsOpen(0), which with the character (0x2000000) goes through both before.
constexpr int BaseStepRandoms = OwnRecordCloseRandoms + 2 + 2;
// Open(0x2000000): CanOpen queries the NPC shop and the trade, and 0x657ab0's mask the two again.
constexpr int OpenRandoms = 4;
// 0x550d10's tail (0x5512bf..0x5513b1): the IDIV's rand() and the two % 100. The 33 closes of 0x40e1b0 stay inside
// the access of 0x65c9a4 and draw nothing.
constexpr int TailRandoms = 3;
// Lorencia and Devias.
constexpr std::array<std::int32_t, 2> Maps{0, 2};
// Where 0x5ee2e0 places the window each frame (0x78aba14 and 0x78aba18).
constexpr std::int32_t WindowX = 0x1c2;
constexpr std::int32_t WindowY = 0;
// −1 in the type's WORD.
constexpr std::uint16_t EmptyType = 0xffff;
// The Dark Lord's class (+0x1d4 & 7) and one that is not.
constexpr std::uint8_t DarkLordClass = 4;
constexpr std::uint8_t KnightClass = 1;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// An Item.bmd with nothing: 0x550d10 adds nothing from an empty slot.
Client::Tables::ItemTable EmptyItems()
{
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(
        std::vector<std::byte>(Client::Tables::ItemTableSize), Client::Tables::ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

Client::Tables::ItemSetTables EmptySets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// The UI step with what the character window reads: 0x550d10's tables, what the last 0x505820 left, the empty
// equipment (the durability at 0, which 0x550cf0 finds) and what goes out through send().
class SteppingCharacter
{
public:
    explicit SteppingCharacter(std::int32_t map)
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.fullSetScan = Client::Actors::Scan505820{};
        runtime.hud.characterX = WindowX;
        runtime.hud.characterY = WindowY;
        Client::Kernel::ItemState empty;
        empty.id = EmptyType;
        world.OwnCharacter().equipment.fill(empty);
    }
    SteppingCharacter(const SteppingCharacter&) = delete;
    SteppingCharacter& operator=(const SteppingCharacter&) = delete;
    SteppingCharacter(SteppingCharacter&&) = delete;
    SteppingCharacter& operator=(SteppingCharacter&&) = delete;
    ~SteppingCharacter() = default;

    Client::Core::Expected<void, std::string> Step(std::int32_t x = 0, std::int32_t y = 0)
    {
        return Client::Hud::StepTheGameUi(GameUiContext{.world = world,
                                                        .random = random,
                                                        .input = input,
                                                        .mouseX = x,
                                                        .mouseY = y,
                                                        .chat = &chat,
                                                        .inventory = &inventory,
                                                        .itemSetTables = sets});
    }
    void Press(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyDown, .key = key});
    }
    void Release(Key key)
    {
        input.Apply(InputEvent{.kind = InputEvent::Kind::KeyUp, .key = key});
    }
    void Click()
    {
        Client::Kernel::LatchTheLeftButton(world.Runtime().mouse, true, true);
    }
    // The window already open, with the horse's request done (0x723911 at 1).
    void OpenTheWindow()
    {
        world.Runtime().panels.character = 1;
        world.Runtime().panels.delegated.pet = 1;
    }

    WorldState world;
    CrtRandom random;
    InputState input;
    std::vector<Outgoing> sent;
    const Client::Tables::ItemTable items = EmptyItems();
    const Client::Tables::ItemSetTables setTables = EmptySets();
    const Client::Tables::ItemSetTables* sets = &setTables;
    const Client::Items::InventoryServices inventory{.items = &items};
    const Client::Chat::GameChatContext chat{
        .world = world, .random = random, .send = [this](Outgoing packet) { sent.push_back(std::move(packet)); }};
};

::testing::AssertionResult SamePacket(const Outgoing& actual, const Outgoing& expected)
{
    if (actual.delivery != expected.delivery)
        return ::testing::AssertionFailure() << "sale de otra forma";
    return SameBytes(actual.packet.Bytes(), expected.packet.Bytes());
}

Outgoing DarkHorseRequest()
{
    return Client::Protocol::Pets::WritePetInfoRequest({Client::Protocol::Pets::DarkHorsePet,
                                                        Client::Protocol::Pets::InventoryPlace,
                                                        Client::Protocol::Pets::PetSlot});
}

// 0x65c94b..0x65ca72: C opens the character, zeroes the set tooltips' state and runs 0x550d10 with the own record
// open around it; the A9 goes out in the next step.
TEST(GameCharacterWindowTest, TheLetterCOpensTheCharacterAndRecalculatesTheOwnStats)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        Client::Player::ItemSetState& sets = tested.world.ItemSets();
        sets.byte9304 = 1;
        sets.byte9305 = 3;
        sets.byte9306 = 1;
        sets.names[1][0] = 'x';
        tested.world.OwnCharacter().wordef8 = 0x3456;
        tested.Press(Key::C);
        const CrtRandom expected =
            Advanced(tested.random, BaseStepRandoms + OpenRandoms + TailRandoms + OwnRecordCloseRandoms);
        const auto stepped = tested.Step(320, 240);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.random.State(), expected.State()) << map;
        EXPECT_EQ(tested.world.Runtime().panels.character, 1);
        EXPECT_EQ(sets.byte9304, 0);
        EXPECT_EQ(sets.byte9305, 0);
        EXPECT_EQ(sets.byte9306, 0);
        EXPECT_EQ(sets.names[1][0], '\0');
        // 0x550d10's tail ran: +0xef8 is a WORD subtraction at 0.
        EXPECT_EQ(tested.world.OwnCharacter().wordef8, 0);
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_TRUE(tested.sent.empty());
    }
}

// 0x550cf0: with the durability of the twelve slots other than 0, 0x550d10 does not run; the access opens and closes
// anyway.
TEST(GameCharacterWindowTest, WithEveryPlaceWornTheOpeningSkipsTheRecalculation)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        for (Client::Kernel::ItemState& item : tested.world.OwnCharacter().equipment)
            item.durability = 1;
        tested.world.OwnCharacter().wordef8 = 0x3456;
        tested.Press(Key::C);
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + OpenRandoms + OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.random.State(), expected.State()) << map;
        EXPECT_EQ(tested.world.OwnCharacter().wordef8, 0x3456);
    }
}

// Without ItemSetType.bmd and ItemSetOption.bmd, 0x550d10 stops at 0x40e1b0, with the access closed.
TEST(GameCharacterWindowTest, WithoutTheSetTablesTheOpeningIsTheCutOf0x40e1b0)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.sets = nullptr;
        tested.Press(Key::C);
        const auto stepped = tested.Step(320, 240);
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_NE(stepped.Error().find("0x40e1b0"), std::string::npos) << stepped.Error();
        EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    }
}

// 0x424230: the A9 01 00 08 goes out once per opening, in the step after the one that opens it; closing the character
// sets 0x723911 to 0 in the next step, and the next opening asks for it again.
TEST(GameCharacterWindowTest, TheDarkHorseIsAskedOncePerOpening)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        const Client::Ui::DelegatedPanels& delegated = tested.world.Runtime().panels.delegated;
        tested.Press(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        ASSERT_EQ(tested.sent.size(), 1u);
        EXPECT_TRUE(SamePacket(tested.sent[0], DarkHorseRequest()));
        EXPECT_EQ(delegated.pet, 1);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.sent.size(), 1u);

        tested.Release(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        tested.Press(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.world.Runtime().panels.character, 0);
        EXPECT_EQ(delegated.pet, 1);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(delegated.pet, 0);

        tested.Release(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        tested.Press(Key::C);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(tested.sent.size(), 1u);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        ASSERT_EQ(tested.sent.size(), 2u);
        EXPECT_TRUE(SamePacket(tested.sent[1], DarkHorseRequest()));
    }
}

// 0x5e4583..0x5e4a5a: with the character open, each step opens and closes the own record (3848 rand()) even without
// points; the mouse over the window belongs to the UI.
TEST(GameCharacterWindowTest, TheOpenWindowOpensTheOwnRecordInEachStep)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Step(WindowX + 0xbd, WindowY + 0x100).HasValue());
        EXPECT_EQ(tested.random.State(), expected.State()) << map;
        EXPECT_EQ(tested.world.Runtime().ui.mouseOnUi, 1);
        EXPECT_TRUE(tested.sent.empty());

        ASSERT_TRUE(tested.Step(WindowX - 1, WindowY + 0x100).HasValue());
        EXPECT_EQ(tested.world.Runtime().ui.mouseOnUi, 0);
    }
}

// 0x5e4638..0x5e4a54: with points (+0x68), the click on button i sends F3 06 i and sounds 0x19. Without the Dark Lord
// there are four buttons: the fifth sends nothing.
TEST(GameCharacterWindowTest, TheStatButtonsSendTheirPoint)
{
    for (const std::int32_t map : Maps)
    {
        for (std::int32_t button = 0; button < 5; ++button)
        {
            for (const std::uint8_t heroClass : {KnightClass, DarkLordClass})
            {
                SteppingCharacter tested(map);
                tested.OpenTheWindow();
                tested.world.Hero().characterClass = heroClass;
                tested.world.OwnCharacter().levelUpPoints = 2;
                tested.Click();
                const CrtRandom expected = Advanced(tested.random, BaseStepRandoms + OwnRecordCloseRandoms);
                const auto stepped = tested.Step(WindowX + 0x7d, WindowY + 0x73 + 0x3c * button + 0x17);
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_EQ(tested.random.State(), expected.State());
                if (button == 4 && heroClass != DarkLordClass)
                {
                    EXPECT_TRUE(tested.sent.empty());
                    continue;
                }
                ASSERT_EQ(tested.sent.size(), 1u) << button;
                EXPECT_TRUE(SamePacket(tested.sent[0],
                                       Client::Protocol::Player::WriteLevelUpPoint(static_cast<std::uint8_t>(button))));
                EXPECT_EQ(tested.world.Runtime().lastSoundId, Client::Audio::SoundId{0x19});
                EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
            }
        }
    }
}

// 0x5e461b: without points, the buttons do not look at the click.
TEST(GameCharacterWindowTest, WithoutPointsTheStatButtonsDoNothing)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        tested.Click();
        ASSERT_TRUE(tested.Step(WindowX + 0x80, WindowY + 0x76).HasValue());
        EXPECT_TRUE(tested.sent.empty());
        EXPECT_FALSE(tested.world.Runtime().lastSoundId.has_value());
    }
}

// 0x5e4c3f..0x5e4ca9: the close button takes the click, sets the mouse counter to 0 with its limit at 6 and closes
// the character.
TEST(GameCharacterWindowTest, TheCloseButtonClosesTheCharacter)
{
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        tested.world.Runtime().mouse.updateTime = 3;
        tested.Click();
        ASSERT_TRUE(tested.Step(WindowX + 0x19, WindowY + 0x18b).HasValue());
        EXPECT_EQ(tested.world.Runtime().panels.character, 0);
        EXPECT_FALSE(tested.world.Runtime().mouse.pendingClick);
        EXPECT_EQ(tested.world.Runtime().mouse.updateTime, 0);
        EXPECT_EQ(tested.world.Runtime().mouse.updateTimeMax, 6);
    }
}

// 0x424230 and 0x659fd9 (CL9 milestone 4): the pet button opens it (0x723911 at 2) and asks for the raven and the
// horse; with the window open, at 0x104 from the character, the click on the other tab chooses it, the one on the
// chosen one (higher) is only swallowed, and the close button closes it, so the next step asks for the horse again.
// No extra rand().
TEST(GameCharacterWindowTest, ThePetWindowOpensChoosesItsTabAndCloses)
{
    constexpr std::int32_t Left = 0x104;
    for (const std::int32_t map : Maps)
    {
        SteppingCharacter tested(map);
        tested.OpenTheWindow();
        Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
        tested.Click();
        ASSERT_TRUE(tested.Step(0x220, 0x190).HasValue());
        EXPECT_EQ(runtime.panels.delegated.pet, 2);
        ASSERT_EQ(tested.sent.size(), 2u);
        EXPECT_TRUE(SamePacket(tested.sent[0], Client::Protocol::Pets::WritePetInfoRequest(
                                                   Client::Protocol::Pets::LoginScenePetInfoRequest)));
        EXPECT_TRUE(SamePacket(tested.sent[1], DarkHorseRequest()));

        tested.Click();
        // IsOpen(0) (0x5a5511) stops at the pet (0x400) before the NPC shop (0x10000): one rand() fewer.
        const CrtRandom expected = Advanced(tested.random, BaseStepRandoms - 1 + OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Step(Left + 15 + 45, 67).HasValue());
        EXPECT_EQ(runtime.petTab, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(runtime.ui.mouseOnUi, 1);
        EXPECT_EQ(tested.random.State(), expected.State()) << map;

        tested.Click();
        ASSERT_TRUE(tested.Step(Left + 15 + 49, 66).HasValue());
        EXPECT_EQ(runtime.petTab, 1) << "la otra pestaña empieza en 67";
        tested.Click();
        ASSERT_TRUE(tested.Step(Left + 15 + 45 + 49, 63).HasValue());
        EXPECT_EQ(runtime.petTab, 1);
        EXPECT_FALSE(runtime.mouse.pendingClick) << "la elegida se come el clic desde 63";

        tested.Click();
        ASSERT_TRUE(tested.Step(Left + 20 + 23, 395 + 23).HasValue());
        EXPECT_EQ(runtime.panels.delegated.pet, 0);
        EXPECT_EQ(tested.sent.size(), 2u);
        ASSERT_TRUE(tested.Step(320, 240).HasValue());
        EXPECT_EQ(runtime.panels.delegated.pet, 1);
        ASSERT_EQ(tested.sent.size(), 3u);
        EXPECT_TRUE(SamePacket(tested.sent[2], DarkHorseRequest()));
    }
}
} // namespace
