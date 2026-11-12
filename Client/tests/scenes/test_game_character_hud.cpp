#include "Scenes/GameCharacterHud.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameData.h"
#include "Scenes/GameHud.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameText.h"
#include "UI/SprintfFormat.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La ventana del personaje en el HUD (CL8 hito 5; 06 § La ventana del personaje): el dibujo de 0x5ee2e0 y el botón de
// cerrar de 0x5f7440, con Text.bmd del laboratorio y el texto falso de RecordingTextDrawer (6 columnas por letra). El
// nombre, los stats y el equipo son inventados; las cuentas de rand() salen del ASM.
namespace
{
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Models::SmallLogicalCloseRandoms;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::UI::Sprintf;

constexpr std::uint32_t Seed = 0x0badf00du;
constexpr Client::App::PixelSize Screen640{640, 480};
constexpr int Own = OwnRecordCloseRandoms;
constexpr int SkillEntry = static_cast<int>(Client::Data::Tables::SkillRecordSize);
// Los accesos de un frame sin set: los siete de arriba, 0x5b8b80, la primera vuelta del set y los tres de abajo.
constexpr int PlainWindowCloses = 7 + 1 + 1 + 3;
// El HUD sin ventanas, como en GameHudTest.
constexpr int HudBaseDraws = 19 * Own + 2 * SmallLogicalCloseRandoms + 9;
// El código del servidor elegido: el grupo 0 (el texto 540) y el número 2, con PvP (el texto 461).
constexpr std::uint16_t ServerCode = 1;
// La ventana en (0x1c2, 0): las x de los textos centrados en sus cajas y las de los renglones.
constexpr int Left = 0x1c2;
constexpr int StatX = Left + 0xe;
constexpr int LineX = Left + 0x18;
constexpr int Letter = Client::Tests::RecordingTextDrawer::LetterWidth;
// Un nombre inventado.
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

// El vértice 0 (arriba a la izquierda, con la y de GL hacia arriba) del quad `index` con la textura `slot`.
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

// Lo que 0x467060 hace con un texto de 6 columnas por letra: centrado en `box` (el modo 2) o desde x con `width`
// (el modo 1).
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
        auto loaded = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // Como lo deja la entrada al mundo con un F3 03 inventado y un F3 10 sin ítems.
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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

    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
    }

    // La clase en el registro (+0xb) y en el héroe (+0x1d4).
    void SetClass(std::uint8_t characterClass)
    {
        world.OwnCharacter().characterClass = characterClass;
        Hero().characterClass = characterClass;
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
        return Client::Scenes::DrawTheCharacterWindow(context);
    }

    std::string TextAt(std::size_t index) const
    {
        return std::string(Client::UI::TextAt(&*data.text, index).value_or("?"));
    }

    // El texto que la ventana dejó con ese contenido, o nada.
    const RecordedText* Find(std::string_view shown) const
    {
        for (const RecordedText& recorded : text.texts)
        {
            if (recorded.text == shown)
                return &recorded;
        }
        return nullptr;
    }

    // Que el texto `index` sea `shown` en (x, y), con la fuente, los colores y la caja de 0x467060.
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
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    Client::UI::Point mouse{};
    bool leftDown = false;
    std::optional<std::uint16_t> chosenServer = ServerCode;
    const Client::Data::Tables::ItemTable* items = nullptr;
    const Client::Data::Tables::SkillTable* skills = nullptr;
    Client::Scenes::GameData data;
};

// Un Dark Knight sin equipo en Lorencia y en Devias: los fondos, los cinco de los stats y cerrar; los 17 textos en su
// orden, con su lugar, su fuente, sus colores y su caja; doce cierres del registro (la primera vuelta del set corta en
// el casco vacío); la esquina en 0x78aba14 y 0x78aba18. La ventana no mira el mapa.
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
    // El fondo de arriba en (450, 0), el primer fondo de los stats en (463, 56) y el último en (463, 296), cerrar en
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

// El Dark Lord, en Lorencia y en Devias: seis fondos, el mando (+0x20 más +0x36, en naranja con algo sumado) con un
// acceso más, el daño de la skill con la energía / 20 y, con puntos para repartir, el cartel y cinco botones, el
// segundo apretado con el botón abajo encima.
TEST_F(GameCharacterHudTest, TheDarkLordAddsTheCommandAndItsButtonsInLorenciaAndDevias)
{
    SetClass(DarkLordClass);
    Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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
    // Los botones en (0x1c2 + 125, 115 + 60 · i).
    const auto second = NthQuadOf(sprites, 0x121);
    ASSERT_TRUE(second.has_value());
    EXPECT_FLOAT_EQ(second->x, 575.0f);
    EXPECT_FLOAT_EQ(second->y, 480.0f - 175.0f);
    const auto fifth = NthQuadOf(sprites, 0x120, 3);
    ASSERT_TRUE(fifth.has_value());
    EXPECT_FLOAT_EQ(fifth->y, 480.0f - 355.0f);
}

// Skill.bmd e Item.bmd inventados para la magia.
struct MagicTables
{
    Client::Data::Tables::SkillTable skills;
    Client::Data::Tables::ItemTable items;
};

MagicTables MagicTablesWith(std::uint8_t skill, std::uint8_t damage, std::uint16_t staff, std::uint8_t durability)
{
    std::vector<std::byte> skillBytes(Client::Data::Tables::SkillTableSize);
    skillBytes[std::size_t{skill} * Client::Data::Tables::SkillRecordSize + 0x21] = std::byte{damage};
    std::vector<std::byte> itemBytes(Client::Data::Tables::ItemTableSize);
    itemBytes[std::size_t{staff} * Client::Data::Tables::ItemRecordSize + 0x2e] = std::byte{durability};
    const auto skillsEncrypted =
        Client::Tests::EncryptRecords(std::move(skillBytes), Client::Data::Tables::SkillRecordSize);
    const auto itemsEncrypted =
        Client::Tests::EncryptRecords(std::move(itemBytes), Client::Data::Tables::ItemRecordSize);
    return MagicTables{.skills = Client::Data::Tables::SkillTable(std::span<const std::byte>(skillsEncrypted)),
                       .items = Client::Data::Tables::ItemTable(std::span<const std::byte>(itemsEncrypted))};
}

// El Dark Wizard: la magia de 0x5513d0 con la skill del héroe (+0x425 sobre +0x6c del registro), con su entrada de
// Skill.bmd (60 rand() en el medio) y, con un báculo en la derecha, el agregado de su +0x16 por la durabilidad que le
// queda contra el +0x2e de Item.bmd; el bit 0x02 de +0x3c suma 10 a los dos, no al máximo del agregado. Sin báculo, el
// texto 216. La velocidad es la de la magia (+0x58) y no hay daño de skill.
TEST_F(GameCharacterHudTest, TheWizardShowsTheMagicOfItsSkill)
{
    SetClass(WizardClass);
    constexpr std::uint8_t Skill = 9;
    constexpr std::uint16_t Staff = 0xa05;
    const MagicTables tables = MagicTablesWith(Skill, 20, Staff, 40);
    items = &tables.items;
    skills = &tables.skills;
    Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
    own.words6c[0] = 0x0000'0900u; // el byte +0x6d: la skill 9.
    Hero().skillIndex = 1;
    own.word5a = 12;
    own.word5c = 30;
    own.speedAdjustBits = 0x02;
    Client::World::ItemState& staff = own.equipment[0];
    staff.id = Staff;
    staff.byte16 = 50;
    staff.durability = 40;
    const CrtRandom expected = Advanced(random, PlainWindowCloses * Own + SkillEntry);

    const auto drawn = Window();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().movement.skillDepths[Skill], 0);
    ASSERT_EQ(text.texts.size(), 17u);
    // min = 12 + 20 y max = 30 + 20 + 10, más 10 cada uno; el agregado, (0,5 − 0 · 0,5) · 60.
    const std::string magic = Sprintf(TextAt(215), 42, 70, 30);
    ExpectText(15, magic, LineX, 0x14f, FontKind::Normal, 0xffffffffu, 0x80000000u, FromLeft(magic, 130));
    EXPECT_EQ(text.texts[10].text, Sprintf(TextAt(64), 9));

    staff.id = 0x0001;
    text.texts.clear();
    ASSERT_TRUE(Window().HasValue());
    EXPECT_EQ(text.texts[15].text, Sprintf(TextAt(216), 42, 70));
}

// El Magic Gladiator sin casco y con una de sus cuatro armaduras: las cinco vueltas del set y la comparación de los
// tipos % 512 (17 cierres), la defensa con la tasa (texto 207, sin +0x60), la magia y el 582 fijo un renglón más
// abajo. Con otra armadura no es set; con las botas rotas, la quinta vuelta corta antes de comparar.
TEST_F(GameCharacterHudTest, TheGladiatorSetComparesTheArmorTypes)
{
    SetClass(GladiatorClass);
    constexpr std::uint8_t Skill = 9;
    const MagicTables tables = MagicTablesWith(Skill, 20, 0xa05, 40);
    items = &tables.items;
    skills = &tables.skills;
    Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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

// Las demás clases comparan los lugares 3 a 6 con el tipo % 512 del casco; con +0x60 y el bit 0x2000000 de +0x78, la
// defensa suma nivel / 5 + 50 y lleva la tasa (texto 206).
TEST_F(GameCharacterHudTest, TheKnightSetComparesWithTheHelm)
{
    Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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
    // Y el daño con el mismo bit: nivel / 3 + 45 a los dos.
    EXPECT_EQ(text.texts[7].text, Sprintf(TextAt(204), 61, 61));
}

// El renglón del daño: dos armas en el Dark Knight lo pintan de naranja, y con el bit 0x400 de +0x78 la agilidad
// (más +0x2c) se suma a +0x4e y sale el texto 203 con la tasa.
TEST_F(GameCharacterHudTest, TheDamageLineTakesItsRateAndItsColor)
{
    Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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
    // La agilidad con lo que le suman los ítems, en 16 bits: 20 + 0xfffe = 18, en naranja.
    EXPECT_EQ(text.texts[8].text, Sprintf(TextAt(205), 18));
    EXPECT_EQ(text.texts[8].style.colors.text, 0xffff9664u);
}

// El renglón de los puntos: antes del nivel 10, en 0; desde el 10, +0x44 y +0x46 con signo y +0x48 y +0x4a negados.
// Las letras: +0x44 · 100 / +0x46 da 70, 60, 50 o 40; +0x48 pasado 10 da 40 (la división por 1, Q-C670).
TEST_F(GameCharacterHudTest, ThePointsLineAndTheLettersFollowTheAddedPoints)
{
    Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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

// Con el mouse sobre cerrar, la ayuda del texto 222 en (_ftol(x + 25), _ftol(395) − 13), con la fuente normal.
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

// La clase y el servidor van en el mismo lugar con alfas que suman 2: 2 menos (float)(sin(T · 0,001f) + 1) y eso. Sin
// PvP (el número 1 del grupo 0), el texto 460.
TEST_F(GameCharacterHudTest, TheClassAndTheServerFadeIntoEachOther)
{
    worldClock = 1000.0f;
    chosenServer = std::uint16_t{0};

    ASSERT_TRUE(Window().HasValue());

    EXPECT_EQ(text.texts[2].text, Sprintf(TextAt(460), std::string_view(TextAt(540)), 1));
    const auto alpha = static_cast<float>(std::sin(1000.0 * static_cast<double>(0.001f)) + 1.0);
    const auto classMarker = NthQuadOf(sprites, Client::Tests::TextMarkerSlot.value, 1);
    const auto serverMarker = NthQuadOf(sprites, Client::Tests::TextMarkerSlot.value, 2);
    ASSERT_TRUE(classMarker.has_value());
    ASSERT_TRUE(serverMarker.has_value());
    EXPECT_FLOAT_EQ(classMarker->color.alpha, static_cast<float>(2.0 - static_cast<double>(alpha)));
    // glColor4f con más de 1: el lote lo recorta, como GL.
    EXPECT_FLOAT_EQ(serverMarker->color.alpha, std::min(alpha, 1.0f));
    EXPECT_GT(alpha, 1.0f);
    const auto level = NthQuadOf(sprites, Client::Tests::TextMarkerSlot.value, 3);
    ASSERT_TRUE(level.has_value());
    EXPECT_EQ(level->color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));
}

// Un texto más ancho que su caja se recorta como en 0x467060: el modo 2 muestra el centro y el 1, el final.
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

// Lo que el original lee y MuRet no tiene corta con su dirección, después de los rand() de antes: sin el servidor
// elegido, después del primer acceso; con +0x46 en 0 y +0x44 en más de 10, la división de 0x5f08ac.
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

// El HUD entero con la ventana del personaje abierta, en Lorencia y en Devias: el estado base más los cierres de la
// ventana, sin corte.
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
        const auto drawn = Client::Scenes::DrawTheHud(context);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_TRUE(NthQuadOf(sprites, 0x104).has_value());
        EXPECT_TRUE(NthQuadOf(sprites, 0x118).has_value());
    }
}
} // namespace
