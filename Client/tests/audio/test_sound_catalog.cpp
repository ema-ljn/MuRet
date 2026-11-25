#include "Audio/ISoundCatalog.h"
#include "Sound/SoundCatalog.h"

#include <gtest/gtest.h>

namespace Client::Audio
{
TEST(SoundCatalogTest, FindsTheFirstUiClickSound)
{
    const auto sound = Sound::FindSound(0x19);

    ASSERT_TRUE(sound.has_value());
    EXPECT_EQ(sound->id, 0x19);
    EXPECT_EQ(sound->path, "sound/iButtonClick.wav");
    EXPECT_EQ(sound->voices, 1);
    EXPECT_FALSE(sound->positional);
}

TEST(SoundCatalogTest, StartupRegistrationsFollowNativeCallOrder)
{
    const auto sounds = Sound::StartupSounds();

    ASSERT_EQ(sounds.size(), 150);
    EXPECT_EQ(sounds[0].id, 0x8c);
    EXPECT_EQ(sounds[0].path, "sound/iDuelWindow.wav");
    EXPECT_EQ(sounds[1].id, 0x8d);
    EXPECT_EQ(sounds[2].id, 0);
    EXPECT_EQ(sounds[2].path, "sound/aWind.wav");
    EXPECT_EQ(sounds.back().id, 0x5d);
    EXPECT_EQ(sounds.back().path, "sound/iEventEnd.wav");
}

TEST(SoundCatalogTest, WorldRegistrationsFollowActiveMapBranch)
{
    const auto bloodCastle = Sound::WorldSoundsForMap(11);
    ASSERT_EQ(bloodCastle.size(), 1);
    EXPECT_EQ(bloodCastle[0].id, 0x8b);

    const auto chaosCastle = Sound::WorldSoundsForMap(18);
    ASSERT_EQ(chaosCastle.size(), 1);
    EXPECT_EQ(chaosCastle[0].id, 0x99);

    EXPECT_EQ(Sound::WorldSoundsForMap(24).size(), 15);
    EXPECT_EQ(Sound::WorldSoundsForMap(36).size(), 15);
    EXPECT_EQ(Sound::WorldSoundsForMap(30).size(), 26);
    EXPECT_EQ(Sound::WorldSoundsForMap(31).size(), 32);
    EXPECT_EQ(Sound::WorldSoundsForMap(33).size(), 34);
    EXPECT_EQ(Sound::WorldSoundsForMap(34).size(), 55);
    EXPECT_TRUE(Sound::WorldSoundsForMap(35).empty());
    EXPECT_TRUE(Sound::WorldSoundsForMap(-1).empty());
}

TEST(SoundCatalogTest, ReturnsExactMapAlternativePath)
{
    const auto sound = Sound::FindSound(0x1e2, "Sound\\BattleCastle\\aSiegeAmbi.wav");

    ASSERT_TRUE(sound.has_value());
    EXPECT_EQ(sound->path, "sound/battlecastle/aSiegeAmbi.wav");
    EXPECT_TRUE(sound->positional);
}

TEST(SoundCatalogTest, KeepsTheFirstGlobalRegistrationForWarriorPain)
{
    const auto sound = Sound::FindSound(0x27b);

    ASSERT_TRUE(sound.has_value());
    EXPECT_EQ(sound->path, "sound/pWpain1.wav");
}

TEST(SoundCatalogTest, ResolvesAlternativeRegistrationPaths)
{
    const auto sound = Sound::FindSound(0x146, "Sound\\mShadowAttack2.wav");

    ASSERT_TRUE(sound.has_value());
    EXPECT_EQ(sound->path, "sound/mShadowAttack2.wav");
    EXPECT_EQ(sound->voices, 2);
    EXPECT_TRUE(sound->positional);

    const auto castle = Sound::FindSound(0x1bb, "Sound\\BattleCastle\\m_BowMercAttack.wav");
    ASSERT_TRUE(castle.has_value());
    EXPECT_EQ(castle->path, "sound/battlecastle/m_BowMercAttack.wav");
}

TEST(SoundCatalogTest, PathLookupIgnoresCaseAndSlashDirection)
{
    const auto sound = Sound::FindSound(0x19, "SOUND\\IBUTTONCLICK.WAV");

    ASSERT_TRUE(sound.has_value());
    EXPECT_EQ(sound->path, "sound/iButtonClick.wav");
}

TEST(SoundCatalogTest, RejectsUnknownAlternativePaths)
{
    EXPECT_FALSE(Sound::FindSound(0x19, "sound/missing.wav").has_value());
}

TEST(SoundCatalogTest, ReturnsNoDefinitionForUnknownIds)
{
    EXPECT_FALSE(Sound::FindSound(-1).has_value());
    EXPECT_FALSE(Sound::FindSound(0x7fffffff).has_value());
}

TEST(SoundCatalogTest, GameCatalogFindsWhatFindSoundFinds)
{
    const ISoundCatalog& catalog = Sound::GameSoundCatalog();

    const auto sound = catalog.Find(0x146, "Sound\\mShadowAttack2.wav");
    ASSERT_TRUE(sound.has_value());
    EXPECT_EQ(sound->path, "sound/mShadowAttack2.wav");
    EXPECT_EQ(sound->voices, 2);
    EXPECT_FALSE(catalog.Find(0x19, "sound/missing.wav").has_value());
    EXPECT_FALSE(catalog.Find(0x7fffffff, "sound/iButtonClick.wav").has_value());
}

TEST(SoundCatalogTest, GameCatalogStartsTheNativeStartupSounds)
{
    const auto sounds = Sound::GameSoundCatalog().StartupSounds();

    EXPECT_EQ(sounds.data(), Sound::StartupSounds().data());
    EXPECT_EQ(sounds.size(), Sound::StartupSounds().size());
}
} // namespace Client::Audio
