#include "Network/Xor32Packet.h"
#include "Events/Crywolf/CrywolfContract.h"
#include "Actors/WalkPath.h"

#include "Core/Expected.h"
#include "Tables/TextTable.h"
#include "Protocol/Outgoing.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Ui/GameUiState.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// CL9 milestone 11, phase 4 (06 § La interfaz de Crywolf): the altar contract, 0x435b00, 0x435ad0, its window's step
// (0x435c5c..0x436220) and the click on an altar (0x567b00..0x567c23), against the ASM of c32bbd5f (*static*: GS 0.97
// has no Crywolf). Text.bmd is made up. None of this draws rand().
namespace
{
using Client::Events::Crywolf::AltarClick;
using Client::Events::Crywolf::ContractMessage;
using Client::Kernel::ActorState;
using Client::Kernel::CrywolfInterface;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;
using Client::Network::Xor32Decode;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;

constexpr std::int32_t CrywolfMap = 34;

std::map<std::size_t, std::string> ContractTexts()
{
    return {{1952, "no se firmo"},         {1953, "firmado"},           {1954, "el altar es tuyo"},
            {1956, "sin mascota"},         {2002, "altar ocupado"},     {2003, "altar agotado"},
            {2004, "no alcanzo"},          {2005, "proba de nuevo"},    {2006, "quedan %d contratos"},
            {2007, "firmas el contrato?"}, {2008, "nivel insuficiente"}};
}

std::string LineOf(const std::array<char, 200>& line)
{
    return std::string(line.data());
}

class CrywolfContractTest : public ::testing::Test
{
protected:
    WorldRuntime& Runtime()
    {
        return world->Runtime();
    }

    CrywolfInterface& Ui()
    {
        return Runtime().crywolf.ui;
    }

    void Enter(std::int32_t map, std::uint8_t stage)
    {
        Runtime() = WorldRuntime{};
        Runtime().map = map;
        Runtime().crywolf.stage = stage;
    }

    Client::Core::Expected<void, std::string> Show(const ContractMessage& message)
    {
        return Client::Events::Crywolf::ShowTheContractMessage(Runtime(), &texts, message);
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, std::uint16_t pet = 0xffff)
    {
        return Client::Events::Crywolf::StepTheContractWindow(Client::Events::Crywolf::ContractWindowContext{
            .runtime = Runtime(),
            .texts = &texts,
            .heroPet = pet,
            .mouseX = x,
            .mouseY = y,
            .send = [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }});
    }

    void Click()
    {
        Runtime().mouse.leftHeld = true;
        Runtime().mouse.pendingClick = true;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(ContractTexts());
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x435a00 && 0x4303c0: the stage 3 or 4, in the map 34.
TEST_F(CrywolfContractTest, TheDefenseNeedsTheStageAndTheMap)
{
    constexpr std::int32_t AidaMap = 33;
    constexpr std::int32_t MapAfterCrywolf = 35;
    for (int stage = 0; stage <= 0xff; ++stage)
    {
        Enter(CrywolfMap, static_cast<std::uint8_t>(stage));
        EXPECT_EQ(Client::Events::Crywolf::InTheCrywolfDefense(Runtime()), stage == 3 || stage == 4)
            << "stage " << stage;
    }
    for (const std::int32_t map : {0, AidaMap, MapAfterCrywolf})
    {
        for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{3, 4})
        {
            Enter(map, stage);
            EXPECT_FALSE(Client::Events::Crywolf::InTheCrywolfDefense(Runtime()))
                << "map " << map << ", stage " << int{stage};
        }
    }
}

// 0x435b00: only with stage 3 or 4 in 34 and without the requested exit. With a message other than 0x38, text
// 1950 + n and window 2 (1 with 0x39); line 0 empties line 1 and the value stays in 0x723be0.
TEST_F(CrywolfContractTest, TheMessageNeedsTheDefenseInCrywolf)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        for (const std::uint8_t stage : {std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(static_cast<int>(stage));
            Enter(map, stage);
            Ui().contractLines[1][0] = 'x';
            ASSERT_TRUE(Show({.message = Client::Events::Crywolf::ContractRefusedMessage, .value = 7}).HasValue());
            const bool shown = map == CrywolfMap && stage != 2;
            EXPECT_EQ(Ui().byte723a44, shown ? 2 : 0);
            EXPECT_EQ(LineOf(Ui().contractLines[0]), shown ? "no se firmo" : "");
            EXPECT_EQ(Ui().contractLines[1][0], shown ? '\0' : 'x');
            EXPECT_EQ(Ui().dwords723bdc[1], shown ? 7 : 0);
        }
    }
    Enter(CrywolfMap, 3);
    Runtime().ui.logoutRequested = true;
    ASSERT_TRUE(Show({.message = Client::Events::Crywolf::ContractRefusedMessage}).HasValue());
    EXPECT_EQ(Ui().byte723a44, 0);
}

// 0x435b33..0x435b5f: 0x38 gives wsprintfA with 2006 and the altar's low nibble, and window 1 without going through
// 2; 0x39 copies 2007 into line 1 and leaves the window at 1. strcpy leaves behind what was there.
TEST_F(CrywolfContractTest, TheOfferFormatsTheContractsLeft)
{
    Enter(CrywolfMap, 4);
    Ui().altars[3] = 0x05;
    std::string old(30, 'z');
    std::copy(old.begin(), old.end(), Ui().contractLines[0].begin());
    ASSERT_TRUE(Show({.message = Client::Events::Crywolf::AltarOfferMessage, .line = 0, .value = 0x13f, .altar = 3})
                    .HasValue());
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "quedan 5 contratos");
    EXPECT_EQ(Ui().contractLines[0][19], 'z');
    ASSERT_TRUE(Show({.message = Client::Events::Crywolf::AltarQuestionMessage, .line = 1, .value = 0x13f}).HasValue());
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "quedan 5 contratos");
    EXPECT_EQ(LineOf(Ui().contractLines[1]), "firmas el contrato?");
    EXPECT_EQ(Ui().dwords723bdc[1], 0x13f);
}

// strcpy and wsprintfA have no bound: a text of 200 bytes or more would overwrite what follows, and it stops. So does a
// line or an altar outside its tables.
TEST_F(CrywolfContractTest, WhatDoesNotFitIsCut)
{
    Enter(CrywolfMap, 3);
    texts = Client::Tests::MakeTextTable({{1952, std::string(199, 'a')}, {1953, std::string(200, 'b')}});
    ASSERT_TRUE(Show({.message = Client::Events::Crywolf::ContractRefusedMessage}).HasValue());
    EXPECT_EQ(LineOf(Ui().contractLines[0]).size(), 199u);
    const auto longer = Show({.message = Client::Events::Crywolf::ContractSignedMessage});
    ASSERT_FALSE(longer.HasValue());
    EXPECT_NE(longer.Error().find("0x435b00"), std::string::npos) << longer.Error();
    const auto line = Show({.message = Client::Events::Crywolf::ContractRefusedMessage, .line = 2});
    ASSERT_FALSE(line.HasValue());
    EXPECT_NE(line.Error().find("0x723a48"), std::string::npos) << line.Error();
    const auto altar = Show({.message = Client::Events::Crywolf::AltarOfferMessage, .altar = -1});
    ASSERT_FALSE(altar.HasValue());
    EXPECT_NE(altar.Error().find("0x435b39"), std::string::npos) << altar.Error();
    const auto missing = Client::Events::Crywolf::ShowTheContractMessage(
        Runtime(), nullptr, {.message = Client::Events::Crywolf::ContractLevelMessage});
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("2008"), std::string::npos) << missing.Error();
}

// 0x435ad0: the altar's byte with stage 3 or 4 in 34, ignoring the exit; outside 0..4, inside the guard, it does not
// write and gives false (the original writes into the neighbouring .data, Q-C1347).
TEST_F(CrywolfContractTest, TheAltarIsSetOnlyInTheDefense)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        Enter(map, 3);
        Runtime().ui.logoutRequested = true;
        const auto before = Ui().altars;
        EXPECT_TRUE(Client::Events::Crywolf::SetTheAltar(Runtime(), 4, 0x13));
        EXPECT_EQ(Ui().altars[4], map == CrywolfMap ? 0x13 : 0x02);
        EXPECT_EQ(Client::Events::Crywolf::SetTheAltar(Runtime(), 5, 1), map != CrywolfMap);
        EXPECT_EQ(Client::Events::Crywolf::SetTheAltar(Runtime(), -1, 1), map != CrywolfMap);
        auto expected = before;
        if (map == CrywolfMap)
            expected[4] = 0x13;
        EXPECT_EQ(Ui().altars, expected);
    }
    Enter(CrywolfMap, 5);
    EXPECT_TRUE(Client::Events::Crywolf::SetTheAltar(Runtime(), -1, 1));
}

// 0x435c6b..0x435cb4 and 0x4361b4: the No leaves 0x723bdc at 1 and goes to window 2 with 1952; window 1 keeps any
// click, but without the button it does nothing.
TEST_F(CrywolfContractTest, TheQuestionTakesEveryClick)
{
    for (const auto& [x, y, refused] :
         {std::tuple{0x14b, 0xfb, true}, std::tuple{0x17f, 0x117, true}, std::tuple{0x14a, 0x100, false},
          std::tuple{0x150, 0x118, false}, std::tuple{100, 100, false}})
    {
        SCOPED_TRACE(x);
        SCOPED_TRACE(y);
        Enter(CrywolfMap, 3);
        Ui().byte723a44 = 1;
        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(Ui().dwords723bdc[0], refused ? 1 : 0);
        EXPECT_EQ(Ui().byte723a44, refused ? 2 : 1);
        EXPECT_EQ(LineOf(Ui().contractLines[0]), refused ? "no se firmo" : "");
        EXPECT_FALSE(Runtime().mouse.leftHeld);
        EXPECT_FALSE(Runtime().mouse.pendingClick);
        EXPECT_TRUE(sent.empty());
    }
    Enter(CrywolfMap, 3);
    Ui().byte723a44 = 1;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step(0x150, 0x100).HasValue());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_EQ(Ui().dwords723bdc[0], 0);
}

// 0x435cf2..0x4361a2: the Yes sends C1 06 BD 03 with the key of 0x723be0 and leaves 0x723bdc at 2; with pet 0x1c14,
// 0x1c15 or 0x1c37, 1956 in window 2 and nothing goes out. The mouse on the Yes without the button does not clear the
// edge.
TEST_F(CrywolfContractTest, TheAcceptSendsTheAltarKey)
{
    Enter(CrywolfMap, 4);
    Ui().byte723a44 = 1;
    Ui().dwords723bdc[1] = 0x013d;
    Click();
    ASSERT_TRUE(Step(0xfb, 0x117).HasValue());
    EXPECT_EQ(Ui().dwords723bdc[0], 2);
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_FALSE(Runtime().mouse.leftHeld);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(sent[0].packet.Bytes(), Bytes({0xC1, 0x06, 0xBD, 0x37, 0x8A, 0x05})));
    EXPECT_TRUE(SameBytes(Xor32Decode(sent[0].packet).Bytes(), Bytes({0xC1, 0x06, 0xBD, 0x03, 0x01, 0x3D})));
    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}, std::uint16_t{0x1c37}})
    {
        SCOPED_TRACE(pet);
        sent.clear();
        Enter(CrywolfMap, 4);
        Ui().byte723a44 = 1;
        Click();
        ASSERT_TRUE(Step(0x12f, 0xfb, pet).HasValue());
        EXPECT_TRUE(sent.empty());
        EXPECT_EQ(Ui().dwords723bdc[0], 0);
        EXPECT_EQ(Ui().byte723a44, 2);
        EXPECT_EQ(LineOf(Ui().contractLines[0]), "sin mascota");
        EXPECT_FALSE(Runtime().mouse.pendingClick);
    }
    sent.clear();
    Enter(CrywolfMap, 4);
    Ui().byte723a44 = 1;
    Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Step(0x100, 0x100).HasValue());
    EXPECT_TRUE(Runtime().mouse.pendingClick);
    EXPECT_TRUE(sent.empty());
}

// 0x4361dc..0x436216: window 2 only takes the OK's click, which leaves 0x723bdc at 3.
TEST_F(CrywolfContractTest, TheNoticeOnlyTakesTheOk)
{
    for (const auto& [x, y, confirmed] : {std::tuple{0x123, 0xfb, true}, std::tuple{0x157, 0x117, true},
                                          std::tuple{0x122, 0x100, false}, std::tuple{0x14b, 0x118, false}})
    {
        SCOPED_TRACE(x);
        SCOPED_TRACE(y);
        Enter(CrywolfMap, 3);
        Ui().byte723a44 = 2;
        Click();
        ASSERT_TRUE(Step(x, y).HasValue());
        EXPECT_EQ(Ui().dwords723bdc[0], confirmed ? 3 : 0);
        EXPECT_EQ(Runtime().mouse.leftHeld, !confirmed);
        EXPECT_EQ(Runtime().mouse.pendingClick, !confirmed);
        EXPECT_EQ(Ui().byte723a44, 2);
    }
}

// 0x567b00..0x567c23: outside the defense in 34 or with another type, it is not an altar; without an elf, the C1 30 of
// 0x5650c0; with an elf, 0x34 with the altar contracted, 0x35 with no contracts and 0x38 + 0x39 with the signed key.
TEST_F(CrywolfContractTest, TheAltarClickShowsTheOfferToAnElf)
{
    ActorState hero;
    hero.characterClass = 0x12; // the second-class elf: & 7 gives 2.
    ActorState altar;
    altar.type = 0x206;
    altar.key = 0x013e;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Enter(map, 3);
        const auto click = Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar);
        ASSERT_TRUE(click.HasValue());
        EXPECT_EQ(click.Value(), AltarClick::NotAnAltar);
    }
    Enter(CrywolfMap, 3);
    ActorState other = altar;
    for (const std::uint16_t type : {std::uint16_t{0x203}, std::uint16_t{0x209}})
    {
        other.type = type;
        EXPECT_EQ(Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, hero, other).Value(),
                  AltarClick::NotAnAltar);
    }
    ActorState knight;
    knight.characterClass = 1;
    EXPECT_EQ(Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, knight, altar).Value(),
              AltarClick::Talk);
    EXPECT_EQ(Ui().byte723a44, 0);

    Ui().altars[2] = 0x13;
    EXPECT_EQ(Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(),
              AltarClick::Contract);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "altar ocupado");
    EXPECT_EQ(Ui().byte723a44, 2);

    Enter(CrywolfMap, 4);
    Ui().altars[2] = 0x20;
    EXPECT_EQ(Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(),
              AltarClick::Contract);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "altar agotado");
    EXPECT_EQ(Ui().byte723a44, 2);

    Enter(CrywolfMap, 4);
    Ui().altars[2] = 0x02;
    altar.key = 0x8140;
    EXPECT_EQ(Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(),
              AltarClick::Contract);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "quedan 2 contratos");
    EXPECT_EQ(LineOf(Ui().contractLines[1]), "firmas el contrato?");
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_EQ(Ui().dwords723bdc[1], -0x7ec0);

    // With the requested exit, 0x435b00 does nothing but the click goes on at 0x569573.
    Enter(CrywolfMap, 4);
    Runtime().ui.logoutRequested = true;
    EXPECT_EQ(Client::Events::Crywolf::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(),
              AltarClick::Contract);
    EXPECT_EQ(Ui().byte723a44, 0);
}

// 0x4eac0a..0x4eac41: the A* flag with stage 3 or 4 in 34 and an altar (0x204..0x208, the signed word +2) in 0x6c8e54,
// alive or not. In Lorencia, Devias, with another stage, another type or with no chosen NPC (Q-C1350), no flag.
TEST_F(CrywolfContractTest, TheAltarGoalNeedsTheDefenseInCrywolfAndAChosenAltar)
{
    constexpr std::size_t AltarSlot = 9;
    const auto goal = [this](std::int32_t map, std::uint8_t stage, std::int32_t chosen, std::uint16_t type)
    {
        Enter(map, stage);
        Runtime().chosenNpc.index = chosen;
        world->Actors()[AltarSlot] = ActorState{};
        world->Actors()[AltarSlot].type = type;
        return Client::Actors::CrywolfAltarGoal(*world);
    };
    const auto slot = static_cast<std::int32_t>(AltarSlot);
    for (const std::uint8_t stage : {std::uint8_t{3}, std::uint8_t{4}})
    {
        for (const std::uint16_t type : {std::uint16_t{0x204}, std::uint16_t{0x206}, std::uint16_t{0x208}})
        {
            SCOPED_TRACE(type);
            EXPECT_TRUE(goal(CrywolfMap, stage, slot, type));
            EXPECT_FALSE(goal(0, stage, slot, type));
            EXPECT_FALSE(goal(2, stage, slot, type));
        }
    }
    for (const std::uint8_t stage : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{5}})
        EXPECT_FALSE(goal(CrywolfMap, stage, slot, 0x205)) << int{stage};
    for (const std::uint16_t type : {std::uint16_t{0x203}, std::uint16_t{0x209}, std::uint16_t{0x8204}})
        EXPECT_FALSE(goal(CrywolfMap, 4, slot, type)) << type;
    EXPECT_FALSE(goal(CrywolfMap, 4, -1, 0x205));
    // Another record in 0x6c8e54: it looks at the chosen one, not at the altar.
    EXPECT_FALSE(goal(CrywolfMap, 4, slot + 1, 0x205));
}
} // namespace
