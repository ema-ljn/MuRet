#include "Scenes/PetTooltip.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Network/Crypto/Xor32.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Pet/PetInfo.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/ItemTooltips.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/ItemState.h"
#include "support/Bytes.h"
#include "support/RecordingTextDrawer.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

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

// El tooltip de las mascotas, 0x422990 (CL9 hito 4; 06 § El A9 de las mascotas, "El tooltip de las mascotas"), por el
// cuerpo del ítem (0x5b2aa0) con el registro propio abierto alrededor como 0x5f7bc0, en 640 × 480 (ry = 1) y con el
// texto falso de RecordingTextDrawer (13 de alto). Los textos son inventados. Cada test corre en Lorencia y en Devias.
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::GameHudContext;
using Client::Scenes::TooltipLines;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x5eed4243u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// El IsOpen(0x10000) del precio (un byte protegido) y un acceso de Skill.bmd que cierra en 0.
constexpr std::uint32_t NpcShopQuery = 1;
constexpr auto SkillAccess = static_cast<std::uint32_t>(Client::Data::Tables::SkillRecordSize);
// Lo que había en las negritas antes: 0x422990 no las limpia.
constexpr std::int32_t StaleBold = 7;

constexpr std::uint16_t DarkHorse = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
// La skill del caballo (el tag de su opción de skill) con su maná en Skill.bmd (+0x22).
constexpr std::uint8_t HorseSkill = 0x3e;
constexpr std::uint16_t HorseSkillMana = 30;
// La caja: el centro de arriba.
constexpr std::int32_t BoxX = 320;
constexpr std::int32_t BoxY = 300;
// 0x78abb28 y 0x78abb2c: el inventario y la casilla del ítem.
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
            {698, "Mando %d"},
            {744, "Defensa total %d"},
            {959, "Defensa %d"},
            {1187, "Corcel"},
            {1188, "Habilidad %d"},
            {1189, "Temblor %d"},
            {1214, "Ave"}};
}

Client::Data::Tables::SkillTable HorseSkills()
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    const std::size_t mana = std::size_t{HorseSkill} * Client::Data::Tables::SkillRecordSize + 0x22;
    plain[mana] = std::byte{HorseSkillMana & 0xff};
    plain[mana + 1] = std::byte{HorseSkillMana >> 8};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

Client::Data::Tables::ItemTable EmptyItems()
{
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(
        std::vector<std::byte>(Client::Data::Tables::ItemTableSize), Client::Data::Tables::ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

struct Line
{
    std::string text;
    std::int32_t color = 0;
    std::int32_t bold = 0;
};

// El caballo como lo deja 0x54bf40: la opción de skill y la 0x96 (con un valor viejo) desde la segunda.
ItemState TheHorse()
{
    ItemState item;
    item.id = DarkHorse;
    item.scalar1Low = 0x80;
    item.optionCount = 2;
    item.optionBytes[0] = HorseSkill;
    item.optionBytes[1] = 0x96;
    item.optionBytes[Client::World::ItemOptionValueOffset + 1] = 99;
    return item;
}

// El cuervo con una opción 0x50.
ItemState TheRaven()
{
    ItemState item;
    item.id = DarkRaven;
    item.scalar1Low = 0x81;
    item.optionCount = 1;
    item.optionBytes[0] = 0x50;
    item.optionBytes[Client::World::ItemOptionValueOffset] = 4;
    return item;
}

class PetTooltipTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    PetTooltipTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Client::Scenes::WorldRuntime& runtime = Runtime();
        runtime.map = GetParam();
        runtime.ui.field78abb28 = 0;
        runtime.ui.field78abb2c = HoveredSlot;
        for (std::int32_t& bold : Lines().bold)
            bold = StaleBold;
        Client::Scenes::OwnCharacterRecord& own = m_world->OwnCharacter();
        own.level = 200;
        own.agility = 40;
        own.leadership = 300;
        own.word36 = 10;
        Client::Scenes::PetStats& horse = runtime.pets[Client::Scenes::DarkHorsePetStats];
        horse.level = 3;
        horse.experience = 1234;
        horse.durability = 200;
        Client::Scenes::PetStats& raven = runtime.pets[Client::Scenes::RavenPetStats];
        raven.level = 2;
        raven.experience = 500;
        raven.durability = 150;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    TooltipLines& Lines()
    {
        return Runtime().hud.tooltip;
    }

    GameHudContext Context()
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
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

    // 0x5f7bc0: el cuerpo del ítem (0x5b2aa0) con el registro propio abierto alrededor.
    Client::Core::Expected<void, std::string> Hover(ItemState& item, std::int32_t y = BoxY)
    {
        GameHudContext context = Context();
        const Client::Render::Models::LogicalAccess own(Runtime().ownRecordDepth, m_random, OwnRecordCloseRandoms);
        return Client::Scenes::DrawTheItemTooltip(
            Client::Scenes::ItemTooltipContext{.hud = context, .x = BoxX, .y = y, .item = item});
    }

    // Dos pasadas: la primera pide el A9 y la segunda dibuja. Devuelve la segunda.
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
        for (std::size_t i = expected.size(); i < Client::Scenes::TooltipLineCount; ++i)
            EXPECT_TRUE(lines.text[i].empty()) << "renglón " << i;
        // 0x422990 cuenta en EBX y [EBP−0x24]: 0x78abb30 y 0x78abb34 quedan en el 0 de 0x5b2aa0.
        EXPECT_EQ(lines.next, 0);
        EXPECT_EQ(lines.halves, 0);
    }

    // El A9 que salió, en claro.
    std::vector<std::byte> SentBytes(std::size_t index) const
    {
        const Client::Network::Packet decoded = Client::Network::Crypto::Xor32Decode(m_sent.at(index).packet);
        const std::span<const std::byte> bytes = decoded.Bytes();
        return {bytes.begin(), bytes.end()};
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<std::int32_t> m_sound;
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(PetTooltipTexts());
    Client::Data::Tables::ItemTable m_items = EmptyItems();
    Client::Data::Tables::SkillTable m_skills = HorseSkills();
    bool m_projecting = false;
    std::vector<Client::Protocol::Outgoing> m_sent;
};

// 0x4229d4..0x422a12 y 0x423c65..0x42403d: la primera vez anota el ítem, pide su A9 (la mascota, 0x78abb28 y 0x78abb2c)
// y no dibuja nada, con los renglones ya limpios por 0x5b2aa0.
TEST_P(PetTooltipTest, TheFirstHoverAsksForThePetAndDrawsNothing)
{
    Lines().text[4] = "viejo";
    ItemState horse = TheHorse();
    const CrtRandom expected = Advanced(m_random, OwnRecordCloseRandoms);

    const auto drawn = Hover(horse);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_TRUE(Client::Tests::SameBytes(SentBytes(0), Client::Tests::Bytes({0xc1, 0x06, 0xa9, 0x01, 0x00, 20})));
    const Client::Scenes::PetTooltipQuery& query = Runtime().petTooltipQuery;
    EXPECT_EQ(query.key, (static_cast<std::uint32_t>(BoxY) << 16) | static_cast<std::uint32_t>(BoxX));
    EXPECT_EQ(query.type, DarkHorse);
    EXPECT_EQ(query.scalar, 0x80u);
    EXPECT_TRUE(Lines().text[4].empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_random.State(), expected.State());
    // Sin el 0x4221d0 ni el 0x4225d0 de la pasada que dibuja.
    EXPECT_EQ(Runtime().pets[Client::Scenes::DarkHorsePetStats].nextExperience, 0u);
    EXPECT_EQ(horse.optionBytes[Client::World::ItemOptionValueOffset + 1], 99);
}

// Cualquier cambio del lugar, del tipo o de D+4 vuelve a pedir; el cuervo pide la mascota 0.
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

// La segunda pasada del caballo: 0x4221d0, el nombre, la experiencia, el nivel, la durabilidad, el nivel pedido (3 · 2
// + 0xda = 224, y al héroe de 200 le faltan 24), las clases, las dos opciones (la de skill lee su maná en Skill.bmd; la
// 0x96 la reescribe 0x4225d0: 40 / 20 + 2 · 3 + 5), la defensa ((3 + 30) / 2) y la skill.
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
    EXPECT_EQ(horse.optionBytes[Client::World::ItemOptionValueOffset + 1], 13);
    EXPECT_EQ(Runtime().pets[Client::Scenes::DarkHorsePetStats].nextExperience, 89600u);
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().ownRecordDepth, 0);
    EXPECT_EQ(Runtime().movement.skillDepths[HorseSkill], 0);
    // 0x423c0f..0x423c3d: 14 renglones y 11 medios, (14 − 11) · 13 + 11 · 13 / 2 = 110 arriba de y.
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].text, "Corcel");
    EXPECT_EQ(m_text.texts[0].y, BoxY - 110);
}

// Con el nivel pedido cumplido, el renglón en blanco y sin lo que falta; sin lugar arriba, la caja va en y.
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
    // 13 renglones y 11 medios: 26 + 71 = 97, más que 50.
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].y, 50);
}

// 0x423adc..0x423ae7: el caballo sin opciones no entra al lazo, y 0x4225d0 no corre ni le agrega la 0x96.
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

// Q-C1044: con la 0x96 en la primera opción, cada 0x4225d0 del lazo agrega otra (Q-C415) y la cuenta sube con el
// índice. El original sigue hasta que el byte da la vuelta, escribiendo más allá del ítem; MuRet corta cuando la 0x96
// cae fuera de las nueve que guarda.
TEST_P(PetTooltipTest, AHorseWithItsDefenseFirstGrowsItsOptionsUntilItCuts)
{
    ItemState horse = TheHorse();
    horse.optionCount = 1;
    horse.optionBytes[0] = 0x96;

    const auto drawn = HoverTwice(horse);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x423b07"), std::string::npos) << drawn.Error();
    EXPECT_EQ(horse.optionCount, Client::World::MaxItemOptions);
    EXPECT_EQ(Lines().text[17], "Defensa 13");
}

// El cuervo: el daño (+0xc, +0xe y +0x12) y la velocidad (+0x10) que calcula 0x4221d0 con el liderazgo 10 + 300, la
// durabilidad, el liderazgo pedido (2 · 15 + 0xb9 = 215, cumplido) y su opción, sin la defensa ni la skill del caballo.
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
    // 12 renglones y 11 medios: 13 + 71 = 84.
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].y, BoxY - 84);
}

// 0x423839..0x423887: el liderazgo del héroe se suma en 16 bits: 65.500 + 100 es 64, y le faltan 151.
TEST_P(PetTooltipTest, TheRavenLeadershipAddsInSixteenBits)
{
    Client::Scenes::OwnCharacterRecord& own = m_world->OwnCharacter();
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

// 0x422a43..0x422a55: con la tienda de NPC abierta, el precio (0x5b1130, CL9.3) corta después de su rand(). La primera
// pasada no llega a mirarla.
TEST_P(PetTooltipTest, TheNpcShopPriceCutsAfterItsQuery)
{
    Runtime().panels.npcShop = 1;
    ItemState horse = TheHorse();
    ASSERT_TRUE(Hover(horse).HasValue());
    const CrtRandom expected = Advanced(m_random, OwnRecordCloseRandoms + NpcShopQuery);

    const auto drawn = Hover(horse);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x422a55"), std::string::npos) << drawn.Error();
    EXPECT_EQ(m_random.State(), expected.State());
}

// Un dibujo intermedio de MuRet no anota ni pide, y dibuja sobre copias: la tabla de las mascotas y el ítem quedan.
TEST_P(PetTooltipTest, AProjectedDrawNeitherAsksNorWrites)
{
    ItemState& cell = m_world->Inventory().cells[3];
    cell = TheHorse();
    Client::UI::GameUiState& ui = Runtime().ui;
    ui.hoveredItem = Client::UI::HoveredItem{.place = Client::UI::HoveredItem::Place::InventoryCell, .index = 3};
    ui.skillTooltipX = BoxX;
    ui.skillTooltipY = BoxY;
    const auto draw = [this](bool projecting)
    {
        m_projecting = projecting;
        GameHudContext context = Context();
        return Client::Scenes::DrawTheTooltips(context);
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
    EXPECT_EQ(Runtime().pets[Client::Scenes::DarkHorsePetStats].nextExperience, 0u);
    EXPECT_EQ(cell.optionBytes[Client::World::ItemOptionValueOffset + 1], 99);
    ASSERT_TRUE(draw(false).HasValue());
    EXPECT_EQ(Runtime().pets[Client::Scenes::DarkHorsePetStats].nextExperience, 89600u);
    EXPECT_EQ(cell.optionBytes[Client::World::ItemOptionValueOffset + 1], 13);
}

// Con otro tipo, 0x422990 devuelve 0 sin tocar nada (0x4229c3).
TEST_P(PetTooltipTest, AnotherTypeIsNotAPet)
{
    ItemState item;
    item.id = 0x1a03;
    GameHudContext context = Context();

    const auto pet = Client::Scenes::DrawThePetTooltip(
        Client::Scenes::ItemTooltipContext{.hud = context, .x = BoxX, .y = BoxY, .item = item});

    ASSERT_TRUE(pet.HasValue()) << pet.Error();
    EXPECT_FALSE(pet.Value());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Runtime().petTooltipQuery.type, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, PetTooltipTest, ::testing::Values(Lorencia, Devias));
} // namespace
