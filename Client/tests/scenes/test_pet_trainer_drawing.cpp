#include "PetTrainer/PetTrainerDrawing.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The trainer's drawing (0x44b510, CL9 milestone 6, phase 4b): the menu (0x44b610), recovery (0x44c850, with the
// rand() of 0x44b190's own record and its own) and the box (0x44d680), in 640 × 480. The texts and the name are made
// up and measure 6 columns per letter; the world in Lorencia (0) and Devias (2).
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Tests::RecordedText;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint32_t Seed = 0x7a1e5u;
constexpr int Own = Client::Models::OwnRecordCloseRandoms;
constexpr std::array<std::int32_t, 3> Maps{0, 2, 3};
constexpr int NoTexture = -1;
constexpr int Marker = -2;
constexpr std::uint16_t Horse = 0x1a04;
constexpr std::size_t HorseSlot = 8;
constexpr std::uint8_t PetDurability = 255;

std::map<std::size_t, std::string> TrainerTexts()
{
    return {{584, "%s: %d%%"},    {586, "joya para %s"}, {587, "y nada mas"},   {1002, "cerrar"},
            {1187, "caballo"},    {1202, "elegi"},       {1203, "hola %s"},     {1204, "recuperar"},
            {1205, "mezclar"},    {1212, "mezcla"},      {1213, "sin %s"},      {1214, "cuervo"},
            {1227, "Entrenador"}, {1228, "a quien"},     {1229, "no esta"},     {1230, "esta entero"},
            {1231, "faltan %s"},  {1232, "cuesta %s"},   {1622, "zen %s (%s)"}, {1623, "tasa %d%%"}};
}

Client::Tables::ItemTable SyntheticItems()
{
    using Client::Tables::ItemRecordSize;
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    plain[std::size_t{Horse} * ItemRecordSize + 0x2d] = std::byte{PetDurability};
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

// The slots of the batch's quads, in order (NoTexture without a texture, Marker for a text's mark).
std::vector<int> SlotsOf(const SpriteBatch& sprites)
{
    std::vector<int> slots;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        int slot = NoTexture;
        if (command.texture == Client::Tests::TextMarkerSlot)
            slot = Marker;
        else if (command.texture)
            slot = command.texture->value;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            slots.push_back(slot);
    }
    return slots;
}

// The slots without the texts' marks.
std::vector<int> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<int> quads;
    for (const int slot : SlotsOf(sprites))
    {
        if (slot != Marker)
            quads.push_back(slot);
    }
    return quads;
}

// The x of a text centered on `x` (0x623690): 6 columns per letter in 640.
std::int32_t Centered(std::int32_t x, const std::string& text)
{
    return x - static_cast<std::int32_t>(text.size()) * 3;
}

class PetTrainerDrawingTest : public ::testing::Test
{
protected:
    PetTrainerDrawingTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        m_world->OwnCharacter().ClearEquipmentForEntry();
        Runtime().pets[Client::Kernel::DarkHorsePetStats].level = 3;
        Runtime().field78abb10 = 1;
        auto& name = m_world->Hero().name;
        const std::string_view hero = "Domador";
        for (std::size_t i = 0; i < hero.size(); ++i)
            name[i] = static_cast<std::byte>(hero[i]);
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Core::Expected<void, std::string> Draw(std::int32_t mode, std::int32_t x = 0, std::int32_t y = 0,
                                                   bool leftDown = false, bool projecting = false)
    {
        Client::Kernel::WorldRuntime& runtime = Runtime();
        runtime.panels.delegated.trainer = mode;
        Client::Kernel::ActorState& hero = m_world->Hero();
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
                               .items = &m_items,
                               .mouse = {.x = x, .y = y},
                               .leftDown = leftDown};
        return Client::PetTrainer::DrawTheTrainer(context);
    }

    void Restart()
    {
        m_text.texts.clear();
        m_sprites.Begin(Screen640);
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    Client::Tests::RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(TrainerTexts());
    Client::Tables::ItemTable m_items = SyntheticItems();
};

void ExpectCentered(const RecordedText& text, std::int32_t x, std::int32_t y, const std::string& expected,
                    std::uint32_t color, FontKind font)
{
    EXPECT_EQ(text.text, expected);
    EXPECT_EQ(text.x, Centered(x, expected)) << expected;
    EXPECT_EQ(text.y, y) << expected;
    EXPECT_EQ(text.style.colors.text, color) << expected;
    EXPECT_EQ(text.style.font, font) << expected;
}

// 0x44b510
TEST_F(PetTrainerDrawingTest, TheIdleModesDrawNothing)
{
    for (const std::int32_t mode : {0, 4, 5, -1})
    {
        ASSERT_TRUE(Draw(mode).HasValue());
        EXPECT_TRUE(m_sprites.Commands().empty()) << mode;
        EXPECT_TRUE(m_text.texts.empty()) << mode;
    }
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x44b610: five strips of 0xfb, the two lines of 0xfc, the two frames of 0xf0 and the close button; the title in the
// 203-column box, black on gray; 1202 and 1203 with the name centered on 0x13f; the two labels at 0xb9 and 0xd7, the
// first in bold with the mouse over it. No rand().
TEST_F(PetTrainerDrawingTest, TheMenuDrawsItsTitleLinesAndLabels)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        ASSERT_TRUE(Draw(1, 0x103, 0xb4).HasValue());
        EXPECT_EQ(m_random.State(), Seed);
        EXPECT_EQ(QuadsOf(m_sprites), (std::vector<int>{0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfc, 0xfc, 0xf0, 0xf0, 0x118}));
        ASSERT_EQ(m_text.texts.size(), 5u);
        const RecordedText& title = m_text.texts[0];
        EXPECT_TRUE(title.windowed);
        EXPECT_EQ(title.text, "Entrenador");
        EXPECT_EQ(title.x, 0xda);
        EXPECT_EQ(title.y, 0x64);
        // cx + 2 · ((203 − cx) / 2): with an even-width text, the odd box loses a column.
        EXPECT_EQ(title.columns, 202);
        EXPECT_EQ(title.style.colors.text, 0u);
        EXPECT_EQ(title.style.colors.background, 0xc8c8c8c8u);
        EXPECT_EQ(title.style.font, FontKind::Bold);
        ExpectCentered(m_text.texts[1], 0x13f, 0x78, "elegi", 0xc8c8c8c8u, FontKind::Normal);
        ExpectCentered(m_text.texts[2], 0x13f, 0x8c, "hola Domador", 0xc8c8c8c8u, FontKind::Normal);
        ExpectCentered(m_text.texts[3], 0x13f, 0xb9, "recuperar", 0xff67bfdfu, FontKind::Bold);
        ExpectCentered(m_text.texts[4], 0x13f, 0xd7, "mezclar", 0xff67bfdfu, FontKind::Normal);
    }
}

// 0x44b89b..0x44b8f6: the close button's help in [0x18a, 0x1a2) × [0x109, 0x121), one column right of the one that
// closes in the step ([0x189, 0x1a1), Q-C958).
TEST_F(PetTrainerDrawingTest, TheMenuCloseHelpIsOneColumnRightOfTheStep)
{
    ASSERT_TRUE(Draw(1, 0x18a, 0x109).HasValue());
    const RecordedText& help = m_text.texts.back();
    EXPECT_EQ(help.text, "cerrar");
    EXPECT_EQ(help.x, 0x18a);
    EXPECT_EQ(help.y, 0xfc);
    // 0x44b8ec writes a 0xff000000 background, which 0x554340 overwrites at 0x55450f.
    EXPECT_EQ(help.style.colors.background, 0u);
    EXPECT_EQ(Runtime().hud.textColors.background, 0u);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);

    for (const auto& [x, y] : {std::pair{0x189, 0x109}, std::pair{0x1a2, 0x109}, std::pair{0x18a, 0x121}})
    {
        Restart();
        ASSERT_TRUE(Draw(1, x, y).HasValue());
        EXPECT_NE(m_text.texts.back().text, "cerrar") << x << ", " << y;
    }
}

// 0x44c850 with the horse at 100 of 255 and without the raven, with no zen: the horse costs 221000 and all of it is
// missing (white on red); without the raven, its label in red in the normal font and "no esta", also on red. The
// rand(): two closes of 0x44b190 and the drawing's with the horse, one and the drawing's without the raven.
TEST_F(PetTrainerDrawingTest, TheRecoveryQuotesEachPetWithItsRecordCloses)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        Runtime().field78abb10 = 1;
        m_world->OwnCharacter().equipment[HorseSlot] = ItemState{.id = Horse, .durability = 100, .occupied = 1};
        m_world->OwnCharacter().zen = 0;
        const CrtRandom expected = Advanced(m_random, 5 * Own);
        const auto drawn = Draw(2, 0x103, 0x96);
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(m_random.State(), expected.State());
        EXPECT_EQ(Runtime().ownRecordDepth, 0);
        EXPECT_EQ(Runtime().field78abb10, 0);
        EXPECT_EQ(QuadsOf(m_sprites), (std::vector<int>{0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfc, 0xfc, 0xf0, 0xf0, 0x118}));
        ASSERT_EQ(m_text.texts.size(), 6u);
        EXPECT_EQ(m_text.texts[0].text, "Entrenador");
        ExpectCentered(m_text.texts[1], 0x13f, 0x78, "a quien", 0xffc8c8c8u, FontKind::Normal);
        ExpectCentered(m_text.texts[2], 0x13f, 0x9b, "caballo", 0xff67bfdfu, FontKind::Bold);
        ExpectCentered(m_text.texts[3], 0x13f, 0xb4, "faltan 221,000", 0xffffffffu, FontKind::Bold);
        EXPECT_EQ(m_text.texts[3].style.colors.background, 0xc80000ffu);
        ExpectCentered(m_text.texts[4], 0x13f, 0xd7, "cuervo", 0xff1933ffu, FontKind::Normal);
        ExpectCentered(m_text.texts[5], 0x13f, 0xf0, "no esta", 0xffffffffu, FontKind::Normal);
        EXPECT_EQ(Runtime().hud.textColors.background, 0u);
    }
}

// With plenty of zen, the price without the doubling (Q-C955) in the labels' color; a MuRet intermediate draw takes
// the same rand() but does not write 0x78abb10. Full, "esta entero" with a single close in 0x44b190.
TEST_F(PetTrainerDrawingTest, TheRecoveryWithZenShowsTheCost)
{
    m_world->OwnCharacter().equipment[HorseSlot] = ItemState{.id = Horse, .durability = 100, .occupied = 1};
    m_world->OwnCharacter().zen = 2'000'000'000;
    const CrtRandom expected = Advanced(m_random, 5 * Own);
    ASSERT_TRUE(Draw(2, 0, 0, false, true).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Runtime().field78abb10, 1);
    ExpectCentered(m_text.texts[3], 0x13f, 0xb4, "cuesta 221,000", 0xff67bfdfu, FontKind::Normal);
    EXPECT_EQ(m_text.texts[3].style.colors.background, 0u);

    Restart();
    m_world->OwnCharacter().equipment[HorseSlot].durability = PetDurability;
    const CrtRandom full = Advanced(m_random, 4 * Own);
    ASSERT_TRUE(Draw(2).HasValue());
    EXPECT_EQ(m_random.State(), full.State());
    EXPECT_EQ(m_text.texts[3].text, "esta entero");
}

// 0x44d680 without a mix: the window (0x104, 0x105), the box's grid (0x115 empty, 0x116 with a type), the two 0x117
// bars, mix button 0x124 and the close one; title 1205 and the rate in the 120 box; "sin mezcla" in red at 0x32, rate
// 0 at 65 and the warnings at 215 and 230. No rand().
TEST_F(PetTrainerDrawingTest, TheBoxWithoutAMixShowsTheEmptyName)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        Restart();
        Runtime().map = map;
        Runtime().npcWindows.trainerBox[1] = ItemState{.id = 0x1c0d, .occupied = 1};
        Runtime().npcWindows.chaosTaxRate = 4;
        Runtime().panels.delegated.field724584 = 0;
        ASSERT_TRUE(Draw(3).HasValue());
        EXPECT_EQ(m_random.State(), Seed);
        const std::vector<int> quads = QuadsOf(m_sprites);
        ASSERT_EQ(quads.size(), 2u + 32u + 4u);
        EXPECT_EQ(quads[0], 0x104);
        EXPECT_EQ(quads[1], 0x105);
        EXPECT_EQ(quads[2], 0x115);
        EXPECT_EQ(quads[3], 0x116);
        EXPECT_EQ(quads[34], 0x117);
        EXPECT_EQ(quads[35], 0x117);
        EXPECT_EQ(quads[36], 0x124);
        EXPECT_EQ(quads[37], 0x118);
        ASSERT_EQ(m_text.texts.size(), 6u);
        EXPECT_EQ(m_text.texts[0].text, "mezclar");
        EXPECT_EQ(m_text.texts[0].x, 0x127);
        EXPECT_EQ(m_text.texts[0].columns, 120);
        EXPECT_EQ(m_text.texts[1].text, "tasa 4%");
        EXPECT_EQ(m_text.texts[1].y, 0x14);
        ExpectCentered(m_text.texts[2], 0x163, 0x32, "sin mezcla", 0xff3030ffu, FontKind::Bold);
        ExpectCentered(m_text.texts[3], 0x163, 65, "mezcla: 0%", 0xffdcdcdcu, FontKind::Bold);
        ExpectCentered(m_text.texts[4], 0x163, 215, "joya para mezcla", 0xffdcdcdcu, FontKind::Bold);
        ExpectCentered(m_text.texts[5], 0x163, 230, "y nada mas", 0xffdcdcdcu, FontKind::Bold);
    }
}

// With the horse mix (1) and the raven's (2): the name "%s %s" in light blue, rate 60 and the taxed price (5000000 and
// 1000000 at 4%) at 80. With another mix it fails.
TEST_F(PetTrainerDrawingTest, TheBoxShowsTheMixOfTheLastStep)
{
    Runtime().npcWindows.chaosTaxRate = 4;
    Runtime().panels.delegated.field724584 = 1;
    ASSERT_TRUE(Draw(3).HasValue());
    ASSERT_EQ(m_text.texts.size(), 7u);
    ExpectCentered(m_text.texts[2], 0x163, 0x32, "caballo mezcla", 0xff19ccffu, FontKind::Bold);
    ExpectCentered(m_text.texts[3], 0x163, 65, "mezcla: 60%", 0xffdcdcdcu, FontKind::Bold);
    ExpectCentered(m_text.texts[4], 0x163, 80, "zen 5,200,000 (5,000,000)", 0xffdcdcdcu, FontKind::Bold);
    EXPECT_EQ(m_text.texts[5].y, 215);

    Restart();
    Runtime().panels.delegated.field724584 = 2;
    ASSERT_TRUE(Draw(3).HasValue());
    ExpectCentered(m_text.texts[2], 0x163, 0x32, "cuervo mezcla", 0xff19ccffu, FontKind::Bold);
    ExpectCentered(m_text.texts[4], 0x163, 80, "zen 1,040,000 (1,000,000)", 0xffdcdcdcu, FontKind::Bold);

    Restart();
    Runtime().panels.delegated.field724584 = 3;
    const auto cut = Draw(3);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x44da59"), std::string::npos);
}

// 0x44d76d..0x44d834: with the mouse in [335, 379) × [300, 332) help 1212 at (0x14f, 0x11f) and, with the button
// down, the 0x125; 0x44db2b..0x44dba1: the close one's in [280, 304) × [395, 419), at (0x118, 0x17e).
TEST_F(PetTrainerDrawingTest, TheBoxButtonsShowTheirHelp)
{
    ASSERT_TRUE(Draw(3, 378, 331, true).HasValue());
    std::vector<int> quads = QuadsOf(m_sprites);
    EXPECT_EQ(std::count(quads.begin(), quads.end(), 0x125), 1);
    ASSERT_FALSE(m_text.texts.empty());
    EXPECT_EQ(m_text.texts[0].text, "mezcla");
    EXPECT_EQ(m_text.texts[0].x, 0x14f);
    EXPECT_EQ(m_text.texts[0].y, 0x11f);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0u);
    EXPECT_EQ(m_text.texts[1].text, "mezclar");

    Restart();
    ASSERT_TRUE(Draw(3, 379, 300, true).HasValue());
    quads = QuadsOf(m_sprites);
    EXPECT_EQ(std::count(quads.begin(), quads.end(), 0x125), 0);
    EXPECT_EQ(m_text.texts[0].text, "mezclar");

    Restart();
    ASSERT_TRUE(Draw(3, 303, 418).HasValue());
    const RecordedText& help = m_text.texts.back();
    EXPECT_EQ(help.text, "cerrar");
    EXPECT_EQ(help.x, 0x118);
    EXPECT_EQ(help.y, 0x17e);
    EXPECT_EQ(Runtime().hud.dcFont, FontKind::Normal);

    Restart();
    ASSERT_TRUE(Draw(3, 304, 418).HasValue());
    EXPECT_NE(m_text.texts.back().text, "cerrar");
}
} // namespace
