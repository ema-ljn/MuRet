#include "Scenes/MonsterModelTable.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Las tablas de 0x612d90 y 0x612270, leídas del listado caso por caso (*estático*). Los .wav son los de la .data, sin
// el "Data\" del principio (como FaunaModels); los bits de cada velocidad, los del MOV del ASM.
namespace Client::Scenes
{
namespace
{
using O = MonsterModelOpKind;
using N = NpcModelOpKind;

constexpr std::array<ActionSpeedStore, 7> CommonSpeeds{{
    {0, 0x3e800000},
    {1, 0x3e4ccccd},
    {2, 0x3eae147b},
    {3, 0x3ea8f5c3},
    {4, 0x3ea8f5c3},
    {5, 0x3f000000},
    {6, 0x3f0ccccd},
}};

struct EarlySpeeds
{
    std::int32_t number = 0;
    std::span<const ActionSpeedStore> stores{};
};

// El primer switch, por dirección del caso. 0x612f00 deja 0x3e19999a en ECX antes del salto, que usan el 0x40, el 0x63,
// el 0x65, el 0x66 y el 0x69.
constexpr ActionSpeedStore Early612f1d[]{{2, 0x3f333333}};
constexpr ActionSpeedStore Early612f2c[]{{2, 0x3f19999a}};
constexpr ActionSpeedStore Early612f3b[]{{2, 0x3f333333}};
constexpr ActionSpeedStore Early612f4a[]{{2, 0x3f99999a}};
constexpr ActionSpeedStore Early612f59[]{{2, 0x3e8f5c29}};
constexpr ActionSpeedStore Early612f68[]{{2, 0x3e8f5c29}};
constexpr ActionSpeedStore Early612f77[]{{2, 0x3f19999a}};
constexpr ActionSpeedStore Early612f86[]{{2, 0x3ecccccd}};
constexpr ActionSpeedStore Early612f95[]{{2, 0x3f000000}};
constexpr ActionSpeedStore Early612fa4[]{{2, 0x3e99999a}};
constexpr ActionSpeedStore Early612fb3[]{{2, 0x3e6147ae}};
constexpr ActionSpeedStore Early612fc2[]{{2, 0x3e3851ec}};
constexpr ActionSpeedStore Early612fd1[]{{3, 0x3eb33333}, {4, 0x3eb33333}};
constexpr ActionSpeedStore Early612fe7[]{{6, 0x3e99999a}};
constexpr ActionSpeedStore Early612ff6[]{{6, 0x3dcccccd}};
constexpr ActionSpeedStore Early613005[]{{3, 0x3e99999a}, {4, 0x3e800000}, {5, 0x3e19999a}, {6, 0x3e800000}};
constexpr ActionSpeedStore Early61302b[]{{3, 0x3e4ccccd}, {4, 0x3e99999a}};
constexpr ActionSpeedStore Early613044[]{{6, 0x3dcccccd}};
// 0x613053 y 0x61305d siguen en 0x6130ad con EAX.
constexpr ActionSpeedStore Early613053[]{{3, 0x3f000000}, {4, 0x3f000000}, {2, 0x3e99999a}};
constexpr ActionSpeedStore Early61305d[]{{3, 0x3ecccccd}, {4, 0x3ecccccd}, {2, 0x3e99999a}};
constexpr ActionSpeedStore Early613067[]{{3, 0x3ebd70a4}, {4, 0x3ebd70a4}, {6, 0x3e19999a}};
constexpr ActionSpeedStore Early613083[]{{6, 0x3e4ccccd}};
// 0x61308f (el 0x65) sigue en 0x613099 (el 0x66), que sigue en 0x6130ad.
constexpr ActionSpeedStore Early61308f[]{{6, 0x3e4ccccd}, {0, 0x3e19999a}, {1, 0x3e19999a},
                                         {3, 0x3e6b851f}, {4, 0x3e6b851f}, {2, 0x3e99999a}};
constexpr ActionSpeedStore Early613099[]{
    {0, 0x3e19999a}, {1, 0x3e19999a}, {3, 0x3e6b851f}, {4, 0x3e6b851f}, {2, 0x3e99999a}};
constexpr ActionSpeedStore Early6130c2[]{{2, 0x3e19999a}};
constexpr ActionSpeedStore Early6130ca[]{{2, 0x3e4ccccd}};
constexpr ActionSpeedStore Early6130d6[]{{7, 0x3ea8f5c3}, {8, 0x3ea8f5c3}};
constexpr ActionSpeedStore Early6130ec[]{{7, 0x3ea8f5c3}, {8, 0x3ea8f5c3}, {9, 0x3ea8f5c3}};

constexpr std::array<EarlySpeeds, 32> EarlyCases{{
    {0x2, Early612f1d},  {0x6, Early612f2c},  {0x8, Early612f3b},  {0x9, Early612f4a},  {0xa, Early612f59},
    {0xc, Early612fa4},  {0xd, Early612f68},  {0x11, Early612f95}, {0x13, Early612f77}, {0x14, Early612f86},
    {0x15, Early612f95}, {0x1c, Early612fa4}, {0x27, Early612fb3}, {0x29, Early612fc2}, {0x2a, Early612fd1},
    {0x2c, Early612fe7}, {0x3f, Early612ff6}, {0x40, Early613005}, {0x42, Early613044}, {0x43, Early61302b},
    {0x45, Early613044}, {0x46, Early613053}, {0x47, Early613053}, {0x48, Early61305d}, {0x59, Early6130ec},
    {0x5c, Early6130d6}, {0x5e, Early6130ca}, {0x63, Early613067}, {0x64, Early613083}, {0x65, Early61308f},
    {0x66, Early613099}, {0x69, Early6130c2},
}};

// 0x6131b3: Monster01 (el modelo 0x152).
constexpr MonsterModelOp Ops00[]{
    {.kind = O::Sound, .value = 0xd2, .path = "Sound\\mBull1.wav"},
    {.kind = O::Sound, .value = 0xd3, .path = "Sound\\mBull2.wav"},
    {.kind = O::Sound, .value = 0xd4, .path = "Sound\\mBullAttack1.wav"},
    {.kind = O::Sound, .value = 0xd5, .path = "Sound\\mBullAttack2.wav"},
    {.kind = O::Sound, .value = 0xd6, .path = "Sound\\mBullDie.wav"},
    {.kind = O::SoundTable, .model = 0x152, .sounds = {0x0, 0x1, 0x2, 0x3, 0x4, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x152, .value = 0x14},
};

// 0x613254: Monster02 (el modelo 0x153).
constexpr MonsterModelOp Ops01[]{
    {.kind = O::Sound, .value = 0xd7, .path = "Sound\\mHound1.wav"},
    {.kind = O::Sound, .value = 0xd8, .path = "Sound\\mHound2.wav"},
    {.kind = O::Sound, .value = 0xd9, .path = "Sound\\mHoundAttack1.wav"},
    {.kind = O::Sound, .value = 0xda, .path = "Sound\\mHoundAttack2.wav"},
    {.kind = O::Sound, .value = 0xdb, .path = "Sound\\mHoundDie.wav"},
    {.kind = O::SoundTable, .model = 0x153, .sounds = {0x5, 0x6, 0x7, 0x8, 0x9, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x153, .value = 0x5},
};

// 0x6132e7: Monster03 (el modelo 0x154).
constexpr MonsterModelOp Ops02[]{
    {.kind = O::Sound, .value = 0xdc, .path = "Sound\\mBudge1.wav"},
    {.kind = O::Sound, .value = 0xdd, .path = "Sound\\mBudgeAttack1.wav"},
    {.kind = O::Sound, .value = 0xde, .path = "Sound\\mBudgeDie.wav"},
    {.kind = O::SoundTable, .model = 0x154, .sounds = {0xa, 0xb, 0xb, 0xb, 0xc, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x154, .value = 0x7},
};

// 0x613387: Monster04 (el modelo 0x155).
constexpr MonsterModelOp Ops03[]{
    {.kind = O::Sound, .value = 0xe1, .path = "Sound\\mDarkKnight1.wav"},
    {.kind = O::Sound, .value = 0xe2, .path = "Sound\\mDarkKnight2.wav"},
    {.kind = O::Sound, .value = 0xe3, .path = "Sound\\mDarkKnightAttack1.wav"},
    {.kind = O::Sound, .value = 0xe4, .path = "Sound\\mDarkKnightAttack2.wav"},
    {.kind = O::Sound, .value = 0xe5, .path = "Sound\\mDarkKnightDie.wav"},
    {.kind = O::SoundTable, .model = 0x155, .sounds = {0xf, 0x10, 0x11, 0x12, 0x13, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x155, .value = 0x10},
};

// 0x61345b: Monster05 (el modelo 0x156).
constexpr MonsterModelOp Ops04[]{
    {.kind = O::Sound, .value = 0xe6, .path = "Sound\\mWizard1.wav"},
    {.kind = O::Sound, .value = 0xe7, .path = "Sound\\mWizard2.wav"},
    {.kind = O::Sound, .value = 0xe8, .path = "Sound\\mWizardAttack1.wav"},
    {.kind = O::Sound, .value = 0xe9, .path = "Sound\\mWizardAttack2.wav"},
    {.kind = O::Sound, .value = 0xea, .path = "Sound\\mWizardDie.wav"},
    {.kind = O::SoundTable, .model = 0x156, .sounds = {0x14, 0x15, 0x16, 0x17, 0x18, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x156, .value = 0x14},
};

// 0x6134ee: Monster06 (el modelo 0x157).
constexpr MonsterModelOp Ops05[]{
    {.kind = O::Sound, .value = 0xeb, .path = "Sound\\mGiant1.wav"},
    {.kind = O::Sound, .value = 0xec, .path = "Sound\\mGiant2.wav"},
    {.kind = O::Sound, .value = 0xed, .path = "Sound\\mGiantAttack1.wav"},
    {.kind = O::Sound, .value = 0xee, .path = "Sound\\mGiantAttack2.wav"},
    {.kind = O::Sound, .value = 0xef, .path = "Sound\\mGiantDie.wav"},
    {.kind = O::SoundTable, .model = 0x157, .sounds = {0x19, 0x1a, 0x1b, 0x1c, 0x1d, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x157, .value = 0x14},
};

// 0x613582: Monster07 (el modelo 0x158).
constexpr MonsterModelOp Ops06[]{
    {.kind = O::Sound, .value = 0xf0, .path = "Sound\\mLarva1.wav"},
    {.kind = O::Sound, .value = 0xf1, .path = "Sound\\mLarva2.wav"},
    {.kind = O::SoundTable, .model = 0x158, .sounds = {0x1e, 0x1f, 0x1e, 0x1f, 0x1f, -1, -1, -1, -1, -1}},
};

// 0x613635: Monster08 (el modelo 0x159).
constexpr MonsterModelOp Ops07[]{
    {.kind = O::Sound, .value = 0xf5, .path = "Sound\\mGhost1.wav"},
    {.kind = O::Sound, .value = 0xf6, .path = "Sound\\mGhost2.wav"},
    {.kind = O::Sound, .value = 0xf7, .path = "Sound\\mGhostAttack1.wav"},
    {.kind = O::Sound, .value = 0xf8, .path = "Sound\\mGhostAttack2.wav"},
    {.kind = O::Sound, .value = 0xf9, .path = "Sound\\mGhostDie.wav"},
    {.kind = O::SoundTable, .model = 0x159, .sounds = {0x23, 0x24, 0x25, 0x26, 0x27, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x159, .value = 0x14},
};

// 0x6135ca: Monster09 (el modelo 0x15a).
constexpr MonsterModelOp Ops08[]{
    {.kind = O::Sound, .value = 0xf2, .path = "Sound\\mHellSpider1.wav"},
    {.kind = O::Sound, .value = 0xf3, .path = "Sound\\mHellSpiderAttack1.wav"},
    {.kind = O::Sound, .value = 0xf4, .path = "Sound\\mHellSpiderDie.wav"},
    {.kind = O::SoundTable, .model = 0x15a, .sounds = {0x20, 0x21, 0x21, 0x21, 0x22, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x15a, .value = 0x10},
};

// 0x613352: Monster10 (el modelo 0x15b).
constexpr MonsterModelOp Ops09[]{
    {.kind = O::Sound, .value = 0xdf, .path = "Sound\\mSpider1.wav"},
    {.kind = O::SoundTable, .model = 0x15b, .sounds = {0xd, 0xd, 0xd, 0xd, 0xd, -1, -1, -1, -1, -1}},
};

// 0x6136c8: Monster11 (el modelo 0x15c).
constexpr MonsterModelOp Ops0a[]{
    {.kind = O::Sound, .value = 0xfa, .path = "Sound\\mOgre1.wav"},
    {.kind = O::Sound, .value = 0xfb, .path = "Sound\\mOgre2.wav"},
    {.kind = O::Sound, .value = 0xfc, .path = "Sound\\mOgreAttack1.wav"},
    {.kind = O::Sound, .value = 0xfd, .path = "Sound\\mOgreAttack2.wav"},
    {.kind = O::Sound, .value = 0xfe, .path = "Sound\\mOgreDie.wav"},
    {.kind = O::SoundTable, .model = 0x15c, .sounds = {0x28, 0x29, 0x2a, 0x2b, 0x2c, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x15c, .value = 0x14},
};

// 0x61375c: Monster12 (el modelo 0x15d).
constexpr MonsterModelOp Ops0b[]{
    {.kind = O::Sound, .value = 0xff, .path = "Sound\\mGorgon1.wav"},
    {.kind = O::Sound, .value = 0x100, .path = "Sound\\mGorgon2.wav"},
    {.kind = O::Sound, .value = 0x101, .path = "Sound\\mGorgonAttack1.wav"},
    {.kind = O::Sound, .value = 0x102, .path = "Sound\\mGorgonAttack2.wav"},
    {.kind = O::Sound, .value = 0x103, .path = "Sound\\mGorgonDie.wav"},
    {.kind = O::SoundTable, .model = 0x15d, .sounds = {0x2d, 0x2e, 0x2f, 0x30, 0x31, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x15d, .value = 0x14},
};

// 0x613a2f: Monster13 (el modelo 0x15e).
constexpr MonsterModelOp Ops0c[]{
    {.kind = O::Sound, .value = 0x116, .path = "Sound\\mYeti1.wav"},
    {.kind = O::Sound, .value = 0x117, .path = "Sound\\mYeti2.wav"},
    {.kind = O::Sound, .value = 0x118, .path = "Sound\\mYetiAttack1.wav"},
    {.kind = O::Sound, .value = 0x119, .path = "Sound\\mYetiDie.wav"},
    {.kind = O::SoundTable, .model = 0x15e, .sounds = {0x44, 0x44, 0x46, 0x46, 0x47, -1, -1, -1, -1, -1}},
    {.kind = O::SoundTable, .model = 0x15f, .sounds = {0x44, 0x45, 0x46, 0x46, 0x47, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x15e, .value = 0x14},
    {.kind = O::HeadBone, .model = 0x15f, .value = 0x14},
};

// 0x613a2f: Monster14 (el modelo 0x15f).
constexpr MonsterModelOp Ops0d[]{
    {.kind = O::Sound, .value = 0x116, .path = "Sound\\mYeti1.wav"},
    {.kind = O::Sound, .value = 0x117, .path = "Sound\\mYeti2.wav"},
    {.kind = O::Sound, .value = 0x118, .path = "Sound\\mYetiAttack1.wav"},
    {.kind = O::Sound, .value = 0x119, .path = "Sound\\mYetiDie.wav"},
    {.kind = O::SoundTable, .model = 0x15e, .sounds = {0x44, 0x44, 0x46, 0x46, 0x47, -1, -1, -1, -1, -1}},
    {.kind = O::SoundTable, .model = 0x15f, .sounds = {0x44, 0x45, 0x46, 0x46, 0x47, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x15e, .value = 0x14},
    {.kind = O::HeadBone, .model = 0x15f, .value = 0x14},
};

// 0x6139ba: Monster15 (el modelo 0x160).
constexpr MonsterModelOp Ops0e[]{
    {.kind = O::Sound, .value = 0x113, .path = "Sound\\mAssassinAttack1.wav"},
    {.kind = O::Sound, .value = 0x114, .path = "Sound\\mAssassinAttack2.wav"},
    {.kind = O::Sound, .value = 0x115, .path = "Sound\\mAssassinDie.wav"},
    {.kind = O::SoundTable, .model = 0x160, .sounds = {-1, -1, 0x41, 0x42, 0x43, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x160, .value = 0x14},
    {.kind = O::Speed, .index = 1, .value = 0x3eb33333},
};

// 0x6137f0: Monster16 (el modelo 0x161).
constexpr MonsterModelOp Ops0f[]{
    {.kind = O::Sound, .value = 0x104, .path = "Sound\\mIceMonster1.wav"},
    {.kind = O::Sound, .value = 0x105, .path = "Sound\\mIceMonster2.wav"},
    {.kind = O::Sound, .value = 0x106, .path = "Sound\\mIceMonsterDie.wav"},
    {.kind = O::SoundTable, .model = 0x161, .sounds = {0x32, 0x33, 0x32, 0x32, 0x34, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x161, .value = 0x13},
};

// 0x6138b5: Monster17 (el modelo 0x162).
constexpr MonsterModelOp Ops10[]{
    {.kind = O::Sound, .value = 0x10a, .path = "Sound\\mHomord1.wav"},
    {.kind = O::Sound, .value = 0x10b, .path = "Sound\\mHomord2.wav"},
    {.kind = O::Sound, .value = 0x10c, .path = "Sound\\mHomordAttack1.wav"},
    {.kind = O::Sound, .value = 0x10d, .path = "Sound\\mHomordDie.wav"},
    {.kind = O::SoundTable, .model = 0x162, .sounds = {0x38, 0x39, 0x3a, 0x3a, 0x3b, -1, -1, -1, -1, -1}},
};

// 0x61385a: Monster18 (el modelo 0x163).
constexpr MonsterModelOp Ops11[]{
    {.kind = O::Sound, .value = 0x107, .path = "Sound\\mWorm1.wav"},
    {.kind = O::Sound, .value = 0x108, .path = "Sound\\mWorm2.wav"},
    {.kind = O::Sound, .value = 0x109, .path = "Sound\\mWormDie.wav"},
    {.kind = O::SoundTable, .model = 0x163, .sounds = {0x35, 0x35, 0x37, 0x37, 0x37, -1, -1, -1, -1, -1}},
};

// 0x613926: Monster19 (el modelo 0x164).
constexpr MonsterModelOp Ops12[]{
    {.kind = O::Sound, .value = 0x10e, .path = "Sound\\mIceQueen1.wav"},
    {.kind = O::Sound, .value = 0x10f, .path = "Sound\\mIceQueen2.wav"},
    {.kind = O::Sound, .value = 0x110, .path = "Sound\\mIceQueenAttack1.wav"},
    {.kind = O::Sound, .value = 0x111, .path = "Sound\\mIceQueenAttack2.wav"},
    {.kind = O::Sound, .value = 0x112, .path = "Sound\\mIceQueenDie.wav"},
    {.kind = O::SoundTable, .model = 0x164, .sounds = {0x3c, 0x3d, 0x3e, 0x3f, 0x40, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x164, .value = 0x10},
};

// 0x613adf: Monster20 (el modelo 0x165).
constexpr MonsterModelOp Ops13[]{
    {.kind = O::Sound, .value = 0x11a, .path = "Sound\\mGoblin1.wav"},
    {.kind = O::Sound, .value = 0x11b, .path = "Sound\\mGoblin2.wav"},
    {.kind = O::Sound, .value = 0x11c, .path = "Sound\\mGoblinAttack1.wav"},
    {.kind = O::Sound, .value = 0x11d, .path = "Sound\\mGoblinAttack2.wav"},
    {.kind = O::Sound, .value = 0x11e, .path = "Sound\\mGoblinDie.wav"},
    {.kind = O::SoundTable, .model = 0x165, .sounds = {0x48, 0x49, 0x4a, 0x4b, 0x4c, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x165, .value = 0x6},
};

// 0x613b72: Monster21 (el modelo 0x166).
constexpr MonsterModelOp Ops14[]{
    {.kind = O::Sound, .value = 0x11f, .path = "Sound\\mScorpion1.wav"},
    {.kind = O::Sound, .value = 0x120, .path = "Sound\\mScorpion2.wav"},
    {.kind = O::Sound, .value = 0x121, .path = "Sound\\mScorpionAttack1.wav"},
    {.kind = O::Sound, .value = 0x122, .path = "Sound\\mScorpionAttack2.wav"},
    {.kind = O::Sound, .value = 0x123, .path = "Sound\\mScorpionDie.wav"},
    {.kind = O::SoundTable, .model = 0x166, .sounds = {0x4d, 0x4e, 0x4f, 0x50, 0x51, -1, -1, -1, -1, -1}},
};

// 0x613bf6: Monster22 (el modelo 0x167).
constexpr MonsterModelOp Ops15[]{
    {.kind = O::Sound, .value = 0x124, .path = "Sound\\mBeetle1.wav"},
    {.kind = O::Sound, .value = 0x125, .path = "Sound\\mBeetleAttack1.wav"},
    {.kind = O::Sound, .value = 0x126, .path = "Sound\\mBeetleDie.wav"},
    {.kind = O::SoundTable, .model = 0x167, .sounds = {0x52, 0x52, 0x53, 0x53, 0x54, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x167, .value = 0x5},
};

// 0x613c61: Monster23 (el modelo 0x168).
constexpr MonsterModelOp Ops16[]{
    {.kind = O::Sound, .value = 0x127, .path = "Sound\\mHunter1.wav"},
    {.kind = O::Sound, .value = 0x128, .path = "Sound\\mHunter2.wav"},
    {.kind = O::Sound, .value = 0x129, .path = "Sound\\mHunterAttack1.wav"},
    {.kind = O::Sound, .value = 0x12a, .path = "Sound\\mHunterAttack2.wav"},
    {.kind = O::Sound, .value = 0x12b, .path = "Sound\\mHunterDie.wav"},
    {.kind = O::SoundTable, .model = 0x168, .sounds = {0x55, 0x56, 0x57, 0x58, 0x59, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x168, .value = 0x6},
};

// 0x613cf5: Monster24 (el modelo 0x169).
constexpr MonsterModelOp Ops17[]{
    {.kind = O::Sound, .value = 0x12c, .path = "Sound\\mWoodMon1.wav"},
    {.kind = O::Sound, .value = 0x12d, .path = "Sound\\mWoodMon2.wav"},
    {.kind = O::Sound, .value = 0x12e, .path = "Sound\\mWoodMonAttack1.wav"},
    {.kind = O::Sound, .value = 0x12f, .path = "Sound\\mWoodMonAttack2.wav"},
    {.kind = O::Sound, .value = 0x130, .path = "Sound\\mWoodMonDie.wav"},
    {.kind = O::SoundTable, .model = 0x169, .sounds = {0x5a, 0x5b, 0x5c, 0x5d, 0x5e, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x169, .value = 0x6},
};

// 0x613d88: Monster25 (el modelo 0x16a).
constexpr MonsterModelOp Ops18[]{
    {.kind = O::Sound, .value = 0x131, .path = "Sound\\mArgon1.wav"},
    {.kind = O::Sound, .value = 0x132, .path = "Sound\\mArgon2.wav"},
    {.kind = O::Sound, .value = 0x133, .path = "Sound\\mArgonAttack1.wav"},
    {.kind = O::Sound, .value = 0x134, .path = "Sound\\mArgonAttack2.wav"},
    {.kind = O::Sound, .value = 0x135, .path = "Sound\\mArgonDie.wav"},
    {.kind = O::SoundTable, .model = 0x16a, .sounds = {0x5f, 0x60, 0x61, 0x62, 0x63, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x16a, .value = 0x10},
};

// 0x613e1c: Monster26 (el modelo 0x16b).
constexpr MonsterModelOp Ops19[]{
    {.kind = O::Sound, .value = 0x136, .path = "Sound\\mGolem1.wav"},
    {.kind = O::Sound, .value = 0x137, .path = "Sound\\mGolem2.wav"},
    {.kind = O::Sound, .value = 0x138, .path = "Sound\\mGolemAttack1.wav"},
    {.kind = O::Sound, .value = 0x139, .path = "Sound\\mGolemAttack2.wav"},
    {.kind = O::Sound, .value = 0x13a, .path = "Sound\\mGolemDie.wav"},
    {.kind = O::SoundTable, .model = 0x16b, .sounds = {0x64, 0x65, 0x66, 0x67, 0x68, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x16b, .value = 0x5},
};

// 0x613eb0: Monster27 (el modelo 0x16c).
constexpr MonsterModelOp Ops1a[]{
    {.kind = O::Sound, .value = 0x13b, .path = "Sound\\mYeti1.wav"},
    {.kind = O::Sound, .value = 0x13c, .path = "Sound\\mSatanAttack1.wav"},
    {.kind = O::Sound, .value = 0x13d, .path = "Sound\\mYetiDie.wav"},
    {.kind = O::SoundTable, .model = 0x16c, .sounds = {0x69, 0x69, 0x6a, 0x6a, 0x6b, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x16c, .value = 0x6},
};

// 0x613f1a: Monster28 (el modelo 0x16d).
constexpr MonsterModelOp Ops1b[]{
    {.kind = O::Sound, .value = 0x13e, .path = "Sound\\mBalrog1.wav"},
    {.kind = O::Sound, .value = 0x13f, .path = "Sound\\mBalrog2.wav"},
    {.kind = O::Sound, .value = 0x140, .path = "Sound\\mWizardAttack2.wav"},
    {.kind = O::Sound, .value = 0x141, .path = "Sound\\mGorgonAttack2.wav"},
    {.kind = O::Sound, .value = 0x142, .path = "Sound\\mBalrogDie.wav"},
    {.kind = O::SoundTable, .model = 0x16d, .sounds = {0x6c, 0x6d, 0x6e, 0x6f, 0x70, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x16d, .value = 0x6},
    {.kind = O::ModelByte, .model = 0x16d, .index = 0x88, .value = 0x1},
};

// 0x613fbb: Monster29 (el modelo 0x16e).
constexpr MonsterModelOp Ops1c[]{
    {.kind = O::Sound, .value = 0x143, .path = "Sound\\mShadow1.wav"},
    {.kind = O::Sound, .value = 0x144, .path = "Sound\\mShadow2.wav"},
    {.kind = O::Sound, .value = 0x145, .path = "Sound\\mShadowAttack1.wav"},
    {.kind = O::Sound, .value = 0x146, .path = "Sound\\mShadowAttack2.wav"},
    {.kind = O::Sound, .value = 0x147, .path = "Sound\\mShadowDie.wav"},
    {.kind = O::SoundTable, .model = 0x16e, .sounds = {0x71, 0x72, 0x73, 0x74, 0x75, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x16e, .value = 0x5},
};

// 0x613387: Monster30 (el modelo 0x16f).
constexpr MonsterModelOp Ops1d[]{
    {.kind = O::Sound, .value = 0xe1, .path = "Sound\\mDarkKnight1.wav"},
    {.kind = O::Sound, .value = 0xe2, .path = "Sound\\mDarkKnight2.wav"},
    {.kind = O::Sound, .value = 0xe3, .path = "Sound\\mDarkKnightAttack1.wav"},
    {.kind = O::Sound, .value = 0xe4, .path = "Sound\\mDarkKnightAttack2.wav"},
    {.kind = O::Sound, .value = 0xe5, .path = "Sound\\mDarkKnightDie.wav"},
    {.kind = O::SoundTable, .model = 0x16f, .sounds = {0xf, 0x10, 0x11, 0x12, 0x13, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x16f, .value = 0x14},
};

// 0x6131b3: Monster31 (el modelo 0x170).
constexpr MonsterModelOp Ops1e[]{
    {.kind = O::Sound, .value = 0xd2, .path = "Sound\\mBull1.wav"},
    {.kind = O::Sound, .value = 0xd3, .path = "Sound\\mBull2.wav"},
    {.kind = O::Sound, .value = 0xd4, .path = "Sound\\mBullAttack1.wav"},
    {.kind = O::Sound, .value = 0xd5, .path = "Sound\\mBullAttack2.wav"},
    {.kind = O::Sound, .value = 0xd6, .path = "Sound\\mBullDie.wav"},
    {.kind = O::SoundTable, .model = 0x170, .sounds = {0x0, 0x1, 0x2, 0x3, 0x4, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x170, .value = 0x14},
};

// 0x61404e: Monster32 (el modelo 0x171).
constexpr MonsterModelOp Ops1f[]{
    {.kind = O::Sound, .value = 0x14d, .path = "Sound\\mYeti1.wav"},
    {.kind = O::Sound, .value = 0x14e, .path = "Sound\\mBullAttack1.wav"},
    {.kind = O::Sound, .value = 0x14f, .path = "Sound\\mYetiDie.wav"},
    {.kind = O::SoundTable, .model = 0x171, .sounds = {0x7b, 0x7b, 0x7c, 0x7c, 0x7d, -1, -1, -1, -1, -1}},
    {.kind = O::Speed, .index = 3, .value = 0x3f000000},
    {.kind = O::Speed, .index = 4, .value = 0x3f333333},
    {.kind = O::Speed, .index = 1, .value = 0x3f4ccccd},
    {.kind = O::Speed, .index = 7, .value = 0x3f4ccccd},
};

// 0x6140ce: Monster33 (el modelo 0x172).
constexpr MonsterModelOp Ops20[]{
    {.kind = O::Sound, .value = 0x150, .path = "Sound\\mBali1.wav"},
    {.kind = O::Sound, .value = 0x151, .path = "Sound\\mBali2.wav"},
    {.kind = O::Sound, .value = 0x152, .path = "Sound\\mBaliAttack1.wav"},
    {.kind = O::Sound, .value = 0x153, .path = "Sound\\mBaliAttack2.wav"},
    {.kind = O::SoundTable, .model = 0x172, .sounds = {0x7e, 0x7f, 0x80, 0x81, 0x7f, -1, -1, -1, -1, -1}},
    {.kind = O::Speed, .index = 8, .value = 0x3ecccccd},
    {.kind = O::Speed, .index = 9, .value = 0x3ecccccd},
    {.kind = O::Speed, .index = 7, .value = 0x3ecccccd},
    {.kind = O::Speed, .index = 10, .value = 0x3ecccccd},
    {.kind = O::HeadBone, .model = 0x172, .value = 0x6},
};

// 0x614172: Monster34 (el modelo 0x173).
constexpr MonsterModelOp Ops21[]{
    {.kind = O::Sound, .value = 0x154, .path = "Sound\\mBahamut1.wav"},
    {.kind = O::Sound, .value = 0x155, .path = "Sound\\mYeti1.wav"},
    {.kind = O::SoundTable, .model = 0x173, .sounds = {0x82, 0x82, 0x83, 0x83, 0x82, -1, -1, -1, -1, -1}},
};

// 0x6141c9: Monster35 (el modelo 0x174).
constexpr MonsterModelOp Ops22[]{
    {.kind = O::Sound, .value = 0x156, .path = "Sound\\mBepar1.wav"},
    {.kind = O::Sound, .value = 0x157, .path = "Sound\\mBepar2.wav"},
    {.kind = O::Sound, .value = 0x158, .path = "Sound\\mBalrog1.wav"},
    {.kind = O::SoundTable, .model = 0x174, .sounds = {0x84, 0x85, 0x68, 0x68, 0x85, -1, -1, -1, -1, -1}},
    {.kind = O::Speed, .index = 3, .value = 0x3f000000},
    {.kind = O::Speed, .index = 4, .value = 0x3f000000},
    {.kind = O::HeadBone, .model = 0x174, .value = 0x14},
};

// 0x614245: Monster36 (el modelo 0x175).
constexpr MonsterModelOp Ops23[]{
    {.kind = O::Sound, .value = 0x159, .path = "Sound\\mValkyrie1.wav"},
    {.kind = O::Sound, .value = 0x15a, .path = "Sound\\mBaliAttack2.wav"},
    {.kind = O::Sound, .value = 0x15b, .path = "Sound\\mValkyrieDie.wav"},
    {.kind = O::SoundTable, .model = 0x175, .sounds = {0x87, 0x87, 0x88, 0x88, 0x89, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x175, .value = 0x13},
};

// 0x6142be: Monster37 (el modelo 0x176).
constexpr MonsterModelOp Ops24[]{
    {.kind = O::Sound, .value = 0x15c, .path = "Sound\\mLizardKing1.wav"},
    {.kind = O::Sound, .value = 0x15d, .path = "Sound\\mLizardKing2.wav"},
    {.kind = O::Sound, .value = 0x15e, .path = "Sound\\mGorgonDie.wav"},
    {.kind = O::SoundTable, .model = 0x176, .sounds = {0x8a, 0x8b, 0x8a, 0x8b, 0x8c, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x176, .value = 0x13},
};

// 0x61435e: Monster38 (el modelo 0x177).
constexpr MonsterModelOp Ops25[]{
    {.kind = O::Sound, .value = 0x15f, .path = "Sound\\mHydra1.wav"},
    {.kind = O::Sound, .value = 0x160, .path = "Sound\\mHydraAttack1.wav"},
    {.kind = O::SoundTable, .model = 0x177, .sounds = {0x8d, 0x8d, 0x8e, 0x8e, 0x8d, -1, -1, -1, -1, -1}},
    {.kind = O::Speed, .index = 3, .value = 0x3e19999a},
    {.kind = O::Speed, .index = 4, .value = 0x3e19999a},
    {.kind = O::Speed, .index = 6, .value = 0x3e4ccccd},
};

// 0x613387: Monster40 (el modelo 0x179).
constexpr MonsterModelOp Ops27[]{
    {.kind = O::Sound, .value = 0xe1, .path = "Sound\\mDarkKnight1.wav"},
    {.kind = O::Sound, .value = 0xe2, .path = "Sound\\mDarkKnight2.wav"},
    {.kind = O::Sound, .value = 0xe3, .path = "Sound\\mDarkKnightAttack1.wav"},
    {.kind = O::Sound, .value = 0xe4, .path = "Sound\\mDarkKnightAttack2.wav"},
    {.kind = O::Sound, .value = 0xe5, .path = "Sound\\mDarkKnightDie.wav"},
    {.kind = O::SoundTable, .model = 0x179, .sounds = {0xf, 0x10, 0x11, 0x12, 0x13, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x179, .value = 0x13},
};

// 0x6142be: Monster41 (el modelo 0x17a).
constexpr MonsterModelOp Ops28[]{
    {.kind = O::Sound, .value = 0x15c, .path = "Sound\\mLizardKing1.wav"},
    {.kind = O::Sound, .value = 0x15d, .path = "Sound\\mLizardKing2.wav"},
    {.kind = O::Sound, .value = 0x15e, .path = "Sound\\mGorgonDie.wav"},
    {.kind = O::SoundTable, .model = 0x17a, .sounds = {0x8a, 0x8b, 0x8a, 0x8b, 0x8c, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x17a, .value = 0x6},
};

// 0x6143d0: Monster42 (el modelo 0x17b).
constexpr MonsterModelOp Ops29[]{
    {.kind = O::Sound, .value = 0x161, .path = "Sound\\iron1.wav"},
    {.kind = O::Sound, .value = 0x162, .path = "Sound\\iron_attack1.wav"},
    {.kind = O::SoundTable, .model = 0x17b, .sounds = {0x8f, 0x8f, 0x90, 0x90, 0x90, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x17b, .value = 0x3},
};

// 0x614437: Monster43 (el modelo 0x17c).
constexpr MonsterModelOp Ops2a[]{
    {.kind = O::Sound, .value = 0x163, .path = "Sound\\jaikan1.wav"},
    {.kind = O::Sound, .value = 0x164, .path = "Sound\\jaikan2.wav"},
    {.kind = O::Sound, .value = 0x165, .path = "Sound\\jaikan_attack1.wav"},
    {.kind = O::Sound, .value = 0x166, .path = "Sound\\jaikan_attack2.wav"},
    {.kind = O::Sound, .value = 0x167, .path = "Sound\\jaikan_die.wav"},
    {.kind = O::SoundTable, .model = 0x17c, .sounds = {0x91, 0x92, 0x93, 0x94, 0x95, -1, -1, -1, -1, -1}},
    {.kind = O::JpegTexture, .value = 0x6f5, .path = "Monster\\bv01_2.jpg", .filter = 0x2601, .wrap = 0x2901},
    {.kind = O::JpegTexture, .value = 0x6f6, .path = "Monster\\bv02_2.jpg", .filter = 0x2601, .wrap = 0x2901},
    {.kind = O::HeadBone, .model = 0x17c, .value = 0x14},
};

// 0x614517: Monster44 (el modelo 0x17d).
constexpr MonsterModelOp Ops2b[]{
    {.kind = O::Sound, .value = 0x168, .path = "Sound\\blood1.wav"},
    {.kind = O::Sound, .value = 0x169, .path = "Sound\\blood_attack1.wav"},
    {.kind = O::Sound, .value = 0x16a, .path = "Sound\\blood_attack2.wav"},
    {.kind = O::Sound, .value = 0x16b, .path = "Sound\\blood_die.wav"},
    {.kind = O::SoundTable, .model = 0x17d, .sounds = {0x96, 0x96, 0x97, 0x98, 0x99, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x17d, .value = 0x7},
};

// 0x6145a6: Monster45 (el modelo 0x17e).
constexpr MonsterModelOp Ops2c[]{
    {.kind = O::Sound, .value = 0x16c, .path = "Sound\\death1.wav"},
    {.kind = O::Sound, .value = 0x16d, .path = "Sound\\death_attack1.wav"},
    {.kind = O::Sound, .value = 0x16e, .path = "Sound\\death_die.wav"},
    {.kind = O::SoundTable, .model = 0x17e, .sounds = {0x9a, 0x9a, 0x9b, 0x9b, 0x9c, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x17e, .value = 0x6},
};

// 0x614620: Monster46 (el modelo 0x17f).
constexpr MonsterModelOp Ops2d[]{
    {.kind = O::Sound, .value = 0x16f, .path = "Sound\\mutant1.wav"},
    {.kind = O::Sound, .value = 0x170, .path = "Sound\\mutant2.wav"},
    {.kind = O::Sound, .value = 0x171, .path = "Sound\\mutant_attack1.wav"},
    {.kind = O::SoundTable, .model = 0x17f, .sounds = {0x9d, 0x9e, 0x9f, 0x9f, 0x9f, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x17f, .value = 0x6},
};

// 0x61469a: Monster47 (el modelo 0x180).
constexpr MonsterModelOp Ops2e[]{
    {.kind = O::Sound, .value = 0x174, .path = "Sound\\mOrcArcherAttack1.wav"},
    {.kind = O::Sound, .value = 0xd6, .path = "Sound\\mBullDie.wav"},
    {.kind = O::SoundTable, .model = 0x180, .sounds = {-1, -1, 0xa2, 0xa2, 0x4, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x180, .value = 0x7},
};

// 0x6146f7: Monster48 (el modelo 0x181).
constexpr MonsterModelOp Ops2f[]{
    {.kind = O::Sound, .value = 0x128, .path = "Sound\\mHunter2.wav"},
    {.kind = O::Sound, .value = 0xd6, .path = "Sound\\mBullDie.wav"},
    {.kind = O::Sound, .value = 0x173, .path = "Sound\\mOrcCapAttack1.wav"},
    {.kind = O::SoundTable, .model = 0x181, .sounds = {0x56, 0x56, 0xa1, 0xa1, 0x4, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x181, .value = 0x14},
};

// 0x614768: Monster49 (el modelo 0x182).
constexpr MonsterModelOp Ops30[]{
    {.kind = O::Sound, .value = 0x172, .path = "Sound\\mCursedKing1.wav"},
    {.kind = O::Sound, .value = 0x176, .path = "Sound\\mCursedKing2.wav"},
    {.kind = O::Sound, .value = 0x175, .path = "Sound\\mCursedKingDie1.wav"},
    {.kind = O::SoundTable, .model = 0x182, .sounds = {0xa0, 0xa4, -1, -1, 0xa3, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x182, .value = 0x14},
};

// 0x61488d: Monster50 (el modelo 0x183).
constexpr MonsterModelOp Ops31[]{
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
    {.kind = O::Sound, .value = 0x183, .path = "Sound\\mMolt1.wav"},
    {.kind = O::Sound, .value = 0x184, .path = "Sound\\mMoltAttack1.wav"},
    {.kind = O::Sound, .value = 0x185, .path = "Sound\\mMoltDie.wav"},
    {.kind = O::SoundTable, .model = 0x183, .sounds = {0xb1, 0xb1, 0xb2, 0xb2, 0xb3, -1, -1, -1, -1, -1}},
};

// 0x614901: Monster51 (el modelo 0x184).
constexpr MonsterModelOp Ops32[]{
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
    {.kind = O::Sound, .value = 0x181, .path = "Sound\\mAlquamosAttack1.wav"},
    {.kind = O::Sound, .value = 0x182, .path = "Sound\\mAlquamosDie.wav"},
    {.kind = O::SoundTable, .model = 0x184, .sounds = {0xaf, 0xaf, 0xaf, 0xaf, 0xb0, -1, -1, -1, -1, -1}},
};

// 0x614962: Monster52 (el modelo 0x185).
constexpr MonsterModelOp Ops33[]{
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
    {.kind = O::Sound, .value = 0x17d, .path = "Sound\\mRainner1.wav"},
    {.kind = O::Sound, .value = 0x17e, .path = "Sound\\mRainnerAttack1.wav"},
    {.kind = O::Sound, .value = 0x17f, .path = "Sound\\mRainnerDie.wav"},
    {.kind = O::SoundTable, .model = 0x185, .sounds = {0xab, -1, 0xac, 0xac, 0xad, -1, -1, -1, -1, -1}},
};

// 0x6147dc: Monster53 (el modelo 0x186).
constexpr MonsterModelOp Ops34[]{
    {.kind = O::TgaTexture, .value = 0x253, .path = "Monster\\iui02.tga", .filter = 0x2600, .wrap = 0x2900},
    {.kind = O::TgaTexture, .value = 0x255, .path = "Monster\\iui03.tga", .filter = 0x2600, .wrap = 0x2900},
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
    {.kind = O::Sound, .value = 0x186, .path = "Sound\\mMegaCrust1.wav"},
    {.kind = O::Sound, .value = 0x187, .path = "Sound\\mMegaCrustAttack1.wav"},
    {.kind = O::Sound, .value = 0x188, .path = "Sound\\mMegaCrustDie.wav"},
    {.kind = O::SoundTable, .model = 0x186, .sounds = {0xb4, 0xb4, 0xb5, 0xb5, 0xb6, -1, -1, -1, -1, -1}},
};

// 0x6149d3: Monster54 (el modelo 0x187).
constexpr MonsterModelOp Ops35[]{
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
    {.kind = O::Sound, .value = 0x17a, .path = "Sound\\mPhantom1.wav"},
    {.kind = O::Sound, .value = 0x17b, .path = "Sound\\mPhantomAttack1.wav"},
    {.kind = O::Sound, .value = 0x17c, .path = "Sound\\mPhantomDie.wav"},
    {.kind = O::SoundTable, .model = 0x187, .sounds = {0xa8, 0xa8, 0xa9, 0xa9, 0xaa, -1, -1, -1, -1, -1}},
};

// 0x614a47: Monster55 (el modelo 0x188).
constexpr MonsterModelOp Ops36[]{
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
    {.kind = O::Sound, .value = 0x177, .path = "Sound\\mDrakan1.wav"},
    {.kind = O::Sound, .value = 0x178, .path = "Sound\\mDrakanAttack1.wav"},
    {.kind = O::Sound, .value = 0x179, .path = "Sound\\mDrakanDie.wav"},
    {.kind = O::SoundTable, .model = 0x188, .sounds = {0xa5, 0xa5, 0xa6, 0xa6, 0xa7, -1, -1, -1, -1, -1}},
};

// 0x614abb: Monster56 (el modelo 0x189).
constexpr MonsterModelOp Ops37[]{
    {.kind = O::Sound, .value = 0x189, .path = "Sound\\mPhoenix1.wav"},
    {.kind = O::Sound, .value = 0x18a, .path = "Sound\\mPhoenix1.wav"},
    {.kind = O::Sound, .value = 0x18b, .path = "Sound\\mPhoenixAttack1.wav"},
    {.kind = O::SoundTable, .model = 0x189, .sounds = {0xb7, 0xb8, 0xb9, 0xb9, -1, -1, -1, -1, -1, -1}},
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
};

// 0x614b1d: Monster57 (el modelo 0x18a).
constexpr MonsterModelOp Ops38[]{
    {.kind = O::Speed, .index = 6, .value = 0x3e6147ae},
};

// 0x614c45: Monster58 (el modelo 0x18b).
constexpr MonsterModelOp Ops39[]{
    {.kind = O::Sound, .value = 0x192, .path = "Sound\\mRedSkull.wav"},
    {.kind = O::Sound, .value = 0x193, .path = "Sound\\mRedSkullDie.wav"},
    {.kind = O::Sound, .value = 0x194, .path = "Sound\\mRedSkullAttack.wav"},
    {.kind = O::SoundTable, .model = 0x18b, .sounds = {0xc0, -1, 0xc2, -1, 0xc1, -1, -1, -1, -1, -1}},
};

// 0x614bf4: Monster59 (el modelo 0x18c).
constexpr MonsterModelOp Ops3a[]{
    {.kind = O::Sound, .value = 0x18e, .path = "Sound\\mHunter2.wav"},
    {.kind = O::Sound, .value = 0x191, .path = "Sound\\mGhaintOrgerDie.wav"},
    {.kind = O::SoundTable, .model = 0x18c, .sounds = {0xbc, -1, 0xbe, -1, 0xbf, -1, -1, -1, -1, -1}},
};

// 0x614b7a: Monster60 (el modelo 0x18d).
constexpr MonsterModelOp Ops3b[]{
    {.kind = O::Sound, .value = 0xd6, .path = "Sound\\mBullDie.wav"},
    {.kind = O::Sound, .value = 0x18e, .path = "Sound\\mHunter2.wav"},
    {.kind = O::Sound, .value = 0x18f, .path = "Sound\\mBlackSkullDie.wav"},
    {.kind = O::Sound, .value = 0x190, .path = "Sound\\mBlackSkullAttack.wav"},
    {.kind = O::SoundTable, .model = 0x18d, .sounds = {0xbc, -1, 0xbe, -1, 0xbd, -1, -1, -1, -1, -1}},
};

// 0x614b2c: Monster63 (el modelo 0x190).
constexpr MonsterModelOp Ops3e[]{
    {.kind = O::Sound, .value = 0x18c, .path = "Sound\\mMagicSkull.wav"},
    {.kind = O::Sound, .value = 0x18d, .path = "Sound\\mMagicSkull.wav"},
    {.kind = O::SoundTable, .model = 0x190, .sounds = {0xba, -1, -1, -1, 0xbb, -1, -1, -1, -1, -1}},
};

// 0x614ca9: Monster64 (el modelo 0x191).
constexpr MonsterModelOp Ops3f[]{
    {.kind = O::Sound, .value = 0x195, .path = "Sound\\mDAngelIdle.wav"},
    {.kind = O::Sound, .value = 0x196, .path = "Sound\\mDAngelAttack.wav"},
    {.kind = O::Sound, .value = 0x197, .path = "Sound\\mDAngelDeath.wav"},
    {.kind = O::SoundTable, .model = 0x191, .sounds = {0xc3, 0xc3, 0xc4, 0xc4, 0xc5, -1, -1, -1, -1, -1}},
};

// 0x614d13: Monster65 (el modelo 0x192).
constexpr MonsterModelOp Ops40[]{
    {.kind = O::Sound, .value = 0x1ba, .path = "Sound\\mKundunIdle.wav"},
    {.kind = O::Sound, .value = 0x1bb, .path = "Sound\\mKundunAttack1.wav"},
    {.kind = O::Sound, .value = 0x1bc, .path = "Sound\\mKundunAttack2.wav"},
    {.kind = O::SoundTable, .model = 0x192, .sounds = {0xe8, 0xe8, 0xe9, 0xea, -1, -1, -1, -1, -1, -1}},
};

// 0x614d7a: Monster66 (el modelo 0x193).
constexpr MonsterModelOp Ops41[]{
    {.kind = O::Sound, .value = 0x198, .path = "Sound\\mBSoldierIdle1.wav"},
    {.kind = O::Sound, .value = 0x199, .path = "Sound\\mBSoldierIdle2.wav"},
    {.kind = O::Sound, .value = 0x19a, .path = "Sound\\mBSoldierAttack1.wav"},
    {.kind = O::Sound, .value = 0x19b, .path = "Sound\\mBSoldierAttack2.wav"},
    {.kind = O::Sound, .value = 0x19c, .path = "Sound\\mBSoldierDeath.wav"},
    {.kind = O::SoundTable, .model = 0x193, .sounds = {0xc6, 0xc7, 0xc8, 0xc9, 0xca, -1, -1, -1, -1, -1}},
};

// 0x614e0d: Monster67 (el modelo 0x194).
constexpr MonsterModelOp Ops42[]{
    {.kind = O::Sound, .value = 0x19d, .path = "Sound\\mEsisIdle.wav"},
    {.kind = O::Sound, .value = 0x19e, .path = "Sound\\mEsisAttack1.wav"},
    {.kind = O::Sound, .value = 0x19f, .path = "Sound\\mEsisAttack2.wav"},
    {.kind = O::Sound, .value = 0x1a0, .path = "Sound\\mEsisDeath.wav"},
    {.kind = O::SoundTable, .model = 0x194, .sounds = {0xcb, 0xcb, 0xcc, 0xcd, 0xce, -1, -1, -1, -1, -1}},
};

// 0x614e8d: Monster68 (el modelo 0x195).
constexpr MonsterModelOp Ops43[]{
    {.kind = O::Sound, .value = 0x1a1, .path = "Sound\\mDsIdle1.wav"},
    {.kind = O::Sound, .value = 0x1a2, .path = "Sound\\mDsIdle2.wav"},
    {.kind = O::Sound, .value = 0x1a3, .path = "Sound\\mDsAttack1.wav"},
    {.kind = O::Sound, .value = 0x1a4, .path = "Sound\\mDsAttack2.wav"},
    {.kind = O::Sound, .value = 0x1a5, .path = "Sound\\mDsDeath.wav"},
    {.kind = O::Sound, .value = 0x1a6, .path = "Sound\\mLsIdle1.wav"},
    {.kind = O::Sound, .value = 0x1a7, .path = "Sound\\mLsIdle2.wav"},
    {.kind = O::Sound, .value = 0x1a8, .path = "Sound\\mLsAttack1.wav"},
    {.kind = O::Sound, .value = 0x1a9, .path = "Sound\\mLsAttack2.wav"},
    {.kind = O::Sound, .value = 0x1aa, .path = "Sound\\mLsDeath.wav"},
    {.kind = O::SoundTable, .model = 0x195, .sounds = {0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8}},
};

// 0x614f91: Monster69 (el modelo 0x196).
constexpr MonsterModelOp Ops44[]{
    {.kind = O::Sound, .value = 0x1ab, .path = "Sound\\mNecronIdle1.wav"},
    {.kind = O::Sound, .value = 0x1ac, .path = "Sound\\mNecronIdle2.wav"},
    {.kind = O::Sound, .value = 0x1ad, .path = "Sound\\mNecronAttack1.wav"},
    {.kind = O::Sound, .value = 0x1ae, .path = "Sound\\mNecronAttack2.wav"},
    {.kind = O::Sound, .value = 0x1af, .path = "Sound\\mNecronDeath.wav"},
    {.kind = O::SoundTable, .model = 0x196, .sounds = {0xd9, 0xda, 0xdb, 0xdc, 0xdd, -1, -1, -1, -1, -1}},
};

// 0x615024: Monster70 (el modelo 0x197).
constexpr MonsterModelOp Ops45[]{
    {.kind = O::Sound, .value = 0x1b0, .path = "Sound\\mSvIdle1.wav"},
    {.kind = O::Sound, .value = 0x1b1, .path = "Sound\\mSvIdle2.wav"},
    {.kind = O::Sound, .value = 0x1b2, .path = "Sound\\mSvAttack1.wav"},
    {.kind = O::Sound, .value = 0x1b3, .path = "Sound\\mSvAttack2.wav"},
    {.kind = O::Sound, .value = 0x1b4, .path = "Sound\\mSvDeath.wav"},
    {.kind = O::Sound, .value = 0x1b5, .path = "Sound\\mLvIdle1.wav"},
    {.kind = O::Sound, .value = 0x1b6, .path = "Sound\\mLvIdle2.wav"},
    {.kind = O::Sound, .value = 0x1b7, .path = "Sound\\mLvAttack1.wav"},
    {.kind = O::Sound, .value = 0x1b8, .path = "Sound\\mLvAttack2.wav"},
    {.kind = O::Sound, .value = 0x1b9, .path = "Sound\\mLvDeath.wav"},
    {.kind = O::SoundTable, .model = 0x197, .sounds = {0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7}},
};

// 0x613137: Monster71 (el modelo 0x198).
constexpr MonsterModelOp Ops46[]{
    {.kind = O::Sound, .value = 0x173, .path = "Sound\\mOrcCapAttack1.wav"},
    {.kind = O::SoundTable, .model = 0x198, .sounds = {-1, -1, 0xa1, 0xa1, -1, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x198, .value = 0x14},
};

// 0x613137: Monster72 (el modelo 0x199).
constexpr MonsterModelOp Ops47[]{
    {.kind = O::Sound, .value = 0x173, .path = "Sound\\mOrcCapAttack1.wav"},
    {.kind = O::SoundTable, .model = 0x199, .sounds = {-1, -1, 0xa1, 0xa1, -1, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x199, .value = 0x14},
};

// 0x613169: Monster73 (el modelo 0x19a).
constexpr MonsterModelOp Ops48[]{
    {.kind = O::Sound, .value = 0x174, .path = "Sound\\mOrcArcherAttack1.wav"},
    {.kind = O::SoundTable, .model = 0x19a, .sounds = {-1, -1, 0xa2, 0xa2, -1, -1, -1, -1, -1, -1}},
    {.kind = O::HeadBone, .model = 0x19a, .value = 0x14},
};

// 0x615128: Monster74 (el modelo 0x19b).
constexpr MonsterModelOp Ops49[]{
    {.kind = O::Sound, .value = 0x1ba, .path = "Sound\\BattleCastle\\oCDoorDis.wav"},
    {.kind = O::SoundTable, .model = 0x19b, .sounds = {-1, -1, -1, -1, 0xe8, -1, -1, -1, -1, -1}},
};

// 0x61519d: Monster77 (el modelo 0x19e).
constexpr MonsterModelOp Ops4c[]{
    {.kind = O::Sound, .value = 0x1bb, .path = "Sound\\BattleCastle\\m_BowMercAttack.wav"},
    {.kind = O::Sound, .value = 0x1bc, .path = "Sound\\BattleCastle\\m_BowMercDeath.wav"},
    {.kind = O::SoundTable, .model = 0x19e, .sounds = {-1, -1, 0xe9, 0xe9, 0xea, -1, -1, -1, -1, -1}},
};

// 0x6151ee: Monster78 (el modelo 0x19f).
constexpr MonsterModelOp Ops4d[]{
    {.kind = O::Sound, .value = 0x1bd, .path = "Sound\\BattleCastle\\m_SpearMercAttack.wav"},
    {.kind = O::Sound, .value = 0x1be, .path = "Sound\\BattleCastle\\m_SpearMercDeath.wav"},
    {.kind = O::SoundTable, .model = 0x19f, .sounds = {-1, -1, 0xeb, 0xeb, 0xec, -1, -1, -1, -1, -1}},
};

// 0x615160: Monster87 (el modelo 0x1a8).
constexpr MonsterModelOp Ops56[]{
    {.kind = O::SoundTable, .model = 0x1a8, .sounds = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}},
    {.kind = O::Speed, .index = 0, .value = 0x3d4ccccd},
    {.kind = O::Speed, .index = 1, .value = 0x3d4ccccd},
    {.kind = O::Speed, .index = 2, .value = 0x3dcccccd},
};

constexpr std::array<MonsterModelCase, 74> Cases{{
    {0x0, 0x6131b3, Ops00},  {0x1, 0x613254, Ops01},  {0x2, 0x6132e7, Ops02},  {0x3, 0x613387, Ops03},
    {0x4, 0x61345b, Ops04},  {0x5, 0x6134ee, Ops05},  {0x6, 0x613582, Ops06},  {0x7, 0x613635, Ops07},
    {0x8, 0x6135ca, Ops08},  {0x9, 0x613352, Ops09},  {0xa, 0x6136c8, Ops0a},  {0xb, 0x61375c, Ops0b},
    {0xc, 0x613a2f, Ops0c},  {0xd, 0x613a2f, Ops0d},  {0xe, 0x6139ba, Ops0e},  {0xf, 0x6137f0, Ops0f},
    {0x10, 0x6138b5, Ops10}, {0x11, 0x61385a, Ops11}, {0x12, 0x613926, Ops12}, {0x13, 0x613adf, Ops13},
    {0x14, 0x613b72, Ops14}, {0x15, 0x613bf6, Ops15}, {0x16, 0x613c61, Ops16}, {0x17, 0x613cf5, Ops17},
    {0x18, 0x613d88, Ops18}, {0x19, 0x613e1c, Ops19}, {0x1a, 0x613eb0, Ops1a}, {0x1b, 0x613f1a, Ops1b},
    {0x1c, 0x613fbb, Ops1c}, {0x1d, 0x613387, Ops1d}, {0x1e, 0x6131b3, Ops1e}, {0x1f, 0x61404e, Ops1f},
    {0x20, 0x6140ce, Ops20}, {0x21, 0x614172, Ops21}, {0x22, 0x6141c9, Ops22}, {0x23, 0x614245, Ops23},
    {0x24, 0x6142be, Ops24}, {0x25, 0x61435e, Ops25}, {0x27, 0x613387, Ops27}, {0x28, 0x6142be, Ops28},
    {0x29, 0x6143d0, Ops29}, {0x2a, 0x614437, Ops2a}, {0x2b, 0x614517, Ops2b}, {0x2c, 0x6145a6, Ops2c},
    {0x2d, 0x614620, Ops2d}, {0x2e, 0x61469a, Ops2e}, {0x2f, 0x6146f7, Ops2f}, {0x30, 0x614768, Ops30},
    {0x31, 0x61488d, Ops31}, {0x32, 0x614901, Ops32}, {0x33, 0x614962, Ops33}, {0x34, 0x6147dc, Ops34},
    {0x35, 0x6149d3, Ops35}, {0x36, 0x614a47, Ops36}, {0x37, 0x614abb, Ops37}, {0x38, 0x614b1d, Ops38},
    {0x39, 0x614c45, Ops39}, {0x3a, 0x614bf4, Ops3a}, {0x3b, 0x614b7a, Ops3b}, {0x3e, 0x614b2c, Ops3e},
    {0x3f, 0x614ca9, Ops3f}, {0x40, 0x614d13, Ops40}, {0x41, 0x614d7a, Ops41}, {0x42, 0x614e0d, Ops42},
    {0x43, 0x614e8d, Ops43}, {0x44, 0x614f91, Ops44}, {0x45, 0x615024, Ops45}, {0x46, 0x613137, Ops46},
    {0x47, 0x613137, Ops47}, {0x48, 0x613169, Ops48}, {0x49, 0x615128, Ops49}, {0x4c, 0x61519d, Ops4c},
    {0x4d, 0x6151ee, Ops4d}, {0x56, 0x615160, Ops56},
}};

struct NpcModelEntry
{
    std::uint16_t model = 0;
    std::uint32_t address = 0;
    std::span<const NpcModelOp> ops{};
};

// 0x6122c4: el modelo 0x1cb.
constexpr NpcModelOp Npc1cb[]{
    {.kind = N::Load, .model = 0x1cb, .folder = "Npc\\", .name = "Female", .number = 1},
    {.kind = N::Load, .model = 0x1da, .folder = "Npc\\", .name = "FemaleHead", .number = 1},
    {.kind = N::Load, .model = 0x1dc, .folder = "Npc\\", .name = "FemaleUpper", .number = 1},
    {.kind = N::Load, .model = 0x1de, .folder = "Npc\\", .name = "FemaleLower", .number = 1},
    {.kind = N::Load, .model = 0x1e2, .folder = "Npc\\", .name = "FemaleBoots", .number = 1},
    {.kind = N::Textures, .model = 0x1da, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1dc, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1de, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1e2, .folder = "Npc\\"},
    {.kind = N::Load, .model = 0x1db, .folder = "Npc\\", .name = "FemaleHead", .number = 2},
    {.kind = N::Load, .model = 0x1dd, .folder = "Npc\\", .name = "FemaleUpper", .number = 2},
    {.kind = N::Load, .model = 0x1df, .folder = "Npc\\", .name = "FemaleLower", .number = 2},
    {.kind = N::Load, .model = 0x1e3, .folder = "Npc\\", .name = "FemaleBoots", .number = 2},
    {.kind = N::Textures, .model = 0x1db, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1dd, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1df, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1e3, .folder = "Npc\\"},
};

// 0x6123c4: el modelo 0x1cc.
constexpr NpcModelOp Npc1cc[]{
    {.kind = N::Load, .model = 0x1cc, .folder = "Npc\\", .name = "Man", .number = 1},
    {.kind = N::Load, .model = 0x1e4, .folder = "Npc\\", .name = "ManHead", .number = 1},
    {.kind = N::Load, .model = 0x1e6, .folder = "Npc\\", .name = "ManUpper", .number = 1},
    {.kind = N::Load, .model = 0x1e8, .folder = "Npc\\", .name = "ManGloves", .number = 1},
    {.kind = N::Load, .model = 0x1ea, .folder = "Npc\\", .name = "ManBoots", .number = 1},
    {.kind = N::Textures, .model = 0x1e4, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1e6, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1e8, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1ea, .folder = "Npc\\"},
    {.kind = N::Load, .model = 0x1e5, .folder = "Npc\\", .name = "ManHead", .number = 2},
    {.kind = N::Load, .model = 0x1e7, .folder = "Npc\\", .name = "ManUpper", .number = 2},
    {.kind = N::Load, .model = 0x1e9, .folder = "Npc\\", .name = "ManGloves", .number = 2},
    {.kind = N::Load, .model = 0x1eb, .folder = "Npc\\", .name = "ManBoots", .number = 2},
    {.kind = N::Textures, .model = 0x1e5, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1e7, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1e9, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1eb, .folder = "Npc\\"},
};

// 0x6124c4: el modelo 0x1cd.
constexpr NpcModelOp Npc1cd[]{
    {.kind = N::Load, .model = 0x1cd, .folder = "Npc\\", .name = "Girl", .number = 1},
    {.kind = N::Load, .model = 0x1ec, .folder = "Npc\\", .name = "GirlHead", .number = 1},
    {.kind = N::Load, .model = 0x1ee, .folder = "Npc\\", .name = "GirlUpper", .number = 1},
    {.kind = N::Load, .model = 0x1f0, .folder = "Npc\\", .name = "GirlLower", .number = 1},
    {.kind = N::Textures, .model = 0x1ec, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1ee, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1f0, .folder = "Npc\\"},
    {.kind = N::Load, .model = 0x1ed, .folder = "Npc\\", .name = "GirlHead", .number = 2},
    {.kind = N::Load, .model = 0x1ef, .folder = "Npc\\", .name = "GirlUpper", .number = 2},
    {.kind = N::Load, .model = 0x1f1, .folder = "Npc\\", .name = "GirlLower", .number = 2},
    {.kind = N::Textures, .model = 0x1ed, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1ef, .folder = "Npc\\"},
    {.kind = N::Textures, .model = 0x1f1, .folder = "Npc\\"},
};

// 0x612590: el modelo 0x1ce.
constexpr NpcModelOp Npc1ce[]{
    {.kind = N::Load, .model = 0x1ce, .folder = "Npc\\", .name = "Smith", .number = 1},
    {.kind = N::Sound, .sound = {.id = 0xa0, .path = "Sound\\nBlackSmith.wav", .buffers = 1, .flags = 0}},
};

// 0x6125c1: el modelo 0x1cf.
constexpr NpcModelOp Npc1cf[]{
    {.kind = N::Load, .model = 0x1cf, .folder = "Npc\\", .name = "Wizard", .number = 1},
};

// 0x6125df: el modelo 0x1d0.
constexpr NpcModelOp Npc1d0[]{
    {.kind = N::Load, .model = 0x1d0, .folder = "Npc\\", .name = "SnowMerchant", .number = 1},
};

// 0x6125fd: el modelo 0x1d1.
constexpr NpcModelOp Npc1d1[]{
    {.kind = N::Load, .model = 0x1d1, .folder = "Npc\\", .name = "SnowSmith", .number = 1},
};

// 0x61261b: el modelo 0x1d2.
constexpr NpcModelOp Npc1d2[]{
    {.kind = N::Load, .model = 0x1d2, .folder = "Npc\\", .name = "SnowWizard", .number = 1},
};

// 0x612639: el modelo 0x1d3.
constexpr NpcModelOp Npc1d3[]{
    {.kind = N::Load, .model = 0x1d3, .folder = "Npc\\", .name = "ElfWizard", .number = 1},
    {.kind = N::Sound, .sound = {.id = 0xa0, .path = "Sound\\nHarp.wav", .buffers = 1, .flags = 0}},
};

// 0x61266a: el modelo 0x1d4.
constexpr NpcModelOp Npc1d4[]{
    {.kind = N::Load, .model = 0x1d4, .folder = "Npc\\", .name = "ElfMerchant", .number = 1},
};

// 0x612688: el modelo 0x1d5.
constexpr NpcModelOp Npc1d5[]{
    {.kind = N::Load, .model = 0x1d5, .folder = "Npc\\", .name = "Master", .number = 1},
};

// 0x6126a6: el modelo 0x1d6.
constexpr NpcModelOp Npc1d6[]{
    {.kind = N::Load, .model = 0x1d6, .folder = "Npc\\", .name = "Storage", .number = 1},
};

// 0x6126c4: el modelo 0x1d7.
constexpr NpcModelOp Npc1d7[]{
    {.kind = N::Load, .model = 0x1d7, .folder = "Npc\\", .name = "Tournament", .number = 1},
};

// 0x6126e2: el modelo 0x1d8.
constexpr NpcModelOp Npc1d8[]{
    {.kind = N::Load, .model = 0x1d8, .folder = "Npc\\", .name = "MixNpc", .number = 1},
    {.kind = N::Sound, .sound = {.id = 0xa1, .path = "Sound\\nMix.wav", .buffers = 1, .flags = 0}},
};

// 0x612713: el modelo 0x1d9.
constexpr NpcModelOp Npc1d9[]{
    {.kind = N::Load, .model = 0x1d9, .folder = "Npc\\", .name = "DevilNpc", .number = 1},
};

// 0x612731: el modelo 0x1f2.
constexpr NpcModelOp Npc1f2[]{
    {.kind = N::Load, .model = 0x1f2, .folder = "Npc\\", .name = "Sevina", .number = 1},
    {.kind = N::Textures, .model = 0x1f2, .folder = "Npc\\"},
};

// 0x61276c: el modelo 0x1f3.
constexpr NpcModelOp Npc1f3[]{
    {.kind = N::Load, .model = 0x1f3, .folder = "Npc\\", .name = "BloodCastle", .number = 1},
    {.kind = N::Textures, .model = 0x1f3, .folder = "Npc\\"},
};

// 0x6127a4: el modelo 0x1f4.
constexpr NpcModelOp Npc1f4[]{
    {.kind = N::Load, .model = 0x1f4, .folder = "Npc\\", .name = "BloodCastle", .number = 2},
    {.kind = N::Textures, .model = 0x1f4, .folder = "Npc\\"},
};

// 0x6127dc: el modelo 0x1f5.
constexpr NpcModelOp Npc1f5[]{
    {.kind = N::Load, .model = 0x1f5, .folder = "Npc\\", .name = "DeviasTrader", .number = 1},
    {.kind = N::Textures, .model = 0x1f5, .folder = "Npc\\"},
};

// 0x612842: el modelo 0x1f6.
constexpr NpcModelOp Npc1f6[]{
    {.kind = N::Load, .model = 0x1f6, .folder = "Npc\\", .name = "Breeder", .number = -1},
    {.kind = N::Textures, .model = 0x1f6, .folder = "Npc\\"},
};

// 0x612873: el modelo 0x1f8.
constexpr NpcModelOp Npc1f8[]{
    {.kind = N::Load, .model = 0x1f8, .folder = "Npc\\", .name = "Model_Npc_Catapult_Att", .number = -1},
    {.kind = N::Textures, .model = 0x1f8, .folder = "Npc\\"},
};

// 0x6128a4: el modelo 0x1f9.
constexpr NpcModelOp Npc1f9[]{
    {.kind = N::Load, .model = 0x1f9, .folder = "Npc\\", .name = "Model_Npc_Catapult_Def", .number = -1},
    {.kind = N::Textures, .model = 0x1f9, .folder = "Npc\\"},
};

// 0x6128d5: el modelo 0x1fa.
constexpr NpcModelOp Npc1fa[]{
    {.kind = N::Load, .model = 0x1fa, .folder = "Npc\\", .name = "NpcSenatus", .number = -1},
    {.kind = N::Textures, .model = 0x1fa, .folder = "Npc\\"},
};

// 0x612906: el modelo 0x1fb.
constexpr NpcModelOp Npc1fb[]{
    {.kind = N::Load, .model = 0x1fb, .folder = "Npc\\", .name = "NpcGateSwitch", .number = -1},
    {.kind = N::Textures, .model = 0x1fb, .folder = "Npc\\"},
};

// 0x612937: el modelo 0x1fc.
constexpr NpcModelOp Npc1fc[]{
    {.kind = N::Load, .model = 0x1fc, .folder = "Npc\\", .name = "NpcCrown", .number = -1},
    {.kind = N::Textures, .model = 0x1fc, .folder = "Npc\\"},
};

// 0x612968: el modelo 0x1fd.
constexpr NpcModelOp Npc1fd[]{
    {.kind = N::Load, .model = 0x1fd, .folder = "Npc\\", .name = "NpcCheckFloor", .number = -1},
    {.kind = N::Textures, .model = 0x1fd, .folder = "Npc\\"},
};

// 0x612999: el modelo 0x1fe.
constexpr NpcModelOp Npc1fe[]{
    {.kind = N::Load, .model = 0x1fe, .folder = "Npc\\", .name = "NpcClerk", .number = -1},
    {.kind = N::Textures, .model = 0x1fe, .folder = "Npc\\"},
};

// 0x6129ca: el modelo 0x1ff.
constexpr NpcModelOp Npc1ff[]{
    {.kind = N::Load, .model = 0x1ff, .folder = "Npc\\", .name = "NpcBarrier", .number = -1},
    {.kind = N::Textures, .model = 0x1ff, .folder = "Npc\\"},
};

// 0x6129fb: el modelo 0x200.
constexpr NpcModelOp Npc200[]{
    {.kind = N::Load, .model = 0x200, .folder = "Npc\\", .name = "npc_mulyak", .number = -1},
    {.kind = N::Textures, .model = 0x200, .folder = "Npc\\"},
};

// 0x612a2c: el modelo 0x203.
constexpr NpcModelOp Npc203[]{
    {.kind = N::Load, .model = 0x203, .folder = "Object35\\", .name = "Object82", .number = -1},
    {.kind = N::Textures, .model = 0x203, .folder = "Object35\\"},
};

// 0x612a5d: el modelo 0x204.
constexpr NpcModelOp Npc204[]{
    {.kind = N::Load, .model = 0x204, .folder = "Object35\\", .name = "Object57", .number = -1},
    {.kind = N::Textures, .model = 0x204, .folder = "Object35\\"},
};

// 0x612a8e: el modelo 0x205.
constexpr NpcModelOp Npc205[]{
    {.kind = N::Load, .model = 0x205, .folder = "Object35\\", .name = "Object57", .number = -1},
    {.kind = N::Textures, .model = 0x205, .folder = "Object35\\"},
};

// 0x612abf: el modelo 0x206.
constexpr NpcModelOp Npc206[]{
    {.kind = N::Load, .model = 0x206, .folder = "Object35\\", .name = "Object57", .number = -1},
    {.kind = N::Textures, .model = 0x206, .folder = "Object35\\"},
};

// 0x612aed: el modelo 0x207.
constexpr NpcModelOp Npc207[]{
    {.kind = N::Load, .model = 0x207, .folder = "Object35\\", .name = "Object57", .number = -1},
    {.kind = N::Textures, .model = 0x207, .folder = "Object35\\"},
};

// 0x612b1b: el modelo 0x208.
constexpr NpcModelOp Npc208[]{
    {.kind = N::Load, .model = 0x208, .folder = "Object35\\", .name = "Object57", .number = -1},
    {.kind = N::Textures, .model = 0x208, .folder = "Object35\\"},
};

constexpr std::array<NpcModelEntry, 35> NpcCases{{
    {0x1cb, 0x6122c4, Npc1cb}, {0x1cc, 0x6123c4, Npc1cc}, {0x1cd, 0x6124c4, Npc1cd}, {0x1ce, 0x612590, Npc1ce},
    {0x1cf, 0x6125c1, Npc1cf}, {0x1d0, 0x6125df, Npc1d0}, {0x1d1, 0x6125fd, Npc1d1}, {0x1d2, 0x61261b, Npc1d2},
    {0x1d3, 0x612639, Npc1d3}, {0x1d4, 0x61266a, Npc1d4}, {0x1d5, 0x612688, Npc1d5}, {0x1d6, 0x6126a6, Npc1d6},
    {0x1d7, 0x6126c4, Npc1d7}, {0x1d8, 0x6126e2, Npc1d8}, {0x1d9, 0x612713, Npc1d9}, {0x1f2, 0x612731, Npc1f2},
    {0x1f3, 0x61276c, Npc1f3}, {0x1f4, 0x6127a4, Npc1f4}, {0x1f5, 0x6127dc, Npc1f5}, {0x1f6, 0x612842, Npc1f6},
    {0x1f8, 0x612873, Npc1f8}, {0x1f9, 0x6128a4, Npc1f9}, {0x1fa, 0x6128d5, Npc1fa}, {0x1fb, 0x612906, Npc1fb},
    {0x1fc, 0x612937, Npc1fc}, {0x1fd, 0x612968, Npc1fd}, {0x1fe, 0x612999, Npc1fe}, {0x1ff, 0x6129ca, Npc1ff},
    {0x200, 0x6129fb, Npc200}, {0x203, 0x612a2c, Npc203}, {0x204, 0x612a5d, Npc204}, {0x205, 0x612a8e, Npc205},
    {0x206, 0x612abf, Npc206}, {0x207, 0x612aed, Npc207}, {0x208, 0x612b1b, Npc208},
}};
} // namespace

std::span<const ActionSpeedStore> CommonMonsterSpeeds() noexcept
{
    return CommonSpeeds;
}

std::optional<float> MonsterSpeedFactorOf(std::int32_t number) noexcept
{
    switch (number)
    {
    case 0x3:
        return 1.2f; // 0x6b8bb0
    case 0x5:
    case 0x19:
        return 0.7f; // 0x6b8a5c
    case 0x25:
    case 0x2a:
        return 0.4f; // 0x6b87b4
    default:
        return std::nullopt;
    }
}

std::span<const ActionSpeedStore> EarlyMonsterSpeedsOf(std::int32_t number) noexcept
{
    const auto* found = std::find_if(EarlyCases.begin(), EarlyCases.end(),
                                     [number](const EarlySpeeds& early) { return early.number == number; });
    return found != EarlyCases.end() ? found->stores : std::span<const ActionSpeedStore>{};
}

bool MonsterFirstActionStops(std::int32_t number) noexcept
{
    return number == 0xe || number == 0x1f || number == 0x27 || number == 0x28;
}

MonsterModelCase MonsterModelCaseOf(std::int32_t number) noexcept
{
    const auto* found = std::find_if(Cases.begin(), Cases.end(),
                                     [number](const MonsterModelCase& entry) { return entry.number == number; });
    return found != Cases.end() ? *found : MonsterModelCase{.number = number};
}

std::string_view MonsterTextureFolderOf(std::int32_t number, std::int32_t map) noexcept
{
    constexpr std::int32_t FirstChaosCastle = 18;
    constexpr std::int32_t LastChaosCastle = 23;
    constexpr std::int32_t CastleSiege = 30;
    if (map >= FirstChaosCastle && map <= LastChaosCastle && number >= 0x46 && number <= 0x48)
        return "Npc\\";
    if (map == CastleSiege && number == 0x4a)
        return "Object31\\";
    return "Monster\\";
}

NpcModelCase NpcModelCaseOf(std::uint16_t model) noexcept
{
    const auto* found = std::find_if(NpcCases.begin(), NpcCases.end(),
                                     [model](const NpcModelEntry& entry) { return entry.model == model; });
    return found != NpcCases.end() ? NpcModelCase{found->address, found->ops} : NpcModelCase{};
}
} // namespace Client::Scenes
