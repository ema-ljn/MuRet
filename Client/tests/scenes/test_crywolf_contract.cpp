#include "Scenes/CrywolfContract.h"

#include "Core/Expected.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include "support/Bytes.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

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

// CL9 hito 11, fase 4 (06 § La interfaz de Crywolf): el contrato de los altares, 0x435b00, 0x435ad0, el paso de su
// ventana (0x435c5c..0x436220) y el clic en un altar (0x567b00..0x567c23), contra el ASM de c32bbd5f (*estático*: GS
// 0.97 no tiene Crywolf). Text.bmd es inventado. Nada de esto saca rand().
namespace
{
using Client::Network::Crypto::Xor32Decode;
using Client::Scenes::ActorState;
using Client::Scenes::AltarClick;
using Client::Scenes::ContractMessage;
using Client::Scenes::CrywolfInterface;
using Client::Scenes::WorldRuntime;
using Client::Scenes::WorldState;
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
        return Client::Scenes::ShowTheContractMessage(Runtime(), &texts, message);
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, std::uint16_t pet = 0xffff)
    {
        return Client::Scenes::StepTheContractWindow(Client::Scenes::ContractWindowContext{
            .runtime = Runtime(),
            .texts = &texts,
            .heroPet = pet,
            .mouseX = x,
            .mouseY = y,
            .send = [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); }});
    }

    void Click()
    {
        Runtime().movement.leftHeld = true;
        Runtime().movement.pendingClick = true;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::Data::Text::TextTable texts = Client::Tests::MakeTextTable(ContractTexts());
    std::vector<Client::Protocol::Outgoing> sent;
};

// 0x435b00: solo con la etapa 3 o 4 en el 34 y sin la salida pedida. Con otro mensaje que 0x38, el texto 1950 + n y la
// ventana 2 (la 1 con 0x39); el renglón 0 vacía el 1 y el valor queda en 0x723be0.
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
            ASSERT_TRUE(Show({.message = Client::Scenes::ContractRefusedMessage, .value = 7}).HasValue());
            const bool shown = map == CrywolfMap && stage != 2;
            EXPECT_EQ(Ui().byte723a44, shown ? 2 : 0);
            EXPECT_EQ(LineOf(Ui().contractLines[0]), shown ? "no se firmo" : "");
            EXPECT_EQ(Ui().contractLines[1][0], shown ? '\0' : 'x');
            EXPECT_EQ(Ui().dwords723bdc[1], shown ? 7 : 0);
        }
    }
    Enter(CrywolfMap, 3);
    Runtime().ui.logoutRequested = true;
    ASSERT_TRUE(Show({.message = Client::Scenes::ContractRefusedMessage}).HasValue());
    EXPECT_EQ(Ui().byte723a44, 0);
}

// 0x435b33..0x435b5f: 0x38 da wsprintfA con 2006 y el nibble bajo del altar, y la ventana 1 sin pasar por la 2; 0x39
// copia 2007 en el renglón 1 y deja la ventana en 1. strcpy deja detrás lo que había.
TEST_F(CrywolfContractTest, TheOfferFormatsTheContractsLeft)
{
    Enter(CrywolfMap, 4);
    Ui().altars[3] = 0x05;
    std::string old(30, 'z');
    std::copy(old.begin(), old.end(), Ui().contractLines[0].begin());
    ASSERT_TRUE(Show({.message = Client::Scenes::AltarOfferMessage, .line = 0, .value = 0x13f, .altar = 3}).HasValue());
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "quedan 5 contratos");
    EXPECT_EQ(Ui().contractLines[0][19], 'z');
    ASSERT_TRUE(Show({.message = Client::Scenes::AltarQuestionMessage, .line = 1, .value = 0x13f}).HasValue());
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "quedan 5 contratos");
    EXPECT_EQ(LineOf(Ui().contractLines[1]), "firmas el contrato?");
    EXPECT_EQ(Ui().dwords723bdc[1], 0x13f);
}

// strcpy y wsprintfA no tienen tope: un texto de 200 bytes o más pisaría lo que sigue, y corta. También un renglón o un
// altar fuera de sus tablas.
TEST_F(CrywolfContractTest, WhatDoesNotFitIsCut)
{
    Enter(CrywolfMap, 3);
    texts = Client::Tests::MakeTextTable({{1952, std::string(199, 'a')}, {1953, std::string(200, 'b')}});
    ASSERT_TRUE(Show({.message = Client::Scenes::ContractRefusedMessage}).HasValue());
    EXPECT_EQ(LineOf(Ui().contractLines[0]).size(), 199u);
    const auto longer = Show({.message = Client::Scenes::ContractSignedMessage});
    ASSERT_FALSE(longer.HasValue());
    EXPECT_NE(longer.Error().find("0x435b00"), std::string::npos) << longer.Error();
    const auto line = Show({.message = Client::Scenes::ContractRefusedMessage, .line = 2});
    ASSERT_FALSE(line.HasValue());
    EXPECT_NE(line.Error().find("0x723a48"), std::string::npos) << line.Error();
    const auto altar = Show({.message = Client::Scenes::AltarOfferMessage, .altar = -1});
    ASSERT_FALSE(altar.HasValue());
    EXPECT_NE(altar.Error().find("0x435b39"), std::string::npos) << altar.Error();
    const auto missing =
        Client::Scenes::ShowTheContractMessage(Runtime(), nullptr, {.message = Client::Scenes::ContractLevelMessage});
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("2008"), std::string::npos) << missing.Error();
}

// 0x435ad0: el byte del altar con la etapa 3 o 4 en el 34, sin mirar la salida; fuera de 0..4, dentro de la guarda, no
// escribe y da false (el original escribe en la .data de al lado, Q-C1347).
TEST_F(CrywolfContractTest, TheAltarIsSetOnlyInTheDefense)
{
    for (const std::int32_t map : {0, 2, CrywolfMap})
    {
        SCOPED_TRACE(map);
        Enter(map, 3);
        Runtime().ui.logoutRequested = true;
        const auto before = Ui().altars;
        EXPECT_TRUE(Client::Scenes::SetTheAltar(Runtime(), 4, 0x13));
        EXPECT_EQ(Ui().altars[4], map == CrywolfMap ? 0x13 : 0x02);
        EXPECT_EQ(Client::Scenes::SetTheAltar(Runtime(), 5, 1), map != CrywolfMap);
        EXPECT_EQ(Client::Scenes::SetTheAltar(Runtime(), -1, 1), map != CrywolfMap);
        auto expected = before;
        if (map == CrywolfMap)
            expected[4] = 0x13;
        EXPECT_EQ(Ui().altars, expected);
    }
    Enter(CrywolfMap, 5);
    EXPECT_TRUE(Client::Scenes::SetTheAltar(Runtime(), -1, 1));
}

// 0x435c6b..0x435cb4 y 0x4361b4: el No deja 0x723bdc en 1 y pasa a la ventana 2 con 1952; la ventana 1 se queda con
// cualquier clic, pero sin el botón no hace nada.
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
        EXPECT_FALSE(Runtime().movement.leftHeld);
        EXPECT_FALSE(Runtime().movement.pendingClick);
        EXPECT_TRUE(sent.empty());
    }
    Enter(CrywolfMap, 3);
    Ui().byte723a44 = 1;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step(0x150, 0x100).HasValue());
    EXPECT_TRUE(Runtime().movement.pendingClick);
    EXPECT_EQ(Ui().dwords723bdc[0], 0);
}

// 0x435cf2..0x4361a2: el Sí manda el C1 06 BD 03 con la clave de 0x723be0 y deja 0x723bdc en 2; con la mascota 0x1c14,
// 0x1c15 o 0x1c37, 1956 en la ventana 2 y nada sale. El mouse en el Sí sin el botón no apaga el flanco.
TEST_F(CrywolfContractTest, TheAcceptSendsTheAltarKey)
{
    Enter(CrywolfMap, 4);
    Ui().byte723a44 = 1;
    Ui().dwords723bdc[1] = 0x013d;
    Click();
    ASSERT_TRUE(Step(0xfb, 0x117).HasValue());
    EXPECT_EQ(Ui().dwords723bdc[0], 2);
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_FALSE(Runtime().movement.leftHeld);
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
        EXPECT_FALSE(Runtime().movement.pendingClick);
    }
    sent.clear();
    Enter(CrywolfMap, 4);
    Ui().byte723a44 = 1;
    Runtime().movement.pendingClick = true;
    ASSERT_TRUE(Step(0x100, 0x100).HasValue());
    EXPECT_TRUE(Runtime().movement.pendingClick);
    EXPECT_TRUE(sent.empty());
}

// 0x4361dc..0x436216: la ventana 2 solo toma el clic del OK, que deja 0x723bdc en 3.
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
        EXPECT_EQ(Runtime().movement.leftHeld, !confirmed);
        EXPECT_EQ(Runtime().movement.pendingClick, !confirmed);
        EXPECT_EQ(Ui().byte723a44, 2);
    }
}

// 0x567b00..0x567c23: fuera de la defensa en el 34 o con otro tipo, no es un altar; sin una elfa, el C1 30 de
// 0x5650c0; con una elfa, 0x34 con el altar contratado, 0x35 sin contratos y 0x38 + 0x39 con la clave con signo.
TEST_F(CrywolfContractTest, TheAltarClickShowsTheOfferToAnElf)
{
    ActorState hero;
    hero.characterClass = 0x12; // la elfa de la segunda clase: & 7 da 2.
    ActorState altar;
    altar.type = 0x206;
    altar.key = 0x013e;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Enter(map, 3);
        const auto click = Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar);
        ASSERT_TRUE(click.HasValue());
        EXPECT_EQ(click.Value(), AltarClick::NotAnAltar);
    }
    Enter(CrywolfMap, 3);
    ActorState other = altar;
    for (const std::uint16_t type : {std::uint16_t{0x203}, std::uint16_t{0x209}})
    {
        other.type = type;
        EXPECT_EQ(Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, hero, other).Value(), AltarClick::NotAnAltar);
    }
    ActorState knight;
    knight.characterClass = 1;
    EXPECT_EQ(Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, knight, altar).Value(), AltarClick::Talk);
    EXPECT_EQ(Ui().byte723a44, 0);

    Ui().altars[2] = 0x13;
    EXPECT_EQ(Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(), AltarClick::Contract);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "altar ocupado");
    EXPECT_EQ(Ui().byte723a44, 2);

    Enter(CrywolfMap, 4);
    Ui().altars[2] = 0x20;
    EXPECT_EQ(Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(), AltarClick::Contract);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "altar agotado");
    EXPECT_EQ(Ui().byte723a44, 2);

    Enter(CrywolfMap, 4);
    Ui().altars[2] = 0x02;
    altar.key = 0x8140;
    EXPECT_EQ(Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(), AltarClick::Contract);
    EXPECT_EQ(LineOf(Ui().contractLines[0]), "quedan 2 contratos");
    EXPECT_EQ(LineOf(Ui().contractLines[1]), "firmas el contrato?");
    EXPECT_EQ(Ui().byte723a44, 1);
    EXPECT_EQ(Ui().dwords723bdc[1], -0x7ec0);

    // Con la salida pedida, 0x435b00 no hace nada pero el clic sigue en 0x569573.
    Enter(CrywolfMap, 4);
    Runtime().ui.logoutRequested = true;
    EXPECT_EQ(Client::Scenes::ClickTheCrywolfAltar(Runtime(), &texts, hero, altar).Value(), AltarClick::Contract);
    EXPECT_EQ(Ui().byte723a44, 0);
}

// 0x4eac0a..0x4eac41: la bandera del A* con la etapa 3 o 4 en el 34 y un altar (0x204..0x208, el word +2 con signo) en
// 0x6c8e54, vivo o no. En Lorencia, Devias, con otra etapa, otro tipo o sin NPC elegido (Q-C1350), sin bandera.
TEST_F(CrywolfContractTest, TheAltarGoalNeedsTheDefenseInCrywolfAndAChosenAltar)
{
    constexpr std::size_t AltarSlot = 9;
    const auto goal = [this](std::int32_t map, std::uint8_t stage, std::int32_t chosen, std::uint16_t type)
    {
        Enter(map, stage);
        Runtime().movement.chosenNpc = chosen;
        world->Actors()[AltarSlot] = ActorState{};
        world->Actors()[AltarSlot].type = type;
        return Client::Scenes::CrywolfAltarGoal(*world);
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
    // Otro registro en 0x6c8e54: mira el elegido, no el altar.
    EXPECT_FALSE(goal(CrywolfMap, 4, slot + 1, 0x205));
}
} // namespace
