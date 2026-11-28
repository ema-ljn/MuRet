#include "Hud/GameHover.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Chat/ChatBubbles.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// The game's hover and Alt (CL8 milestone 6; 06 § HUD y ventanas, "La pasada 2D del juego"): 0x5bbbc0. The text
// measures like the fake rasterizer: 6 columns per letter and 13 rows. The names are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHoverContext;
using Client::Kernel::ActorState;
using Client::Kernel::GameDrawTailState;
using Client::Kernel::HudState;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::SpriteBatch;
using Client::Tests::RecordingTextDrawer;

constexpr std::uint32_t Seed = 0x13572468u;
constexpr std::int32_t None = -1;
constexpr std::size_t Hero = 0;
constexpr std::size_t Guard = 5;
constexpr std::size_t Player = 7;
constexpr std::size_t Monster = 9;
// +0x88: 1 a player, 2 a monster and 4 an NPC.
constexpr std::uint8_t PlayerKind = 1;
constexpr std::uint8_t MonsterKind = 2;
constexpr std::uint8_t NpcKind = 4;

class GameHoverTest : public ::testing::Test
{
protected:
    GameHoverTest()
        : m_world(std::make_unique<WorldState>()), m_tail(std::make_unique<GameDrawTailState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Client::Core::PixelSize{640, 480});
        m_world->Runtime().hero = Hero;
        Actor(Hero, "Heroe", PlayerKind);
        Actor(Guard, "Guardia", NpcKind);
        Actor(Player, "Viajero", PlayerKind);
        Actor(Monster, "Arana", MonsterKind);
    }

    ActorState& Actor(std::size_t index, std::string_view name, std::uint8_t kind)
    {
        ActorState& actor = m_world->Actors()[index];
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = kind;
        actor.word1f2 = 0xffff;
        actor.name.fill(std::byte{0});
        for (std::size_t i = 0; i < name.size(); ++i)
            actor.name[i] = static_cast<std::byte>(name[i]);
        return actor;
    }

    void FloorItem(std::size_t index, bool seen)
    {
        m_tail->floorItems[index].alive = true;
        m_tail->floorItems[index].seen = seen;
    }

    Client::Core::Expected<void, std::string> Draw(bool altDown = false, bool projecting = false)
    {
        GameHoverContext context{.world = *m_world,
                                 .tail = *m_tail,
                                 .random = m_random,
                                 .targets = m_targets,
                                 .hud = m_hud,
                                 .altLatch = m_altLatch,
                                 .altNames = m_altNames,
                                 .altDown = altDown,
                                 .sprites = m_sprites,
                                 .text = m_text,
                                 .projecting = projecting};
        return Client::Hud::DrawTheHover(context);
    }

    [[nodiscard]] Client::Chat::ChatBubbleTable& Bubbles()
    {
        return m_world->Runtime().chat.bubbles;
    }

    std::unique_ptr<WorldState> m_world;
    std::unique_ptr<GameDrawTailState> m_tail;
    CrtRandom m_random{Seed};
    // The floor item, the NPC, the character, 0x6c8e34 and 0x6c8e38.
    std::array<std::int32_t, 5> m_targets{None, None, None, None, None};
    HudState m_hud{};
    std::int32_t m_altLatch = 0;
    std::uint8_t m_altNames = 0;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
};

// 0x5bbc10..0x5bbc2c: with no target, only 0x77f3b66 at 1, the white and no blending.
TEST_F(GameHoverTest, WithoutATargetItOnlyRaisesTheDurabilityWarning)
{
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_hud.durabilityWarning, 1);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_FALSE(Bubbles()[0].owner.has_value());
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5bbc57..0x5bbc78: the NPC, with its name alone in a bubble (the empty text: 100 steps).
TEST_F(GameHoverTest, AnNpcGetsItsNameAloneInABubble)
{
    m_targets[1] = static_cast<std::int32_t>(Guard);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles()[0].owner, Guard);
    EXPECT_EQ(Bubbles()[0].name, "Guardia");
    EXPECT_EQ(Bubbles()[0].nameTime, Client::Chat::ChatBubbleNewNameSteps);
    EXPECT_TRUE(Bubbles()[0].line1.empty());
    EXPECT_TRUE(m_text.texts.empty());
}

// 0x5bbc53: with the NPC picked, the character is not looked at.
TEST_F(GameHoverTest, TheNpcGoesBeforeTheCharacter)
{
    m_targets[1] = static_cast<std::int32_t>(Guard);
    m_targets[2] = static_cast<std::int32_t>(Monster);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles()[0].owner, Guard);
    EXPECT_FALSE(Bubbles()[1].owner.has_value());
    EXPECT_TRUE(m_text.texts.empty());
}

// 0x5bbd17..0x5bbd27: a player, without a personal shop, in a bubble like the NPC.
TEST_F(GameHoverTest, APlayerGetsItsNameInABubble)
{
    m_targets[2] = static_cast<std::int32_t>(Player);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Bubbles()[0].owner, Player);
    EXPECT_EQ(Bubbles()[0].name, "Viajero");
}

// 0x5bbc92: with 0x6c8e38 picked, the character does not go.
TEST_F(GameHoverTest, ThePickedOtherHidesTheCharacter)
{
    m_targets[2] = static_cast<std::int32_t>(Player);
    m_targets[4] = 3;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_FALSE(Bubbles()[0].owner.has_value());
    EXPECT_EQ(m_hud.durabilityWarning, 1);
}

// 0x5bbcbb..0x5bbcfc: a monster, with its name centered in the middle of the world (640 without panels) and at y = 10,
// with the background 0xff000064 and the text 0xffc8e6ff, which stay; 0x554180 keeps the size.
TEST_F(GameHoverTest, AMonsterGetsItsNameAtTheTop)
{
    m_targets[2] = static_cast<std::int32_t>(Monster);
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, "Arana");
    EXPECT_EQ(m_text.texts[0].x, 320 - 15);
    EXPECT_EQ(m_text.texts[0].y, 10);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xffc8e6ffu);
    EXPECT_EQ(m_text.texts[0].style.colors.background, 0xff000064u);
    EXPECT_EQ(m_text.texts[0].style.font, m_hud.dcFont);
    EXPECT_EQ(m_hud.textColors.text, 0xffc8e6ffu);
    EXPECT_EQ(m_hud.arrowsTextWidth, 30);
    EXPECT_EQ(m_hud.arrowsTextHeight, 13);
    EXPECT_FALSE(Bubbles()[0].owner.has_value());
    // Without the inventory open, the world's width does not consult the trade or the NPC shop.
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5bbd31..0x5bbd62: the name of the picked floor item (0x5ba790) without its tables stops. With them, in
// test_floor_items_scene.
TEST_F(GameHoverTest, TheFloorItemUnderTheMouseCuts)
{
    m_targets[0] = 12;
    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5bbd62"), std::string::npos);
}

// 0x5bbbd0..0x5bbc0a: in Chaos Castle, without "webzen" in the hero's name and with an NPC or a character picked,
// nothing.
TEST_F(GameHoverTest, ChaosCastleHidesTheNamesOfThePicked)
{
    m_world->Runtime().map = 0x12;
    m_targets[1] = static_cast<std::int32_t>(Guard);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_hud.durabilityWarning, 0);
    EXPECT_FALSE(Bubbles()[0].owner.has_value());

    m_world->Runtime().map = 0x17;
    m_targets[1] = None;
    m_targets[2] = static_cast<std::int32_t>(Monster);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(m_text.texts.empty());

    // Outside the two, the gate lets it through.
    m_world->Runtime().map = 0x18;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_text.texts.size(), 1U);
}

// 0x5bbbec: "webzen" in the hero's name.
TEST_F(GameHoverTest, WebzenSeesTheNamesInChaosCastle)
{
    m_world->Runtime().map = 0x14;
    Actor(Hero, "xwebzenx", PlayerKind);
    m_targets[1] = static_cast<std::int32_t>(Guard);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_hud.durabilityWarning, 1);
    EXPECT_EQ(Bubbles()[0].owner, Guard);
}

// 0x5bbd6a..0x5bbd7f: the Alt edge toggles 0x78abb38; Alt held does not toggle it again and releasing it lowers the
// latch.
TEST_F(GameHoverTest, TheAltEdgeTogglesTheFloorNames)
{
    ASSERT_TRUE(Draw(true).HasValue());
    EXPECT_EQ(m_altNames, 1);
    EXPECT_EQ(m_altLatch, 1);
    ASSERT_TRUE(Draw(true).HasValue());
    EXPECT_EQ(m_altNames, 1);
    ASSERT_TRUE(Draw(false).HasValue());
    EXPECT_EQ(m_altLatch, 0);
    EXPECT_EQ(m_altNames, 1);
    ASSERT_TRUE(Draw(true).HasValue());
    EXPECT_EQ(m_altNames, 0);
}

// 0x5bbd86..0x5bbdd2: with the names on, the name of a live, visible floor item (0x5ba790) without its tables stops;
// the one not visible is not named.
TEST_F(GameHoverTest, TheFloorNamesCutOnASeenItem)
{
    FloorItem(30, false);
    m_altNames = 1;
    ASSERT_TRUE(Draw().HasValue());
    FloorItem(31, true);
    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5bbdd2"), std::string::npos);
    EXPECT_NE(drawn.Error().find("31"), std::string::npos);
}

// 0x5bbd8f..0x5bbd9e: with the names off, only with Alt held. Here Alt turns off the ones that were on and stays held.
TEST_F(GameHoverTest, AltDownShowsTheFloorNamesEvenWhenItTurnsThemOff)
{
    FloorItem(31, true);
    ASSERT_TRUE(Draw().HasValue());
    m_altNames = 1;
    const auto drawn = Draw(true);
    EXPECT_EQ(m_altNames, 0);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5bbdd2"), std::string::npos);
}

// 0x5bbdb6: the picked item is not named again. With the NPC picked, the floor one does not stop at 0x5bbd62.
TEST_F(GameHoverTest, ThePickedFloorItemIsSkippedWithAlt)
{
    FloorItem(31, true);
    m_targets[0] = 31;
    m_targets[1] = static_cast<std::int32_t>(Guard);
    ASSERT_TRUE(Draw(true).HasValue());
    EXPECT_EQ(Bubbles()[0].owner, Guard);
}

// An intermediate MuRet drawing does not write the bubbles; the monster's name comes out anyway.
TEST_F(GameHoverTest, AProjectedDrawWritesNoBubble)
{
    m_targets[1] = static_cast<std::int32_t>(Guard);
    ASSERT_TRUE(Draw(false, true).HasValue());
    EXPECT_FALSE(Bubbles()[0].owner.has_value());
    m_targets[1] = None;
    m_targets[2] = static_cast<std::int32_t>(Monster);
    ASSERT_TRUE(Draw(false, true).HasValue());
    EXPECT_EQ(m_text.texts.size(), 1U);
}
} // namespace
