#include "Tooltips/SkillTooltip.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Tooltips/GameTooltips.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The body of a bar skill's tooltip (0x5b90b0) and the help of the raven's commands (0x421c10), CL8 milestone 5, with
// invented Text.bmd and Skill.bmd, the fake text of RecordingTextDrawer (13 high) and 640 × 480, where ry is 1. Nobody
// opens the own record around it, as in 0x5f7bc0: its close spends 3848 rand().
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Render::SpriteBatch;
using Client::Tooltips::TooltipLines;
using Client::Ui::GameHudContext;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Client::Core::PixelSize Screen800{800, 600};
// A Skill.bmd access that closes at 0 (0x3c bytes).
constexpr int SkillAccess = 60;
// Every skill's: 0x5513d0, 0x5516e0, 0x551c70, 0x5519f0 and the inline access of its entry.
constexpr int SkillAccesses = 5 * SkillAccess;
// What was in the colors, the bolds and the two counters before the body: it neither clears nor touches them.
constexpr std::int32_t StaleColor = 8;
constexpr std::int32_t StaleBold = 7;
constexpr std::int32_t StaleNext = 11;
constexpr std::int32_t StaleHalves = 2;
constexpr std::int32_t Height = Client::Tests::RecordingTextDrawer::Height;

// The hero's classes (+0x1d4 & 7).
constexpr std::uint8_t Wizard = 0;
constexpr std::uint8_t Knight = 1;
constexpr std::uint8_t Elf = 2;
constexpr std::uint8_t Gladiator = 3;
constexpr std::uint8_t DarkLord = 4;
constexpr std::uint8_t FifthClass = 5;

// The invented skills, at the numbers the tooltip looks at.
constexpr std::uint8_t Slash = 0x07;
constexpr std::uint8_t Bolt = 0x05;
constexpr std::uint8_t Absorb = 0x10;
constexpr std::uint8_t Heal = 0x1a;
constexpr std::uint8_t Guard = 0x1b;
constexpr std::uint8_t Might = 0x1c;
constexpr std::uint8_t Lance = 0x2f;
constexpr std::uint8_t Spin = 0x13;
constexpr std::uint8_t Given = 0x30;
constexpr std::uint8_t War = 0x31;
constexpr std::uint8_t Force = 0x3c;
constexpr std::uint8_t Pet = 0x3e;
constexpr std::uint8_t Party = 0x3f;
constexpr std::uint8_t StatDamage = 0x4c;
constexpr std::uint8_t Nameless = 0x50;

Client::Tables::TextTable Texts()
{
    return Client::Tests::MakeTextTable({{96, "A pie"},
                                         {99, "Cadena"},
                                         {170, "Magia %d~%d"},
                                         {171, "Cura %d"},
                                         {172, "Escudo %d"},
                                         {173, "Fuerza %d"},
                                         {174, "Alcance %d"},
                                         {175, "Mana %d"},
                                         {360, "Aguante %d"},
                                         {578, "Absorbe %d"},
                                         {879, "Golpe %d~%d"},
                                         {880, "Gasto %d"},
                                         {881, "Dura %d"},
                                         {1080, "Fuego"},
                                         {1081, "Hielo %d"},
                                         {1185, "Con grupo"},
                                         {1200, "Onda"},
                                         {1219, "Orden A"},
                                         {1220, "Orden B"},
                                         {1221, "Orden C"},
                                         {1222, "Orden D"},
                                         {1223, "Ayuda A"},
                                         {1224, "Ayuda B"},
                                         {1225, "Ayuda C"},
                                         {1226, "Ayuda D"},
                                         {1237, "Ver mascota"},
                                         {1480, "Dada por %s"},
                                         {1481, "Espera %d"},
                                         {1482, "Guerra"},
                                         {1483, "Bajas %d"},
                                         {1926, ""},
                                         {1927, ""}});
}

// Skill.bmd in clear: all without an attribute (+0x30 at 0xff) except the ones that say otherwise.
class SkillRecords
{
public:
    SkillRecords() : m_plain(Client::Tables::SkillTableSize)
    {
        for (std::size_t skill = 0; skill < Client::Tables::SkillTableSize / Record; ++skill)
            m_plain[skill * Record + 0x30] = std::byte{0xff};
    }

    SkillRecords& Named(std::uint8_t skill, std::string_view name)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            At(skill, i) = static_cast<std::byte>(name[i]);
        return *this;
    }

    SkillRecords& Byte(std::uint8_t skill, std::size_t offset, std::uint8_t value)
    {
        At(skill, offset) = std::byte{value};
        return *this;
    }

    SkillRecords& Word(std::uint8_t skill, std::size_t offset, std::uint16_t value)
    {
        At(skill, offset) = std::byte{static_cast<std::uint8_t>(value & 0xff)};
        At(skill, offset + 1) = std::byte{static_cast<std::uint8_t>(value >> 8)};
        return *this;
    }

    // The damage (+0x21), the mana (+0x22), the stamina (+0x24) and the range (+0x26).
    SkillRecords& Costs(std::uint8_t skill, std::uint8_t damage, std::uint16_t mana, std::uint16_t stamina,
                        std::uint8_t range)
    {
        return Byte(skill, 0x21, damage).Word(skill, 0x22, mana).Word(skill, 0x24, stamina).Byte(skill, 0x26, range);
    }

    [[nodiscard]] Client::Tables::SkillTable Table() const
    {
        const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(m_plain, Record);
        return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
    }

private:
    static constexpr std::size_t Record = Client::Tables::SkillRecordSize;

    std::byte& At(std::uint8_t skill, std::size_t offset)
    {
        return m_plain[std::size_t{skill} * Record + offset];
    }

    std::vector<std::byte> m_plain;
};

SkillRecords BaseSkills()
{
    SkillRecords records;
    records.Named(Slash, "Tajo").Costs(Slash, 20, 12, 0, 3);
    records.Named(Bolt, "Rayo").Costs(Bolt, 18, 10, 0, 6).Byte(Bolt, 0x30, 0);
    records.Named(Absorb, "Velo").Costs(Absorb, 0, 30, 0, 0).Byte(Absorb, 0x30, 1);
    records.Named(Heal, "Alivio").Costs(Heal, 0, 20, 0, 5);
    records.Named(Guard, "Muro").Costs(Guard, 0, 22, 0, 5);
    records.Named(Might, "Brio").Costs(Might, 0, 24, 0, 5);
    records.Named(Lance, "Lanza").Costs(Lance, 25, 9, 8, 0);
    records.Named(Spin, "Giro").Costs(Spin, 14, 7, 0, 2);
    records.Named(Given, "Regalo").Costs(Given, 0, 5, 0, 0).Byte(Given, 0x31, 2).Byte(Given, 0x32, Slash);
    records.Named(War, "Bandera").Costs(War, 0, 6, 0, 0).Byte(War, 0x31, 1).Byte(War, 0x33, 5);
    records.Named(Force, "Golpe Fuerte").Costs(Force, 10, 15, 0, 4);
    records.Named(Pet, "Corcel").Costs(Pet, 0, 3, 0, 0);
    records.Named(Party, "Llamado").Costs(Party, 0, 4, 0, 0);
    records.Named(StatDamage, "Furia").Costs(StatDamage, 9, 40, 0, 0);
    // A name that fills the whole entry, without '\0'.
    for (std::size_t i = 0; i < Client::Tables::SkillRecordSize; ++i)
        records.Byte(Nameless, i, 'x');
    return records;
}

struct Line
{
    std::string text;
    std::int32_t color = StaleColor;
    std::int32_t bold = StaleBold;
};

// The lines the body always writes: the "\n", the name in blue and bold and another "\n".
std::vector<Line> Header(std::string_view name)
{
    return {{"\n"}, {std::string(name), 1, 1}, {"\n"}};
}

class SkillTooltipTest : public ::testing::Test
{
protected:
    SkillTooltipTest()
    {
        sprites.Begin(Screen640);
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        // The attack (0x5b8b80): the empty hands and the two equal pairs.
        for (Client::Kernel::ItemState& item : own.equipment)
            item.id = 0xffff;
        own.word50 = 6;
        own.word54 = 6;
        own.word52 = 9;
        own.word56 = 9;
        // The magic (0x5513d0).
        own.word5a = 10;
        own.word5c = 20;
        own.level = 150;
        own.strength = 90;
        own.agility = 40;
        own.word2c = 5;
        own.vitality = 50;
        own.energy = 120;
        own.leadership = 30;
        own.maxMana = 250;
        ResetLines();
        HeroIs(Knight);
        Hero().hands[0].model = 0xffff;
    }

    GameHudContext Context()
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        Client::Kernel::ActorState& hero = Hero();
        return GameHudContext{.world = world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .sprites = sprites,
                              .text = text,
                              .texts = withTexts ? &texts : nullptr,
                              .skills = withSkills ? &skills : nullptr};
    }

    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
    }

    void HeroIs(std::uint8_t characterClass)
    {
        Hero().characterClass = characterClass;
    }

    // Skill `skill` in slot `slot` of the record (+0x6c + slot).
    void SkillIn(std::size_t slot, std::uint8_t skill)
    {
        std::uint32_t& word = world.OwnCharacter().words6c[slot / 4];
        const auto shift = static_cast<std::uint32_t>(slot % 4) * 8u;
        word = (word & ~(0xffu << shift)) | (std::uint32_t{skill} << shift);
    }

    // 0x5f7bc0 with skill `skill` in slot 2.
    Client::Core::Expected<void, std::string> Draw(std::uint8_t skill, std::int32_t siegeSkill = 0,
                                                   std::int32_t x = 320, std::int32_t y = 300)
    {
        SkillIn(2, skill);
        return DrawSlot(2, siegeSkill, x, y);
    }

    Client::Core::Expected<void, std::string> DrawSlot(std::int32_t slot, std::int32_t siegeSkill = 0,
                                                       std::int32_t x = 320, std::int32_t y = 300)
    {
        GameHudContext context = Context();
        return Client::Tooltips::DrawTheSkillTooltip(context, x, y, slot, siegeSkill);
    }

    TooltipLines& Lines()
    {
        return world.Runtime().hud.tooltip;
    }

    // The lines empty, and the colors, the bolds and the counters with what was there before.
    void ResetLines()
    {
        TooltipLines& lines = Lines();
        lines.ClearTexts();
        lines.colors.fill(StaleColor);
        lines.bold.fill(StaleBold);
        lines.next = StaleNext;
        lines.halves = StaleHalves;
    }

    // The written lines, and the two counters of 0x78abb30 and 0x78abb34 as they were.
    void ExpectLines(const std::vector<Line>& expected)
    {
        const TooltipLines& lines = Lines();
        EXPECT_EQ(lines.next, StaleNext);
        EXPECT_EQ(lines.halves, StaleHalves);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(lines.text[i], expected[i].text) << "renglón " << i;
            EXPECT_EQ(lines.colors[i], expected[i].color) << "renglón " << i;
            EXPECT_EQ(lines.bold[i], expected[i].bold) << "renglón " << i;
        }
        for (std::size_t i = expected.size(); i < Client::Tooltips::TooltipLineCount; ++i)
        {
            EXPECT_TRUE(lines.text[i].empty()) << "renglón " << i;
            EXPECT_EQ(lines.colors[i], StaleColor) << "renglón " << i;
            EXPECT_EQ(lines.bold[i], StaleBold) << "renglón " << i;
        }
    }

    static CrtRandom Advanced(CrtRandom from, int count)
    {
        for (int i = 0; i < count; ++i)
            static_cast<void>(from.Next());
        return from;
    }

    // What the box drew, without the "\n".
    std::vector<std::string> DrawnTexts() const
    {
        std::vector<std::string> drawn;
        for (const auto& recorded : text.texts)
            drawn.push_back(recorded.text);
        return drawn;
    }

    // The y of the first text of the box that 0x5af090 draws with `count` lines above at `top`.
    std::int32_t FirstTextYOfABoxAt(std::int32_t top, std::int32_t count)
    {
        const std::vector<Client::Tests::RecordedText> drawn = text.texts;
        text.texts.clear();
        GameHudContext context = Context();
        EXPECT_TRUE(Client::Tooltips::DrawTheTooltipBox(context, 320, top, count, 0, 2, 1).HasValue());
        const std::int32_t y = text.texts.empty() ? -1 : text.texts[0].y;
        text.texts = drawn;
        return y;
    }

    WorldState world;
    CrtRandom random;
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Tables::TextTable texts = Texts();
    Client::Tables::SkillTable skills = BaseSkills().Table();
    bool withSkills = true;
    bool withTexts = true;
};

// Classes 1 to 4: the skill's damage (0x5516e0, s and s + s / 2) plus the attack's (0x5b8b80), the range and the mana.
// Without an attribute (0xff), the access of 0x5519f0 opens anyway. Five Skill.bmd accesses and the record's close.
TEST_F(SkillTooltipTest, AKnightSeesTheSkillDamagePlusTheAttack)
{
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);

    const auto drawn = Draw(Slash);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    std::vector<Line> lines = Header("Tajo");
    lines.insert(lines.end(), {{"Golpe 26~39", 0, 0}, {"Alcance 3", 0, 0}, {"Mana 12", 0, 0}, {"\n"}});
    ExpectLines(lines);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(world.Runtime().skillDepths[Slash], 0);
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Tajo", "Golpe 26~39", "Alcance 3", "Mana 12"}));
}

// The Gladiator, the Dark Lord and the elf see the same damage; classes 5 to 7, none.
TEST_F(SkillTooltipTest, TheOtherClassesShareTheDamageLineButTheFifthHasNone)
{
    for (const std::uint8_t heroClass : {Gladiator, DarkLord, Elf})
    {
        HeroIs(heroClass);
        ASSERT_TRUE(Draw(Slash).HasValue());
        EXPECT_EQ(Lines().text[3], "Golpe 26~39") << int{heroClass};
    }
    HeroIs(FifthClass);
    ResetLines();

    ASSERT_TRUE(Draw(Slash).HasValue());

    std::vector<Line> lines = Header("Tajo");
    lines.insert(lines.end(), {{"Alcance 3", 0, 0}, {"Mana 12", 0, 0}, {"\n"}});
    ExpectLines(lines);
}

// The Wizard sees the magic (0x5513d0: +0x5a + s and +0x5c + s + s / 2) and attribute 0 (text 1080) in blue, except
// with a siege skill from 0x43 to 0x45 in the fourth argument (Q-C722).
TEST_F(SkillTooltipTest, AWizardSeesTheMagicUnlessTheSiegeSkillIsOneOfThree)
{
    HeroIs(Wizard);

    ASSERT_TRUE(Draw(Bolt).HasValue());

    std::vector<Line> lines = Header("Rayo");
    lines.insert(lines.end(), {{"Magia 28~47", 0, 0}, {"Alcance 6", 0, 0}, {"Mana 10", 0, 0}, {"Fuego", 1, 0}, {"\n"}});
    ExpectLines(lines);

    for (const std::int32_t siege : {0x43, 0x44, 0x45})
    {
        ASSERT_TRUE(Draw(Bolt, siege).HasValue());
        EXPECT_EQ(Lines().text[3], "Alcance 6") << siege;
    }
    ASSERT_TRUE(Draw(Bolt, 0x46).HasValue());
    EXPECT_EQ(Lines().text[3], "Magia 28~47");
}

// The Wizard's 0x10: the absorption with the x87, where 0.02f is a bit less than 0.02: 250 · 0.02f gives 4, not 5.
TEST_F(SkillTooltipTest, TheWizardAbsorptionTruncatesTheFloatProducts)
{
    HeroIs(Wizard);

    ASSERT_TRUE(Draw(Absorb).HasValue());

    // 45 · 0.02f + 120 · 0.005f + 10 = 11.49…; 120 · 0.025f + 60 = 63.00…
    std::vector<Line> lines = Header("Velo");
    lines.insert(
        lines.end(),
        {{"Absorbe 11", 0, 0}, {"Gasto 4", 0, 0}, {"Dura 63", 0, 0}, {"Mana 30", 0, 0}, {"Hielo %d", 1, 0}, {"\n"}});
    ExpectLines(lines);
}

// The elf: the energy lines of her three skills, which have no damage line.
TEST_F(SkillTooltipTest, TheElfSeesTheEnergyOfItsThreeSkills)
{
    HeroIs(Elf);

    ASSERT_TRUE(Draw(Heal).HasValue());
    std::vector<Line> lines = Header("Alivio");
    lines.insert(lines.end(), {{"Cura 29", 0, 0}, {"Alcance 5", 0, 0}, {"Mana 20", 0, 0}, {"\n"}});
    ExpectLines(lines);

    ASSERT_TRUE(Draw(Guard).HasValue());
    EXPECT_EQ(Lines().text[3], "Escudo 17");
    ASSERT_TRUE(Draw(Might).HasValue());
    EXPECT_EQ(Lines().text[3], "Fuerza 20");

    HeroIs(Knight);
    ASSERT_TRUE(Draw(Heal).HasValue());
    EXPECT_EQ(Lines().text[3], "Alcance 5");
}

// The Dark Knight: 0x2f with 96 after the stamina, and the combo with the hero's +0x390 at 1, level 220 or more and a
// combo skill.
TEST_F(SkillTooltipTest, TheDarkKnightSeesTheLanceNoteAndTheComboAfterTheQuest)
{
    ASSERT_TRUE(Draw(Lance).HasValue());
    std::vector<Line> lines = Header("Lanza");
    lines.insert(lines.end(), {{"Golpe 31~46", 0, 0}, {"Mana 9", 0, 0}, {"Aguante 8", 0, 0}, {"A pie", 5, 0}, {"\n"}});
    ExpectLines(lines);

    world.OwnCharacter().level = 220;
    ASSERT_TRUE(Draw(Spin).HasValue());
    EXPECT_EQ(Lines().text[5], "Mana 7");
    EXPECT_EQ(Lines().text[6], "\n");

    Hero().byte390 = 1;
    ASSERT_TRUE(Draw(Spin).HasValue());
    EXPECT_EQ(Lines().text[6], "Cadena");
    EXPECT_EQ(Lines().colors[6], 5);

    world.OwnCharacter().level = 219;
    ASSERT_TRUE(Draw(Spin).HasValue());
    EXPECT_EQ(Lines().text[6], "\n");

    HeroIs(Gladiator);
    world.OwnCharacter().level = 220;
    ASSERT_TRUE(Draw(Spin).HasValue());
    EXPECT_EQ(Lines().text[6], "\n");
}

// 0x4c: the +0x21 byte of its entry (0x552470, one more access) plus each class's stats, up to 30 more, and the two
// notes 1926 and 1927, empty in the client's Text.bmd: the box stops drawing there (Q-C723).
TEST_F(SkillTooltipTest, TheStatDamageAddsTheStatsOfEachClass)
{
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses + SkillAccess);

    ASSERT_TRUE(Draw(StatDamage).HasValue());

    // 90 / 3 + 45 / 5 + 50 / 5 + 120 / 7 + 9.
    std::vector<Line> lines = Header("Furia");
    lines.insert(lines.end(), {{"Golpe 75~105", 0, 0}, {"Mana 40", 0, 0}, {"", 5, 0}, {"", 5, 0}, {"\n"}});
    ExpectLines(lines);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Furia", "Golpe 75~105", "Mana 40"}));

    HeroIs(Gladiator);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Golpe 75~105");
    // 90 / 5 + 45 / 3 + 50 / 7 + 120 / 5 + 9.
    HeroIs(Elf);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Golpe 73~103");
    // Plus 30 / 3 from the command.
    HeroIs(DarkLord);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Golpe 93~123");
    // Classes 5 to 7: the skill's plus the attack's, 9 + 6.
    HeroIs(FifthClass);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Golpe 15~45");
    // The Wizard also has his magic: 90 / 5 + 45 / 5 + 50 / 7 + 120 / 3 + 9.
    HeroIs(Wizard);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Magia 19~33");
    EXPECT_EQ(Lines().text[4], "Golpe 83~113");
}

// 0x3c with option 0x42 in slot 0 and something in the hero's right hand is named like text 1200.
TEST_F(SkillTooltipTest, TheForceSkillTakesTheWaveNameWithItsOption)
{
    Client::Kernel::ItemState& right = world.OwnCharacter().equipment[0];
    right.optionCount = 2;
    right.optionBytes[0] = 0x10;
    right.optionBytes[1] = 0x42;

    ASSERT_TRUE(Draw(Force).HasValue());
    EXPECT_EQ(Lines().text[1], "Golpe Fuerte");

    Hero().hands[0].model = 0x0005;
    ASSERT_TRUE(Draw(Force).HasValue());
    EXPECT_EQ(Lines().text[1], "Onda");

    right.optionCount = 1;
    ASSERT_TRUE(Draw(Force).HasValue());
    EXPECT_EQ(Lines().text[1], "Golpe Fuerte");

    // Without 0x42 in its 17 bytes, the loop goes on through D+0x3c.
    right.optionCount = 20;
    right.optionBytes[1] = 0x11;
    const auto drawn = Draw(Force);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5b9271"), std::string::npos);
}

// The 0x3e of classes 1 to 4 says 1237 instead of the damage; the Dark Lord's 0x3f, 1185 without a party.
TEST_F(SkillTooltipTest, ThePetAndThePartySkillsWriteTheirNotes)
{
    ASSERT_TRUE(Draw(Pet).HasValue());
    std::vector<Line> lines = Header("Corcel");
    lines.insert(lines.end(), {{"Ver mascota", 5, 0}, {"Mana 3", 0, 0}, {"\n"}});
    ExpectLines(lines);

    HeroIs(DarkLord);
    ASSERT_TRUE(Draw(Party).HasValue());
    EXPECT_EQ(Lines().text[4], "Con grupo");
    EXPECT_EQ(Lines().colors[4], 5);

    world.Runtime().hud.partyCount = 2;
    ASSERT_TRUE(Draw(Party).HasValue());
    EXPECT_EQ(Lines().text[4], "\n");
}

// The entry's +0x31: with 2, the skill at +0x32 (its name and its +0x21, with its access inside: 60 more if it is not
// the same); with 1, the guild war and +0x33.
TEST_F(SkillTooltipTest, TheOriginNamesTheGiverOrTheGuildWar)
{
    CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses + SkillAccess);

    ASSERT_TRUE(Draw(Given).HasValue());

    std::vector<Line> lines = Header("Regalo");
    lines.insert(lines.end(), {{"Mana 5", 0, 0}, {"Dada por Tajo", 5, 0}, {"Espera 20", 5, 0}, {"\n"}});
    ExpectLines(lines);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().skillDepths[Slash], 0);
    EXPECT_EQ(world.Runtime().skillDepths[Given], 0);

    HeroIs(Wizard);
    expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);
    ASSERT_TRUE(Draw(War).HasValue());
    lines = Header("Bandera");
    lines.insert(lines.end(), {{"Magia 10~20", 0, 0}, {"Mana 6", 0, 0}, {"Guerra", 5, 0}, {"Bajas 5", 5, 0}, {"\n"}});
    ExpectLines(lines);
    EXPECT_EQ(random.State(), expected.State());
}

// A skill that gives itself: the inner access is the same and draws nothing.
TEST_F(SkillTooltipTest, ASkillThatGivesItselfOpensItsEntryTwice)
{
    skills = BaseSkills().Byte(Given, 0x32, Given).Table();
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);

    ASSERT_TRUE(Draw(Given).HasValue());

    EXPECT_EQ(Lines().text[4], "Dada por Regalo");
    EXPECT_EQ(Lines().text[5], "Espera 0");
    EXPECT_EQ(random.State(), expected.State());
}

// The box goes above y: ((N − 3) · cy + 3 · cy / 2) / ry, with N = 7 and cy = 13; at 800 × 600, ry = 1.25.
TEST_F(SkillTooltipTest, TheBoxGoesAboveYInEachResolution)
{
    ASSERT_TRUE(Draw(Slash, 0, 320, 300).HasValue());
    const std::int32_t sum = 4 * Height + 3 * Height / 2;
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, FirstTextYOfABoxAt(300 - sum, 7));

    sprites.Begin(Screen800);
    text.texts.clear();
    ASSERT_TRUE(Draw(Slash, 0, 320, 300).HasValue());
    // 71 / 1.25 = 56.8: _ftol truncates.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, FirstTextYOfABoxAt(300 - 56, 7));
    EXPECT_NE(text.texts[0].y, FirstTextYOfABoxAt(300 - 57, 7));
}

// 0x421c10: the Dark Lord's slots 0x78 to 0x7b are the commands' help, without rand(): the name in blue and bold, line
// 1's color at 0 and the rest as they were (Q-C720), with the height of 4 halves (Q-C721).
TEST_F(SkillTooltipTest, TheDarkLordCommandsShowTheirHelpWithoutTheRecord)
{
    HeroIs(DarkLord);
    const CrtRandom before = random;

    ASSERT_TRUE(DrawSlot(0x79).HasValue());

    ExpectLines({{"Orden B", 1, 1}, {"\n", 0, StaleBold}, {"\n"}, {"Ayuda B"}});
    EXPECT_EQ(random.State(), before.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Orden B", "Ayuda B"}));
    EXPECT_EQ(text.texts[0].y, FirstTextYOfABoxAt(300 - 4 * Height / 2, 4));

    ASSERT_TRUE(DrawSlot(0x7b).HasValue());
    EXPECT_EQ(Lines().text[0], "Orden D");
    EXPECT_EQ(Lines().text[3], "Ayuda D");
}

// Outside 0x78 to 0x7b, or with another class, the skill body: byte +0x6c + slot, of the 256 that F3 11 writes
// (0x4c5070, CL9 milestone 4). Outside them it cuts.
TEST_F(SkillTooltipTest, OtherSlotsReadTheSkillByteOfTheRecord)
{
    HeroIs(DarkLord);
    SkillIn(0x3f, Slash);
    ASSERT_TRUE(DrawSlot(0x3f).HasValue());
    EXPECT_EQ(Lines().text[1], "Tajo");

    HeroIs(Knight);
    world.OwnCharacter().SetSkillAt(0x79, Slash);
    ASSERT_TRUE(DrawSlot(0x79).HasValue());
    EXPECT_EQ(Lines().text[1], "Tajo");
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    const auto past = DrawSlot(0x100);
    ASSERT_FALSE(past.HasValue());
    EXPECT_NE(past.Error().find("0x5b91af"), std::string::npos);
    const auto negative = DrawSlot(-2);
    ASSERT_FALSE(negative.HasValue());
    EXPECT_NE(negative.Error().find("0x5b91af"), std::string::npos);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// What MuRet does not have cuts with its address: Skill.bmd, a name without '\0' in its entry (the skill's and the
// giver's) and Text.bmd.
TEST_F(SkillTooltipTest, WhatMuRetLacksStopsWithItsAddress)
{
    withSkills = false;
    const auto noSkills = Draw(Slash);
    ASSERT_FALSE(noSkills.HasValue());
    EXPECT_NE(noSkills.Error().find("0x5513d0"), std::string::npos);
    withSkills = true;

    const auto nameless = Draw(Nameless);
    ASSERT_FALSE(nameless.HasValue());
    EXPECT_NE(nameless.Error().find("0x551e0c"), std::string::npos);

    skills = BaseSkills().Byte(Given, 0x32, Nameless).Table();
    const auto giver = Draw(Given);
    ASSERT_FALSE(giver.HasValue());
    EXPECT_NE(giver.Error().find("0x5b9ee6"), std::string::npos);

    withTexts = false;
    const auto noText = Draw(Slash);
    ASSERT_FALSE(noText.HasValue());
    EXPECT_NE(noText.Error().find("879"), std::string::npos);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(world.Runtime().skillDepths[Slash], 0);
}

// 0x5f7bc0: with 0x6c9320 other than −1, the skill body of that slot with the fourth argument at 0.
TEST_F(SkillTooltipTest, TheTooltipsDrawTheSkillOfTheHoveredSlot)
{
    SkillIn(4, Slash);
    Client::Ui::GameUiState& ui = world.Runtime().ui;
    ui.skillTooltip = 4;
    ui.skillTooltipX = 320;
    ui.skillTooltipY = 300;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);
    GameHudContext context = Context();

    const auto drawn = Client::Tooltips::DrawTheTooltips(context);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Tajo", "Golpe 26~39", "Alcance 3", "Mana 12"}));
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
