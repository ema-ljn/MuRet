#include "Hud/GameHud.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Protocol/Items/Inventory.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Events/EventMapObject.h"
#include "Tables/GameData.h"
#include "Scenes/GameProjection.h"
#include "Items/HudItems.h"
#include "Items/InventoryMoves.h"
#include "Hud/MapBanner.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Social/SocialState.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "CastleSiege/CastleBattleState.h"
#include "Chat/ChatInput.h"
#include "Ui/GameText.h"
#include "Messenger/LetterModel.h"
#include "Ui/OldWindows.h"
#include "Crt/OriginalFormat.h"
#include "Crt/SprintfFormat.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/GoldenDir.h"
#include "support/RecordingTextDrawer.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

// The game's HUD (0x5a7320), its overlays (0x5b0550) and the cursor (0x5ac2d0), CL8 milestone 2. The big test is frame
// 1 of Lorencia's GL trace (GLIntercept, 1024 × 768, lines 124230..126903, outside the repo): every 2D quad, with its
// texture, blending, color, top left corner and bottom right corner, as the trace prints them. The character state
// comes from what it draws: life 1421 of 31131 (the only one that gives the trace's orb), mana 10613 full, AG 17749
// full, experience with no progress, cell (143, 120), skill 0x2c, 653 life potions and 760 mana potions, a helper with
// the bar at 48 and the mouse at (356, 193).
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Models::SmallLogicalCloseRandoms;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;

constexpr std::uint32_t Seed = 0x2468ace0u;
constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Client::Core::PixelSize Screen1024{1024, 768};

// The base state: 19 own record closes (the arrows, the twelve notices, the bottom bar, the experience, the AG and the
// three of the main bar), the hero's two cells and nine queries of the trade (0x20) and of the NPC shop (0x10000): the
// notices, the windows, the 3D, the grids and the IsOpen(0) at the end.
constexpr int HudBaseDraws = 19 * OwnRecordCloseRandoms + 2 * SmallLogicalCloseRandoms + 9;
// The bar's skill: its Skill.bmd entry (60) at 0x5a5926 and in the two calls to 0x40ca20.
constexpr int SkillIconDraws = 3 * 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

Client::Tables::SkillTable SkillsForTheList()
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    plain[9 * Client::Tables::SkillRecordSize + 0x31] = std::byte{1};
    plain[2 * Client::Tables::SkillRecordSize + 0x31] = std::byte{2};
    const auto encrypted = Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(encrypted);
}

// A batch quad: its texture, the blending, the color of its vertices and the fan's corners 0 and 2 (top left and
// bottom right, in GL pixels with y up).
struct DrawnQuad
{
    std::optional<std::uint16_t> slot;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// The batch's quads in order, without the text markers.
std::vector<DrawnQuad> QuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(
                DrawnQuad{.slot = command.texture ? std::optional<std::uint16_t>{command.texture->value} : std::nullopt,
                          .blend = command.blend,
                          .color = vertices[first].color,
                          .first = vertices[first],
                          .third = vertices[first + 2]});
        }
    }
    return quads;
}

// A trace quad: the line of its glBegin, the MuRet slot of its texture (or none), the blending, the color and corners 0
// and 2 with their UVs.
struct TraceQuad
{
    int line = 0;
    std::optional<std::uint16_t> slot;
    BlendMode blend = BlendMode::Alpha;
    Color color{};
    std::array<float, 4> first{};
    std::array<float, 4> third{};
};

constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Digits{1.0f, 0.95f, 0.75f, 1.0f};
constexpr Color Grey{0.6f, 0.6f, 0.6f, 1.0f};
constexpr std::optional<std::uint16_t> NoTexture{};
constexpr std::uint16_t DigitSlot = 1;

// The 2D quads of Lorencia's frame 1, without the helper's text (124259): the helper's bar (0x5a6af0), the bottom bar
// (0x5a9290), the experience, the AG, the orbs, the skill, the cells, the life, the mana, the slots (0x5a9630) and the
// two cursors (0x5ac2d0). GL textures 21, 18, 19, 20, 74, 22, 23, 879, 2 and 9 are the slots 0xe9, 0xe6, 0xe7, 0xe8,
// 0x101, 0xea, 0xeb, 0x12a, 1 and 2.
const std::vector<TraceQuad>& LorenciaFrameQuads()
{
    static const std::vector<TraceQuad> quads{
        {124272,
         NoTexture,
         BlendMode::Alpha,
         {0.0f, 0.0f, 0.0f, 0.5f},
         {921.600037f, 740.799988f},
         {1008.000061f, 731.200012f}},
        {124285,
         NoTexture,
         BlendMode::Additive,
         {0.0f, 0.2f, 0.2f, 1.0f},
         {920.0f, 742.400024f},
         {1006.400024f, 732.799988f}},
        {124292,
         NoTexture,
         BlendMode::Additive,
         {0.0f, 0.196078f, 0.196078f, 1.0f},
         {923.200012f, 739.200012f},
         {1003.200012f, 736.0f}},
        {124299,
         NoTexture,
         BlendMode::Additive,
         {0.0f, 0.784314f, 0.196078f, 1.0f},
         {923.200012f, 739.200012f},
         {1000.0f, 736.0f}},
        {124324,
         std::uint16_t{0xe9},
         BlendMode::Alpha,
         White,
         {0.0f, 148.799988f, 0.0f, 0.0f},
         {172.800003f, 76.799988f, 0.84375f, 0.703125f}},
        {124334,
         std::uint16_t{0xe9},
         BlendMode::Alpha,
         White,
         {851.200012f, 148.799988f, 0.84375f, 0.0f},
         {1024.0f, 76.799988f, 0.0f, 0.703125f}},
        {124348,
         std::uint16_t{0xe6},
         BlendMode::Opaque,
         White,
         {0.0f, 76.799988f, 0.0f, 0.0f},
         {409.600006f, -0.000015f, 1.0f, 0.75f}},
        {124359,
         std::uint16_t{0xe7},
         BlendMode::Opaque,
         White,
         {409.600006f, 76.799988f, 0.0f, 0.0f},
         {614.400024f, -0.000015f, 1.0f, 0.75f}},
        {124370,
         std::uint16_t{0xe8},
         BlendMode::Opaque,
         White,
         {614.400024f, 76.799988f, 0.0f, 0.0f},
         {1024.0f, -0.000015f, 1.0f, 0.75f}},
        {124388,
         NoTexture,
         BlendMode::Alpha,
         {0.92f, 0.8f, 0.34f, 1.0f},
         {353.600006f, 65.599976f},
         {353.600006f, 59.199974f}},
        {124398,
         DigitSlot,
         BlendMode::Alpha,
         {0.91f, 0.81f, 0.6f, 1.0f},
         {672.799988f, 73.599976f, 0.0f, 0.0f},
         {687.200012f, 57.599976f, 0.0625f, 0.5f}},
        {124418,
         std::uint16_t{0x101},
         BlendMode::Alpha,
         Digits,
         {881.600037f, 68.799988f, 0.0f, 0.0f},
         {905.600037f, 11.199986f, 0.9375f, 0.5625f}},
        {124429,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {877.600037f, 20.799988f, 0.0625f, 0.0f},
         {892.000061f, 4.799988f, 0.125f, 0.5f}},
        {124439,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {887.679993f, 20.799988f, 0.4375f, 0.0f},
         {902.080017f, 4.799988f, 0.5f, 0.5f}},
        {124449,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {897.759949f, 20.799988f, 0.4375f, 0.0f},
         {912.159973f, 4.799988f, 0.5f, 0.5f}},
        {124459,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {907.839966f, 20.799988f, 0.25f, 0.0f},
         {922.239990f, 4.799988f, 0.3125f, 0.5f}},
        {124469,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {917.919922f, 20.799988f, 0.5625f, 0.0f},
         {932.319946f, 4.799988f, 0.625f, 0.5f}},
        {126601,
         std::uint16_t{0xea},
         BlendMode::Alpha,
         White,
         {155.199997f, 3.505554f, 0.0f, 0.715766f},
         {240.0f, -72.149223f, 0.828125f, 1.465766f}},
        {126612,
         std::uint16_t{0xeb},
         BlendMode::Alpha,
         White,
         {782.400024f, 76.799988f, 0.0f, 0.0f},
         {867.200012f, -0.000015f, 0.828125f, 0.75f}},
        {126625,
         std::uint16_t{0x12a},
         BlendMode::Alpha,
         {1.0f, 0.5f, 0.5f, 1.0f},
         {486.399994f, 57.599976f, 0.375f, 0.703125f},
         {537.599976f, -0.000027f, 0.5f, 0.839844f}},
        {126637,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {19.200001f, 28.799988f, 0.0625f, 0.0f},
         {32.0f, 14.399987f, 0.125f, 0.5f}},
        {126647,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {28.160002f, 28.799988f, 0.25f, 0.0f},
         {40.960003f, 14.399987f, 0.3125f, 0.5f}},
        {126657,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {37.120003f, 28.799988f, 0.1875f, 0.0f},
         {49.920002f, 14.399987f, 0.25f, 0.5f}},
        {126667,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {57.600002f, 28.799988f, 0.0625f, 0.0f},
         {70.400002f, 14.399987f, 0.125f, 0.5f}},
        {126677,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {66.559998f, 28.799988f, 0.125f, 0.0f},
         {79.360001f, 14.399987f, 0.1875f, 0.5f}},
        {126687,
         DigitSlot,
         BlendMode::Alpha,
         Grey,
         {75.519997f, 28.799988f, 0.0f, 0.0f},
         {88.320000f, 14.399987f, 0.0625f, 0.5f}},
        {126698,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {131.199997f, 20.799988f, 0.0625f, 0.0f},
         {145.599991f, 4.799988f, 0.125f, 0.5f}},
        {126708,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {141.280014f, 20.799988f, 0.25f, 0.0f},
         {155.680008f, 4.799988f, 0.3125f, 0.5f}},
        {126718,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {151.360016f, 20.799988f, 0.125f, 0.0f},
         {165.760010f, 4.799988f, 0.1875f, 0.5f}},
        {126728,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {161.440018f, 20.799988f, 0.0625f, 0.0f},
         {175.840012f, 4.799988f, 0.125f, 0.5f}},
        {126738,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {828.0f, 20.799988f, 0.0625f, 0.0f},
         {842.400024f, 4.799988f, 0.125f, 0.5f}},
        {126748,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {838.080017f, 20.799988f, 0.0f, 0.0f},
         {852.480042f, 4.799988f, 0.0625f, 0.5f}},
        {126758,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {848.159973f, 20.799988f, 0.375f, 0.0f},
         {862.559998f, 4.799988f, 0.4375f, 0.5f}},
        {126768,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {858.239929f, 20.799988f, 0.0625f, 0.0f},
         {872.639954f, 4.799988f, 0.125f, 0.5f}},
        {126778,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {868.319946f, 20.799988f, 0.1875f, 0.0f},
         {882.719971f, 4.799988f, 0.25f, 0.5f}},
        {126788,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {340.0f, 52.799988f, 0.375f, 0.0f},
         {354.399994f, 36.799988f, 0.4375f, 0.5f}},
        {126798,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {350.080017f, 52.799988f, 0.3125f, 0.0f},
         {364.480011f, 36.799988f, 0.375f, 0.5f}},
        {126808,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {360.160004f, 52.799988f, 0.1875f, 0.0f},
         {374.559998f, 36.799988f, 0.25f, 0.5f}},
        {126818,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {389.600006f, 52.799988f, 0.4375f, 0.0f},
         {404.0f, 36.799988f, 0.5f, 0.5f}},
        {126828,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {399.680023f, 52.799988f, 0.375f, 0.0f},
         {414.080017f, 36.799988f, 0.4375f, 0.5f}},
        {126838,
         DigitSlot,
         BlendMode::Alpha,
         Digits,
         {409.760010f, 52.799988f, 0.0f, 0.0f},
         {424.160004f, 36.799988f, 0.0625f, 0.5f}},
        {126875,
         std::uint16_t{2},
         BlendMode::Alpha,
         White,
         {566.400024f, 462.399994f, 0.0f, 0.0f},
         {604.800049f, 424.0f, 1.0f, 1.0f}},
        {126886,
         std::uint16_t{2},
         BlendMode::Alpha,
         White,
         {566.400024f, 462.399994f, 0.0f, 0.0f},
         {604.800049f, 424.0f, 1.0f, 1.0f}},
    };
    return quads;
}

// The trace prints six decimals: half a millionth of rounding, and some margin.
constexpr float TraceTolerance = 2e-6f;
// The colors of 0x5a6af0 are bytes over 255, which the trace prints with six decimals.
constexpr float ColorTolerance = 1e-6f;

void ExpectTheTraceQuad(const DrawnQuad& drawn, const TraceQuad& trace)
{
    SCOPED_TRACE(trace.line);
    EXPECT_EQ(drawn.slot, trace.slot);
    EXPECT_EQ(drawn.blend, trace.blend);
    EXPECT_NEAR(drawn.color.red, trace.color.red, ColorTolerance);
    EXPECT_NEAR(drawn.color.green, trace.color.green, ColorTolerance);
    EXPECT_NEAR(drawn.color.blue, trace.color.blue, ColorTolerance);
    EXPECT_NEAR(drawn.color.alpha, trace.color.alpha, ColorTolerance);
    EXPECT_NEAR(drawn.first.x, trace.first[0], TraceTolerance);
    EXPECT_NEAR(drawn.first.y, trace.first[1], TraceTolerance);
    EXPECT_NEAR(drawn.third.x, trace.third[0], TraceTolerance);
    EXPECT_NEAR(drawn.third.y, trace.third[1], TraceTolerance);
    if (!trace.slot)
        return;
    EXPECT_NEAR(drawn.first.u, trace.first[2], TraceTolerance);
    EXPECT_NEAR(drawn.first.v, trace.first[3], TraceTolerance);
    EXPECT_NEAR(drawn.third.u, trace.third[2], TraceTolerance);
    EXPECT_NEAR(drawn.third.v, trace.third[3], TraceTolerance);
}

class GameHudTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        auto loaded = Client::Tables::LoadGameData(m_dataRoot);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        data = std::move(loaded).Value();
        // As world entry leaves it.
        world.OwnCharacter().ClearEquipmentForEntry();
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.life = 100;
        own.maxLife = 100;
        own.mana = 100;
        own.maxMana = 100;
        // The base capture already received F3 30 with no keys: FF in the five banks, not the .bss from before the
        // login.
        for (auto& keys : own.skillKeys)
            keys.fill(0xff);
        sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
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
                              .projecting = projecting,
                              .projection = projection,
                              .sprites = sprites,
                              .text = text,
                              .texts = &*data.text,
                              .items = &data.items->table,
                              .skills = &*data.skills,
                              .worldClock = worldClock,
                              .tickCount = tickCount,
                              .mouse = mouse,
                              .leftDown = leftDown,
                              .controlDown = controlDown};
    }

    Client::Core::Expected<void, std::string> Hud(bool projecting = false)
    {
        GameHudContext context = Context(projecting);
        return Client::Hud::DrawTheHud(context);
    }

    Client::Core::Expected<void, std::string> Cursor()
    {
        GameHudContext context = Context();
        return Client::Hud::DrawTheGameCursor(context);
    }

    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
    }

    // The quick slot's count: potions in row 0 of the inventory.
    void PutPotion(std::size_t cell, std::uint16_t type, std::uint8_t count)
    {
        ItemState& item = world.Inventory().cells[cell];
        item.id = type;
        item.durability = count;
        item.occupied = 1;
    }

    void TheLorenciaHero()
    {
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.level = 1;
        own.experience = 0;
        own.nextExperience = 100;
        own.life = 1421;
        own.maxLife = 31131;
        own.mana = 10613;
        own.maxMana = 10613;
        own.bp = 17749;
        own.maxBp = 17749;
        own.byte6a = 1;
        own.words6c[0] = 0x2c;
        own.equipment[8].id = 0x1a00;
        own.equipment[8].durability = 245;
        Client::Kernel::ActorState& hero = Hero();
        hero.cellX = 143;
        hero.cellY = 120;
        hero.skillIndex = 0;
        hero.pet.model = 0x1c12;
        PutPotion(0, 0x1c00, 255);
        PutPotion(1, 0x1c01, 255);
        PutPotion(2, 0x1c03, 143);
        PutPotion(8, 0x1c04, 255);
        PutPotion(9, 0x1c05, 255);
        PutPotion(10, 0x1c06, 250);
        mouse = {356, 193};
        sprites.Begin(Screen1024);
    }

    WorldState world;
    CrtRandom random{Seed};
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    float worldClock = 0.0f;
    std::uint32_t tickCount = 0;
    Client::Ui::Point mouse{};
    bool leftDown = false;
    bool controlDown = false;
    const Client::Scenes::GameProjection* projection = nullptr;
    Client::Tables::GameData data;
};

TEST_F(GameHudTest, TheLorenciaFrameDrawsTheQuadsOfTheTrace)
{
    TheLorenciaHero();
    // The helper (0x5ab26c) opens the own record once more for its durability.
    const CrtRandom expected = Advanced(random, HudBaseDraws + OwnRecordCloseRandoms + SkillIconDraws);

    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_TRUE(Cursor().HasValue());
    ASSERT_TRUE(Cursor().HasValue());

    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    const std::vector<TraceQuad>& trace = LorenciaFrameQuads();
    ASSERT_EQ(quads.size(), trace.size());
    for (std::size_t i = 0; i < quads.size(); ++i)
        ExpectTheTraceQuad(quads[i], trace[i]);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(world.Runtime().movement.skillDepths[0x2c], 0);
    // Life below 20 %: the sound 0x22 of 0x5a97c3.
    EXPECT_EQ(sound, Client::Audio::SoundId{0x22});
    // The only text is the helper's name, which ends at x = 575 + 50.
    ASSERT_EQ(text.texts.size(), 1u);
    const auto record = data.items->table.Record(0x1a00);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(text.texts[0].text, Client::Items::ItemRecordName(*record));
    EXPECT_EQ(text.texts[0].y, 4);
}

TEST_F(GameHudTest, TheBaseStateSpendsTheRandomsOfItsAccessesAndLeavesTheTextColors)
{
    const CrtRandom expected = Advanced(random, HudBaseDraws);

    const auto drawn = Hud();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(Hero().cellXAccesses, 0);
    EXPECT_EQ(Hero().cellYAccesses, 0);
    EXPECT_TRUE(text.texts.empty());
    EXPECT_FALSE(sound.has_value());
    // The bottom bar leaves white on black (0x5a92a6) and turns off the durability warning.
    const Client::Kernel::HudState& hud = world.Runtime().hud;
    EXPECT_EQ(hud.textColors.text, 0xffffffffu);
    EXPECT_EQ(hud.textColors.background, 0xff000000u);
    EXPECT_EQ(hud.durabilityWarning, 0);
}

// 0x5a9796..0x5a97b8: the 26 (0x4d3510) and the 27 (0x4d3820) write the life and mana the orbs read: with 25 of 100
// the life orb lacks 0.75 (v = 36 / 64) and with 50 of 100 the mana orb lacks 0.5 (v = 24 / 64).
TEST_F(GameHudTest, TheLifeAndTheManaPacketsMoveTheOrbs)
{
    const auto orbV = [&](std::uint16_t slot)
    {
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        const auto orb = std::ranges::find(quads, std::optional<std::uint16_t>{slot}, &DrawnQuad::slot);
        return orb == quads.end() ? -1.0f : orb->first.v;
    };
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(orbV(0xea), 0.0f);
    EXPECT_EQ(orbV(0xeb), 0.0f);

    namespace Inventory = Client::Protocol::Items;
    ASSERT_TRUE(Client::Items::ApplyLifeRefill(Inventory::LifeRefill{.place = Inventory::CurrentPlace, .life = 25},
                                               world, random)
                    .HasValue());
    ASSERT_TRUE(Client::Items::ApplyManaRefill(
                    Inventory::ManaRefill{.place = Inventory::CurrentPlace, .mana = 50, .bp = 0}, world, random)
                    .HasValue());
    sprites.Begin(Screen640);
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(orbV(0xea), 0.5625f);
    EXPECT_EQ(orbV(0xeb), 0.375f);
}

// 0x5a7539..0x5a7604: in Tarkan, the two additive sands with (0.3, 0.3, 0.25), first the slow one and then the fast
// one, with the clock's u: ((int) T % 100000) · speed.
TEST_F(GameHudTest, TarkanSlidesItsTwoSandsWithTheClock)
{
    world.Runtime().map = 8;
    worldClock = 123456.75f;
    ASSERT_TRUE(Hud().HasValue());

    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 2u);
    EXPECT_EQ(quads[0].slot, 0x5c2);
    EXPECT_EQ(quads[1].slot, 0x5c3);
    for (std::size_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ(quads[i].blend, BlendMode::Additive);
        EXPECT_EQ(quads[i].color, (Color{0.3f, 0.3f, 0.25f, 1.0f}));
    }
    EXPECT_EQ(quads[0].first.u, static_cast<float>(23456.0 * static_cast<double>(0.0002f)));
    EXPECT_EQ(quads[1].first.u, static_cast<float>(23456.0 * static_cast<double>(0.001f)));
}

// 0x5a760e: in Crywolf, the smoke of 0x435930 only once: the fast one with normal blending and (0.4, 0.4, 0.45), the
// slow one additive, and 0x576353c at 0.
TEST_F(GameHudTest, CrywolfDrawsItsSmokeOnce)
{
    world.Runtime().map = 34;
    world.Runtime().crywolf.sandOverlay = 1;
    ASSERT_TRUE(Hud().HasValue());

    std::vector<DrawnQuad> quads = QuadsOf(sprites);
    ASSERT_GE(quads.size(), 2u);
    EXPECT_EQ(quads[0].slot, 0x5c3);
    EXPECT_EQ(quads[0].blend, BlendMode::Alpha);
    EXPECT_EQ(quads[0].color, (Color{0.4f, 0.4f, 0.45f, 1.0f}));
    EXPECT_EQ(quads[1].slot, 0x5c2);
    EXPECT_EQ(quads[1].blend, BlendMode::Additive);
    EXPECT_EQ(world.Runtime().crywolf.sandOverlay, 0);

    sprites.Begin(Screen640);
    ASSERT_TRUE(Hud().HasValue());
    quads = QuadsOf(sprites);
    ASSERT_FALSE(quads.empty());
    EXPECT_NE(quads[0].slot, 0x5c3);
}

// 0x5aac20: an equipped item with low durability shows its warning with the Item.bmd name and the count, in color, and
// leaves its number at D+0x44; an intermediate MuRet drawing does not write it.
TEST_F(GameHudTest, AWornItemShowsItsWarningAndMarksIt)
{
    ItemState& sword = world.OwnCharacter().equipment[0];
    sword.id = 0;
    sword.durability = 1;
    const auto record = data.items->table.Record(0);
    ASSERT_TRUE(record.has_value());
    const std::uint16_t maximum = Client::Items::MaxDurability(sword, *record, 0);
    ASSERT_GT(maximum, 5);

    ASSERT_TRUE(Hud(true).HasValue());
    EXPECT_EQ(sword.warning, 0) << "el dibujo intermedio no escribe los avisos";
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, Client::Items::ItemRecordName(*record) + " (1/" + std::to_string(maximum) + ")");
    EXPECT_EQ(text.texts[0].style.colors.text, 0xff0a0affu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0x80000000u);
    EXPECT_EQ(text.texts[0].y, 15);
    // Mode 4: it ends at the world's width.
    EXPECT_EQ(text.texts[0].x +
                  static_cast<int>(text.texts[0].text.size()) * Client::Tests::RecordingTextDrawer::LetterWidth,
              640);

    text.texts.clear();
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(sword.warning, 7);

    // Broken: the 8, white on red.
    sword.durability = 0;
    text.texts.clear();
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(sword.warning, 8);
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].style.colors.background, 0x800a0affu);

    // Without the strength it asks for (D+0x1e against +0x2a + the strength), the 3, even if the durability is enough.
    sword.durability = static_cast<std::uint8_t>(maximum);
    sword.word1e = 10;
    ASSERT_TRUE(Hud().HasValue());
    EXPECT_EQ(sword.warning, 3);
}

// 0x5ab810: the arrows in the right hand show Text 351 with theirs and the inventory's, at x = width − 0x5a with a
// width of 640, and the helper goes down to y = 24.
TEST_F(GameHudTest, TheArrowsShowTheirCountAndPushTheHelperDown)
{
    ItemState& arrows = world.OwnCharacter().equipment[0];
    arrows.id = 0x80f;
    arrows.durability = 100;
    world.OwnCharacter().equipment[8].id = 0x1a00;
    world.OwnCharacter().equipment[8].durability = 255;
    Hero().pet.model = 0x1c12;

    ASSERT_TRUE(Hud().HasValue());

    ASSERT_GE(text.texts.size(), 2u);
    const auto format = Client::Ui::TextAt(&*data.text, 351);
    ASSERT_TRUE(format.has_value());
    const std::array<std::string_view, 2> arguments{"100", "0"};
    EXPECT_EQ(text.texts[0].text, Client::Ui::PlaceArguments(*format, arguments));
    EXPECT_EQ(text.texts[0].x, 640 - 0x5a);
    EXPECT_EQ(text.texts[0].y, 4);
    EXPECT_EQ(world.Runtime().hud.arrowsTextWidth,
              static_cast<std::int32_t>(text.texts[0].text.size()) * Client::Tests::RecordingTextDrawer::LetterWidth);
    EXPECT_EQ(text.texts[1].y, 24);
}

// 10 and 22 (CL8 milestone 3): the macro brake draws "Macro Time" centered in the world, at y = 0x188; with the chat
// field open, the box without blending (0xf8 and 0xf9) and, with the F3 mode, its indicator (0xef).
TEST_F(GameHudTest, TheMacroTimeAndTheChatBoxAreDrawn)
{
    world.Runtime().hud.macroTime = Client::Chat::MacroLockSteps;
    world.Runtime().ui.chatOpen = true;

    ASSERT_TRUE(Hud().HasValue());

    const auto macro = std::ranges::find_if(text.texts, [](const auto& drawn) { return drawn.text == "Macro Time"; });
    ASSERT_NE(macro, text.texts.end());
    EXPECT_EQ(macro->x, (640 - 50) / 2);
    EXPECT_EQ(macro->y, 0x188);
    std::vector<std::uint16_t> chatSlots;
    for (const DrawnQuad& quad : QuadsOf(sprites))
    {
        if (!quad.slot || (*quad.slot != 0xf8 && *quad.slot != 0xf9 && *quad.slot != 0xef))
            continue;
        chatSlots.push_back(*quad.slot);
        EXPECT_EQ(quad.blend, *quad.slot == 0xef ? BlendMode::Alpha : BlendMode::Opaque);
    }
    EXPECT_EQ(chatSlots, (std::vector<std::uint16_t>{0xf8, 0xf9, 0xef}));
}

// 0x5a5860 with a command skill (0x78 to 0x7c): the command atlas (300) and its number from 1, without the wait.
TEST_F(GameHudTest, ACommandSkillDrawsItsIconAndItsNumber)
{
    world.OwnCharacter().byte6a = 1;
    Hero().skillIndex = 0x79;
    const CrtRandom expected = Advanced(random, HudBaseDraws + SkillIconDraws);

    ASSERT_TRUE(Hud().HasValue());

    EXPECT_EQ(random.State(), expected.State());
    const std::vector<DrawnQuad> quads = QuadsOf(sprites);
    const auto icon = std::ranges::find_if(quads, [](const DrawnQuad& quad) { return quad.slot == 300; });
    ASSERT_NE(icon, quads.end());
    // The atlas's second one: u from 32/256 to 64/256.
    EXPECT_FLOAT_EQ(icon->first.u, 0.125f);
    EXPECT_FLOAT_EQ(icon->third.u, 0.25f);
    const auto number = std::next(icon);
    ASSERT_NE(number, quads.end());
    EXPECT_EQ(number->slot, DigitSlot);
    EXPECT_EQ(number->color, (Color{1.0f, 0.9f, 0.8f, 1.0f}));
    // The 2, at (308 − 4.5, 470).
    EXPECT_FLOAT_EQ(number->first.u, 0.125f);
    EXPECT_FLOAT_EQ(number->first.x, 303.5f);
}

// 0x5aa29d filters hidden==1 and 43..48; 0x5aa2b0 subtracts 46 only once, without paging again to the shown 41.
TEST_F(GameHudTest, TheOpenSkillListDrawsTheNativeRowsColorsAndAccesses)
{
    const auto skills = SkillsForTheList();
    auto& own = world.OwnCharacter();
    for (std::size_t slot = 0; slot < 41; ++slot)
        own.SetSkillAt(slot, 1);
    own.SetSkillAt(41, 2); // hidden==2 is drawn too, though the rules tint it for lacking 0x200.
    own.SetSkillAt(42, 9);
    own.SetSkillAt(43, 0x43);
    own.SetSkillAt(63, 0x11);
    own.byte6a = 45;
    own.byte6b = 1;
    own.energy = 100;
    Hero().skillIndex = 63;
    Hero().flags78 = 0x200;
    world.Runtime().ui.skillListOpen = 1;
    auto context = Context();
    context.skills = &skills;

    const auto drawn = Client::Hud::DrawTheHud(context);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    std::vector<DrawnQuad> icons;
    for (const auto& quad : QuadsOf(sprites))
        if (quad.slot == 0x12a && quad.first.y > 36.0f)
            icons.push_back(quad);
    ASSERT_EQ(icons.size(), 43u);
    EXPECT_FLOAT_EQ(icons[0].first.x, 0.0f);
    EXPECT_FLOAT_EQ(icons[0].first.y, 110.0f); // GL flips Y: 480 − 370.
    EXPECT_FLOAT_EQ(icons[19].first.x, 608.0f);
    EXPECT_FLOAT_EQ(icons[20].first.x, 0.0f);
    EXPECT_FLOAT_EQ(icons[20].first.y, 156.0f);
    EXPECT_FLOAT_EQ(icons[40].first.x, 640.0f);
    EXPECT_FLOAT_EQ(icons[40].first.y, 156.0f);
    EXPECT_FLOAT_EQ(icons.back().first.x, 704.0f);
    EXPECT_EQ(icons.front().color, (Color{0.6f, 0.7f, 0.8f, 1.0f}));
    EXPECT_EQ(icons.back().color, (Color{1.0f, 0.9f, 0.8f, 1.0f}));
    const int listDraws = 44 * 60 + 43 * SkillIconDraws;
    EXPECT_EQ(random.State(), Advanced(CrtRandom(Seed), HudBaseDraws + SkillIconDraws + listDraws).State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

TEST_F(GameHudTest, TheOpenListAddsRavenCommandsOnlyWhenTheRavenExists)
{
    const auto skills = SkillsForTheList();
    world.OwnCharacter().byte6a = 1;
    Hero().skillIndex = 0x79;
    Hero().darkSpirit.emplace();
    world.Runtime().ui.skillListOpen = 1;
    auto context = Context();
    context.skills = &skills;
    const auto drawn = Client::Hud::DrawTheHud(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    std::vector<DrawnQuad> commands;
    for (const auto& quad : QuadsOf(sprites))
        if (quad.slot == 300 && std::abs(quad.first.y - 150.0f) < 0.001f)
            commands.push_back(quad);
    ASSERT_EQ(commands.size(), 4u);
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        EXPECT_FLOAT_EQ(commands[i].first.x, 256.0f + 32.0f * static_cast<float>(i));
        EXPECT_FLOAT_EQ(commands[i].third.y, 114.0f);
        EXPECT_EQ(commands[i].color, (i == 1 ? Color{1.0f, 0.9f, 0.8f, 1.0f} : Color{0.6f, 0.7f, 0.8f, 1.0f}));
    }
    // The raven also turns on its HUD of 0x418460, which queries the equipment with its own close.
    EXPECT_EQ(random.State(),
              Advanced(CrtRandom(Seed), HudBaseDraws + OwnRecordCloseRandoms + 5 * SkillIconDraws).State());
    Hero().darkSpirit.reset();
    sprites.Begin(Screen640);
    ASSERT_TRUE(Client::Hud::DrawTheHud(context).HasValue());
    for (const auto& quad : QuadsOf(sprites))
        EXPECT_FALSE(quad.slot == 300 && std::abs(quad.first.y - 150.0f) < 0.001f);
}

TEST_F(GameHudTest, TheOrdinarySkillDrawsTheHotkeyOfTheChosenCharacter)
{
    const auto skills = SkillsForTheList();
    auto& own = world.OwnCharacter();
    own.byte6a = 1;
    own.SetSkillAt(0, 0x11);
    own.skillKeys[0][0] = 3;
    own.skillKeys[3][0] = 7;
    auto context = Context();
    context.skills = &skills;
    context.chosenSlot = std::uint8_t{3};
    ASSERT_TRUE(Client::Hud::DrawTheHud(context).HasValue());
    const auto quads = QuadsOf(sprites);
    const auto key = std::ranges::find_if(quads,
                                          [](const DrawnQuad& quad)
                                          {
                                              return quad.slot == DigitSlot &&
                                                     std::abs(quad.first.x - 303.5f) < 0.001f &&
                                                     std::abs(quad.first.y - 10.0f) < 0.001f;
                                          });
    ASSERT_NE(key, quads.end());
    EXPECT_FLOAT_EQ(key->first.u, 7.0f / 16.0f);
    EXPECT_EQ(key->color, (Color{1.0f, 0.9f, 0.8f, 1.0f}));
    EXPECT_EQ(random.State(), Advanced(CrtRandom(Seed), HudBaseDraws + SkillIconDraws).State());
}

// 18. 0x5f7440: the party (0x5f0be0, the first thing) and the commands (0x40a650, almost at the end) at (0x1c2, 0),
// with the background of Item_Back01 and 02 and the close button; the commands, with their eleven Message_box buttons.
// In Lorencia and in Devias.
TEST_F(GameHudTest, TheEighteenthChildDrawsThePartyAndTheCommands)
{
    const auto windowAt = [](const std::vector<DrawnQuad>& quads)
    {
        return std::ranges::find_if(quads, [](const DrawnQuad& quad)
                                    { return quad.slot == std::uint16_t{0x104} && quad.first.x == 450.0f; });
    };
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.party = 1;
        sprites.Begin(Screen640);
        const auto party = Hud();
        ASSERT_TRUE(party.HasValue()) << party.Error();
        const std::vector<DrawnQuad> partyQuads = QuadsOf(sprites);
        const auto partyWindow = windowAt(partyQuads);
        ASSERT_NE(partyWindow, partyQuads.end());
        ASSERT_GE(std::distance(partyWindow, partyQuads.end()), 3);
        EXPECT_EQ(std::next(partyWindow)->slot, std::uint16_t{0x105});
        EXPECT_EQ(std::next(partyWindow, 2)->slot, std::uint16_t{0x118});
        EXPECT_EQ(world.Runtime().hud.partyX, 0x1c2);

        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.delegated.commands = 1;
        sprites.Begin(Screen640);
        const auto commands = Hud();
        ASSERT_TRUE(commands.HasValue()) << commands.Error();
        const std::vector<DrawnQuad> commandQuads = QuadsOf(sprites);
        const auto commandWindow = windowAt(commandQuads);
        ASSERT_NE(commandWindow, commandQuads.end());
        EXPECT_EQ(std::count_if(commandWindow, commandQuads.end(),
                                [](const DrawnQuad& quad) { return quad.slot == std::uint16_t{0xf0}; }),
                  11);
        EXPECT_EQ(world.Runtime().hud.commandsX, 0x1c2);
    }
}

// 18. and 30. (CL9 milestone 6, phase 3): the shop (0x5f4720) and the warehouse (0x5f4e30) at (0x104, 0) with their
// buttons, and the password box (0x5e9b10) with the bar and the delete key, with the Text.bmd texts. In Lorencia,
// Devias and Noria.
TEST_F(GameHudTest, TheNpcWindowsAndTheKeypadAreDrawn)
{
    const auto has = [](const std::vector<DrawnQuad>& quads, std::uint16_t slot, float x) {
        return std::ranges::any_of(quads,
                                   [&](const DrawnQuad& quad) { return quad.slot == slot && quad.first.x == x; });
    };
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.npcShop = 1;
        sprites.Begin(Screen640);
        const auto shop = Hud();
        ASSERT_TRUE(shop.HasValue()) << shop.Error();
        std::vector<DrawnQuad> quads = QuadsOf(sprites);
        EXPECT_TRUE(has(quads, 0x104, 260.0f));
        EXPECT_TRUE(has(quads, 0x11b, 285.0f));
        EXPECT_EQ(world.Runtime().ui.npcWindowX, 0x104);

        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.warehouse = 1;
        world.Runtime().ui.field78abb20 = 1;
        sprites.Begin(Screen640);
        const auto warehouse = Hud();
        ASSERT_TRUE(warehouse.HasValue()) << warehouse.Error();
        quads = QuadsOf(sprites);
        EXPECT_TRUE(has(quads, 0x126, 260.0f + 0x7e));
        EXPECT_TRUE(has(quads, 0x127, 260.0f + 0x7e));
        EXPECT_TRUE(has(quads, 0xfd, 294.0f));
        EXPECT_TRUE(has(quads, 0xfe, 223.0f));
    }
}

// 0x5f75ef→0x5f7180: the HUD writes the corner 450 and draws the acceptance of panel 2000.
TEST_F(GameHudTest, TheHudDrawsThePanel2000ThroughItsWindowPass)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().panels.panel2000 = 1;
        sprites.Begin(Screen640);
        const auto drawn = Hud();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(world.Runtime().hud.inventoryX, 450);
        const auto quads = QuadsOf(sprites);
        EXPECT_TRUE(std::ranges::any_of(quads, [](const DrawnQuad& quad)
                                        { return quad.slot == 0x122 && quad.first.x == 475.0f; }));
    }
}

// 19. 0x65dd70: the fade runs on every drawing (0x65dcd0) and, if it did not turn off, the banner goes with
// 0x620a00(1) and glColor4f(1, 1, 1, alpha): 0x621b60(0x66a, (world width · rx − 166) · 0.5, ry · 220, 166, 90, 0, 0,
// 0.6484375, 0.703125, 0, 0, 0), without scaling the size or the position. In 1024 × 768, with the 640 world: x = 429
// and y = 352.00003 (ry = 768 · 0x3b088889 is 1.6000001 in float), and the top edge in GL, 768 − y = 415.99997.
TEST_F(GameHudTest, TheNineteenthChildDrawsTheMapBannerWhileItFades)
{
    Client::Hud::MapBanner& banner = world.Runtime().hud.mapBanner;
    Client::Hud::ResetTheMapBanner(banner, 0);
    const auto bannerQuads = [this]
    {
        std::vector<DrawnQuad> quads = QuadsOf(sprites);
        std::erase_if(quads, [](const DrawnQuad& quad) { return quad.slot != Client::Hud::MapBannerSlot; });
        return quads;
    };
    sprites.Begin(Screen1024);
    tickCount = 10;
    const auto off = Hud();
    ASSERT_TRUE(off.HasValue()) << off.Error();
    EXPECT_TRUE(bannerQuads().empty());
    EXPECT_EQ(banner.last, 10u);

    Client::Hud::ShowTheMapBanner(banner, 0);
    sprites.Begin(Screen1024);
    tickCount = 30;
    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const float alpha = std::bit_cast<float>(0x3e4ccccdu) + std::bit_cast<float>(0x3c75c28fu);
    EXPECT_EQ(banner.alpha, alpha);
    EXPECT_EQ(banner.last, 30u);
    const std::vector<DrawnQuad> quads = bannerQuads();
    ASSERT_EQ(quads.size(), 1u);
    EXPECT_EQ(quads[0].blend, BlendMode::Alpha);
    EXPECT_EQ(quads[0].color.red, 1.0f);
    EXPECT_EQ(quads[0].color.alpha, alpha);
    EXPECT_EQ(quads[0].first.x, 429.0f);
    EXPECT_EQ(quads[0].first.y, 415.99997f);
    EXPECT_EQ(quads[0].third.x, 429.0f + 166.0f);
    EXPECT_EQ(quads[0].third.y, 325.99997f);
    EXPECT_EQ(quads[0].first.u, 0.0f);
    EXPECT_EQ(quads[0].third.u, 0.6484375f);
    EXPECT_EQ(quads[0].third.v, 0.703125f);
}

TEST_F(GameHudTest, TheTwelfthChildDrawsTheChatBubbles)
{
    Client::Kernel::ActorState& owner = world.Actors()[4];
    owner.alive = true;
    owner.seen = true;
    owner.boundsTop = 120.0f;
    Client::Chat::ChatBubble& bubble = world.Runtime().chat.bubbles[0];
    bubble.name = "Tero";
    bubble.nameTime = 100;
    bubble.owner = 4;

    const auto cut = Hud();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5a90be"), std::string::npos) << cut.Error();

    // The view leaves the eye at (x, y, 1): the owner at the origin goes to the center.
    Client::Scenes::GameProjection flat;
    flat.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    flat.horizontalScale = 1.0f;
    flat.verticalScale = 1.0f;
    flat.centerX = 320;
    flat.centerY = 240;
    flat.screenWidth = 640;
    flat.screenHeight = 480;
    projection = &flat;
    text.texts.clear();
    const auto drawn = Hud();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Tero");
    EXPECT_TRUE(text.texts[0].box.has_value());
    EXPECT_EQ(bubble.x, 320 - 27 / 2);
    EXPECT_EQ(bubble.y, 240 - 0x24);
    EXPECT_EQ(world.Runtime().hud.durabilityWarning, 0);
}

// 13. 0x451c20: the personal shop signs, after the bubbles and with the same projection (without it, a sign stops).
// Without the inventory, 0x5bb8d0 draws no rand(): the count is the base state's. In Lorencia and Devias.
TEST_F(GameHudTest, TheThirteenthChildDrawsTheShopSigns)
{
    const std::optional<std::string_view> header = data.text->Text(1104);
    ASSERT_TRUE(header.has_value());
    const int headerWidth = static_cast<int>(header->size()) * 6;
    Client::Scenes::GameProjection flat;
    flat.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    flat.horizontalScale = 1.0f;
    flat.verticalScale = 1.0f;
    flat.centerX = 320;
    flat.centerY = 240;
    flat.screenWidth = 640;
    flat.screenHeight = 480;
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        Client::Kernel::ActorState& owner = world.Actors()[4];
        owner.alive = true;
        owner.seen = true;
        owner.boundsTop = 120.0f;
        world.Runtime().social.personalShop.titles.emplace(
            4, Client::Social::ShopTitle{.key = 0x234, .name = "Brezal", .title = "Arcos"});
        projection = nullptr;
        const auto cut = Hud();
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x452146"), std::string::npos) << cut.Error();

        projection = &flat;
        text.texts.clear();
        CrtRandom before = random;
        const auto drawn = Hud();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), Advanced(before, HudBaseDraws).State());
        ASSERT_EQ(text.texts.size(), 3u);
        // The width is that of text 1104 plus the name; 36 rows above the projected point.
        const int x = 320 - (headerWidth + 36) / 2;
        EXPECT_EQ(text.texts[0].text, *header);
        EXPECT_EQ(text.texts[0].x, x);
        EXPECT_EQ(text.texts[0].y, 240 - 36);
        EXPECT_EQ(text.texts[1].text, "Brezal");
        EXPECT_EQ(text.texts[1].x, x + headerWidth);
        EXPECT_EQ(text.texts[2].text, "Arcos");
        EXPECT_EQ(text.texts[2].y, 240 - 36 + 12);
        EXPECT_EQ(world.Runtime().social.personalShop.signBlink, 1);
    }
}

// 5. 0x42a4d0: the queue's health bars, with the world pass's projection (without it, a bar stops). Each frame draws
// the half rounded up and leaves the rest; an intermediate drawing empties a copy.
TEST_F(GameHudTest, TheFifthChildDrawsHalfTheHealthBars)
{
    std::deque<Client::Kernel::QueuedHealthBar>& bars = world.Runtime().healthBars;
    for (std::uint8_t stage = 0; stage < 3; ++stage)
        bars.push_back(Client::Kernel::QueuedHealthBar{.position = {0.0f, 0.0f, 0.0f}, .fill = stage});

    const auto cut = Hud();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42a529"), std::string::npos) << cut.Error();
    EXPECT_EQ(bars.size(), 3u);

    Client::Scenes::GameProjection flat;
    flat.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    flat.horizontalScale = 1.0f;
    flat.verticalScale = 1.0f;
    flat.centerX = 320;
    flat.centerY = 240;
    flat.screenWidth = 640;
    flat.screenHeight = 480;
    projection = &flat;
    const auto projected = Hud(true);
    ASSERT_TRUE(projected.HasValue()) << projected.Error();
    EXPECT_EQ(bars.size(), 3u);

    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ASSERT_EQ(bars.size(), 1u);
    EXPECT_EQ(bars.front().fill, 2);
}

// Devil Square has its event object, but its method +4 is a ret (0x40bdb0) and the countdown (0x40bab0) exits with +8
// at 0: the HUD goes on.
TEST_F(GameHudTest, TheEventObjectsWithoutTheirPacketsDrawNothing)
{
    for (const auto& [map, kind] : {std::pair{9, Client::Events::EventMapKind::DevilSquare},
                                    std::pair{11, Client::Events::EventMapKind::BloodCastle},
                                    std::pair{18, Client::Events::EventMapKind::ChaosCastle}})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        world.Runtime().eventObject = Client::Events::EventMapObject{.kind = kind};
        const auto drawn = Hud();
        EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
    }
}

// CL9 milestone 11, method +4 (0x40c2d0, 0x405090) in the HUD, with the Data's Text.bmd: the points (864 or 1161), 865
// and the time in column 570. Chaos Castle chooses the trap area (0x6c634c) by +0x20; an intermediate drawing does not
// write it. Outside their maps (Lorencia, Devias) it does not draw.
TEST_F(GameHudTest, TheCastleObjectsDrawTheirPointsAndTime)
{
    struct Case
    {
        std::int32_t map;
        Client::Events::EventMapKind kind;
        std::uint8_t state;
        std::size_t points;
        bool draws;
    };
    for (const Case& probe : {Case{11, Client::Events::EventMapKind::BloodCastle, 1, 864, true},
                              Case{23, Client::Events::EventMapKind::ChaosCastle, 7, 1161, true},
                              Case{0, Client::Events::EventMapKind::BloodCastle, 1, 864, false},
                              Case{2, Client::Events::EventMapKind::ChaosCastle, 7, 1161, false}})
    {
        SCOPED_TRACE(probe.map);
        const std::optional<std::string_view> format = Client::Ui::TextAt(&*data.text, probe.points);
        ASSERT_TRUE(format.has_value());
        const auto points = Client::Crt::FormatOriginal(Client::Crt::FormatFunction::Wsprintf, *format,
                                                        std::vector<Client::Crt::FormatArgument>{42, 70}, "test");
        ASSERT_TRUE(points.HasValue()) << points.Error();
        for (const bool projecting : {true, false})
        {
            SCOPED_TRACE(projecting);
            world.Runtime() = Client::Kernel::WorldRuntime{};
            world.Runtime().map = probe.map;
            world.Runtime().eventObject = Client::Events::EventMapObject{
                .kind = probe.kind, .field10 = probe.state, .field14 = {900, 1000, 70, 42}};
            text.texts.clear();
            const auto drawn = Hud(projecting);
            ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
            const auto line = std::ranges::find(text.texts, points.Value(), &Client::Tests::RecordedText::text);
            const auto time =
                std::ranges::find(text.texts, std::string(" 16 : 40"), &Client::Tests::RecordedText::text);
            if (!probe.draws)
            {
                EXPECT_EQ(line, text.texts.end());
                EXPECT_EQ(time, text.texts.end());
                EXPECT_EQ(world.Runtime().chaosCastleArea, 0xff);
                continue;
            }
            ASSERT_NE(line, text.texts.end());
            EXPECT_EQ(line->y, 345);
            ASSERT_NE(time, text.texts.end());
            EXPECT_EQ(time->y, 367);
            const bool writes = probe.kind == Client::Events::EventMapKind::ChaosCastle && !projecting;
            EXPECT_EQ(world.Runtime().chaosCastleArea, writes ? 0 : 0xff);
        }
    }
}

// CL9 milestone 11, 0x40bab0: with +8 from 1 to 14, the entry text with the seconds left of 30 since +0xc
// (GetTickCount), in 0xffff8080 over 0x80000000 at (10, 0x174). Chaos Castle's 12 takes the 828 (not the 825).
// Expired, the original's frame turns off +8; an intermediate drawing does not. In Lorencia, Devias and the event maps.
TEST_F(GameHudTest, TheEventCountdownDrawsTheSecondsLeftAndExpires)
{
    const std::optional<std::string_view> format = Client::Ui::TextAt(&*data.text, 828);
    const std::optional<std::string_view> name = Client::Ui::TextAt(&*data.text, 1147);
    ASSERT_TRUE(format && name);
    const std::vector<Client::Crt::FormatArgument> arguments{Client::Crt::TextArgument{.index = 1147, .text = *name},
                                                             18};
    const auto expected =
        Client::Crt::FormatOriginal(Client::Crt::FormatFunction::Wsprintf, *format, arguments, "test");
    ASSERT_TRUE(expected.HasValue()) << expected.Error();
    for (const std::int32_t map : {0, 2, 9, 11, 18})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().eventObject = Client::Events::EventMapObject{.field08 = 12, .field0c = 1000};
        tickCount = 1000 + 12345;
        text.texts.clear();
        ASSERT_TRUE(Hud().HasValue());
        const auto countdown = std::ranges::find(text.texts, expected.Value(), &Client::Tests::RecordedText::text);
        ASSERT_NE(countdown, text.texts.end());
        EXPECT_EQ(countdown->x, 10);
        EXPECT_EQ(countdown->y, 0x174);
        EXPECT_EQ(countdown->style.colors.text, 0xffff8080u);
        EXPECT_EQ(countdown->style.colors.background, 0x80000000u);

        tickCount = 1000 + 30001;
        ASSERT_TRUE(Hud(true).HasValue());
        EXPECT_EQ(world.Runtime().eventObject->field08, 12);
        ASSERT_TRUE(Hud().HasValue());
        EXPECT_EQ(world.Runtime().eventObject->field08, 0);
    }
}

// CL9 milestone 11, 0x4365c0: child 21. With the Crywolf notice (0x6c73d1) at 1, which the BD 00 with stage 5 sets
// (0x42fec8), the HUD draws the banner 0x6c0 (0x6b6 + 0x6c73dc) and the rank table on any map; with −1, nothing.
TEST_F(GameHudTest, TheCrywolfNoticeIsDrawnOnAnyMap)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        const auto countOf = [this](std::uint16_t slot)
        {
            const std::vector<DrawnQuad> quads = QuadsOf(sprites);
            return std::count_if(quads.begin(), quads.end(),
                                 [slot](const DrawnQuad& quad) { return quad.slot == slot; });
        };
        sprites.Begin(Screen640);
        ASSERT_EQ(world.Runtime().crywolf.ui.message, -1);
        const auto quiet = Hud();
        ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
        EXPECT_EQ(countOf(0x6c0), 0);
        EXPECT_EQ(countOf(0x6d0), 0);
        sprites.Begin(Screen640);
        world.Runtime().crywolf.ui.message = 1;
        const auto drawn = Hud();
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(countOf(0x6c0), 1);
        EXPECT_EQ(countOf(0x6d0), 1);
        EXPECT_EQ(world.Runtime().crywolf.ui.dword6c73d8, 2);
    }
}

// 0x5ac2d0: the 24 × 24 square at (x − 2, y − 2) of the slot its table chooses.
TEST_F(GameHudTest, TheCursorPicksItsSlot)
{
    mouse = {100, 200};
    const auto slotOf = [this]() -> std::optional<std::uint16_t>
    {
        sprites.Begin(Screen640);
        const auto drawn = Cursor();
        EXPECT_TRUE(drawn.HasValue()) << drawn.Error();
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        if (quads.size() != 1u)
            return std::nullopt;
        EXPECT_FLOAT_EQ(quads[0].first.x, 98.0f);
        EXPECT_FLOAT_EQ(quads[0].first.y, 480.0f - 198.0f);
        return quads[0].slot;
    };
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(slotOf(), 2);
    leftDown = true;
    EXPECT_EQ(slotOf(), 3);
    runtime.hud.pushCursor = 1;
    EXPECT_EQ(slotOf(), 0xa);
    runtime.panels.repairMode = 1;
    EXPECT_EQ(slotOf(), 7);
    runtime.panels.delegated.commands = 1;
    EXPECT_EQ(slotOf(), 0x65a);
    Hero().safeZone = 1;
    runtime.selectedTargets[2] = 3;
    EXPECT_EQ(slotOf(), 0x65a) << "en la zona segura, el personaje elegido no cambia el cursor";
    runtime.selectedTargets[0] = 7;
    EXPECT_EQ(slotOf(), 5);
    runtime.dialogs.current = 0x6e;
    EXPECT_EQ(slotOf(), 2);
}

// The talk one (6) animates its quarters with ftol(T · 0.01) % 6: u at 0.5 with 1, 3 and 5; v at 0.5 with 2, 3 and 4.
// The 0.01 is a float (0x6b88d4) a bit below 0.01, so with T = 100 it is still frame 0.
TEST_F(GameHudTest, TheTalkCursorWalksItsQuarters)
{
    world.Runtime().selectedTargets[1] = 4;
    for (const auto& [clock, u, v] :
         {std::tuple{0.0f, 0.0f, 0.0f}, std::tuple{100.0f, 0.0f, 0.0f}, std::tuple{150.0f, 0.5f, 0.0f},
          std::tuple{250.0f, 0.0f, 0.5f}, std::tuple{350.0f, 0.5f, 0.5f}, std::tuple{450.0f, 0.0f, 0.5f},
          std::tuple{550.0f, 0.5f, 0.0f}, std::tuple{650.0f, 0.0f, 0.0f}})
    {
        SCOPED_TRACE(clock);
        worldClock = clock;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Cursor().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 1u);
        EXPECT_EQ(quads[0].slot, 6);
        EXPECT_EQ(quads[0].first.u, u);
        EXPECT_EQ(quads[0].first.v, v);
        EXPECT_EQ(quads[0].third.u, u + 0.5f);
    }
}

// With repair mode 2, the 7 turned by the sign of sin(T · 0.02): 45° at (x + 5, y + 18) or straight at (x + 10,
// y + 10), centered there.
TEST_F(GameHudTest, TheRepairCursorTurnsWithTheClock)
{
    world.Runtime().panels.repairMode = 2;
    mouse = {100, 200};
    for (const auto& [clock, x, y] : {std::tuple{0.0f, 105.0f, 218.0f}, std::tuple{100.0f, 110.0f, 210.0f}})
    {
        SCOPED_TRACE(clock);
        worldClock = clock;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Cursor().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 1u);
        EXPECT_EQ(quads[0].slot, 7);
        EXPECT_NEAR((quads[0].first.x + quads[0].third.x) * 0.5f, x, 1e-4f);
        EXPECT_NEAR((quads[0].first.y + quads[0].third.y) * 0.5f, 480.0f - y, 1e-4f);
    }
}

// The cursor another wave reads: the objects one (CL9.1).
TEST_F(GameHudTest, TheCursorsOfOtherWavesStop)
{
    world.Runtime().selectedTargets[3] = 1;
    auto drawn = Cursor();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5ac3d8"), std::string::npos) << drawn.Error();
}

// CL9 milestone 3, 0x5ac53c..0x5ac60b: outside the safe zone, the attackable chosen one (0x558bb0) with the mouse
// outside the UI draws the attack one (4, or 0x68d in the 30); the one that cannot be attacked, the normal one (2).
TEST_F(GameHudTest, TheAttackCursorFollowsWhetherTheChosenCanBeAttacked)
{
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    Hero().safeZone = 0;
    auto& chosen = world.Actors()[7];
    runtime.selectedTargets[2] = 7;
    struct Case
    {
        std::int32_t map;
        std::uint8_t object;
        bool control;
        std::uint16_t slot;
    };
    constexpr std::array<Case, 6> Cases{{{0, 2, false, 4},
                                         {2, 2, false, 4},
                                         {30, 2, false, 0x68d},
                                         {0, 1, false, 2},
                                         {2, 1, true, 4},
                                         {0, 3, true, 2}}};
    for (const Case& test : Cases)
    {
        SCOPED_TRACE(test.map);
        runtime.map = test.map;
        chosen.byte88 = test.object;
        controlDown = test.control;
        sprites.Begin(Screen640);
        ASSERT_TRUE(Cursor().HasValue());
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        ASSERT_EQ(quads.size(), 1u);
        EXPECT_EQ(quads[0].slot, test.slot);
    }
    // The mouse over the UI does not attack.
    runtime.map = 0;
    chosen.byte88 = 2;
    runtime.ui.mouseOnUi = 1;
    sprites.Begin(Screen640);
    ASSERT_TRUE(Cursor().HasValue());
    ASSERT_EQ(QuadsOf(sprites).size(), 1u);
    EXPECT_EQ(QuadsOf(sprites)[0].slot, 2);
}

// 6. 0x5a6600 (from 0x42a735): the mouse over a status icon draws its help in the tooltip box, at (x + 10, 0x1d), with
// the own record and the icon's Skill.bmd entry opened around it (3848 and 60 rand()). The 0xcc writes three texts
// and leaves the colors of the 3 and the 4 (Q-C597).
TEST_F(GameHudTest, TheMouseOverAStatusIconShowsItsHelp)
{
    Hero().flags78 = 0x10000;
    Client::Tooltips::TooltipLines& lines = world.Runtime().hud.tooltip;
    lines.colors[3] = Client::Tooltips::TooltipWhiteOnRed;
    const CrtRandom before = random;
    mouse = {320, 300};
    ASSERT_TRUE(Hud().HasValue());
    CrtRandom away = random;
    for (std::uint32_t i = 0; i < OwnRecordCloseRandoms + Client::Tables::SkillRecordSize; ++i)
        static_cast<void>(away.Next());

    random = before;
    sprites.Begin(Screen640);
    mouse = {190, 10};
    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), away.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    const auto record = data.skills->Record(0xcc);
    ASSERT_TRUE(record.has_value());
    std::string name;
    for (const std::byte value : *record)
    {
        if (value == std::byte{0})
            break;
        name.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    EXPECT_EQ(lines.text[0], "\n");
    ASSERT_FALSE(name.empty());
    EXPECT_EQ(lines.text[1], name);
    EXPECT_TRUE(std::ranges::any_of(text.texts, [&name](const auto& drawnText) { return drawnText.text == name; }));
    EXPECT_EQ(lines.colors[1], Client::Tooltips::TooltipBlue);
    EXPECT_EQ(lines.bold[1], 1);
    EXPECT_EQ(lines.text[2], "\n");
    const std::array<std::size_t, 3> texts{1638, 1639, 1472};
    for (std::size_t i = 0; i < texts.size(); ++i)
    {
        const std::optional<std::string_view> help = Client::Ui::TextAt(&*data.text, texts[i]);
        ASSERT_TRUE(help.has_value());
        EXPECT_EQ(lines.text[3 + i], Client::Crt::Sprintf(*help)) << i;
    }
    EXPECT_EQ(lines.colors[3], Client::Tooltips::TooltipWhiteOnRed);
    EXPECT_EQ(lines.colors[5], Client::Tooltips::TooltipWhite);
    EXPECT_EQ(lines.text[6], "\n");
}

// 0x5a6600 with an icon without text in the table of 0x5a69fc (the 0x35): line 3 keeps what it had, in white
// (Q-C597).
TEST_F(GameHudTest, AStatusIconWithoutTextKeepsTheThirdLine)
{
    Hero().flags78 = 0x400;
    Client::Tooltips::TooltipLines& lines = world.Runtime().hud.tooltip;
    lines.text[3] = "previo";
    lines.colors[3] = Client::Tooltips::TooltipRed;
    mouse = {190, 10};
    const auto drawn = Hud();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(lines.text[3], "previo");
    EXPECT_EQ(lines.colors[3], Client::Tooltips::TooltipWhite);
    EXPECT_EQ(lines.text[4], "\n");
    // The name of Skill.bmd's entry 0x35 is empty: the box measures up to line 1 and writes nothing.
    EXPECT_EQ(lines.text[1], "");
    EXPECT_TRUE(text.texts.empty());
}
// 0x5a9ddb: the bottom bar draws the friend and letter icons (0x4a5120) with the window list, after the slots and
// before the commands button. With a chat waiting, a new letter and an unread one, without the friends one: the man at
// (586, 437) and the letter at (615, 440), on the first drawing (lit). No rand(). In Lorencia and in Devias.
TEST_F(GameHudTest, TheMainBarDrawsTheFriendAndLetterIcons)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        Client::Ui::OldWindowManager& windows = world.Runtime().oldWindows;
        windows.windowList.list.waiting.push_back(7);
        windows.windowList.list.newLetter = 1;
        Client::Messenger::AddALetter(windows.letterModel, 3, "Tordo", "Asado", "2026/11/13", "21:30", 0);
        sprites.Begin(Screen640);
        const CrtRandom expected = Advanced(random, HudBaseDraws);

        const auto drawn = Hud();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        const std::vector<DrawnQuad> quads = QuadsOf(sprites);
        const auto man = std::ranges::find(quads, std::optional<std::uint16_t>{std::uint16_t{0x64e}}, &DrawnQuad::slot);
        const auto letter =
            std::ranges::find(quads, std::optional<std::uint16_t>{std::uint16_t{0x64d}}, &DrawnQuad::slot);
        ASSERT_NE(man, quads.end());
        ASSERT_NE(letter, quads.end());
        EXPECT_EQ(std::next(man), letter);
        EXPECT_FLOAT_EQ(man->first.x, 586.0f);
        // GL's y (H − y scaled) with the float margin.
        EXPECT_NEAR(man->first.y, 480.0f - 437.0f, 1e-3f);
        EXPECT_FLOAT_EQ(letter->first.x, 615.0f);
        EXPECT_NEAR(letter->first.y, 480.0f - 440.0f, 1e-3f);
        EXPECT_EQ(std::ranges::count(quads, std::optional<std::uint16_t>{std::uint16_t{0x64f}}, &DrawnQuad::slot), 0);
        EXPECT_EQ(windows.windowList.list.barBlink, 1);
    }
}

// An intermediate MuRet drawing (GameScene, in the decoupled one) draws the icons with the projection's copy of the old
// UI: the drawings and blinks of 0x4a5120 (+0x148 and +0x14c) and the new letter (+0x144) of WorldRuntime's list stay
// as the original's last frame left them.
TEST_F(GameHudTest, AnIntermediateDrawBlinksTheCopyOfTheIcons)
{
    Client::Ui::OldWindowManager& windows = world.Runtime().oldWindows;
    windows.windowList.list.waiting.push_back(7);
    windows.windowList.list.newLetter = 1;
    windows.windowList.list.barBlink = 11;
    windows.windowList.list.letterBlinks = 5;
    Client::Ui::OldWindowManager copy = windows;
    GameHudContext context = Context(true);
    context.oldWindows = &copy;

    const auto drawn = Client::Hud::DrawTheHud(context);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // On the 11th of every 24, lit, one more blink; with the sixth, the new letter turns off and the count goes back to
    // 0.
    EXPECT_EQ(copy.windowList.list.barBlink, 12);
    EXPECT_EQ(copy.windowList.list.letterBlinks, 0);
    EXPECT_EQ(copy.windowList.list.newLetter, 0);
    EXPECT_EQ(windows.windowList.list.barBlink, 11);
    EXPECT_EQ(windows.windowList.list.letterBlinks, 5);
    EXPECT_EQ(windows.windowList.list.newLetter, 1);
}
// 4. 0x5a6fc0 with the object of 0x5767924 (the B2 20, milestone 13): the hero's two cells with their accesses and,
// inside the zone (X 150..200, Y 180..230), one line per half with +6 nonzero at (0, 150 + 15 · half). It does not
// look at the map. The names are made up.
std::array<std::uint8_t, Client::CastleSiege::SwitchHoldersSize> SwitchHolders()
{
    std::array<std::uint8_t, Client::CastleSiege::SwitchHoldersSize> holders{};
    const auto put = [&holders](std::size_t at, std::string_view text)
    {
        for (std::size_t i = 0; i < text.size(); ++i)
            holders.at(at + i) = static_cast<std::uint8_t>(text[i]);
    };
    holders[6] = 1;
    holders[7] = 2;
    put(8, "Robles");
    put(0x10, "Garza");
    holders[0x1b + 6] = 1;
    holders[0x1b + 7] = 4;
    put(0x1b + 8, "Pinos");
    put(0x1b + 0x10, "Tero");
    return holders;
}

TEST_F(GameHudTest, TheSwitchHoldersDrawOneLinePerShownHalfInsideTheZone)
{
    const auto title = data.text->Text(1981);
    ASSERT_TRUE(title.has_value());
    for (const std::int32_t map : {0, 2, 30})
    {
        SCOPED_TRACE(map);
        world.Runtime() = Client::Kernel::WorldRuntime{};
        world.Runtime().map = map;
        world.Runtime().castleSiege.castleSiegeInfo = SwitchHolders();
        Hero().cellX = 150;
        Hero().cellY = 230;
        // Bit 0x80000 with side 2 of the first half: with the name; the second (4 without 0x800000), without it.
        Hero().flags78 = 0x80000;
        text.texts.clear();
        random = CrtRandom(Seed);
        const CrtRandom expected = Advanced(random, HudBaseDraws + 2 * SmallLogicalCloseRandoms);

        const auto drawn = Hud();

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(Hero().cellXAccesses, 0);
        EXPECT_EQ(Hero().cellYAccesses, 0);
        ASSERT_EQ(text.texts.size(), 2u);
        EXPECT_EQ(text.texts[0].text, std::string(*title) + "1 / Robles / Garza");
        EXPECT_EQ(text.texts[0].x, 0);
        EXPECT_EQ(text.texts[0].y, 150);
        EXPECT_EQ(text.texts[1].text, std::string(*title) + "2 / Pinos");
        EXPECT_EQ(text.texts[1].y, 165);
        EXPECT_TRUE(world.Runtime().castleSiege.castleSiegeInfo.has_value());
    }
}

// The other two pairs of bit and side, and a half with +6 at 0, which is not drawn.
TEST_F(GameHudTest, TheSwitchHoldersMatchEachSideWithItsFlag)
{
    world.Runtime().castleSiege.castleSiegeInfo = SwitchHolders();
    auto& holders = *world.Runtime().castleSiege.castleSiegeInfo;
    holders[6] = 0;
    Hero().cellX = 200;
    Hero().cellY = 180;
    Hero().flags78 = 0x800000;
    const auto title = data.text->Text(1981);
    ASSERT_TRUE(title.has_value());
    ASSERT_TRUE(Hud().HasValue());
    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, std::string(*title) + "2 / Pinos / Tero");
    EXPECT_EQ(text.texts[0].y, 165);

    holders[6] = 1;
    holders[7] = 3;
    Hero().flags78 = 0x400000;
    text.texts.clear();
    ASSERT_TRUE(Hud().HasValue());
    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, std::string(*title) + "1 / Robles / Garza");
    EXPECT_EQ(text.texts[1].text, std::string(*title) + "2 / Pinos");
}

// Outside the zone, 0x4e05f0 frees the object after the two accesses; an intermediate drawing does not free it.
TEST_F(GameHudTest, TheSwitchHoldersAreFreedOutsideTheZone)
{
    for (const auto& [x, y] :
         std::array<std::array<std::uint8_t, 2>, 4>{{{149, 200}, {201, 200}, {170, 179}, {170, 231}}})
    {
        SCOPED_TRACE(static_cast<int>(x) * 1000 + y);
        world.Runtime().castleSiege.castleSiegeInfo = SwitchHolders();
        Hero().cellX = x;
        Hero().cellY = y;
        text.texts.clear();
        random = CrtRandom(Seed);
        const CrtRandom expected = Advanced(random, HudBaseDraws + 2 * SmallLogicalCloseRandoms);

        ASSERT_TRUE(Hud(true).HasValue());
        EXPECT_TRUE(world.Runtime().castleSiege.castleSiegeInfo.has_value());

        random = CrtRandom(Seed);
        ASSERT_TRUE(Hud().HasValue());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_FALSE(world.Runtime().castleSiege.castleSiegeInfo.has_value());
        EXPECT_TRUE(text.texts.empty());
    }
}

// A %s without a 0 before the end of the 0x36 bytes would keep reading the heap (Q-C1403): it stops with the address.
TEST_F(GameHudTest, ASwitchHolderTextWithoutItsZeroCuts)
{
    world.Runtime().castleSiege.castleSiegeInfo = SwitchHolders();
    auto& holders = *world.Runtime().castleSiege.castleSiegeInfo;
    for (std::size_t at = 0x1b + 8; at < holders.size(); ++at)
        holders.at(at) = 'z';
    Hero().cellX = 170;
    Hero().cellY = 200;
    const auto drawn = Hud();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("Q-C1403"), std::string::npos) << drawn.Error();
}
} // namespace
