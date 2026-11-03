#include "Scenes/SceneTransitions.h"

#include "Scenes/SceneId.h"
#include "support/ScenePrinters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>

// La tabla de transiciones de escena: los ocho `mov` a DAT_006d5dc8 del binario (02 § Las cinco
// escenas y sus transiciones, leída otra vez en el paso 1 de la CL5.4). La usa el SceneManager
// (test_scene_manager.cpp).
namespace
{
using Client::Scenes::FirstScene;
using Client::Scenes::IsOriginalTransition;
using Client::Scenes::OriginalTransitions;
using Client::Scenes::SceneId;
using Client::Scenes::SceneName;
using Client::Scenes::SceneTransition;

constexpr std::array<SceneId, 5> AllScenes = {SceneId::Loading, SceneId::Login, SceneId::WorldLoading,
                                              SceneId::CharacterSelect, SceneId::Game};

// Las del original, escritas otra vez a mano: de dónde, adónde, y dónde lo escribe el binario.
constexpr std::array<SceneTransition, 8> Expected = {{
    {SceneId::Loading, SceneId::Login},                // 0x622f93, al final de 0x622e40
    {SceneId::Login, SceneId::CharacterSelect},        // 0x634dce, en 0x634c60
    {SceneId::CharacterSelect, SceneId::WorldLoading}, // 0x633f05, 0x634470 y 0x634638
    {SceneId::WorldLoading, SceneId::Game},            // 0x635a91, en 0x6356f0
    {SceneId::Game, SceneId::CharacterSelect},         // 0x4c363f, en 0x4c2710
    {SceneId::CharacterSelect, SceneId::CharacterSelect},
    {SceneId::Game, SceneId::Login}, // 0x4c2f1a, en 0x4c2710
    {SceneId::CharacterSelect, SceneId::Login},
}};

bool IsExpected(SceneId from, SceneId to)
{
    return std::any_of(Expected.begin(), Expected.end(), [from, to](const SceneTransition& transition)
                       { return transition.from == from && transition.to == to; });
}

// Los números de DAT_006d5dc8, para el log y las trazas.
TEST(SceneTransitionsTest, TheScenesHaveTheNumbersOfTheOriginal)
{
    EXPECT_EQ(static_cast<int>(SceneId::Loading), 1);
    EXPECT_EQ(static_cast<int>(SceneId::Login), 2);
    EXPECT_EQ(static_cast<int>(SceneId::WorldLoading), 3);
    EXPECT_EQ(static_cast<int>(SceneId::CharacterSelect), 4);
    EXPECT_EQ(static_cast<int>(SceneId::Game), 5);
}

// En la .data de c32bbd5f, DAT_006d5dc8 vale 1.
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

// Las 25 de cinco escenas: solo las ocho de la tabla valen. Entre las que no: de la carga al juego,
// quedarse en el login, volver a la carga, y el F1 02 en el login o en la carga del mundo (Q-C79).
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
