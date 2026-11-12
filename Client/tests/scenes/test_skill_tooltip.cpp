#include "Scenes/SkillTooltip.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"
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

// El cuerpo del tooltip de una skill de la barra (0x5b90b0) y la ayuda de los comandos del cuervo (0x421c10), CL8
// hito 5, con Text.bmd y Skill.bmd inventados, el texto falso de RecordingTextDrawer (13 de alto) y 640 × 480, donde
// ry es 1. Nadie abre el registro propio alrededor, como en 0x5f7bc0: su cierre gasta 3.848 rand().
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::GameHudContext;
using Client::Scenes::TooltipLines;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Client::App::PixelSize Screen800{800, 600};
// Un acceso de Skill.bmd que cierra en 0 (0x3c bytes).
constexpr int SkillAccess = 60;
// Los de toda skill: 0x5513d0, 0x5516e0, 0x551c70, 0x5519f0 y el acceso en línea de su entrada.
constexpr int SkillAccesses = 5 * SkillAccess;
// Lo que había en los colores, las negritas y los dos contadores antes del cuerpo: no los limpia ni los toca.
constexpr std::int32_t StaleColor = 8;
constexpr std::int32_t StaleBold = 7;
constexpr std::int32_t StaleNext = 11;
constexpr std::int32_t StaleHalves = 2;
constexpr std::int32_t Height = Client::Tests::RecordingTextDrawer::Height;

// Las clases del héroe (+0x1d4 & 7).
constexpr std::uint8_t Wizard = 0;
constexpr std::uint8_t Knight = 1;
constexpr std::uint8_t Elf = 2;
constexpr std::uint8_t Gladiator = 3;
constexpr std::uint8_t DarkLord = 4;
constexpr std::uint8_t FifthClass = 5;

// Las skills inventadas, en los números que el tooltip mira.
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

Client::Data::Text::TextTable Texts()
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

// Skill.bmd en claro: todas sin atributo (+0x30 en 0xff) salvo las que dicen otra cosa.
class SkillRecords
{
public:
    SkillRecords() : m_plain(Client::Data::Tables::SkillTableSize)
    {
        for (std::size_t skill = 0; skill < Client::Data::Tables::SkillTableSize / Record; ++skill)
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

    // El daño (+0x21), el maná (+0x22), la stamina (+0x24) y el alcance (+0x26).
    SkillRecords& Costs(std::uint8_t skill, std::uint8_t damage, std::uint16_t mana, std::uint16_t stamina,
                        std::uint8_t range)
    {
        return Byte(skill, 0x21, damage).Word(skill, 0x22, mana).Word(skill, 0x24, stamina).Byte(skill, 0x26, range);
    }

    [[nodiscard]] Client::Data::Tables::SkillTable Table() const
    {
        const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(m_plain, Record);
        return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
    }

private:
    static constexpr std::size_t Record = Client::Data::Tables::SkillRecordSize;

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
    // Un nombre que llena la entrada entera, sin '\0'.
    for (std::size_t i = 0; i < Client::Data::Tables::SkillRecordSize; ++i)
        records.Byte(Nameless, i, 'x');
    return records;
}

struct Line
{
    std::string text;
    std::int32_t color = StaleColor;
    std::int32_t bold = StaleBold;
};

// Los renglones que el cuerpo escribe siempre: el "\n", el nombre en azul y negrita y otro "\n".
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
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
        // El ataque (0x5b8b80): las manos vacías y los dos pares iguales.
        for (Client::World::ItemState& item : own.equipment)
            item.id = 0xffff;
        own.word50 = 6;
        own.word54 = 6;
        own.word52 = 9;
        own.word56 = 9;
        // La magia (0x5513d0).
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
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        Client::Scenes::ActorState& hero = Hero();
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

    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
    }

    void HeroIs(std::uint8_t characterClass)
    {
        Hero().characterClass = characterClass;
    }

    // La skill `skill` en el lugar `slot` del registro (+0x6c + lugar).
    void SkillIn(std::size_t slot, std::uint8_t skill)
    {
        std::uint32_t& word = world.OwnCharacter().words6c[slot / 4];
        const auto shift = static_cast<std::uint32_t>(slot % 4) * 8u;
        word = (word & ~(0xffu << shift)) | (std::uint32_t{skill} << shift);
    }

    // 0x5f7bc0 con la skill `skill` en el lugar 2.
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
        return Client::Scenes::DrawTheSkillTooltip(context, x, y, slot, siegeSkill);
    }

    TooltipLines& Lines()
    {
        return world.Runtime().hud.tooltip;
    }

    // Los renglones vacíos, y los colores, las negritas y los contadores con lo que había antes.
    void ResetLines()
    {
        TooltipLines& lines = Lines();
        lines.ClearTexts();
        lines.colors.fill(StaleColor);
        lines.bold.fill(StaleBold);
        lines.next = StaleNext;
        lines.halves = StaleHalves;
    }

    // Los renglones escritos, y los dos contadores de 0x78abb30 y 0x78abb34 como estaban.
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
        for (std::size_t i = expected.size(); i < Client::Scenes::TooltipLineCount; ++i)
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

    // Lo que dibujó la caja, sin los "\n".
    std::vector<std::string> DrawnTexts() const
    {
        std::vector<std::string> drawn;
        for (const auto& recorded : text.texts)
            drawn.push_back(recorded.text);
        return drawn;
    }

    // La y del primer texto de la caja que 0x5af090 dibuja con `count` renglones arriba en `top`.
    std::int32_t FirstTextYOfABoxAt(std::int32_t top, std::int32_t count)
    {
        const std::vector<Client::Tests::RecordedText> drawn = text.texts;
        text.texts.clear();
        GameHudContext context = Context();
        EXPECT_TRUE(Client::Scenes::DrawTheTooltipBox(context, 320, top, count, 0, 2, 1).HasValue());
        const std::int32_t y = text.texts.empty() ? -1 : text.texts[0].y;
        text.texts = drawn;
        return y;
    }

    WorldState world;
    CrtRandom random;
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Data::Text::TextTable texts = Texts();
    Client::Data::Tables::SkillTable skills = BaseSkills().Table();
    bool withSkills = true;
    bool withTexts = true;
};

// Las clases 1 a 4: el daño de la skill (0x5516e0, s y s + s / 2) más el del ataque (0x5b8b80), el alcance y el maná.
// Sin atributo (0xff), el acceso de 0x5519f0 se abre igual. Cinco accesos de Skill.bmd y el cierre del registro.
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
    EXPECT_EQ(world.Runtime().movement.skillDepths[Slash], 0);
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Tajo", "Golpe 26~39", "Alcance 3", "Mana 12"}));
}

// El Gladiator, el Dark Lord y la elfa ven el mismo daño; las clases 5 a 7, ninguno.
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

// El Wizard ve la magia (0x5513d0: +0x5a + s y +0x5c + s + s / 2) y el atributo 0 (el texto 1080) en azul, salvo con
// una skill de asedio de 0x43 a 0x45 en el cuarto argumento (Q-C722).
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

// La 0x10 del Wizard: la absorción con la x87, donde 0,02f es un poco menos que 0,02: 250 · 0,02f da 4, no 5.
TEST_F(SkillTooltipTest, TheWizardAbsorptionTruncatesTheFloatProducts)
{
    HeroIs(Wizard);

    ASSERT_TRUE(Draw(Absorb).HasValue());

    // 45 · 0,02f + 120 · 0,005f + 10 = 11,49…; 120 · 0,025f + 60 = 63,00…
    std::vector<Line> lines = Header("Velo");
    lines.insert(
        lines.end(),
        {{"Absorbe 11", 0, 0}, {"Gasto 4", 0, 0}, {"Dura 63", 0, 0}, {"Mana 30", 0, 0}, {"Hielo %d", 1, 0}, {"\n"}});
    ExpectLines(lines);
}

// La elfa: lo de la energía de sus tres skills, que no tienen renglón de daño.
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

// El Dark Knight: la 0x2f con el 96 después de la stamina, y el combo con el +0x390 del héroe en 1, nivel 220 o más
// y una skill del combo.
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

// La 0x4c: el byte +0x21 de su entrada (0x552470, un acceso más) más los stats de cada clase, hasta 30 más, y las dos
// notas 1926 y 1927, vacías en el Text.bmd del cliente: la caja deja de dibujar ahí (Q-C723).
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
    // Más 30 / 3 del comando.
    HeroIs(DarkLord);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Golpe 93~123");
    // Las clases 5 a 7: el de la skill más el del ataque, 9 + 6.
    HeroIs(FifthClass);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Golpe 15~45");
    // El Wizard tiene también su magia: 90 / 5 + 45 / 5 + 50 / 7 + 120 / 3 + 9.
    HeroIs(Wizard);
    ASSERT_TRUE(Draw(StatDamage).HasValue());
    EXPECT_EQ(Lines().text[3], "Magia 19~33");
    EXPECT_EQ(Lines().text[4], "Golpe 83~113");
}

// La 0x3c con la opción 0x42 en el lugar 0 y algo en la mano derecha del héroe se llama como el texto 1200.
TEST_F(SkillTooltipTest, TheForceSkillTakesTheWaveNameWithItsOption)
{
    Client::World::ItemState& right = world.OwnCharacter().equipment[0];
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

    // Sin la 0x42 en sus 17 bytes, el lazo sigue por D+0x3c.
    right.optionCount = 20;
    right.optionBytes[1] = 0x11;
    const auto drawn = Draw(Force);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5b9271"), std::string::npos);
}

// La 0x3e de las clases 1 a 4 dice el 1237 en vez del daño; la 0x3f del Dark Lord, el 1185 sin party.
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

// El +0x31 de la entrada: con 2, la skill del +0x32 (su nombre y su +0x21, con su acceso adentro: 60 más si no es la
// misma); con 1, la guerra de guilds y el +0x33.
TEST_F(SkillTooltipTest, TheOriginNamesTheGiverOrTheGuildWar)
{
    CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses + SkillAccess);

    ASSERT_TRUE(Draw(Given).HasValue());

    std::vector<Line> lines = Header("Regalo");
    lines.insert(lines.end(), {{"Mana 5", 0, 0}, {"Dada por Tajo", 5, 0}, {"Espera 20", 5, 0}, {"\n"}});
    ExpectLines(lines);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().movement.skillDepths[Slash], 0);
    EXPECT_EQ(world.Runtime().movement.skillDepths[Given], 0);

    HeroIs(Wizard);
    expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);
    ASSERT_TRUE(Draw(War).HasValue());
    lines = Header("Bandera");
    lines.insert(lines.end(), {{"Magia 10~20", 0, 0}, {"Mana 6", 0, 0}, {"Guerra", 5, 0}, {"Bajas 5", 5, 0}, {"\n"}});
    ExpectLines(lines);
    EXPECT_EQ(random.State(), expected.State());
}

// Una skill que se da a sí misma: el acceso de adentro es el mismo y no saca nada.
TEST_F(SkillTooltipTest, ASkillThatGivesItselfOpensItsEntryTwice)
{
    skills = BaseSkills().Byte(Given, 0x32, Given).Table();
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);

    ASSERT_TRUE(Draw(Given).HasValue());

    EXPECT_EQ(Lines().text[4], "Dada por Regalo");
    EXPECT_EQ(Lines().text[5], "Espera 0");
    EXPECT_EQ(random.State(), expected.State());
}

// La caja va arriba de y: ((N − 3) · cy + 3 · cy / 2) / ry, con N = 7 y cy = 13; en 800 × 600, ry = 1,25.
TEST_F(SkillTooltipTest, TheBoxGoesAboveYInEachResolution)
{
    ASSERT_TRUE(Draw(Slash, 0, 320, 300).HasValue());
    const std::int32_t sum = 4 * Height + 3 * Height / 2;
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, FirstTextYOfABoxAt(300 - sum, 7));

    sprites.Begin(Screen800);
    text.texts.clear();
    ASSERT_TRUE(Draw(Slash, 0, 320, 300).HasValue());
    // 71 / 1,25 = 56,8: _ftol trunca.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, FirstTextYOfABoxAt(300 - 56, 7));
    EXPECT_NE(text.texts[0].y, FirstTextYOfABoxAt(300 - 57, 7));
}

// 0x421c10: los lugares 0x78 a 0x7b del Dark Lord son la ayuda de los comandos, sin rand(): el nombre en azul y
// negrita, el color del renglón 1 en 0 y los demás como estaban (Q-C720), con la altura de 4 medios (Q-C721).
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

// Fuera de 0x78 a 0x7b, o con otra clase, el cuerpo de la skill: el byte +0x6c + lugar, que MuRet tiene hasta el 63.
TEST_F(SkillTooltipTest, OtherSlotsReadTheSkillByteOfTheRecord)
{
    HeroIs(DarkLord);
    SkillIn(0x3f, Slash);
    ASSERT_TRUE(DrawSlot(0x3f).HasValue());
    EXPECT_EQ(Lines().text[1], "Tajo");

    HeroIs(Knight);
    const auto commands = DrawSlot(0x79);
    ASSERT_FALSE(commands.HasValue());
    EXPECT_NE(commands.Error().find("0x5b91af"), std::string::npos);
    const auto negative = DrawSlot(-2);
    ASSERT_FALSE(negative.HasValue());
    EXPECT_NE(negative.Error().find("0x5b91af"), std::string::npos);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// Lo que MuRet no tiene corta con su dirección: Skill.bmd, un nombre sin '\0' en su entrada (el de la skill y el de
// la que la dio) y Text.bmd.
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
    EXPECT_EQ(world.Runtime().movement.skillDepths[Slash], 0);
}

// 0x5f7bc0: con 0x6c9320 distinto de −1, el cuerpo de la skill de ese lugar con el cuarto argumento en 0.
TEST_F(SkillTooltipTest, TheTooltipsDrawTheSkillOfTheHoveredSlot)
{
    SkillIn(4, Slash);
    Client::UI::GameUiState& ui = world.Runtime().ui;
    ui.skillTooltip = 4;
    ui.skillTooltipX = 320;
    ui.skillTooltipY = 300;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + SkillAccesses);
    GameHudContext context = Context();

    const auto drawn = Client::Scenes::DrawTheTooltips(context);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Tajo", "Golpe 26~39", "Alcance 3", "Mana 12"}));
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
