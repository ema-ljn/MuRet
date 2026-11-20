#include "Scenes/MapBanner.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// [0x5763534], el cartel con el nombre del mapa: la tabla de 0x65d080, el reinicio (0x65dc00), el disparo (0x65dc30)
// y el fundido de cada dibujo (0x65dcd0).
namespace
{
using Client::Scenes::MapBanner;
using Client::Scenes::MapBannerState;

// 0x65d080: el mapa → Interface\*.tga; del 11 al 17 Blood Castle, del 18 al 23 Chaos Castle, del 24 al 29 y el 36
// Kalima. El 5, el 32 y el 35 no están.
TEST(MapBannerTest, TheTableHasTheImageOfEachMap)
{
    const MapBanner banner = Client::Scenes::MakeTheMapBanner();
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
    MapBanner banner = Client::Scenes::MakeTheMapBanner();
    banner.state = MapBannerState::FadingOut;
    banner.elapsed = 99;

    Client::Scenes::ResetTheMapBanner(banner, 1234);

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
    MapBanner banner = Client::Scenes::MakeTheMapBanner();
    Client::Scenes::ResetTheMapBanner(banner, 0);

    Client::Scenes::ShowTheMapBanner(banner, 2);
    EXPECT_EQ(banner.state, MapBannerState::FadingIn);
    EXPECT_EQ(banner.alpha, 0.2f);
    EXPECT_EQ(banner.elapsed, 0u);
    EXPECT_EQ(banner.shownMap, 2);
    ASSERT_TRUE(banner.pendingImage.has_value());
    EXPECT_EQ(*banner.pendingImage, "Interface\\devias.tga");

    banner.pendingImage.reset();
    banner.state = MapBannerState::Holding;
    Client::Scenes::ShowTheMapBanner(banner, 2);
    EXPECT_EQ(banner.state, MapBannerState::FadingIn);
    EXPECT_FALSE(banner.pendingImage.has_value());

    Client::Scenes::ShowTheMapBanner(banner, 5);
    ASSERT_TRUE(banner.pendingImage.has_value());
    EXPECT_EQ(*banner.pendingImage, "");
    EXPECT_TRUE(banner.files.contains(5));
    EXPECT_EQ(banner.shownMap, 5);
}

// 0x65dcd0: entra de a 0,015 por dibujo hasta llegar a 1 (la suma en la x87, contra 1,0: JZ con C0 | C3); se queda
// 5.000 ms de timeGetTime (JBE, sin signo) y sale de a 0,015 hasta 0, cuando se apaga. El reloj se guarda siempre.
TEST(MapBannerTest, ItFadesInHoldsFiveSecondsAndFadesOut)
{
    MapBanner banner = Client::Scenes::MakeTheMapBanner();
    Client::Scenes::ResetTheMapBanner(banner, 0);
    Client::Scenes::ShowTheMapBanner(banner, 0);
    std::uint32_t now = 0;
    int steps = 0;
    while (banner.state == MapBannerState::FadingIn && steps < 1000)
    {
        now += 20;
        Client::Scenes::StepTheMapBanner(banner, now);
        ++steps;
    }
    // De 0,2 a 1 de a 0,015: 0,2 + 54 · 0,015 = 1,01 es el primero que llega.
    EXPECT_EQ(steps, 54);
    EXPECT_EQ(banner.state, MapBannerState::Holding);
    EXPECT_EQ(banner.alpha, 1.0f);
    EXPECT_EQ(banner.last, now);

    now += 5000;
    Client::Scenes::StepTheMapBanner(banner, now);
    EXPECT_EQ(banner.state, MapBannerState::Holding);
    EXPECT_EQ(banner.elapsed, 5000u);
    now += 1;
    Client::Scenes::StepTheMapBanner(banner, now);
    EXPECT_EQ(banner.state, MapBannerState::FadingOut);
    EXPECT_EQ(banner.elapsed, 0u);

    steps = 0;
    while (banner.state == MapBannerState::FadingOut && steps < 1000)
    {
        Client::Scenes::StepTheMapBanner(banner, now);
        ++steps;
    }
    EXPECT_EQ(steps, 67);
    EXPECT_EQ(banner.state, MapBannerState::Off);
    EXPECT_EQ(banner.alpha, 0.0f);
}
} // namespace
