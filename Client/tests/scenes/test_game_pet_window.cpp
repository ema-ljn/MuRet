#include "Scenes/GamePetWindow.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameHud.h"
#include "Scenes/GamePanels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "support/RecordingTextDrawer.h"
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
#include <tuple>
#include <utility>
#include <vector>

// El dibujo de la ventana de la mascota y el cartel del cuervo (CL9 hito 4; 06 § El A9 de las mascotas, "La ventana de
// la mascota"): 0x424160 con 0x4249a0, 0x424e40, 0x416670, 0x4149b0 y la vtable + 0xc del cuervo (0x418460), en 640 ×
// 480. Los textos son inventados y miden 6 columnas por letra. Cada test corre en Lorencia y en Devias.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::WorldState;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x5eed4243u;
constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
// La ventana va siempre al lado del personaje: 0x104.
constexpr float Left = 260.0f;
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
// 0x414fd4: la skill del daño del caballo, con 20 de daño (+0x21): 20 y 30 más.
constexpr std::uint8_t HorseSkill = 0x3e;
constexpr std::uint8_t HorseSkillDamage = 20;
constexpr auto SkillCloseRandoms = static_cast<std::uint32_t>(Client::Data::Tables::SkillRecordSize);

std::map<std::size_t, std::string> PetTexts()
{
    return {{64, "Velocidad %d"},  {200, "Nivel %d"},     {201, "Exp %u/%u"},    {203, "Golpe %d~%d (%d)"},
            {358, "Vida %d/%d"},   {698, "Mando %d"},     {1002, "Cerrar"},      {1169, "Sin cuervo"},
            {1187, "Corcel"},      {1214, "Ave"},         {1217, "Bicho"},       {1218, "Ordenes"},
            {1219, "Libre"},       {1220, "Junto"},       {1221, "Ataca"},       {1222, "Ayuda"},
            {1223, "ayuda libre"}, {1224, "ayuda junto"}, {1225, "ayuda ataca"}, {1226, "ayuda ayuda"},
            {1233, "Sin %s"},      {1234, "Cetro +%d%%"}};
}

struct DrawnQuad
{
    int slot = NoTexture;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(DrawnQuad{.slot = slot,
                                      .blend = command.blend,
                                      .color = vertices[first].color,
                                      .first = vertices[first],
                                      .third = vertices[first + 2]});
        }
    }
    return quads;
}

// Las esquinas en píxeles de GL (H − (y + h) · ry).
void ExpectQuadAt(const DrawnQuad& quad, int slot, float x, float y, float width, float height)
{
    constexpr float Tolerance = 1e-3f;
    EXPECT_EQ(quad.slot, slot);
    EXPECT_NEAR(quad.first.x, x, Tolerance);
    EXPECT_NEAR(quad.first.y, 480.0f - y, Tolerance);
    EXPECT_NEAR(quad.third.x, x + width, Tolerance);
    EXPECT_NEAR(quad.third.y, 480.0f - y - height, Tolerance);
}

// Los quads de una textura, en orden.
std::vector<DrawnQuad> QuadsWith(const std::vector<DrawnQuad>& quads, int slot)
{
    std::vector<DrawnQuad> found;
    for (const DrawnQuad& quad : quads)
    {
        if (quad.slot == slot)
            found.push_back(quad);
    }
    return found;
}

Client::Data::Tables::SkillTable HorseSkills()
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    plain[std::size_t{HorseSkill} * Client::Data::Tables::SkillRecordSize + 0x21] = std::byte{HorseSkillDamage};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

class GamePetWindowTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GamePetWindowTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Runtime().map = GetParam();
        Own().equipment[0].id = Client::Scenes::EmptyOwnItemId;
    }

    Client::Scenes::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Scenes::ActorState& Hero()
    {
        return m_world->Actors()[Runtime().hero];
    }

    Client::Scenes::OwnCharacterRecord& Own()
    {
        return m_world->OwnCharacter();
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t x = 0, std::int32_t y = 0, bool projecting = false)
    {
        Client::Scenes::WorldRuntime& runtime = Runtime();
        Client::Scenes::ActorState& hero = Hero();
        GameHudContext context{.world = *m_world,
                               .random = m_random,
                               .ownRecordDepth = runtime.ownRecordDepth,
                               .cellXDepth = hero.cellXAccesses,
                               .cellYDepth = hero.cellYAccesses,
                               .hud = runtime.hud,
                               .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                               .sound = m_sound,
                               .projecting = projecting,
                               .sprites = m_sprites,
                               .text = m_text,
                               .texts = &m_texts,
                               .skills = &m_skills,
                               .mouse = {.x = x, .y = y}};
        Client::Scenes::GamePanels panels(*m_world, m_random);
        return Client::Scenes::DrawThePetWindow(context, panels.Registry());
    }

    // El personaje abierto, la ventana de la mascota abierta (0x723911 en 2) y la pestaña.
    void OpenTheWindow(std::uint8_t tab)
    {
        Runtime().panels.character = 1;
        Runtime().panels.delegated.pet = 2;
        Runtime().petTab = tab;
    }

    void GiveTheHorse()
    {
        Hero().pet.model = 0x1c16;
        Client::Scenes::PetStats& horse = Runtime().pets[Client::Scenes::DarkHorsePetStats];
        horse.level = 3;
        horse.durability = 37;
        horse.experience = 1234;
        horse.nextExperience = 5678;
        horse.word0c = 100;
        horse.word0e = 150;
        horse.word10 = 7;
        horse.word12 = 9;
    }

    Client::Scenes::DarkSpirit& GiveTheRaven()
    {
        Client::Scenes::DarkSpirit& raven = Hero().darkSpirit.emplace();
        raven.level = 2;
        raven.durability = 300;
        raven.experience = 11;
        raven.nextExperience = 22;
        raven.word454 = 40;
        raven.word456 = 60;
        raven.word458 = 5;
        raven.word45a = 8;
        raven.order = 2;
        return raven;
    }

    const Client::Tests::RecordedText* TextNamed(const std::string& text) const
    {
        for (const auto& recorded : m_text.texts)
        {
            if (recorded.text == text)
                return &recorded;
        }
        return nullptr;
    }

    void ExpectRandoms(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        EXPECT_EQ(m_random.State(), expected.State());
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(PetTexts());
    Client::Data::Tables::SkillTable m_skills = HorseSkills();
};

// 0x4249a6..0x424d9f: con el personaje cerrado, 0x723911 en 0 y nada más; sin cuervo, sin rand(). Un dibujo intermedio
// no escribe el 0.
TEST_P(GamePetWindowTest, WithTheCharacterClosedItOnlyClearsTheWindow)
{
    Runtime().panels.delegated.pet = 2;
    ASSERT_TRUE(Draw(0, 0, true).HasValue());
    EXPECT_EQ(Runtime().panels.delegated.pet, 2) << "un dibujo intermedio";
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Runtime().panels.delegated.pet, 0);
    EXPECT_TRUE(QuadsOf(m_sprites).empty());
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x4249bf..0x424a82: con el personaje abierto y la ventana cerrada, el botón (pet1.jpg, 0x65d) en (535, 395) y, con
// el mouse en [0x217, 0x22f) × [0x18b, 0x1a3), su ayuda (1217) en (0x217, 0x17e).
TEST_P(GamePetWindowTest, TheButtonAndItsHelp)
{
    Runtime().panels.character = 1;
    Runtime().panels.delegated.pet = 1;
    for (const auto& [x, y] :
         {std::pair{0x216, 0x18b}, std::pair{0x22f, 0x18b}, std::pair{0x217, 0x18a}, std::pair{0x217, 0x1a3}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        m_sprites.Begin(Screen640);
        m_text.texts.clear();
        ASSERT_TRUE(Draw(x, y).HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
        ASSERT_EQ(quads.size(), 1U);
        ExpectQuadAt(quads[0], 0x65d, 535.0f, 395.0f, 24.0f, 24.0f);
        EXPECT_FLOAT_EQ(quads[0].third.u, 0.75f);
        EXPECT_TRUE(m_text.texts.empty());
    }
    ASSERT_TRUE(Draw(0x22e, 0x1a2).HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, "Bicho");
    EXPECT_EQ(m_text.texts[0].x, 0x217);
    EXPECT_EQ(m_text.texts[0].y, 0x17e);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(Runtime().hud.textColors.background, 0U);
    EXPECT_EQ(Runtime().panels.delegated.pet, 1);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x424a88..0x424e2b y 0x424e40: la pestaña del caballo con el caballo. El botón abierto (pet2.jpg), el fondo, la
// pestaña del cuervo en gris y la del caballo en blanco, las líneas y el marco, el botón de cerrar, el título y los
// nombres; después Level_box, el nombre y los renglones de 0x4149b0 con el daño de la skill 0x3e. Tres accesos al
// registro propio (el cetro, el daño y el liderazgo) y la entrada de la skill.
TEST_P(GamePetWindowTest, TheHorseTabWithTheHorse)
{
    OpenTheWindow(0);
    GiveTheHorse();
    ASSERT_TRUE(Draw().HasValue());

    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ASSERT_GE(quads.size(), 15U);
    ExpectQuadAt(quads[0], 0x65e, 535.0f, 395.0f, 24.0f, 24.0f);
    ExpectQuadAt(quads[1], 0x104, Left, 0.0f, 190.0f, 256.0f);
    ExpectQuadAt(quads[2], 0x105, Left, 256.0f, 190.0f, 177.0f);
    ExpectQuadAt(quads[3], 0x646, Left + 60.0f, 67.0f, 49.0f, 16.0f);
    EXPECT_EQ(quads[3].color, (Color{0.6f, 0.6f, 0.6f, 1.0f}));
    EXPECT_FLOAT_EQ(quads[3].third.u, 0.765625f);
    ExpectQuadAt(quads[4], 0x646, Left + 15.0f, 63.0f, 49.0f, 20.0f);
    EXPECT_EQ(quads[4].color, White);
    ExpectQuadAt(quads[5], 0x117, Left, 80.0f, 190.0f, 10.0f);
    ExpectQuadAt(quads[6], 0x117, Left, 380.0f, 190.0f, 10.0f);
    EXPECT_FLOAT_EQ(quads[6].third.v, 0.625f);
    ExpectQuadAt(quads[7], NoTexture, Left + 12.0f, 90.0f, 166.0f, 1.0f);
    EXPECT_EQ(quads[7].color, (Color{0.9f, 0.9f, 0.6f, 1.0f}));
    ExpectQuadAt(quads[8], NoTexture, Left + 12.0f, 380.0f, 166.0f, 1.0f);
    ExpectQuadAt(quads[9], NoTexture, Left + 12.0f, 90.0f, 1.0f, 290.0f);
    ExpectQuadAt(quads[10], NoTexture, Left + 178.0f, 90.0f, 1.0f, 291.0f);
    ExpectQuadAt(quads[11], 0x118, Left + 25.0f, 395.0f, 24.0f, 24.0f);
    EXPECT_EQ(quads[11].blend, BlendMode::Alpha);
    for (std::size_t marker = 12; marker < 15; ++marker)
        EXPECT_EQ(quads[marker].slot, Marker);
    ExpectQuadAt(quads[15], 0xf5, Left + 13.0f, 96.0f, 75.0f, 21.0f);
    EXPECT_FLOAT_EQ(quads[15].third.u, 0.5859375f);

    // La barra de la durabilidad: el fondo de 145 × 8 y lo lleno, 37 · 1/255 · 143 de 6 de alto.
    const std::vector<DrawnQuad> bars = QuadsWith(quads, NoTexture);
    ASSERT_EQ(bars.size(), 6U);
    ExpectQuadAt(bars[4], NoTexture, Left + 24.0f, 135.0f, 145.0f, 8.0f);
    EXPECT_EQ(bars[4].color, (Color{0.2f, 0.0f, 0.0f, 1.0f}));
    const float filled = static_cast<float>(37.0 * static_cast<double>(1.0f / 255.0f) * 143.0);
    ExpectQuadAt(bars[5], NoTexture, Left + 25.0f, 136.0f, filled, 6.0f);
    EXPECT_EQ(bars[5].color, (Color{0.88235295f, 0.05882353f, 0.0f, 1.0f}));

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 10U);
    // El título en la caja de 120 y los nombres de las pestañas, en negrita.
    EXPECT_EQ(texts[0].text, "Bicho");
    EXPECT_EQ(texts[0].x, 0x104 + 0x23);
    EXPECT_EQ(texts[0].columns, 120);
    EXPECT_EQ(texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(texts[1].text, "Corcel");
    EXPECT_EQ(texts[1].x, 0x104 + 0x28 - 18);
    EXPECT_EQ(texts[1].y, 0x46);
    EXPECT_EQ(texts[2].text, "Bicho");
    EXPECT_EQ(texts[2].x, 0x104 + 0x55 - 15);
    EXPECT_EQ(texts[3].text, "Corcel");
    EXPECT_EQ(texts[3].x, 0x104 + 0x55 - 18);
    EXPECT_EQ(texts[3].y, 0x14);
    EXPECT_EQ(texts[3].style.font, FontKind::Bold);
    // El nivel centrado en la caja de 70, y los renglones de a 15 en la de 145.
    EXPECT_EQ(texts[4].text, "Nivel 3");
    EXPECT_EQ(texts[4].x, 0x104 + 0xe);
    EXPECT_EQ(texts[4].y, 100);
    EXPECT_EQ(texts[4].columns, 70);
    EXPECT_EQ(texts[4].style.colors.text, 0xff67bfdfu);
    EXPECT_EQ(texts[4].style.colors.background, 0U);
    const std::vector<std::pair<std::string, int>> lines{{"Vida 37/255", 120},
                                                         {"Exp 1234/5678", 150},
                                                         {"Golpe 120~180 (9)", 165},
                                                         {"Velocidad 7", 180},
                                                         {"Mando 230", 195}};
    for (std::size_t line = 0; line < lines.size(); ++line)
    {
        SCOPED_TRACE(lines[line].first);
        EXPECT_EQ(texts[line + 5].text, lines[line].first);
        EXPECT_EQ(texts[line + 5].x, 0x104 + 0x18);
        EXPECT_EQ(texts[line + 5].y, lines[line].second);
        EXPECT_TRUE(texts[line + 5].windowed);
        EXPECT_EQ(texts[line + 5].columns, 145);
        EXPECT_EQ(texts[line + 5].firstColumn, 0);
        EXPECT_EQ(texts[line + 5].style.colors.background, 0x80000000u);
    }
    // Sin el liderazgo que pide (3 · 15 + 185), en rojo.
    EXPECT_EQ(texts[9].style.colors.text, 0xff3c3cdcu);
    EXPECT_EQ(texts[8].style.colors.text, 0xffe6e6e6u);
    EXPECT_EQ(Runtime().hud.textColors.text, 0xffe6e6e6u);
    ExpectRandoms(3 * OwnRecordCloseRandoms + SkillCloseRandoms);
}

// 0x414bdd..0x414d21 y 0x415321..0x415336: con un cetro (algo en el lugar 0), su % en un renglón más, que el tipo 1 no
// suma al daño (Q-C1040); con el liderazgo (+0x36 + +0x20) que alcanza, el renglón en el gris de los otros.
TEST_P(GamePetWindowTest, TheScepterLineAndEnoughLeadership)
{
    OpenTheWindow(0);
    GiveTheHorse();
    Own().equipment[0].id = 0x2a3;
    Own().equipment[0].byte16 = 25;
    Own().leadership = 200;
    Own().word36 = 30;
    ASSERT_TRUE(Draw().HasValue());
    const auto* scepter = TextNamed("Cetro +25%");
    ASSERT_NE(scepter, nullptr);
    EXPECT_EQ(scepter->y, 165);
    const auto* damage = TextNamed("Golpe 120~180 (9)");
    ASSERT_NE(damage, nullptr);
    EXPECT_EQ(damage->y, 180);
    const auto* leadership = TextNamed("Mando 230");
    ASSERT_NE(leadership, nullptr);
    EXPECT_EQ(leadership->y, 210);
    EXPECT_EQ(leadership->style.colors.text, 0xffe6e6e6u);
    ExpectRandoms(3 * OwnRecordCloseRandoms + SkillCloseRandoms);
}

// 0x424f4d..0x424fb7: sin el caballo (otra mascota o ninguna), "Sin Corcel" con la grande en negrita, en negro en
// (x + 0x5f, 0xdc) y en rojo en (x + 0x5e, 0xdb), centrado. Sin accesos.
TEST_P(GamePetWindowTest, TheHorseTabWithoutTheHorse)
{
    OpenTheWindow(0);
    Hero().pet.model = 0x1c12;
    ASSERT_TRUE(Draw().HasValue());
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 5U);
    for (std::size_t text = 3; text < 5; ++text)
    {
        EXPECT_EQ(texts[text].text, "Sin Corcel");
        EXPECT_EQ(texts[text].style.font, FontKind::BigBold);
        EXPECT_EQ(texts[text].style.colors.background, 0U);
    }
    EXPECT_EQ(texts[3].x, 0x104 + 0x5f - 30);
    EXPECT_EQ(texts[3].y, 0xdc);
    EXPECT_EQ(texts[3].style.colors.text, 0xff000000u);
    EXPECT_EQ(texts[4].x, 0x104 + 0x5e - 30);
    EXPECT_EQ(texts[4].y, 0xdb);
    EXPECT_EQ(texts[4].style.colors.text, 0xff3c3cdcu);
    EXPECT_TRUE(QuadsWith(QuadsOf(m_sprites), 0xf5).empty());
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x416670 y 0x4155c8..0x4157af: la pestaña del cuervo con el cuervo: los dos Level_box, el título (1214) en la caja
// de 120, los renglones del tipo 0 (el daño sin la skill), el título de las órdenes y sus cuatro íconos con su texto;
// después el cartel del cuervo. Dos accesos: el liderazgo y el cartel.
TEST_P(GamePetWindowTest, TheRavenTabWithTheRaven)
{
    OpenTheWindow(1);
    GiveTheRaven();
    ASSERT_TRUE(Draw().HasValue());
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    ExpectQuadAt(quads[3], 0x646, Left + 15.0f, 67.0f, 49.0f, 16.0f);
    EXPECT_EQ(quads[3].color, (Color{0.6f, 0.6f, 0.6f, 1.0f}));
    ExpectQuadAt(quads[4], 0x646, Left + 60.0f, 63.0f, 49.0f, 20.0f);
    const std::vector<DrawnQuad> backs = QuadsWith(quads, 0xf5);
    ASSERT_EQ(backs.size(), 2U);
    ExpectQuadAt(backs[0], 0xf5, Left + 13.0f, 96.0f, 75.0f, 21.0f);
    ExpectQuadAt(backs[1], 0xf5, Left + 13.0f, 236.0f, 75.0f, 21.0f);
    // Los cuatro íconos de command.jpg y el de la orden del cartel.
    const std::vector<DrawnQuad> icons = QuadsWith(quads, 0x12c);
    ASSERT_EQ(icons.size(), 5U);
    for (std::size_t icon = 0; icon < 4; ++icon)
    {
        SCOPED_TRACE(icon);
        ExpectQuadAt(icons[icon], 0x12c, Left + 24.0f, 260.0f + 28.0f * static_cast<float>(icon), 22.0f, 26.0f);
        EXPECT_FLOAT_EQ(icons[icon].first.u, 0.125f * static_cast<float>(icon));
        EXPECT_FLOAT_EQ(icons[icon].third.u, 0.125f * static_cast<float>(icon + 1));
        EXPECT_FLOAT_EQ(icons[icon].third.v, 0.13671875f);
    }
    // La barra de la durabilidad, recortada a 255.
    const std::vector<DrawnQuad> bars = QuadsWith(quads, NoTexture);
    ASSERT_GE(bars.size(), 6U);
    ExpectQuadAt(bars[5], NoTexture, Left + 25.0f, 136.0f, 143.0f, 6.0f);

    const auto* title = TextNamed("Ave");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->x, 0x104 + 0x23);
    EXPECT_EQ(title->y, 0x14);
    EXPECT_EQ(title->columns, 120);
    const auto* life = TextNamed("Vida 300/255");
    ASSERT_NE(life, nullptr);
    EXPECT_EQ(life->y, 120);
    const auto* damage = TextNamed("Golpe 40~60 (8)");
    ASSERT_NE(damage, nullptr);
    EXPECT_EQ(damage->y, 165);
    const auto* leadership = TextNamed("Mando 215");
    ASSERT_NE(leadership, nullptr);
    EXPECT_EQ(leadership->y, 195);
    EXPECT_EQ(leadership->style.colors.text, 0xff3c3cdcu);
    const auto* orders = TextNamed("Ordenes");
    ASSERT_NE(orders, nullptr);
    EXPECT_EQ(orders->x, 0x104 + 0xe);
    EXPECT_EQ(orders->y, 0xf0);
    EXPECT_EQ(orders->columns, 70);
    EXPECT_EQ(orders->style.colors.text, 0xff67bfdfu);
    EXPECT_EQ(orders->style.colors.background, 0x0000000fu);
    const std::vector<std::string> names{"Libre", "Junto", "Ataca", "Ayuda"};
    for (std::size_t command = 0; command < names.size(); ++command)
    {
        const auto* name = TextNamed(names[command]);
        ASSERT_NE(name, nullptr) << names[command];
        EXPECT_EQ(name->x, 0x104 + 24 + 30);
        EXPECT_EQ(name->y, 270 + 28 * static_cast<int>(command));
        EXPECT_FALSE(name->windowed);
        EXPECT_EQ(name->style.colors.text, 0xffe6e6e6u);
    }
    EXPECT_EQ(TextNamed("Sin cuervo"), nullptr);
    ExpectRandoms(2 * OwnRecordCloseRandoms);
}

// 0x4157c5..0x415858: con el mouse sobre un ícono, con los bordes incluidos, su ayuda en la esquina del ícono.
TEST_P(GamePetWindowTest, TheRavenCommandsShowTheirHelp)
{
    OpenTheWindow(1);
    GiveTheRaven();
    for (const auto& [x, y] : {std::pair{0x104 + 23, 316}, std::pair{0x104 + 24 + 23, 316}, std::pair{0x104 + 24, 259},
                               std::pair{0x104 + 24, 371}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        m_text.texts.clear();
        ASSERT_TRUE(Draw(x, y).HasValue());
        for (const auto& text : m_text.texts)
            EXPECT_EQ(text.text.find("ayuda"), std::string::npos) << text.text;
    }
    for (const auto& [x, y, help] :
         {std::tuple{0x104 + 24, 316, "ayuda ataca"}, std::tuple{0x104 + 24 + 22, 316 + 26, "ayuda ataca"},
          std::tuple{0x104 + 30, 260, "ayuda libre"}})
    {
        SCOPED_TRACE(help);
        m_text.texts.clear();
        ASSERT_TRUE(Draw(x, y).HasValue());
        const auto* found = TextNamed(help);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->x, 0x104 + 24);
        EXPECT_EQ(found->y, help == std::string("ayuda libre") ? 260 : 316);
    }
}

// 0x4241a0..0x424215: la pestaña del cuervo sin cuervo, 1169 como el "no tiene" del caballo; sin cartel ni accesos.
TEST_P(GamePetWindowTest, TheRavenTabWithoutTheRaven)
{
    OpenTheWindow(1);
    GiveTheHorse();
    ASSERT_TRUE(Draw().HasValue());
    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 5U);
    EXPECT_EQ(texts[3].text, "Sin cuervo");
    EXPECT_EQ(texts[3].x, 0x104 + 0x5f - 30);
    EXPECT_EQ(texts[3].style.colors.text, 0xff000000u);
    EXPECT_EQ(texts[4].text, "Sin cuervo");
    EXPECT_EQ(texts[4].y, 0xdb);
    EXPECT_EQ(texts[4].style.colors.text, 0xff3c3cdcu);
    EXPECT_EQ(texts[4].style.font, FontKind::BigBold);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x418460 sin la ventana: el nombre en negrita que termina en _ftol(640 − 50 − 15) + 50, la barra (0x5a6af0) con la
// durabilidad del lugar 1 por 50 / 255 y el ícono de la orden 2 ((2 · 32 + 6) / 256, 3 / 256). Con la mascota de
// 0x1c12 a 0x1c16 o Fenrir, 60 más a la izquierda. Un acceso al registro propio.
TEST_P(GamePetWindowTest, TheRavenHudWithoutTheWindow)
{
    GiveTheRaven();
    Own().equipment[1].durability = 102;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, "Ave");
    EXPECT_EQ(m_text.texts[0].x, 575 + 50 - 18);
    EXPECT_EQ(m_text.texts[0].y, 4);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffdcdcdcu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0x80000000u);
    const std::vector<DrawnQuad> quads = QuadsOf(m_sprites);
    const std::vector<DrawnQuad> icons = QuadsWith(quads, 0x12c);
    ASSERT_EQ(icons.size(), 1U);
    ExpectQuadAt(icons[0], 0x12c, 555.0f, 5.0f, 16.0f, 20.0f);
    EXPECT_FLOAT_EQ(icons[0].first.u, 70.0f / 256.0f);
    EXPECT_FLOAT_EQ(icons[0].first.v, 3.0f / 256.0f);
    EXPECT_FLOAT_EQ(icons[0].third.u, 70.0f / 256.0f + 0.078125f);
    // La barra: lo lleno es 102 · 50 / 255 = 20, en (x + 2, 18).
    const std::vector<DrawnQuad> bars = QuadsWith(quads, NoTexture);
    ASSERT_FALSE(bars.empty());
    ExpectQuadAt(bars.back(), NoTexture, 577.0f, 18.0f, 20.0f, 2.0f);
    ExpectRandoms(OwnRecordCloseRandoms);

    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    Hero().pet.model = 0x1c37;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_text.texts[0].x, 515 + 50 - 18);
    ExpectQuadAt(QuadsWith(QuadsOf(m_sprites), 0x12c)[0], 0x12c, 495.0f, 5.0f, 16.0f, 20.0f);
}

// 0x41889a..0x418905: con el mouse sobre el ícono (los bordes incluidos, de 12 × 20), la ayuda de la orden (1219 +
// orden) en (_ftol(x), 25). Con el inventario abierto, cada ancho del mundo saca dos rand().
TEST_P(GamePetWindowTest, TheRavenHudOrderHelpAndTheWorldWidth)
{
    GiveTheRaven().order = 1;
    for (const auto& [x, y] : {std::pair{554, 10}, std::pair{568, 10}, std::pair{560, 4}, std::pair{560, 26}})
    {
        SCOPED_TRACE(testing::Message() << x << ", " << y);
        m_text.texts.clear();
        ASSERT_TRUE(Draw(x, y).HasValue());
        EXPECT_EQ(m_text.texts.size(), 1U);
    }
    m_text.texts.clear();
    ASSERT_TRUE(Draw(567, 25).HasValue());
    ASSERT_EQ(m_text.texts.size(), 2U);
    EXPECT_EQ(m_text.texts[1].text, "Junto");
    EXPECT_EQ(m_text.texts[1].x, 555);
    EXPECT_EQ(m_text.texts[1].y, 25);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);

    // 0x5bb8d0 con el inventario solo: la tienda del NPC y el comercio, dos rand() en cada uno.
    m_random = CrtRandom(Seed);
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Draw().HasValue());
    ExpectRandoms(OwnRecordCloseRandoms + 4);
}

// Con la ventana abierta en cualquier pestaña, el botón de cerrar muestra su ayuda (1002) en (x + 25, 382).
TEST_P(GamePetWindowTest, TheCloseButtonShowsItsHelp)
{
    OpenTheWindow(0);
    Hero().pet.model = 0xffff;
    ASSERT_TRUE(Draw(0x104 + 25 + 23, 395).HasValue());
    const auto* help = TextNamed("Cerrar");
    ASSERT_NE(help, nullptr);
    EXPECT_EQ(help->x, 0x104 + 25);
    EXPECT_EQ(help->y, 382);
    m_text.texts.clear();
    ASSERT_TRUE(Draw(0x104 + 25 + 24, 395).HasValue());
    EXPECT_EQ(TextNamed("Cerrar"), nullptr);
}

// Sin Skill.bmd, el daño del caballo corta (0x414fd6); sin un texto, corta con su dirección.
TEST_P(GamePetWindowTest, MissingTablesCut)
{
    OpenTheWindow(0);
    GiveTheHorse();
    Runtime().hud.partyCount = 0;
    Client::Scenes::WorldRuntime& runtime = Runtime();
    Client::Scenes::ActorState& hero = Hero();
    GameHudContext context{.world = *m_world,
                           .random = m_random,
                           .ownRecordDepth = runtime.ownRecordDepth,
                           .cellXDepth = hero.cellXAccesses,
                           .cellYDepth = hero.cellYAccesses,
                           .hud = runtime.hud,
                           .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                           .sound = m_sound,
                           .sprites = m_sprites,
                           .text = m_text,
                           .texts = &m_texts};
    Client::Scenes::GamePanels panels(*m_world, m_random);
    const auto drawn = Client::Scenes::DrawThePetWindow(context, panels.Registry());
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x414fd6"), std::string::npos) << drawn.Error();

    context.texts = nullptr;
    context.skills = &m_skills;
    const auto noTexts = Client::Scenes::DrawThePetWindow(context, panels.Registry());
    ASSERT_FALSE(noTexts.HasValue());
    EXPECT_NE(noTexts.Error().find("0x424dfb"), std::string::npos) << noTexts.Error();
}

INSTANTIATE_TEST_SUITE_P(Maps, GamePetWindowTest, ::testing::Values(Lorencia, Devias));
} // namespace
