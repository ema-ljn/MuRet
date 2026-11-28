#include "Scenes/SceneTransitions.h"

#include "Kernel/SceneId.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>

// The scene transition table: the binary's eight `mov` to DAT_006d5dc8 (02 § Las cinco escenas y sus transiciones,
// read again in step 1 of CL5.4). The SceneManager uses it (test_scene_manager.cpp).
namespace
{
using Client::Kernel::SceneId;
using Client::Kernel::SceneName;
using Client::Scenes::FirstScene;
using Client::Scenes::IsOriginalTransition;
using Client::Scenes::OriginalTransitions;
using Client::Scenes::SceneTransition;

constexpr std::array<SceneId, 5> AllScenes = {SceneId::Loading, SceneId::Login, SceneId::WorldLoading,
                                              SceneId::CharacterSelect, SceneId::Game};

// The original's, written again by hand: from where, to where, and where the binary writes it.
constexpr std::array<SceneTransition, 8> Expected = {{
    {SceneId::Loading, SceneId::Login},                // 0x622f93, at the end of 0x622e40
    {SceneId::Login, SceneId::CharacterSelect},        // 0x634dce, in 0x634c60
    {SceneId::CharacterSelect, SceneId::WorldLoading}, // 0x633f05, 0x634470 and 0x634638
    {SceneId::WorldLoading, SceneId::Game},            // 0x635a91, in 0x6356f0
    {SceneId::Game, SceneId::CharacterSelect},         // 0x4c363f, in 0x4c2710
    {SceneId::CharacterSelect, SceneId::CharacterSelect},
    {SceneId::Game, SceneId::Login}, // 0x4c2f1a, in 0x4c2710
    {SceneId::CharacterSelect, SceneId::Login},
}};

bool IsExpected(SceneId from, SceneId to)
{
    return std::any_of(Expected.begin(), Expected.end(), [from, to](const SceneTransition& transition)
                       { return transition.from == from && transition.to == to; });
}

// The numbers of DAT_006d5dc8, for the log and the traces.
TEST(SceneTransitionsTest, TheScenesHaveTheNumbersOfTheOriginal)
{
    EXPECT_EQ(static_cast<int>(SceneId::Loading), 1);
    EXPECT_EQ(static_cast<int>(SceneId::Login), 2);
    EXPECT_EQ(static_cast<int>(SceneId::WorldLoading), 3);
    EXPECT_EQ(static_cast<int>(SceneId::CharacterSelect), 4);
    EXPECT_EQ(static_cast<int>(SceneId::Game), 5);
}

// In c32bbd5f's .data, DAT_006d5dc8 is 1.
TEST(SceneTransitionsTest, TheFirstSceneIsTheLoading)
{
    EXPECT_EQ(FirstScene, SceneId::Loading);
}

TEST(SceneTransitionsTest, TheTableIsTheOneOfTheOriginal)
{
    ASSERT_EQ(OriginalTransitions.size(), Expected.size());
    for (const SceneTransition& transition : Expected)
    {
        EXPECT_TRUE(IsOriginalTransition(transition.from, transition.to))
            << SceneName(transition.from) << " a " << SceneName(transition.to);
    }
}

// The 25 of five scenes: only the table's eight are valid. Among those that are not: from the loading to the game,
// staying in the login, going back to the loading, and the F1 02 in the login or in the world loading (Q-C79).
TEST(SceneTransitionsTest, RefusesEveryOther)
{
    for (const SceneId from : AllScenes)
    {
        for (const SceneId to : AllScenes)
        {
            EXPECT_EQ(IsOriginalTransition(from, to), IsExpected(from, to))
                << SceneName(from) << " a " << SceneName(to);
        }
    }
    EXPECT_FALSE(IsOriginalTransition(SceneId::Loading, SceneId::Game));
    EXPECT_FALSE(IsOriginalTransition(SceneId::Login, SceneId::Login));
    EXPECT_FALSE(IsOriginalTransition(SceneId::Login, SceneId::Loading));
    EXPECT_FALSE(IsOriginalTransition(SceneId::WorldLoading, SceneId::Login));
}

TEST(SceneTransitionsTest, NamesEachScene)
{
    EXPECT_EQ(SceneName(SceneId::Loading), "Loading");
    EXPECT_EQ(SceneName(SceneId::Login), "Login");
    EXPECT_EQ(SceneName(SceneId::WorldLoading), "WorldLoading");
    EXPECT_EQ(SceneName(SceneId::CharacterSelect), "CharacterSelect");
    EXPECT_EQ(SceneName(SceneId::Game), "Game");
    constexpr std::uint8_t outside = 9;
    EXPECT_EQ(SceneName(static_cast<SceneId>(outside)), "?");
}
} // namespace
