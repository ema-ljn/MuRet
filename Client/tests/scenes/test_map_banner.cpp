#include "Hud/MapBanner.h"

#include "Data/Textures/Sampling.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// [0x5763534], the banner with the map's name: the table of 0x65d080, the reset (0x65dc00), the trigger (0x65dc30) and
// the fade of each drawing (0x65dcd0).
namespace
{
using Client::Hud::MapBanner;
using Client::Hud::MapBannerState;

// 0x65d080: the map → Interface\*.tga; 11 to 17 Blood Castle, 18 to 23 Chaos Castle, 24 to 29 and 36 Kalima. 5, 32 and
// 35 are not there.
TEST(MapBannerTest, TheTableHasTheImageOfEachMap)
{
    const MapBanner banner = Client::Hud::MakeTheMapBanner();
    EXPECT_EQ(banner.files.size(), 34u);
    EXPECT_EQ(banner.files.at(0), "Interface\\lorencia.tga");
    EXPECT_EQ(banner.files.at(1), "Interface\\dungeun.tga");
    EXPECT_EQ(banner.files.at(10), "Interface\\Icarus.tga");
    for (std::int32_t map = 11; map <= 17; ++map)
        EXPECT_EQ(banner.files.at(map), "Interface\\bloodcastle.tga") << map;
    for (std::int32_t map = 18; map <= 23; ++map)
        EXPECT_EQ(banner.files.at(map), "Interface\\chaoscastle.tga") << map;
    EXPECT_EQ(banner.files.at(30), "Interface\\loren.tga");
    EXPECT_EQ(banner.files.at(34), "Interface\\crywolffortress.tga");
    EXPECT_EQ(banner.files.at(36), "Interface\\Kalima.tga");
    EXPECT_FALSE(banner.files.contains(5));
    EXPECT_FALSE(banner.files.contains(32));
    EXPECT_FALSE(banner.files.contains(35));
}

// 0x65dc00, on the first step of scene 5 (0x63692b): off, without a map (+0x14 at −1), the clock, the count at 0 and
// the alpha at 1.
TEST(MapBannerTest, TheResetLeavesItOffWithoutAMap)
{
    MapBanner banner = Client::Hud::MakeTheMapBanner();
    banner.state = MapBannerState::FadingOut;
    banner.elapsed = 99;

    Client::Hud::ResetTheMapBanner(banner, 1234);

    EXPECT_EQ(banner.state, MapBannerState::Off);
    EXPECT_EQ(banner.shownMap, -1);
    EXPECT_EQ(banner.last, 1234u);
    EXPECT_EQ(banner.elapsed, 0u);
    EXPECT_EQ(banner.alpha, 1.0f);
}

// 0x65dc30: it enters (1) with the alpha at 0.2 and the count at 0; with a map other than the one at +0x14, it asks for
// its image in slot 0x66a (0x63bd00) and keeps it. With the same one, it does not ask again. A map without an image
// looks it up with the tree's operator[], which inserts it empty.
TEST(MapBannerTest, ShowingAskForTheImageOfANewMapOnce)
{
    MapBanner banner = Client::Hud::MakeTheMapBanner();
    Client::Hud::ResetTheMapBanner(banner, 0);

    Client::Hud::ShowTheMapBanner(banner, 2);
    EXPECT_EQ(banner.state, MapBannerState::FadingIn);
    EXPECT_EQ(banner.alpha, 0.2f);
    EXPECT_EQ(banner.elapsed, 0u);
    EXPECT_EQ(banner.shownMap, 2);
    ASSERT_TRUE(banner.pendingImage.has_value());
    EXPECT_EQ(*banner.pendingImage, "Interface\\devias.tga");

    banner.pendingImage.reset();
    banner.state = MapBannerState::Holding;
    Client::Hud::ShowTheMapBanner(banner, 2);
    EXPECT_EQ(banner.state, MapBannerState::FadingIn);
    EXPECT_FALSE(banner.pendingImage.has_value());

    Client::Hud::ShowTheMapBanner(banner, 5);
    ASSERT_TRUE(banner.pendingImage.has_value());
    EXPECT_EQ(*banner.pendingImage, "");
    EXPECT_TRUE(banner.files.contains(5));
    EXPECT_EQ(banner.shownMap, 5);
}

// 0x65dcd0: it fades in by 0.015 per drawing until it reaches 1 (the sum on the x87, against 1.0: JZ with C0 | C3); it
// holds 5000 ms of timeGetTime (JBE, unsigned) and fades out by 0.015 down to 0, when it turns off. The clock is always
// saved.
TEST(MapBannerTest, ItFadesInHoldsFiveSecondsAndFadesOut)
{
    MapBanner banner = Client::Hud::MakeTheMapBanner();
    Client::Hud::ResetTheMapBanner(banner, 0);
    Client::Hud::ShowTheMapBanner(banner, 0);
    std::uint32_t now = 0;
    int steps = 0;
    while (banner.state == MapBannerState::FadingIn && steps < 1000)
    {
        now += 20;
        Client::Hud::StepTheMapBanner(banner, now);
        ++steps;
    }
    // From 0.2 to 1 by 0.015: 0.2 + 54 · 0.015 = 1.01 is the first that reaches it.
    EXPECT_EQ(steps, 54);
    EXPECT_EQ(banner.state, MapBannerState::Holding);
    EXPECT_EQ(banner.alpha, 1.0f);
    EXPECT_EQ(banner.last, now);

    now += 5000;
    Client::Hud::StepTheMapBanner(banner, now);
    EXPECT_EQ(banner.state, MapBannerState::Holding);
    EXPECT_EQ(banner.elapsed, 5000u);
    now += 1;
    Client::Hud::StepTheMapBanner(banner, now);
    EXPECT_EQ(banner.state, MapBannerState::FadingOut);
    EXPECT_EQ(banner.elapsed, 0u);

    steps = 0;
    while (banner.state == MapBannerState::FadingOut && steps < 1000)
    {
        Client::Hud::StepTheMapBanner(banner, now);
        ++steps;
    }
    EXPECT_EQ(steps, 67);
    EXPECT_EQ(banner.state, MapBannerState::Off);
    EXPECT_EQ(banner.alpha, 0.0f);
}

// 0x63bd00(image, 0x66a, GL_NEAREST, GL_CLAMP, 0, 1): the pending image is uploaded from Data\ to slot 0x66a (the OZT
// of the name up to the dot) and stops being pending; another map's replaces it in the same slot. A 24-bit one is not
// uploaded and does not warn (Q-C14, Q-C29): the earlier one stays. A missing one, or the empty name of a map without
// an image, stops: the original warns "File not exist" and closes (Q-C23).
TEST(MapBannerTest, TheImageGoesToItsSlotAndAMissingOneCuts)
{
    const Client::Tests::TemporaryFolder data("map-banner");
    Client::Tests::WriteSceneFile(data.Path(), "Interface/lorencia.OZT", Client::Tests::PlainOzt(4, 4));
    Client::Tests::WriteSceneFile(data.Path(), "Interface/noria.OZT", Client::Tests::PlainOzt(8, 4));
    Client::Tests::WriteSceneFile(data.Path(), "Interface/devias.OZT", Client::Tests::PlainOzt(4, 4, 24));
    Client::Tests::FakeTextureStore store;
    const Client::Render::TextureSlot slot{Client::Hud::MapBannerSlot};
    MapBanner banner = Client::Hud::MakeTheMapBanner();
    Client::Hud::ResetTheMapBanner(banner, 0);
    const auto upload = [&] { return Client::Hud::UploadTheMapBanner(banner, data.Path(), store); };

    ASSERT_TRUE(upload().HasValue());
    EXPECT_EQ(store.loadCalls, 0);

    Client::Hud::ShowTheMapBanner(banner, 0);
    const auto lorencia = upload();
    ASSERT_TRUE(lorencia.HasValue()) << lorencia.Error();
    EXPECT_FALSE(banner.pendingImage.has_value());
    const auto first = store.Find(slot);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->texture.width, 4);
    EXPECT_EQ(first->sampling.filter, Client::Data::Textures::Filter::Nearest);
    EXPECT_EQ(first->sampling.wrap, Client::Data::Textures::Wrap::Clamp);

    Client::Hud::ShowTheMapBanner(banner, 3);
    const auto noria = upload();
    ASSERT_TRUE(noria.HasValue()) << noria.Error();
    const auto second = store.Find(slot);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->texture.width, 8);

    Client::Hud::ShowTheMapBanner(banner, 2);
    const auto devias = upload();
    ASSERT_TRUE(devias.HasValue()) << devias.Error();
    EXPECT_FALSE(banner.pendingImage.has_value());
    EXPECT_EQ(store.Find(slot)->texture.width, 8);
    EXPECT_EQ(store.loadCalls, 2);

    Client::Hud::ShowTheMapBanner(banner, 1);
    const auto dungeon = upload();
    ASSERT_FALSE(dungeon.HasValue());
    EXPECT_NE(dungeon.Error().find("dungeun"), std::string::npos) << dungeon.Error();

    Client::Hud::ShowTheMapBanner(banner, 5);
    EXPECT_FALSE(upload().HasValue());
    EXPECT_EQ(store.loadCalls, 2);
}
} // namespace
