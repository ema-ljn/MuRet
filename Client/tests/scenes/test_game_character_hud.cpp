#include "Hud/GameCharacterHud.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Models/LogicalAccess.h"
#include "Math/RoundedMath.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Tables/GameData.h"
#include "Hud/GameHud.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameText.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"
#include "support/TableFiles.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The character window in the HUD (CL8 milestone 5; 06 § La ventana del personaje): the drawing of 0x5ee2e0 and the
// close button of 0x5f7440, with the lab's Text.bmd and RecordingTextDrawer's fake text (6 columns per letter). The
// name, the stats and the equipment are made up; the rand() counts come from the ASM.
namespace
{
using Client::Crt::CrtRandom;
using Client::Crt::Sprintf;
using Client::Ui::GameHudContext;
using Client::Kernel::WorldState;
using Client::Math::RoundedSin;
using Client::Models::OwnRecordCloseRandoms;
using Client::Models::SmallLogicalCloseRandoms;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Tests::RecordedText;

constexpr std::uint32_t Seed = 0x0badf00du;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr int Own = OwnRecordCloseRandoms;
constexpr int SkillEntry = static_cast<int>(Client::Tables::SkillRecordSize);
// The accesses of a frame without a set: the seven above, 0x5b8b80, the set's first pass and the three below.
constexpr int PlainWindowCloses = 7 + 1 + 1 + 3;
// The HUD with no windows, as in GameHudTest.
constexpr int HudBaseDraws = 19 * Own + 2 * SmallLogicalCloseRandoms + 9;
// The chosen server's code: group 0 (text 540) and number 2, with PvP (text 461).
constexpr std::uint16_t ServerCode = 1;
// The window at (0x1c2, 0): the x of the texts centered in their boxes and of the lines.
constexpr int Left = 0x1c2;
constexpr int StatX = Left + 0xe;
constexpr int LineX = Left + 0x18;
constexpr int Letter = Client::Tests::RecordingTextDrawer::LetterWidth;
constexpr std::string_view Name = "Probador";

constexpr std::uint8_t WizardClass = 0;
constexpr std::uint8_t KnightClass = 1;
constexpr std::uint8_t GladiatorClass = 3;
constexpr std::uint8_t DarkLordClass = 4;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

std::vector<std::uint16_t> SlotsOf(const SpriteBatch& sprites)
{
    std::vector<std::uint16_t> slots;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            slots.push_back(command.texture->value);
    }
    return slots;
}

// Vertex 0 (top left, with GL's y going up) of quad `index` with texture `slot`.
std::optional<Client::Render::SpriteVertex> NthQuadOf(const SpriteBatch& sprites, std::uint16_t slot,
                                                      std::size_t index = 0)
{
    const auto vertices = sprites.Vertices();
    std::size_t seen = 0;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture || command.texture->value != slot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            if (seen++ == index)
                return vertices[command.firstVertex + quad * 6];
        }
    }
    return std::nullopt;
}

// What 0x467060 does with a text of 6 columns per letter: centered in `box` (mode 2) or from x with `width` (mode 1).
struct Columns
{
    int first = 0;
    int count = 0;
};

Columns Centered(std::string_view text, int box)
{
    const int width = static_cast<int>(text.size()) * Letter;
    const int offset = (box - width) / 2;
    return Columns{.first = -offset, .count = width + 2 * offset};
}

Columns FromLeft(std::string_view text, int width)
{
    const int measured = static_cast<int>(text.size()) * Letter;
    return Columns{.first = width < measured ? measured - width : 0, .count = width};
}

class GameCharacterHudTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto loaded = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // As world entry leaves it with a made-up F3 03 and an F3 10 with no items.
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.ClearEquipmentForEntry();
        std::memcpy(own.name.data(), Name.data(), Name.size());
        own.level = 50;
        own.experience = 1234;
        own.nextExperience = 5678;
        own.strength = 28;
        own.agility = 20;
        own.vitality = 25;
        own.energy = 10;
        own.leadership = 0;
        own.life = 90;
        own.maxLife = 110;
        own.mana = 20;
        own.maxMana = 30;
        own.attackSpeed = 7;
        own.magicSpeed = 9;
        world.Inventory().ClearForInventoryReply();
        world.Runtime().panels.character = 1;
        SetClass(KnightClass);
        Hero().pk = 3;
        sprites.Begin(Screen640);
    }

    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
    }

    // The class in the record (+0xb) and in the hero (+0x1d4).
    void SetClass(std::uint8_t characterClass)
    {
        world.OwnCharacter().characterClass = characterClass;
        Hero().characterClass = characterClass;
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
                              .texts = &*data.text,
                              .items = items != nullptr ? items : &data.items->table,
                              .skills = skills != nullptr ? skills : &*data.skills,
                              .worldClock = worldClock,
                              .mouse = mouse,
                              .leftDown = leftDown,
                              .chosenServer = chosenServer};
    }

    Client::Core::Expected<void, std::string> Window()
    {
        GameHudContext context = Context();
        return Client::Hud::DrawTheCharacterWindow(context);
    }

    std::string TextAt(std::size_t index) const
    {
        return std::string(Client::Ui::TextAt(&*data.text, index).value_or("?"));
    }

    const RecordedText* Find(std::string_view shown) const
    {
        for (const RecordedText& recorded : text.texts)
        {
            if (recorded.text == shown)
                return &recorded;
        }
        return nullptr;
    }

    void ExpectText(std::size_t index, std::string_view shown, int x, int y, FontKind font, std::uint32_t color,
                    std::uint32_t background, Columns columns) const
    {
        SCOPED_TRACE(shown);
        ASSERT_LT(index, text.texts.size());
        const RecordedText& recorded = text.texts[index];
        EXPECT_EQ(recorded.text, shown);
        EXPECT_EQ(recorded.x, x);
        EXPECT_EQ(recorded.y, y);
        EXPECT_EQ(recorded.style.font, font);
        EXPECT_EQ(recorded.style.colors.text, color);
        EXPECT_EQ(recorded.style.colors.background, background);
        EXPECT_TRUE(recorded.windowed);
        EXPECT_EQ(recorded.firstColumn, columns.first);
        EXPECT_EQ(recorded.columns, columns.count);
    }

    WorldState world;
    CrtRandom random{Seed};
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::Ui::Point mouse{};
    bool leftDown = false;
    std::optional<std::uint16_t> chosenServer = ServerCode;
    const Client::Tables::ItemTable* items = nullptr;
    const Client::Tables::SkillTable* skills = nullptr;
    Client::Tables::GameData data;
};

// A Dark Knight with no equipment: the backgrounds, the five of the stats and close; the 17 texts in their order, with
// their place, font, colors and box; twelve record closes (the set's first pass stops at the empty helm); the corner
// at 0x78aba14 and 0x78aba18. The window does not look at the map.
TEST_F(GameCharacterHudTest, TheKnightWindowDrawsItsQuadsTextsAndRandomsInLorenciaAndDevias)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        world.Runtime().hud.characterX = 0;
        sprites.Begin(Screen640);
        text.texts.clear();
        random = CrtRandom{Seed};
        const CrtRandom expected = Advanced(random, PlainWindowCloses * Own);

        const auto drawn = Window();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(world.Runtime().hud.characterX, Left);
        EXPECT_EQ(world.Runtime().hud.characterY, 0);
        EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{0x104, 0x105, 0xf5, 0xf5, 0xf5, 0xf5, 0xf5, 0x118}));
        ASSERT_EQ(text.texts.size(), 17u);
        const std::string server = Sprintf(TextAt(461), std::string_view(TextAt(540)), 2);
        const std::string className = "(" + TextAt(21) + ")";
        const std::string level = Sprintf(TextAt(200), 50);
        const std::string experience = Sprintf(TextAt(201), 1234, 5678);
        const std::string points = TextAt(1412) + " 0/0 | " + TextAt(1903) + " 0/0";
        const std::string strength = Sprintf(TextAt(202), 28);
        const std::string damage = Sprintf(TextAt(204), 0, 0);
        const std::string agility = Sprintf(TextAt(205), 20);
        const std::string defense = Sprintf(TextAt(209), 0);
        const std::string speed = Sprintf(TextAt(64), 7);
        const std::string vitality = Sprintf(TextAt(210), 25);
        const std::string life = Sprintf(TextAt(211), 90, 110);
        const std::string energy = Sprintf(TextAt(212), 10);
        const std::string mana = Sprintf(TextAt(213), 20, 30);
        const std::string skill = Sprintf(TextAt(582), 201);
        const std::string letters = Sprintf(TextAt(1907), 100, 100);
        constexpr std::uint32_t Orange = 0xffff9664u;
        constexpr std::uint32_t Blue = 0xff67bfdfu;
        constexpr std::uint32_t White = 0xffffffffu;
        constexpr std::uint32_t Shade = 0x80000000u;
        constexpr std::uint32_t PkThree = 0xffffdcc8u;
        ExpectText(0, Name, Left + 0x23, 0xc, FontKind::Bold, PkThree, 0xff141414u, Centered(Name, 120));
        ExpectText(1, className, Left + 0x16, 0x16, FontKind::Bold, PkThree, 0xff141414u, Centered(className, 150));
        ExpectText(2, server, Left + 0x16, 0x16, FontKind::Bold, PkThree, 0xff141414u, Centered(server, 150));
        ExpectText(3, level, StatX, 0x3c, FontKind::Bold, 0xffe6e6e6u, 0, Centered(level, 82));
        ExpectText(4, experience, LineX, 0x50, FontKind::Normal, White, Shade, FromLeft(experience, 145));
        ExpectText(5, points, LineX, 0x5f, FontKind::Bold, Orange, Shade, FromLeft(points, 130));
        ExpectText(6, strength, StatX, 0x78, FontKind::Bold, Blue, 0, Centered(strength, 70));
        ExpectText(7, damage, LineX, 0x8c, FontKind::Normal, White, Shade, FromLeft(damage, 130));
        ExpectText(8, agility, StatX, 0xb4, FontKind::Bold, Blue, 0, Centered(agility, 70));
        ExpectText(9, defense, LineX, 200, FontKind::Normal, White, Shade, FromLeft(defense, 130));
        ExpectText(10, speed, LineX, 0xd7, FontKind::Normal, White, Shade, FromLeft(speed, 130));
        ExpectText(11, vitality, StatX, 0xf0, FontKind::Bold, Blue, 0, Centered(vitality, 70));
        ExpectText(12, life, LineX, 0x104, FontKind::Normal, White, Shade, FromLeft(life, 130));
        ExpectText(13, energy, StatX, 300, FontKind::Bold, Blue, 0, Centered(energy, 70));
        ExpectText(14, mana, LineX, 0x140, FontKind::Normal, White, Shade, FromLeft(mana, 130));
        ExpectText(15, skill, LineX, 0x14f, FontKind::Normal, White, Shade, FromLeft(skill, 130));
        ExpectText(16, letters, Left + 0x5b, 0x41, FontKind::Normal, Orange, Shade, Centered(letters, 84));
    }
    // The top background at (450, 0), the first stats background at (463, 56) and the last at (463, 296), close at
    // (475, 395).
    const auto back = NthQuadOf(sprites, 0x104);
    ASSERT_TRUE(back.has_value());
    EXPECT_FLOAT_EQ(back->x, 450.0f);
    EXPECT_FLOAT_EQ(back->y, 480.0f);
    const auto lower = NthQuadOf(sprites, 0x105);
    ASSERT_TRUE(lower.has_value());
    EXPECT_FLOAT_EQ(lower->y, 480.0f - 256.0f);
    const auto firstStat = NthQuadOf(sprites, 0xf5);
    ASSERT_TRUE(firstStat.has_value());
    EXPECT_FLOAT_EQ(firstStat->x, 463.0f);
    EXPECT_FLOAT_EQ(firstStat->y, 480.0f - 56.0f);
    const auto lastStat = NthQuadOf(sprites, 0xf5, 4);
    ASSERT_TRUE(lastStat.has_value());
    EXPECT_FLOAT_EQ(lastStat->y, 480.0f - 296.0f);
    const auto close = NthQuadOf(sprites, 0x118);
    ASSERT_TRUE(close.has_value());
    EXPECT_FLOAT_EQ(close->x, 475.0f);
    EXPECT_FLOAT_EQ(close->y, 480.0f - 395.0f);
}

// The Dark Lord: six backgrounds, the command (+0x20 plus +0x36, orange when something is added) with one more access,
// the skill damage with energy / 20 and, with points to spend, the banner and five buttons, the second pressed with
// the button down over it.
TEST_F(GameCharacterHudTest, TheDarkLordAddsTheCommandAndItsButtonsInLorenciaAndDevias)
{
    SetClass(DarkLordClass);
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    own.leadership = 25;
    own.word36 = 5;
    own.energy = 85;
    own.levelUpPoints = 7;
    mouse = {Left + 125 + 3, 60 + 115 + 3};
    leftDown = true;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        sprites.Begin(Screen640);
        text.texts.clear();
        random = CrtRandom{Seed};
        const CrtRandom expected = Advanced(random, (PlainWindowCloses + 1) * Own);

        const auto drawn = Window();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_EQ(SlotsOf(sprites), (std::vector<std::uint16_t>{0x104, 0x105, 0xf5, 0xf5, 0xf5, 0xf5, 0xf5, 0xf5, 0x120,
                                                                0x121, 0x120, 0x120, 0x120, 0x118}));
        ASSERT_EQ(text.texts.size(), 19u);
        EXPECT_EQ(text.texts[1].text, "(" + TextAt(24) + ")");
        const std::string skill = Sprintf(TextAt(582), 85 / 20 + 200);
        ExpectText(15, skill, LineX, 0x14f, FontKind::Normal, 0xffffffffu, 0x80000000u, FromLeft(skill, 130));
        const std::string points = Sprintf(TextAt(217), 7);
        ExpectText(16, points, Left + 0x5f, 0x32, FontKind::Bold, 0xff000000u, 0xffc86e1eu, Centered(points, 80));
        const std::string command = Sprintf(TextAt(1170), 30);
        ExpectText(18, command, StatX, 0x168, FontKind::Bold, 0xffff9664u, 0, Centered(command, 70));
    }
    // The buttons at (0x1c2 + 125, 115 + 60 · i).
    const auto second = NthQuadOf(sprites, 0x121);
    ASSERT_TRUE(second.has_value());
    EXPECT_FLOAT_EQ(second->x, 575.0f);
    EXPECT_FLOAT_EQ(second->y, 480.0f - 175.0f);
    const auto fifth = NthQuadOf(sprites, 0x120, 3);
    ASSERT_TRUE(fifth.has_value());
    EXPECT_FLOAT_EQ(fifth->y, 480.0f - 355.0f);
}

// Made-up Skill.bmd and Item.bmd for the magic.
struct MagicTables
{
    Client::Tables::SkillTable skills;
    Client::Tables::ItemTable items;
};

MagicTables MagicTablesWith(std::uint8_t skill, std::uint8_t damage, std::uint16_t staff, std::uint8_t durability)
{
    std::vector<std::byte> skillBytes(Client::Tables::SkillTableSize);
    skillBytes[std::size_t{skill} * Client::Tables::SkillRecordSize + 0x21] = std::byte{damage};
    std::vector<std::byte> itemBytes(Client::Tables::ItemTableSize);
    itemBytes[std::size_t{staff} * Client::Tables::ItemRecordSize + 0x2e] = std::byte{durability};
    const auto skillsEncrypted = Client::Tests::EncryptRecords(std::move(skillBytes), Client::Tables::SkillRecordSize);
    const auto itemsEncrypted = Client::Tests::EncryptRecords(std::move(itemBytes), Client::Tables::ItemRecordSize);
    return MagicTables{.skills = Client::Tables::SkillTable(std::span<const std::byte>(skillsEncrypted)),
                       .items = Client::Tables::ItemTable(std::span<const std::byte>(itemsEncrypted))};
}

// The Dark Wizard: the magic of 0x5513d0 with the hero's skill (+0x425 over the record's +0x6c), its Skill.bmd entry
// (60 rand() in between) and, with a staff in the right hand, the bonus of its +0x16 by the durability it has left
// against Item.bmd's +0x2e; bit 0x02 of +0x3c adds 10 to both, not to the bonus's maximum. Without a staff, text 216.
// The speed is the magic's (+0x58) and there is no skill damage.
TEST_F(GameCharacterHudTest, TheWizardShowsTheMagicOfItsSkill)
{
    SetClass(WizardClass);
    constexpr std::uint8_t Skill = 9;
    constexpr std::uint16_t Staff = 0xa05;
    const MagicTables tables = MagicTablesWith(Skill, 20, Staff, 40);
    items = &tables.items;
    skills = &tables.skills;
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    own.words6c[0] = 0x0000'0900u; // byte +0x6d: skill 9
    Hero().skillIndex = 1;
    own.word5a = 12;
    own.word5c = 30;
    own.speedAdjustBits = 0x02;
    Client::Kernel::ItemState& staff = own.equipment[0];
    staff.id = Staff;
    staff.byte16 = 50;
    staff.durability = 40;
    const CrtRandom expected = Advanced(random, PlainWindowCloses * Own + SkillEntry);

    const auto drawn = Window();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().skillDepths[Skill], 0);
    ASSERT_EQ(text.texts.size(), 17u);
    // min = 12 + 20 and max = 30 + 20 + 10, plus 10 each; the bonus, (0.5 − 0 · 0.5) · 60.
    const std::string magic = Sprintf(TextAt(215), 42, 70, 30);
    ExpectText(15, magic, LineX, 0x14f, FontKind::Normal, 0xffffffffu, 0x80000000u, FromLeft(magic, 130));
    EXPECT_EQ(text.texts[10].text, Sprintf(TextAt(64), 9));

    staff.id = 0x0001;
    text.texts.clear();
    ASSERT_TRUE(Window().HasValue());
    EXPECT_EQ(text.texts[15].text, Sprintf(TextAt(216), 42, 70));
}

// The Magic Gladiator with no helm and one of its four armors: the set's five passes and the comparison of the types
// % 512 (17 closes), the defense with the rate (text 207, without +0x60), the magic and the fixed 582 one line lower.
// With another armor it is not a set; with broken boots, the fifth pass stops before comparing.
TEST_F(GameCharacterHudTest, TheGladiatorSetComparesTheArmorTypes)
{
    SetClass(GladiatorClass);
    constexpr std::uint8_t Skill = 9;
    const MagicTables tables = MagicTablesWith(Skill, 20, 0xa05, 40);
    items = &tables.items;
    skills = &tables.skills;
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    own.words6c[0] = Skill;
    own.word62 = 123;
    const std::array<std::uint16_t, 4> pieces{0x100f, 0x120f, 0x140f, 0x160f};
    for (std::size_t i = 0; i < pieces.size(); ++i)
    {
        own.equipment[3 + i].id = pieces[i];
        own.equipment[3 + i].durability = 10;
    }
    const CrtRandom expected = Advanced(random, (PlainWindowCloses + 5) * Own + SkillEntry);

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(text.texts.size(), 18u);
    EXPECT_EQ(text.texts[9].text, Sprintf(TextAt(207), 123, 12));
    EXPECT_EQ(text.texts[15].text, Sprintf(TextAt(216), 20, 30));
    const std::string skill = Sprintf(TextAt(582), 200);
    ExpectText(16, skill, LineX, 0x15e, FontKind::Normal, 0xffffffffu, 0x80000000u, FromLeft(skill, 130));

    own.equipment[3].id = 0x1010;
    own.equipment[4].id = 0x1210;
    own.equipment[5].id = 0x1410;
    own.equipment[6].id = 0x1610;
    text.texts.clear();
    random = CrtRandom{Seed};
    ASSERT_TRUE(Window().HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(text.texts[9].text, Sprintf(TextAt(209), 123));

    own.equipment[3].id = 0x100f;
    own.equipment[4].id = 0x120f;
    own.equipment[5].id = 0x140f;
    own.equipment[6].id = 0x160f;
    own.equipment[6].durability = 0;
    text.texts.clear();
    random = CrtRandom{Seed};
    ASSERT_TRUE(Window().HasValue());
    EXPECT_EQ(random.State(), Advanced(CrtRandom{Seed}, (PlainWindowCloses + 4) * Own + SkillEntry).State());
    EXPECT_EQ(text.texts[9].text, Sprintf(TextAt(209), 123));
}

// The other classes compare slots 3 to 6 with the helm's type % 512; with +0x60 and bit 0x2000000 of +0x78, the
// defense adds level / 5 + 50 and carries the rate (text 206).
TEST_F(GameCharacterHudTest, TheKnightSetComparesWithTheHelm)
{
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    for (std::size_t slot = 2; slot <= 6; ++slot)
    {
        own.equipment[slot].id = static_cast<std::uint16_t>(0x0e01 + 0x200 * (slot - 2));
        own.equipment[slot].durability = 5;
    }
    own.word60 = 4;
    own.word62 = 100;
    Hero().flags78 = 0x2000000u;
    const CrtRandom expected = Advanced(random, (PlainWindowCloses + 5) * Own);

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(text.texts[9].text, Sprintf(TextAt(206), 160, 4, 16));
    // And the damage with the same bit: level / 3 + 45 to both.
    EXPECT_EQ(text.texts[7].text, Sprintf(TextAt(204), 61, 61));
}

// The damage line: two weapons on the Dark Knight paint it orange, and with bit 0x400 of +0x78 the agility (plus
// +0x2c) is added to +0x4e and text 203 comes out with the rate.
TEST_F(GameCharacterHudTest, TheDamageLineTakesItsRateAndItsColor)
{
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    own.equipment[0].id = 0x0001;
    own.equipment[1].id = 0x0002;
    own.word50 = 40;
    own.word52 = 60;
    own.word2c = 0xfffe;
    own.word4e = 3;
    Hero().flags78 = 0x400u;

    ASSERT_TRUE(Window().HasValue());

    const std::string damage = Sprintf(TextAt(203), 22, 33, 3 + 18);
    ExpectText(7, damage, LineX, 0x8c, FontKind::Normal, 0xffff9664u, 0x80000000u, FromLeft(damage, 130));
    // The agility with what the items add, in 16 bits: 20 + 0xfffe = 18, in orange.
    EXPECT_EQ(text.texts[8].text, Sprintf(TextAt(205), 18));
    EXPECT_EQ(text.texts[8].style.colors.text, 0xffff9664u);
}

// The points line: before level 10, at 0; from 10, +0x44 and +0x46 signed and +0x48 and +0x4a negated. The letters:
// +0x44 · 100 / +0x46 gives 70, 60, 50 or 40; +0x48 past 10 gives 40 (the division by 1, Q-C670).
TEST_F(GameCharacterHudTest, ThePointsLineAndTheLettersFollowTheAddedPoints)
{
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    own.level = 9;
    own.addedPoints = 30;
    own.maxAddedPoints = 100;
    ASSERT_TRUE(Window().HasValue());
    EXPECT_EQ(text.texts[5].text, TextAt(1412) + " 0/0 | " + TextAt(1903) + " 0/0");
    EXPECT_EQ(text.texts[16].text, Sprintf(TextAt(1907), 60, 100));

    struct Case
    {
        std::uint16_t added;
        std::uint16_t maxAdded;
        std::uint16_t word48;
        int first;
        int second;
    };
    const std::array<Case, 7> cases{{{10, 0, 0, 100, 100},
                                     {11, 200, 11, 70, 40},
                                     {30, 100, 10, 60, 100},
                                     {31, 100, 3, 50, 100},
                                     {50, 100, 0, 50, 100},
                                     {51, 100, 0, 40, 100},
                                     {0xffff, 100, 0, 100, 100}}};
    own.level = 10;
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.added);
        own.addedPoints = probe.added;
        own.maxAddedPoints = probe.maxAdded;
        own.word48 = probe.word48;
        text.texts.clear();
        ASSERT_TRUE(Window().HasValue());
        EXPECT_EQ(text.texts[5].text, TextAt(1412) + " " + std::to_string(static_cast<std::int16_t>(probe.added)) +
                                          "/" + std::to_string(probe.maxAdded) + " | " + TextAt(1903) + " " +
                                          std::to_string(-static_cast<int>(probe.word48)) + "/0");
        EXPECT_EQ(text.texts[16].text, Sprintf(TextAt(1907), probe.first, probe.second));
    }
}

// With the mouse over close, the help of text 222 at (_ftol(x + 25), _ftol(395) − 13), with the normal font.
TEST_F(GameCharacterHudTest, TheCloseButtonShowsItsHelp)
{
    mouse = {Left + 25 + 5, 395 + 5};

    ASSERT_TRUE(Window().HasValue());

    ASSERT_EQ(text.texts.size(), 18u);
    EXPECT_EQ(text.texts[17].text, TextAt(222));
    EXPECT_EQ(text.texts[17].x, Left + 25);
    EXPECT_EQ(text.texts[17].y, 395 - 13);
    EXPECT_EQ(text.texts[17].style.font, FontKind::Normal);
}

// The class and the server go in the same place with alphas that add up to 2: 2 minus (float)(sin(T · 0.001f) + 1),
// and that. Without PvP (number 1 of group 0), text 460.
TEST_F(GameCharacterHudTest, TheClassAndTheServerFadeIntoEachOther)
{
    worldClock = 1000.0f;
    chosenServer = std::uint16_t{0};

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(text.texts[2].text, Sprintf(TextAt(460), std::string_view(TextAt(540)), 1));
    const auto alpha = static_cast<float>(RoundedSin(1000.0 * static_cast<double>(0.001f)) + 1.0);
    const auto classMarker = NthQuadOf(sprites, Client::Tests::TextMarkerSlot.value, 1);
    const auto serverMarker = NthQuadOf(sprites, Client::Tests::TextMarkerSlot.value, 2);
    ASSERT_TRUE(classMarker.has_value());
    ASSERT_TRUE(serverMarker.has_value());
    EXPECT_FLOAT_EQ(classMarker->color.alpha, static_cast<float>(2.0 - static_cast<double>(alpha)));
    // glColor4f with more than 1: the batch clamps it, like GL.
    EXPECT_FLOAT_EQ(serverMarker->color.alpha, std::min(alpha, 1.0f));
    EXPECT_GT(alpha, 1.0f);
    const auto level = NthQuadOf(sprites, Client::Tests::TextMarkerSlot.value, 3);
    ASSERT_TRUE(level.has_value());
    EXPECT_EQ(level->color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));
}

// A text wider than its box is clipped as in 0x467060: mode 2 shows the center and mode 1 the end.
TEST_F(GameCharacterHudTest, ATextWiderThanItsBoxIsClipped)
{
    world.OwnCharacter().experience = 4000000000u;
    world.OwnCharacter().nextExperience = 4100000000u;
    world.OwnCharacter().strength = 32000;

    ASSERT_TRUE(Window().HasValue());

    const RecordedText& experience = text.texts[4];
    const int measured = static_cast<int>(experience.text.size()) * Letter;
    ASSERT_GT(measured, 145);
    EXPECT_EQ(experience.firstColumn, measured - 145);
    EXPECT_EQ(experience.columns, 145);
    const RecordedText& strength = text.texts[6];
    const int width = static_cast<int>(strength.text.size()) * Letter;
    ASSERT_GT(width, 70);
    EXPECT_EQ(strength.firstColumn, -((70 - width) / 2));
    EXPECT_EQ(strength.columns, width + 2 * ((70 - width) / 2));
}

// What the original reads and MuRet does not have stops with its address, after the earlier rand(): without the
// chosen server, after the first access; with +0x46 at 0 and +0x44 above 10, the division at 0x5f08ac.
TEST_F(GameCharacterHudTest, WhatMuRetDoesNotHaveStopsWithItsAddress)
{
    chosenServer.reset();
    const CrtRandom afterFirst = Advanced(random, Own);
    const auto noServer = Window();
    ASSERT_FALSE(noServer.HasValue());
    EXPECT_NE(noServer.Error().find("0x5ee629"), std::string::npos);
    EXPECT_EQ(random.State(), afterFirst.State());

    chosenServer = ServerCode;
    world.OwnCharacter().addedPoints = 11;
    world.OwnCharacter().maxAddedPoints = 0;
    const auto zero = Window();
    ASSERT_FALSE(zero.HasValue());
    EXPECT_NE(zero.Error().find("0x5f08ac"), std::string::npos);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// The base state plus the window's closes, with no stop.
TEST_F(GameCharacterHudTest, TheHudWithTheCharacterOpenSpendsItsRandomsInLorenciaAndDevias)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        random = CrtRandom{Seed};
        sprites.Begin(Screen640);
        const CrtRandom expected = Advanced(random, HudBaseDraws + PlainWindowCloses * Own);

        GameHudContext context = Context();
        const auto drawn = Client::Hud::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_TRUE(NthQuadOf(sprites, 0x104).has_value());
        EXPECT_TRUE(NthQuadOf(sprites, 0x118).has_value());
    }
}
} // namespace
