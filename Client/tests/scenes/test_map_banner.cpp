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

// [0x5763534], el cartel con el nombre del mapa: la tabla de 0x65d080, el reinicio (0x65dc00), el disparo (0x65dc30)
// y el fundido de cada dibujo (0x65dcd0).
namespace
{
using Client::Hud::MapBanner;
using Client::Hud::MapBannerState;

// 0x65d080: el mapa → Interface\*.tga; del 11 al 17 Blood Castle, del 18 al 23 Chaos Castle, del 24 al 29 y el 36
// Kalima. El 5, el 32 y el 35 no están.
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

// 0x65dc00, en el primer paso de la escena 5 (0x63692b): apagado, sin mapa (+0x14 en −1), el reloj, la cuenta en 0 y
// el alfa en 1.
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

// 0x65dc30: entra (1) con el alfa en 0,2 y la cuenta en 0; con un mapa distinto del de +0x14, pide su imagen al slot
// 0x66a (0x63bd00) y lo guarda. Con el mismo, no la vuelve a pedir. Un mapa sin imagen la busca con el operator[] del
// árbol, que lo mete vacío.
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

// 0x65dcd0: entra de a 0,015 por dibujo hasta llegar a 1 (la suma en la x87, contra 1,0: JZ con C0 | C3); se queda
// 5.000 ms de timeGetTime (JBE, sin signo) y sale de a 0,015 hasta 0, cuando se apaga. El reloj se guarda siempre.
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
    // De 0,2 a 1 de a 0,015: 0,2 + 54 · 0,015 = 1,01 es el primero que llega.
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

// 0x63bd00(imagen, 0x66a, GL_NEAREST, GL_CLAMP, 0, 1): la imagen pendiente sube desde Data\ al slot 0x66a (el OZT del
// nombre hasta el punto) y deja de estar pendiente; la de otro mapa la reemplaza en el mismo slot. Una de 24 bits no se
// sube y no avisa (Q-C14, Q-C29): queda la de antes. Una que falta, o el nombre vacío de un mapa sin imagen, corta: el
// original avisa "File not exist" y cierra (Q-C23).
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
