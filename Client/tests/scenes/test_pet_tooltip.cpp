#include "Network/Xor32Packet.h"
#include "Tooltips/PetTooltip.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pets/PetInfo.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Tooltips/GameTooltips.h"
#include "Tooltips/ItemTooltips.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/Bytes.h"
#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"
#include "Social/SocialState.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The pets' tooltip, 0x422990 (CL9 milestone 4; 06 § El A9 de las mascotas, "El tooltip de las mascotas"), through
// the item body (0x5b2aa0) with the own record open around it like 0x5f7bc0, at 640 × 480 (ry = 1) and with the fake
// text of RecordingTextDrawer (13 high). The texts are invented. Each test runs in Lorencia and in Devias.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Render::SpriteBatch;
using Client::Tooltips::TooltipLines;
using Client::Ui::GameHudContext;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x5eed4243u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// The price's IsOpen(0x10000) (a protected byte) and a Skill.bmd access that closes at 0.
constexpr std::uint32_t NpcShopQuery = 1;
constexpr auto SkillAccess = static_cast<std::uint32_t>(Client::Tables::SkillRecordSize);
// What was in the bolds before: 0x422990 does not clear them.
constexpr std::int32_t StaleBold = 7;

constexpr std::uint16_t DarkHorse = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
// The horse's skill (the tag of its skill option) with its mana in Skill.bmd (+0x22).
constexpr std::uint8_t HorseSkill = 0x3e;
constexpr std::uint16_t HorseSkillMana = 30;
// The box's top center.
constexpr std::int32_t BoxX = 320;
constexpr std::int32_t BoxY = 300;
// 0x78abb28 and 0x78abb2c: the inventory and the item's cell.
constexpr std::int32_t HoveredSlot = 20;

std::map<std::size_t, std::string> PetTooltipTexts()
{
    return {{24, "Senor"},
            {61, "Si: %s"},
            {63, "Precio %s"},
            {64, "Velocidad %d"},
            {70, "Aguante %d"},
            {74, "Faltan %d"},
            {76, "Nivel pedido %d"},
            {88, "Opcion +%d"},
            {201, "Exp %u/%u"},
            {203, "Golpe %d~%d (%d)"},
            {368, "Nivel"},
            {423, "Sin zen"},
            {1101, "Sin precio"},
            {698, "Mando %d"},
            {744, "Defensa total %d"},
            {959, "Defensa %d"},
            {1187, "Corcel"},
            {1188, "Habilidad %d"},
            {1189, "Temblor %d"},
            {1214, "Ave"}};
}

Client::Tables::SkillTable HorseSkills()
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    const std::size_t mana = std::size_t{HorseSkill} * Client::Tables::SkillRecordSize + 0x22;
    plain[mana] = std::byte{HorseSkillMana & 0xff};
    plain[mana + 1] = std::byte{HorseSkillMana >> 8};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

Client::Tables::ItemTable EmptyItems()
{
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(
        std::vector<std::byte>(Client::Tables::ItemTableSize), Client::Tables::ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

struct Line
{
    std::string text;
    std::int32_t color = 0;
    std::int32_t bold = 0;
};

// The horse as 0x54bf40 leaves it: the skill option and the 0x96 (with an old value) from the second.
ItemState TheHorse()
{
    ItemState item;
    item.id = DarkHorse;
    item.scalar1Low = 0x80;
    item.optionCount = 2;
    item.optionBytes[0] = HorseSkill;
    item.optionBytes[1] = 0x96;
    item.optionBytes[Client::Kernel::ItemOptionValueOffset + 1] = 99;
    return item;
}

// The raven with a 0x50 option.
ItemState TheRaven()
{
    ItemState item;
    item.id = DarkRaven;
    item.scalar1Low = 0x81;
    item.optionCount = 1;
    item.optionBytes[0] = 0x50;
    item.optionBytes[Client::Kernel::ItemOptionValueOffset] = 4;
    return item;
}

class PetTooltipTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    PetTooltipTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Client::Kernel::WorldRuntime& runtime = Runtime();
        runtime.map = GetParam();
        runtime.ui.field78abb28 = 0;
        runtime.ui.field78abb2c = HoveredSlot;
        for (std::int32_t& bold : Lines().bold)
            bold = StaleBold;
        Client::Kernel::OwnCharacterRecord& own = m_world->OwnCharacter();
        own.level = 200;
        own.agility = 40;
        own.leadership = 300;
        own.word36 = 10;
        Client::Kernel::PetStats& horse = runtime.pets[Client::Kernel::DarkHorsePetStats];
        horse.level = 3;
        horse.experience = 1234;
        horse.durability = 200;
        Client::Kernel::PetStats& raven = runtime.pets[Client::Kernel::RavenPetStats];
        raven.level = 2;
        raven.experience = 500;
        raven.durability = 150;
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    TooltipLines& Lines()
    {
        return Runtime().hud.tooltip;
    }

    GameHudContext Context()
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        Client::Kernel::ActorState& hero = m_world->Hero();
        return GameHudContext{.world = *m_world,
                              .random = m_random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = m_sound,
                              .projecting = m_projecting,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = &m_texts,
                              .items = &m_items,
                              .skills = &m_skills,
                              .send = [this](Client::Protocol::Outgoing packet)
                              { m_sent.push_back(std::move(packet)); }};
    }

    // 0x5f7bc0: the item body (0x5b2aa0) with the own record open around it.
    Client::Core::Expected<void, std::string> Hover(ItemState& item, std::int32_t y = BoxY)
    {
        GameHudContext context = Context();
        const Client::Models::LogicalAccess own(Runtime().ownRecordDepth, m_random, OwnRecordCloseRandoms);
        return Client::Tooltips::DrawTheItemTooltip(
            Client::Tooltips::ItemTooltipContext{.hud = context, .x = BoxX, .y = y, .item = item});
    }

    // Two passes: the first requests the A9 and the second draws. Returns the second.
    Client::Core::Expected<void, std::string> HoverTwice(ItemState& item, std::int32_t y = BoxY)
    {
        if (auto first = Hover(item, y); !first.HasValue())
            return first;
        m_text.texts.clear();
        return Hover(item, y);
    }

    static CrtRandom Advanced(CrtRandom from, std::uint32_t count)
    {
        for (std::uint32_t i = 0; i < count; ++i)
            static_cast<void>(from.Next());
        return from;
    }

    void ExpectLines(const std::vector<Line>& expected)
    {
        const TooltipLines& lines = Lines();
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(lines.text[i], expected[i].text) << "renglón " << i;
            EXPECT_EQ(lines.colors[i], expected[i].color) << "renglón " << i;
            EXPECT_EQ(lines.bold[i], expected[i].bold) << "renglón " << i;
        }
        for (std::size_t i = expected.size(); i < Client::Tooltips::TooltipLineCount; ++i)
            EXPECT_TRUE(lines.text[i].empty()) << "renglón " << i;
        // 0x422990 counts in EBX and [EBP−0x24]: 0x78abb30 and 0x78abb34 stay at the 0 of 0x5b2aa0.
        EXPECT_EQ(lines.next, 0);
        EXPECT_EQ(lines.halves, 0);
    }

    // The A9 that went out, in clear.
    std::vector<std::byte> SentBytes(std::size_t index) const
    {
        const Client::Network::Packet decoded = Client::Network::Xor32Decode(m_sent.at(index).packet);
        const std::span<const std::byte> bytes = decoded.Bytes();
        return {bytes.begin(), bytes.end()};
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<Client::Audio::SoundId> m_sound;
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(PetTooltipTexts());
    Client::Tables::ItemTable m_items = EmptyItems();
    Client::Tables::SkillTable m_skills = HorseSkills();
    bool m_projecting = false;
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x4229d4..0x422a12 and 0x423c65..0x42403d: the first time it notes the item, requests its A9 (the pet, 0x78abb28
// and 0x78abb2c) and draws nothing, with the lines already cleared by 0x5b2aa0.
TEST_P(PetTooltipTest, TheFirstHoverAsksForThePetAndDrawsNothing)
{
    Lines().text[4] = "viejo";
    ItemState horse = TheHorse();
    const CrtRandom expected = Advanced(m_random, OwnRecordCloseRandoms);

    const auto drawn = Hover(horse);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(SentBytes(0), Client::Tests::Bytes({0xc1, 0x06, 0xa9, 0x01, 0x00, 20})));
    const Client::Kernel::PetTooltipQuery& query = Runtime().petTooltipQuery;
    EXPECT_EQ(query.key, (static_cast<std::uint32_t>(BoxY) << 16) | static_cast<std::uint32_t>(BoxX));
    EXPECT_EQ(query.type, DarkHorse);
    EXPECT_EQ(query.scalar, 0x80u);
    EXPECT_TRUE(Lines().text[4].empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_random.State(), expected.State());
    // Without the 0x4221d0 or the 0x4225d0 of the drawing pass.
    EXPECT_EQ(Runtime().pets[Client::Kernel::DarkHorsePetStats].nextExperience, 0u);
    EXPECT_EQ(horse.optionBytes[Client::Kernel::ItemOptionValueOffset + 1], 99);
}

// Any change of the place, the type or D+4 asks again; the raven asks for pet 0.
TEST_P(PetTooltipTest, AnotherPlaceTypeOrScalarAsksAgain)
{
    ItemState horse = TheHorse();
    ASSERT_TRUE(Hover(horse).HasValue());
    ASSERT_TRUE(Hover(horse, BoxY + 1).HasValue());
    horse.scalar1Low = 0x88;
    ASSERT_TRUE(Hover(horse, BoxY + 1).HasValue());
    ItemState raven = TheRaven();
    ASSERT_TRUE(Hover(raven, BoxY + 1).HasValue());

    ASSERT_EQ(m_sent.size(), 4u);
    EXPECT_TRUE(Client::Tests::SameBytes(SentBytes(3), Client::Tests::Bytes({0xc1, 0x06, 0xa9, 0x00, 0x00, 20})));
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(Runtime().petTooltipQuery.type, DarkRaven);
}

// The horse's second pass: 0x4221d0, the name, the experience, the level, the durability, the required level (3 · 2 +
// 0xda = 224, and the level 200 hero is missing 24), the classes, the two options (the skill one reads its mana in
// Skill.bmd; 0x4225d0 rewrites the 0x96: 40 / 20 + 2 · 3 + 5), the defense ((3 + 30) / 2) and the skill.
TEST_P(PetTooltipTest, TheSecondHoverDrawsTheHorse)
{
    ItemState horse = TheHorse();
    ASSERT_TRUE(Hover(horse).HasValue());
    const CrtRandom expected = Advanced(m_random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Hover(horse);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(m_sent.size(), 1u);
    ExpectLines({{"Corcel", 1, 1},
                 {"\n", 0, StaleBold},
                 {"\n", 0, StaleBold},
                 {"Exp 1234/89600", 0, StaleBold},
                 {"Nivel : 3", 0, StaleBold},
                 {"Aguante 200", 0, StaleBold},
                 {"Nivel pedido 224", 2, 0},
                 {"Faltan 24", 2, 0},
                 {"\n", 0, StaleBold},
                 {"Si: Senor", 5, StaleBold},
                 {"Temblor 30", 1, 0},
                 {"Defensa 13", 1, 0},
                 {"Defensa total 16", 1, StaleBold},
                 {"Habilidad 2", 1, 0}});
    EXPECT_EQ(horse.optionCount, 2);
    EXPECT_EQ(horse.optionBytes[Client::Kernel::ItemOptionValueOffset + 1], 13);
    EXPECT_EQ(Runtime().pets[Client::Kernel::DarkHorsePetStats].nextExperience, 89600u);
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ownRecordDepth, 0);
    EXPECT_EQ(Runtime().skillDepths[HorseSkill], 0);
    // 0x423c0f..0x423c3d: 14 lines and 11 halves, (14 − 11) · 13 + 11 · 13 / 2 = 110 above y.
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].text, "Corcel");
    EXPECT_EQ(m_text.texts[0].y, BoxY - 110);
}

// With the required level met, the line in white and without what is missing; with no room above, the box goes at y.
TEST_P(PetTooltipTest, AHeroWithTheLevelGetsAWhiteLineAndTheBoxStaysAtYWithoutRoom)
{
    m_world->OwnCharacter().level = 224;
    ItemState horse = TheHorse();

    const auto drawn = HoverTwice(horse, 50);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const TooltipLines& lines = Lines();
    EXPECT_EQ(lines.text[6], "Nivel pedido 224");
    EXPECT_EQ(lines.colors[6], 0);
    EXPECT_EQ(lines.bold[6], 0);
    EXPECT_EQ(lines.text[7], "\n");
    EXPECT_EQ(lines.text[8], "Si: Senor");
    // 13 lines and 11 halves: 26 + 71 = 97, more than 50.
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].y, 50);
}

// 0x423adc..0x423ae7: the horse without options does not enter the loop, and 0x4225d0 neither runs nor adds the 0x96.
TEST_P(PetTooltipTest, AHorseWithoutOptionsSkipsTheLoop)
{
    ItemState horse = TheHorse();
    horse.optionCount = 0;
    const CrtRandom expected = Advanced(m_random, 2 * OwnRecordCloseRandoms + NpcShopQuery);

    const auto drawn = HoverTwice(horse);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(horse.optionCount, 0);
    EXPECT_EQ(Lines().text[10], "Defensa total 16");
    EXPECT_EQ(Lines().text[11], "Habilidad 2");
    EXPECT_EQ(m_random.State(), expected.State());
}

// Q-C1044: with the 0x96 in the first option, each 0x4225d0 of the loop adds another (Q-C415) and the count rises with
// the index. The original goes on until the byte wraps around, writing beyond the item; MuRet cuts when the 0x96 falls
// outside the nine it keeps.
TEST_P(PetTooltipTest, AHorseWithItsDefenseFirstGrowsItsOptionsUntilItCuts)
{
    ItemState horse = TheHorse();
    horse.optionCount = 1;
    horse.optionBytes[0] = 0x96;

    const auto drawn = HoverTwice(horse);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x423b07"), std::string::npos) << drawn.Error();
    EXPECT_EQ(horse.optionCount, Client::Kernel::MaxItemOptions);
    EXPECT_EQ(Lines().text[17], "Defensa 13");
}

// The raven: the damage (+0xc, +0xe and +0x12) and the speed (+0x10) that 0x4221d0 computes with leadership 10 + 300,
// the durability, the required leadership (2 · 15 + 0xb9 = 215, met) and its option, without the horse's defense or
// skill.
TEST_P(PetTooltipTest, TheSecondHoverDrawsTheRaven)
{
    ItemState raven = TheRaven();
    ASSERT_TRUE(Hover(raven).HasValue());
    const CrtRandom expected = Advanced(m_random, OwnRecordCloseRandoms + NpcShopQuery);

    const auto drawn = Hover(raven);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines({{"Ave", 1, 1},
                 {"\n", 0, StaleBold},
                 {"\n", 0, StaleBold},
                 {"Exp 500/35100", 0, StaleBold},
                 {"Nivel : 2", 0, StaleBold},
                 {"Golpe 248~307 (1032)", 0, StaleBold},
                 {"Velocidad 27", 0, StaleBold},
                 {"Aguante 150", 0, StaleBold},
                 {"Mando 215", 0, 0},
                 {"\n", 0, StaleBold},
                 {"Si: Senor", 5, StaleBold},
                 {"Opcion +4", 1, 0}});
    EXPECT_EQ(raven.optionCount, 1);
    EXPECT_EQ(m_random.State(), expected.State());
    // 12 lines and 11 halves: 13 + 71 = 84.
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].y, BoxY - 84);
}

// 0x423839..0x423887: the hero's leadership adds in 16 bits: 65500 + 100 is 64, and 151 are missing.
TEST_P(PetTooltipTest, TheRavenLeadershipAddsInSixteenBits)
{
    Client::Kernel::OwnCharacterRecord& own = m_world->OwnCharacter();
    own.leadership = 65500;
    own.word36 = 100;
    ItemState raven = TheRaven();

    const auto drawn = HoverTwice(raven);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const TooltipLines& lines = Lines();
    EXPECT_EQ(lines.text[8], "Mando 215");
    EXPECT_EQ(lines.colors[8], 2);
    EXPECT_EQ(lines.bold[8], 0);
    EXPECT_EQ(lines.text[9], "Faltan 151");
    EXPECT_EQ(lines.colors[9], 2);
    EXPECT_EQ(lines.bold[9], 0);
    EXPECT_EQ(lines.text[10], "\n");
}

// 0x422a43..0x422ad4: the pet's own price, divided by 3 and truncated to hundreds, before the name.
TEST_P(PetTooltipTest, TheNpcShopShowsThePetPriceBeforeItsName)
{
    for (const bool horse : {true, false})
    {
        SCOPED_TRACE(horse);
        Runtime().panels.npcShop = 1;
        ItemState item = horse ? TheHorse() : TheRaven();
        ASSERT_TRUE(Hover(item).HasValue());
        const CrtRandom expected = Advanced(m_random, OwnRecordCloseRandoms + NpcShopQuery + (horse ? SkillAccess : 0));
        const auto drawn = Hover(item);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(Lines().text[0], horse ? "Precio 2,000,000" : "Precio 666,600");
        EXPECT_EQ(Lines().colors[0], 0);
        EXPECT_EQ(Lines().bold[0], 1);
        EXPECT_EQ(Lines().text[1], "\n");
        EXPECT_EQ(Lines().text[2], horse ? "Corcel" : "Ave");
        EXPECT_EQ(Lines().next, 0);
        EXPECT_EQ(Lines().halves, 0);
        EXPECT_EQ(m_random.State(), expected.State());
    }
}

// 0x422ad9..0x42303d: the pet uses the same maps as the personal price, with local counters.
TEST_P(PetTooltipTest, TheOtherShopPetPriceWarnsWhenZenIsMissing)
{
    Runtime().panels.personalShop = 1;
    Runtime().social.personalShop.mode = 2;
    Runtime().social.personalShop.pricesBuilt = true;
    Runtime().social.personalShop.otherPrices[0x4c] = 1'234'567;
    Runtime().ui.shopGridHovered = 1;
    ItemState horse = TheHorse();
    const auto drawn = HoverTwice(horse);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Lines().text[0], "Precio 1,234,567");
    EXPECT_EQ(Lines().colors[0], 3);
    EXPECT_EQ(Lines().bold[0], 1);
    EXPECT_EQ(Lines().text[1], "\n");
    EXPECT_EQ(Lines().text[2], "Sin zen");
    EXPECT_EQ(Lines().colors[2], 2);
    EXPECT_EQ(Lines().bold[2], 1);
    EXPECT_EQ(Lines().text[3], "\n");
    EXPECT_EQ(Lines().text[4], "Corcel");
    EXPECT_EQ(Lines().next, 0);
    EXPECT_EQ(Lines().halves, 0);
}

// A MuRet intermediate drawing neither notes nor requests, and draws on copies: the pets table and the item stay.
TEST_P(PetTooltipTest, AProjectedDrawNeitherAsksNorWrites)
{
    ItemState& cell = m_world->Inventory().cells[3];
    cell = TheHorse();
    Client::Ui::GameUiState& ui = Runtime().ui;
    ui.hoveredItem = Client::Ui::HoveredItem{.place = Client::Ui::HoveredItem::Place::InventoryCell, .index = 3};
    ui.skillTooltipX = BoxX;
    ui.skillTooltipY = BoxY;
    const auto draw = [this](bool projecting)
    {
        m_projecting = projecting;
        GameHudContext context = Context();
        return Client::Tooltips::DrawTheTooltips(context);
    };

    ASSERT_TRUE(draw(true).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Runtime().petTooltipQuery.type, 0);
    ASSERT_TRUE(draw(false).HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    const auto projected = draw(true);

    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(Lines().text[0], "Corcel");
    EXPECT_EQ(Runtime().pets[Client::Kernel::DarkHorsePetStats].nextExperience, 0u);
    EXPECT_EQ(cell.optionBytes[Client::Kernel::ItemOptionValueOffset + 1], 99);
    ASSERT_TRUE(draw(false).HasValue());
    EXPECT_EQ(Runtime().pets[Client::Kernel::DarkHorsePetStats].nextExperience, 89600u);
    EXPECT_EQ(cell.optionBytes[Client::Kernel::ItemOptionValueOffset + 1], 13);
}

// With another type, 0x422990 returns 0 without touching anything (0x4229c3).
TEST_P(PetTooltipTest, AnotherTypeIsNotAPet)
{
    ItemState item;
    item.id = 0x1a03;
    GameHudContext context = Context();

    const auto pet = Client::Tooltips::DrawThePetTooltip(
        Client::Tooltips::ItemTooltipContext{.hud = context, .x = BoxX, .y = BoxY, .item = item});

    ASSERT_TRUE(pet.HasValue()) << pet.Error();
    EXPECT_FALSE(pet.Value());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Runtime().petTooltipQuery.type, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, PetTooltipTest, ::testing::Values(Lorencia, Devias, 3));
} // namespace
