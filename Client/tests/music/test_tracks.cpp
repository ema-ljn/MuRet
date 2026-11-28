#include "Music/Tracks.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace Client::Music
{
namespace
{
TEST(TracksTest, NamesTheTwentyTwoPathsInTheTableSlotOrder)
{
    constexpr std::array<std::string_view, 22> Table{"Music\\Pub.mp3",
                                                     "Music\\Mutheme.mp3",
                                                     "Music\\Church.mp3",
                                                     "Music\\Devias.mp3",
                                                     "Music\\Noria.mp3",
                                                     "Music\\Dungeon.mp3",
                                                     "Music\\atlans.mp3",
                                                     "Music\\icarus.mp3",
                                                     "Music\\tarkan.mp3",
                                                     "Music\\lost_tower_a.mp3",
                                                     "Music\\lost_tower_b.mp3",
                                                     "Music\\kalima.mp3",
                                                     "Music\\castle.mp3",
                                                     "Music\\charge.mp3",
                                                     "Music\\lastend.mp3",
                                                     "Music\\huntingground.mp3",
                                                     "Music\\Aida.mp3",
                                                     "Music\\crywolf1st.mp3",
                                                     "Music\\crywolf_ready-02.ogg",
                                                     "Music\\crywolf_before-01.ogg",
                                                     "Music\\crywolf_back-03.ogg",
                                                     "Music\\main_theme.mp3"};
    EXPECT_EQ(TrackPaths, Table);
}

TEST(TracksTest, EachTrackOpensItsSlot)
{
    EXPECT_EQ(PathOf(Track::Pub), "Music\\Pub.mp3");
    EXPECT_EQ(PathOf(Track::LostTowerB), "Music\\lost_tower_b.mp3");
    EXPECT_EQ(PathOf(Track::CrywolfBack), "Music\\crywolf_back-03.ogg");
    EXPECT_EQ(PathOf(Track::MainTheme), "Music\\main_theme.mp3");
}
} // namespace
} // namespace Client::Music
