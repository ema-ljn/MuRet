#include "Scenes/MonsterKinds.h"

#include <array>
#include <span>

// Los casos de 0x5141e0 y de sus seis creadores de evento, leídos del listado caso por caso (*estático*): cada
// arreglo es un caso con sus pasos en el orden del ASM, y `address` dice dónde está cada uno. Un caso que cambia con
// el mapa o con el rand() lleva la condición en el paso (MonsterStepWhen). Los nombres son los bytes CP949 de la
// .data del original (la dirección va en `extra`), y los de los huesos, los del std::string que arma el creador.
namespace Client::Scenes
{
namespace
{
using K = MonsterStepKind;

// 0x516e2a: el tipo 0.
constexpr MonsterStep Case516e2a[]{
    {.kind = K::LoadMonster, .value = 0x0, .address = 0x516e2c},
    {.kind = K::Create, .value = 0x152, .extra = 0x0, .address = 0x516e3b},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0x0, .address = 0x516e56},
    {.kind = K::Name, .extra = 0x6c88e4, .address = 0x516e72, .text = "\xbc\xd2\xbb\xd4\xc0\xfc\xbb\xe7"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x516e7b},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x418, .address = 0x516e82},
};

// 0x515be6: el tipo 1.
constexpr MonsterStep Case515be6[]{
    {.kind = K::LoadMonster, .value = 0x1, .address = 0x515be8},
    {.kind = K::Create, .value = 0x153, .extra = 0x0, .address = 0x515bf7},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0x0, .address = 0x515c13},
    {.kind = K::Name, .extra = 0x6c8a70, .address = 0x515c2f, .text = "\xc7\xcf\xbf\xee\xb5\xe5"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f59999a, .address = 0x515c38},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x216, .address = 0x515c3f},
};

// 0x515cb0: el tipo 2.
constexpr MonsterStep Case515cb0[]{
    {.kind = K::LoadMonster, .value = 0x2, .address = 0x515cb2},
    {.kind = K::Create, .value = 0x154, .extra = 0x0, .address = 0x515cc7},
    {.kind = K::Name, .extra = 0x6c8a58, .address = 0x515cf0, .text = "\xb9\xf6\xc1\xf6\xb5\xe5\xb7\xa1\xb0\xef"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f000000, .address = 0x515cf9},
};

// 0x515a1f: el tipo 3.
constexpr MonsterStep Case515a1f[]{
    {.kind = K::LoadMonster, .value = 0x9, .address = 0x515a21},
    {.kind = K::Create, .value = 0x15b, .extra = 0x0, .address = 0x515a36},
    {.kind = K::Name, .extra = 0x6c8aa0, .address = 0x515a5f, .text = "\xb0\xc5\xb9\xcc"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3ecccccd, .address = 0x515a68},
};

// 0x516e2a: el tipo 4.
constexpr MonsterStep Case516e2a_2[]{
    {.kind = K::LoadMonster, .value = 0x0, .address = 0x516e2c},
    {.kind = K::Create, .value = 0x152, .extra = 0x0, .address = 0x516e3b},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x819, .address = 0x516ea2},
    {.kind = K::Name,
     .extra = 0x6c88d4,
     .address = 0x516ec0,
     .text = "\xbc\xd2\xbb\xd4\xc0\xfc\xbb\xe7 \xb4\xeb\xc0\xe5"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f933333, .address = 0x516ec9},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x516ed0},
};

// 0x515be6: el tipo 5.
constexpr MonsterStep Case515be6_2[]{
    {.kind = K::LoadMonster, .value = 0x1, .address = 0x515be8},
    {.kind = K::Create, .value = 0x153, .extra = 0x0, .address = 0x515bf7},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0x1, .address = 0x515c66},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x219, .address = 0x515c69},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe1b, .address = 0x515c72},
    {.kind = K::Name, .extra = 0x6c8a64, .address = 0x515c94, .text = "\xc7\xef\xc7\xcf\xbf\xee\xb5\xe5"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x515c9d},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x515ca4},
};

// 0x515d6c: el tipo 6.
constexpr MonsterStep Case515d6c[]{
    {.kind = K::LoadMonster, .value = 0x4, .address = 0x515d6e},
    {.kind = K::Create, .value = 0x156, .extra = 0x0, .address = 0x515d7d},
    {.kind = K::Name, .extra = 0x6c8a44, .address = 0x515dae, .text = "\xb8\xae\xc4\xa1"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc14, .address = 0x515db7},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f59999a, .address = 0x515dc0},
};

// 0x515e0c: el tipo 7.
constexpr MonsterStep Case515e0c[]{
    {.kind = K::LoadMonster, .value = 0x5, .address = 0x515e0e},
    {.kind = K::Create, .value = 0x157, .extra = 0x0, .address = 0x515e23},
    {.kind = K::Name, .extra = 0x6c8a2c, .address = 0x515e4c, .text = "\xc0\xda\xc0\xcc\xbe\xf0\xc6\xae"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x414, .address = 0x515e5a},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x414, .address = 0x515e61},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fcccccd, .address = 0x515e68},
};

// 0x516e2a: el tipo 8.
constexpr MonsterStep Case516e2a_3[]{
    {.kind = K::LoadMonster, .value = 0x0, .address = 0x516e2c},
    {.kind = K::Create, .value = 0x152, .extra = 0x0, .address = 0x516e3b},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x81a, .address = 0x516eea},
    {.kind = K::Name,
     .extra = 0x6c88c4,
     .address = 0x516f08,
     .text = "\xc6\xf7\xc0\xcc\xc1\xf0 \xbc\xd2\xbb\xd4\xc0\xfc\xbb\xe7"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x516f11},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x2, .address = 0x516f18},
    {.kind = K::Write, .size = 4, .offset = 0x78, .value = 0x1, .address = 0x516f21},
};

// 0x515d6c: el tipo 9.
constexpr MonsterStep Case515d6c_2[]{
    {.kind = K::LoadMonster, .value = 0x4, .address = 0x515d6e},
    {.kind = K::Create, .value = 0x156, .extra = 0x0, .address = 0x515d7d},
    {.kind = K::Name, .extra = 0x6c8a38, .address = 0x515de5, .text = "\xbd\xe3\xb4\xf5 \xb8\xae\xc4\xa1"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc15, .address = 0x515dee},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x515df7},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x515e00},
};

// 0x515d05: el tipo 10.
constexpr MonsterStep Case515d05[]{
    {.kind = K::LoadMonster, .value = 0x3, .address = 0x515d07},
    {.kind = K::Create, .value = 0x155, .extra = 0x0, .address = 0x515d1c},
    {.kind = K::Name, .extra = 0x6c8a4c, .address = 0x515d45, .text = "\xb4\xd9\xc5\xa9\xb3\xaa\xc0\xcc\xc6\xae"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x515d4e},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x515d55},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x21f, .address = 0x515d5e},
};

// 0x515acb: el tipo 11.
constexpr MonsterStep Case515acb[]{
    {.kind = K::LoadMonster, .value = 0x7, .address = 0x515acd},
    {.kind = K::Create, .value = 0x159, .extra = 0x0, .address = 0x515ae2},
    {.kind = K::Name, .extra = 0x6c8a8c, .address = 0x515b0b, .text = "\xb0\xed\xbd\xba\xc6\xae"},
    {.kind = K::Write, .size = 4, .offset = 0x170, .value = 0x3ecccccd, .address = 0x515b14},
    {.kind = K::Write, .size = 2, .offset = 0x386, .value = 0xf, .address = 0x515b1e},
    {.kind = K::Write, .size = 1, .offset = 0x38a, .value = 0x1, .address = 0x515b27},
};

// 0x515b33: el tipo 12.
constexpr MonsterStep Case515b33[]{
    {.kind = K::LoadMonster, .value = 0x6, .address = 0x515b35},
    {.kind = K::Create, .value = 0x158, .extra = 0x0, .address = 0x515b4a},
    {.kind = K::Name, .extra = 0x6c8a84, .address = 0x515b73, .text = "\xc0\xaf\xc3\xe6"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f19999a, .address = 0x515b7c},
};

// 0x515b88: el tipo 13.
constexpr MonsterStep Case515b88[]{
    {.kind = K::LoadMonster, .value = 0x8, .address = 0x515b8a},
    {.kind = K::Create, .value = 0x15a, .extra = 0x0, .address = 0x515b9f},
    {.kind = K::Name, .extra = 0x6c8a78, .address = 0x515bc8, .text = "\xc7\xef\xbd\xba\xc6\xc4\xc0\xcc\xb4\xf5"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc14, .address = 0x515bd1},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x515bda},
};

// 0x515e74: el tipo 14.
constexpr MonsterStep Case515e74[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x515e84},
    {.kind = K::Name, .extra = 0x6c8a20, .address = 0x515ead, .text = "\xc7\xd8\xb0\xf1\xc0\xfc\xbb\xe7"},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0xd1, .address = 0x515ebc},
    {.kind = K::Write, .size = 1, .offset = 0x38a, .value = 0x1, .address = 0x515ec3},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f733333, .address = 0x515ecc},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x218, .address = 0x515ed3},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe16, .address = 0x515edc},
};

// 0x516033: el tipo 15.
constexpr MonsterStep Case516033[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x516043},
    {.kind = K::Name, .extra = 0x6c8a04, .address = 0x51606c, .text = "\xc7\xd8\xb0\xf1\xb1\xc3\xbc\xf6"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x516075},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa14, .address = 0x51607c},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0xd2, .address = 0x516085},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x5160f0},
    {.kind = K::Write, .size = 1, .offset = 0x38a, .value = 0x1, .address = 0x5160f9},
};

// 0x51608e: el tipo 16.
constexpr MonsterStep Case51608e[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x51609e},
    {.kind = K::Name,
     .extra = 0x6c89f4,
     .address = 0x5160c7,
     .text = "\xc7\xd8\xb0\xf1\xc0\xfc\xbb\xe7 \xb4\xeb\xc0\xe5"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x5160d0},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x415, .address = 0x5160d7},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe18, .address = 0x5160e0},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0xd3, .address = 0x5160e9},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x5160f0},
    {.kind = K::Write, .size = 1, .offset = 0x38a, .value = 0x1, .address = 0x5160f9},
};

// 0x515a74: el tipo 17.
constexpr MonsterStep Case515a74[]{
    {.kind = K::LoadMonster, .value = 0xa, .address = 0x515a76},
    {.kind = K::Create, .value = 0x15c, .extra = 0x0, .address = 0x515a8b},
    {.kind = K::Name, .extra = 0x6c8a94, .address = 0x515ab4, .text = "\xbd\xce\xc0\xcc\xc5\xa9\xb7\xd3\xbd\xba"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x41a, .address = 0x515abd},
};

// 0x5159b3: el tipo 18.
constexpr MonsterStep Case5159b3[]{
    {.kind = K::LoadMonster, .value = 0xb, .address = 0x5159b5},
    {.kind = K::Create, .value = 0x15d, .extra = 0x0, .address = 0x5159ca},
    {.kind = K::Name, .extra = 0x6c8aa8, .address = 0x5159f3, .text = "\xb0\xed\xb8\xa3\xb0\xef"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x5159fc},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc16, .address = 0x515a03},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x515a0c},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x515a13},
};

// 0x51595e: el tipo 19.
constexpr MonsterStep Case51595e[]{
    {.kind = K::LoadMonster, .value = 0xc, .address = 0x515960},
    {.kind = K::Create, .value = 0x15e, .extra = 0x0, .address = 0x515975},
    {.kind = K::Name, .extra = 0x6c8ab0, .address = 0x51599e, .text = "\xbc\xb3\xc0\xce"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x5159a7},
};

// 0x515909: el tipo 20.
constexpr MonsterStep Case515909[]{
    {.kind = K::LoadMonster, .value = 0xd, .address = 0x51590b},
    {.kind = K::Create, .value = 0x15f, .extra = 0x0, .address = 0x515920},
    {.kind = K::Name, .extra = 0x6c8ab8, .address = 0x515949, .text = "\xbc\xb3\xc0\xce \xb4\xeb\xc0\xe5"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x515952},
};

// 0x5158b4: el tipo 21.
constexpr MonsterStep Case5158b4[]{
    {.kind = K::LoadMonster, .value = 0xe, .address = 0x5158b6},
    {.kind = K::Create, .value = 0x160, .extra = 0x0, .address = 0x5158cb},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f733333, .address = 0x5158df},
    {.kind = K::Name, .extra = 0x6c8ac4, .address = 0x5158fb, .text = "\xbe\xcf\xbb\xec\xc0\xda"},
};

// 0x515858: el tipo 22.
constexpr MonsterStep Case515858[]{
    {.kind = K::LoadMonster, .value = 0xf, .address = 0x51585a},
    {.kind = K::Create, .value = 0x161, .extra = 0x0, .address = 0x51586f},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x0, .address = 0x515883},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x51588a},
    {.kind = K::Name, .extra = 0x6c8acc, .address = 0x5158a6, .text = "\xbe\xf3\xc0\xbd\xb1\xab\xb9\xb0"},
};

// 0x5157f1: el tipo 23.
constexpr MonsterStep Case5157f1[]{
    {.kind = K::LoadMonster, .value = 0x10, .address = 0x5157f3},
    {.kind = K::Create, .value = 0x162, .extra = 0x0, .address = 0x515808},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x419, .address = 0x51581c},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe1c, .address = 0x515825},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f933333, .address = 0x51582e},
    {.kind = K::Name, .extra = 0x6c8ad8, .address = 0x51584a, .text = "\xc8\xa3\xb8\xd3\xb5\xe5"},
};

// 0x5157a3: el tipo 24.
constexpr MonsterStep Case5157a3[]{
    {.kind = K::LoadMonster, .value = 0x11, .address = 0x5157a5},
    {.kind = K::Create, .value = 0x163, .extra = 0x0, .address = 0x5157ba},
    {.kind = K::Name, .extra = 0x6c8ae0, .address = 0x5157ea, .text = "\xbf\xfa"},
};

// 0x515727: el tipo 25.
constexpr MonsterStep Case515727[]{
    {.kind = K::LoadMonster, .value = 0x12, .address = 0x515729},
    {.kind = K::Create, .value = 0x164, .extra = 0x0, .address = 0x51573e},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc13, .address = 0x515752},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x2, .address = 0x51575b},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x515762},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x515769},
    {.kind = K::Write, .size = 1, .offset = 0xe8, .value = 0x0, .address = 0x515770},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x3, .address = 0x515777},
    {.kind = K::Name, .extra = 0x6c8ae4, .address = 0x515795, .text = "\xbe\xc6\xc0\xcc\xbd\xba\xc4\xfd"},
};

// 0x5156c9: el tipo 26.
constexpr MonsterStep Case5156c9[]{
    {.kind = K::LoadMonster, .value = 0x13, .address = 0x5156cb},
    {.kind = K::Create, .value = 0x165, .extra = 0x0, .address = 0x5156e0},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x412, .address = 0x5156ea},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x5156f3},
    {.kind = K::Name, .extra = 0x6c8af0, .address = 0x515719, .text = "\xb0\xed\xba\xed\xb8\xb0"},
};

// 0x515674: el tipo 27.
constexpr MonsterStep Case515674[]{
    {.kind = K::LoadMonster, .value = 0x14, .address = 0x515676},
    {.kind = K::Create, .value = 0x166, .extra = 0x0, .address = 0x51568b},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x51569f},
    {.kind = K::Name, .extra = 0x6c8af8, .address = 0x5156bb, .text = "\xb0\xed\xb8\xae\xc0\xfc\xb0\xa5"},
};

// 0x51560f: el tipo 28.
constexpr MonsterStep Case51560f[]{
    {.kind = K::LoadMonster, .value = 0x15, .address = 0x515611},
    {.kind = K::Create, .value = 0x167, .extra = 0x0, .address = 0x515626},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x813, .address = 0x51563a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x515643},
    {.kind = K::Name, .extra = 0x6c8b04, .address = 0x51565f, .text = "\xc7\xb3\xb5\xad\xc0\xcc\xb1\xab\xb9\xb0"},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x515668},
};

// 0x5155b1: el tipo 29.
constexpr MonsterStep Case5155b1[]{
    {.kind = K::LoadMonster, .value = 0x16, .address = 0x5155b3},
    {.kind = K::Create, .value = 0x168, .extra = 0x0, .address = 0x5155c8},
    {.kind = K::Name, .extra = 0x6c8b10, .address = 0x5155f1, .text = "\xc7\xe5\xc5\xcd"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xa1c, .address = 0x5155fa},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f733333, .address = 0x515603},
};

// 0x51555c: el tipo 30.
constexpr MonsterStep Case51555c[]{
    {.kind = K::LoadMonster, .value = 0x17, .address = 0x51555e},
    {.kind = K::Create, .value = 0x169, .extra = 0x0, .address = 0x515573},
    {.kind = K::Name, .extra = 0x6c8b18, .address = 0x51559c, .text = "\xbd\xa3\xc0\xc7\xb1\xab\xb9\xb0"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f400000, .address = 0x5155a5},
};

// 0x5154f4: el tipo 31.
constexpr MonsterStep Case5154f4[]{
    {.kind = K::LoadMonster, .value = 0x18, .address = 0x5154f6},
    {.kind = K::Create, .value = 0x16a, .extra = 0x0, .address = 0x51550b},
    {.kind = K::Name, .extra = 0x6c8b24, .address = 0x515534, .text = "\xbe\xc6\xb0\xef"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x515542},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x21a, .address = 0x515549},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x21a, .address = 0x515550},
};

// 0x5154a6: el tipo 32.
constexpr MonsterStep Case5154a6[]{
    {.kind = K::LoadMonster, .value = 0x19, .address = 0x5154a8},
    {.kind = K::Create, .value = 0x16b, .extra = 0x0, .address = 0x5154bd},
    {.kind = K::Name, .extra = 0x6c8b2c, .address = 0x5154e6, .text = "\xb5\xb9\xb1\xab\xb9\xb0"},
};

// 0x515436: el tipo 33.
constexpr MonsterStep Case515436[]{
    {.kind = K::LoadMonster, .value = 0x13, .address = 0x515438},
    {.kind = K::Create, .value = 0x165, .extra = 0x0, .address = 0x51544d},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x613, .address = 0x515461},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe13, .address = 0x51546a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x515473},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x51547a},
    {.kind = K::Name, .extra = 0x6c8b34, .address = 0x515498, .text = "\xb0\xed\xba\xed\xb8\xb0 \xb4\xeb\xc0\xe5"},
};

// 0x51536b: el tipo 34.
constexpr MonsterStep Case51536b[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x51537b},
    {.kind = K::Name,
     .extra = 0x6c8b40,
     .address = 0x5153a2,
     .text = "\xc0\xfa\xc1\xd6\xb9\xde\xc0\xba \xb9\xfd\xbb\xe7"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1015, .address = 0x5153ab},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1215, .address = 0x5153b4},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x1415, .address = 0x5153bd},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x1615, .address = 0x5153c6},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x1815, .address = 0x5153cf},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc17, .address = 0x5153d8},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe20, .address = 0x5153e1},
    {.kind = K::Write, .size = 1, .offset = 0x21a, .value = 0x9, .address = 0x5153ea},
    {.kind = K::Write, .size = 1, .offset = 0x23a, .value = 0x9, .address = 0x5153f1},
    {.kind = K::Write, .size = 1, .offset = 0x25a, .value = 0x9, .address = 0x5153f8},
    {.kind = K::Write, .size = 1, .offset = 0x27a, .value = 0x9, .address = 0x5153ff},
    {.kind = K::Write, .size = 1, .offset = 0x29a, .value = 0x9, .address = 0x515406},
    {.kind = K::Write, .size = 1, .offset = 0x375, .value = 0x6, .address = 0x51540d},
    {.kind = K::Dress, .address = 0x515414},
    {.kind = K::Write,
     .when = MonsterStepWhen::InWorlds,
     .size = 4,
     .low = 9,
     .high = 9,
     .offset = 0xc,
     .value = 0x3f800000,
     .address = 0x51542a},
};

// 0x5152ec: el tipo 35.
constexpr MonsterStep Case5152ec[]{
    {.kind = K::LoadMonster, .value = 0xb, .address = 0x5152ee},
    {.kind = K::Create, .value = 0x15d, .extra = 0x0, .address = 0x515303},
    {.kind = K::Name, .extra = 0x6c8b50, .address = 0x51532c, .text = "\xb5\xa5\xbe\xb2 \xb0\xed\xb8\xa3\xb0\xef"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x51533a},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x41a, .address = 0x515341},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x41a, .address = 0x515348},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x51534f},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x515356},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x2, .address = 0x51535d},
};

// 0x515297: el tipo 36.
constexpr MonsterStep Case515297[]{
    {.kind = K::LoadMonster, .value = 0x1c, .address = 0x515299},
    {.kind = K::Create, .value = 0x16e, .extra = 0x0, .address = 0x5152ae},
    {.kind = K::Name, .extra = 0x6c8b5c, .address = 0x5152d7, .text = "\xbd\xa6\xb5\xb5\xbf\xec"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x5152e0},
};

// 0x515242: el tipo 37.
constexpr MonsterStep Case515242[]{
    {.kind = K::LoadMonster, .value = 0x1a, .address = 0x515244},
    {.kind = K::Create, .value = 0x16c, .extra = 0x0, .address = 0x515259},
    {.kind = K::Name, .extra = 0x6c8b64, .address = 0x515282, .text = "\xb5\xa5\xba\xf4"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x51528b},
};

// 0x5151dd: los tipos 38, 67.
constexpr MonsterStep Case5151dd[]{
    {.kind = K::LoadMonster, .value = 0x1b, .address = 0x5151df},
    {.kind = K::Create, .value = 0x16d, .extra = 0x0, .address = 0x5151f4},
    {.kind = K::Name, .extra = 0x6c8b6c, .address = 0x51521d, .text = "\xb9\xdf\xb7\xcf"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x81b, .address = 0x515226},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x9, .address = 0x51522f},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fcccccd, .address = 0x515236},
};

// 0x51517f: el tipo 39.
constexpr MonsterStep Case51517f[]{
    {.kind = K::LoadMonster, .value = 0x1c, .address = 0x515181},
    {.kind = K::Create, .value = 0x16e, .extra = 0x0, .address = 0x515196},
    {.kind = K::Name,
     .extra = 0x6c8b74,
     .address = 0x5151bf,
     .text = "\xc6\xf7\xc0\xcc\xc1\xf0 \xbd\xa6\xb5\xb5\xbf\xec"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x5151c8},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x5151cf},
};

// 0x515121: el tipo 40.
constexpr MonsterStep Case515121[]{
    {.kind = K::LoadMonster, .value = 0x1d, .address = 0x515123},
    {.kind = K::Create, .value = 0x16f, .extra = 0x0, .address = 0x515138},
    {.kind = K::Name, .extra = 0x6c8b84, .address = 0x515161, .text = "\xb5\xa5\xbe\xb2 \xb3\xaa\xc0\xcc\xc6\xae"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x220, .address = 0x51516a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x515173},
};

// 0x5150c3: el tipo 41.
constexpr MonsterStep Case5150c3[]{
    {.kind = K::LoadMonster, .value = 0x1e, .address = 0x5150c5},
    {.kind = K::Create, .value = 0x170, .extra = 0x0, .address = 0x5150da},
    {.kind = K::Name, .extra = 0x6c8b90, .address = 0x515103, .text = "\xb5\xa5\xbe\xb2 \xc4\xab\xbf\xec"},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x615, .address = 0x51510c},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x515115},
};

// 0x515050: el tipo 42.
constexpr MonsterStep Case515050[]{
    {.kind = K::LoadMonster, .value = 0x1f, .address = 0x515052},
    {.kind = K::Create, .value = 0x171, .extra = 0x0, .address = 0x515067},
    {.kind = K::Name, .extra = 0x6c8b9c, .address = 0x515090, .text = "\xc4\xef\xb5\xd0"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x515099},
    {.kind = K::Write, .size = 4, .offset = 0x130, .value = 0x43480000, .address = 0x5150a0},
    {.kind = K::Write, .size = 4, .offset = 0x134, .value = 0x43160000, .address = 0x5150aa},
    {.kind = K::Write, .size = 4, .offset = 0x138, .value = 0x438c0000, .address = 0x5150b4},
};

// 0x514ffb: el tipo 43.
constexpr MonsterStep Case514ffb[]{
    {.kind = K::LoadMonster, .value = 0x2, .address = 0x514ffd},
    {.kind = K::Create, .value = 0x154, .extra = 0x0, .address = 0x515012},
    {.kind = K::Name,
     .extra = 0x6c8ba4,
     .address = 0x51503b,
     .text = "\xc8\xb2\xb1\xdd\xb9\xf6\xc1\xf6\xb5\xe5\xb7\xa1\xb0\xef"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f333333, .address = 0x515044},
};

// 0x514fa6: los tipos 44, 79.
constexpr MonsterStep Case514fa6[]{
    {.kind = K::LoadMonster, .value = 0x1f, .address = 0x514fa8},
    {.kind = K::Create, .value = 0x171, .extra = 0x0, .address = 0x514fbd},
    {.kind = K::Name, .extra = 0x6c8bb4, .address = 0x514fe6, .text = "\xb5\xe5\xb7\xa1\xb0\xef"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f666666, .address = 0x514fef},
};

// 0x514f2a: el tipo 45.
constexpr MonsterStep Case514f2a[]{
    {.kind = K::LoadMonster, .value = 0x21, .address = 0x514f2c},
    {.kind = K::Create, .value = 0x173, .extra = 0x0, .address = 0x514f3b},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f19999a, .address = 0x514f45},
};

// 0x514f03: los tipos 46, 81.
constexpr MonsterStep Case514f03[]{
    {.kind = K::LoadMonster, .value = 0x22, .address = 0x514f05},
    {.kind = K::Create, .value = 0x174, .extra = 0x0, .address = 0x514f14},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x514f1e},
};

// 0x514ec5: el tipo 47.
constexpr MonsterStep Case514ec5[]{
    {.kind = K::LoadMonster, .value = 0x23, .address = 0x514ec7},
    {.kind = K::Create, .value = 0x175, .extra = 0x0, .address = 0x514ed6},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x514ee0},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xa1f, .address = 0x514ee7},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x0, .address = 0x514ef0},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514ef7},
};

// 0x514e95: el tipo 48.
constexpr MonsterStep Case514e95[]{
    {.kind = K::LoadMonster, .value = 0x24, .address = 0x514e97},
    {.kind = K::Create, .value = 0x176, .extra = 0x0, .address = 0x514ea6},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x514eb0},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc18, .address = 0x514eb7},
};

// 0x514e60: el tipo 49.
constexpr MonsterStep Case514e60[]{
    {.kind = K::LoadMonster, .value = 0x25, .address = 0x514e62},
    {.kind = K::Create, .value = 0x177, .extra = 0x0, .address = 0x514e71},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x514e7b},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x5, .address = 0x514e82},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x0, .address = 0x514e89},
};

// 0x514e39: el tipo 50.
constexpr MonsterStep Case514e39[]{
    {.kind = K::LoadMonster, .value = 0x26, .address = 0x514e3b},
    {.kind = K::Create, .value = 0x178, .extra = 0x0, .address = 0x514e4a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x514e54},
};

// 0x514e09: el tipo 51.
constexpr MonsterStep Case514e09[]{
    {.kind = K::LoadMonster, .value = 0x21, .address = 0x514e0b},
    {.kind = K::Create, .value = 0x173, .extra = 0x0, .address = 0x514e1a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x514e24},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x514e2b},
};

// 0x514dd9: el tipo 52.
constexpr MonsterStep Case514dd9[]{
    {.kind = K::LoadMonster, .value = 0x23, .address = 0x514ddb},
    {.kind = K::Create, .value = 0x175, .extra = 0x0, .address = 0x514dea},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x514df4},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xa1f, .address = 0x514dfb},
};

// 0x515f23: el tipo 53.
constexpr MonsterStep Case515f23[]{
    {.kind = K::LoadMonster, .value = 0x27, .address = 0x515f25},
    {.kind = K::Create, .value = 0x179, .extra = 0x0, .address = 0x515f3a},
    {.kind = K::Name, .extra = 0x6c8a18, .address = 0x515f62, .text = "\xc5\xb8\xc0\xcc\xc5\xba"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x515f87},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x2, .address = 0x515f8e},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x515f95},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x515f9c},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x515fbc},
};

// 0x515fc9: el tipo 54.
constexpr MonsterStep Case515fc9[]{
    {.kind = K::LoadMonster, .value = 0x28, .address = 0x515fcb},
    {.kind = K::Create, .value = 0x17a, .extra = 0x0, .address = 0x515fe0},
    {.kind = K::Name, .extra = 0x6c8a10, .address = 0x516009, .text = "\xbc\xd6\xc1\xae"},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa20, .address = 0x516018},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x516027},
};

// 0x515e74: el tipo 55.
constexpr MonsterStep Case515e74_2[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x515e84},
    {.kind = K::Name, .extra = 0x6c8a20, .address = 0x515ead, .text = "\xc7\xd8\xb0\xf1\xc0\xfc\xbb\xe7"},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0xd1, .address = 0x515ebc},
    {.kind = K::Write, .size = 1, .offset = 0x38a, .value = 0x1, .address = 0x515ec3},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x515f05},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x515f0e},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x81b, .address = 0x515f15},
};

// 0x515e74: el tipo 56.
constexpr MonsterStep Case515e74_3[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x515e84},
    {.kind = K::Name, .extra = 0x6c8a20, .address = 0x515ead, .text = "\xc7\xd8\xb0\xf1\xc0\xfc\xbb\xe7"},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0xd1, .address = 0x515ebc},
    {.kind = K::Write, .size = 1, .offset = 0x38a, .value = 0x1, .address = 0x515ec3},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x515ef0},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x81a, .address = 0x515ef7},
};

// 0x514d66: el tipo 57.
constexpr MonsterStep Case514d66[]{
    {.kind = K::LoadMonster, .value = 0x29, .address = 0x514d68},
    {.kind = K::Create, .value = 0x17b, .extra = 0x0, .address = 0x514d77},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x514d9c},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xa20, .address = 0x514da3},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514dac},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514dcc},
};

// 0x514cc4: el tipo 58.
constexpr MonsterStep Case514cc4[]{
    {.kind = K::LoadMonster, .value = 0x2a, .address = 0x514cc6},
    {.kind = K::Create, .value = 0x17c, .extra = 0x0, .address = 0x514cd5},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x2, .address = 0x514ce5},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514cec},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x514cf5},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x222, .address = 0x514cfc},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514d3c},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514d59},
};

// 0x514cc4: el tipo 59.
constexpr MonsterStep Case514cc4_2[]{
    {.kind = K::LoadMonster, .value = 0x2a, .address = 0x514cc6},
    {.kind = K::Create, .value = 0x17c, .extra = 0x0, .address = 0x514cd5},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x2, .address = 0x514ce5},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514cec},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x40066666, .address = 0x514d07},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x1, .address = 0x514d0e},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc1a, .address = 0x514d15},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514d3c},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514d59},
};

// 0x514c5a: el tipo 60.
constexpr MonsterStep Case514c5a[]{
    {.kind = K::LoadMonster, .value = 0x2b, .address = 0x514c5c},
    {.kind = K::Create, .value = 0x17d, .extra = 0x0, .address = 0x514c6b},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x400ccccd, .address = 0x514c90},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514c97},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514cb7},
};

// 0x514bd1: el tipo 61.
constexpr MonsterStep Case514bd1[]{
    {.kind = K::LoadMonster, .value = 0x2c, .address = 0x514bd3},
    {.kind = K::Create, .value = 0x17e, .extra = 0x0, .address = 0x514be2},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x514c0b},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514c30},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514c4d},
};

// 0x514b67: los tipos 62, 300.
constexpr MonsterStep Case514b67[]{
    {.kind = K::LoadMonster, .value = 0x2d, .address = 0x514b69},
    {.kind = K::Create, .value = 0x17f, .extra = 0x0, .address = 0x514b78},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x514b9d},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514ba4},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514bc4},
};

// 0x514bd1: el tipo 63.
constexpr MonsterStep Case514bd1_2[]{
    {.kind = K::LoadMonster, .value = 0x2c, .address = 0x514bd3},
    {.kind = K::Create, .value = 0x17e, .extra = 0x0, .address = 0x514be2},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3ff33333, .address = 0x514bf4},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0xfffffffe, .address = 0x514bfb},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514c02},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x514c30},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x514c4d},
};

// 0x514a96: el tipo 64.
constexpr MonsterStep Case514a96[]{
    {.kind = K::LoadMonster, .value = 0x2e, .address = 0x514a98},
    {.kind = K::Create, .value = 0x180, .extra = 0x0, .address = 0x514aa7},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x514ab1},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa15, .address = 0x514ab8},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x3, .address = 0x514ac1},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0x1, .address = 0x514ac8},
};

// 0x514b12: los tipos 65, 136.
constexpr MonsterStep Case514b12[]{
    {.kind = K::LoadMonster, .value = 0x2f, .address = 0x514b14},
    {.kind = K::Create, .value = 0x181, .extra = 0x0, .address = 0x514b23},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x514b2d},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0x2, .address = 0x514b34},
};

// 0x514b40: los tipos 66, 135.
constexpr MonsterStep Case514b40[]{
    {.kind = K::LoadMonster, .value = 0x30, .address = 0x514b42},
    {.kind = K::Create, .value = 0x182, .extra = 0x0, .address = 0x514b51},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fd9999a, .address = 0x514b5b},
};

// 0x514818: el tipo 68.
constexpr MonsterStep Case514818[]{
    {.kind = K::LoadMonster, .value = 0x31, .address = 0x51481a},
    {.kind = K::Create, .value = 0x183, .extra = 0x0, .address = 0x514829},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x514833},
};

// 0x51483f: el tipo 69.
constexpr MonsterStep Case51483f[]{
    {.kind = K::LoadMonster, .value = 0x32, .address = 0x514841},
    {.kind = K::Create, .value = 0x184, .extra = 0x0, .address = 0x514850},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x51485a},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x0, .address = 0x514861},
};

// 0x51486d: el tipo 70.
constexpr MonsterStep Case51486d[]{
    {.kind = K::LoadMonster, .value = 0x33, .address = 0x51486f},
    {.kind = K::Create, .value = 0x185, .extra = 0x0, .address = 0x51487e},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x514888},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0xfffffffe, .address = 0x51488f},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514896},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x51489d},
};

// 0x5148a9: el tipo 71.
constexpr MonsterStep Case5148a9[]{
    {.kind = K::LoadMonster, .value = 0x34, .address = 0x5148ab},
    {.kind = K::Create, .value = 0x186, .extra = 0x0, .address = 0x5148ba},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x5148cc},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x224, .address = 0x5148d3},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x5, .address = 0x5148dc},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe20, .address = 0x5148e3},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x5148ec},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x5148f3},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x5148fa},
};

// 0x514940: el tipo 72.
constexpr MonsterStep Case514940[]{
    {.kind = K::LoadMonster, .value = 0x35, .address = 0x514942},
    {.kind = K::Create, .value = 0x187, .extra = 0x0, .address = 0x514951},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb9999a, .address = 0x51495b},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x223, .address = 0x514962},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x5, .address = 0x51496b},
};

// 0x514977: el tipo 73.
constexpr MonsterStep Case514977[]{
    {.kind = K::LoadMonster, .value = 0x36, .address = 0x514979},
    {.kind = K::Create, .value = 0x188, .extra = 0x0, .address = 0x514988},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x514998},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x5149ad},
    {.kind = K::MeshFlag, .offset = 0, .value = 1, .address = 0x5149ce},
    {.kind = K::MeshFlag, .offset = 1, .value = 0, .address = 0x5149eb},
    {.kind = K::MeshFlag, .offset = 2, .value = 0, .address = 0x514a09},
    {.kind = K::MeshFlag, .offset = 3, .value = 1, .address = 0x514a27},
    {.kind = K::MeshFlag, .offset = 4, .value = 1, .address = 0x514a45},
};

// 0x5148a9: los tipos 74, 301.
constexpr MonsterStep Case5148a9_2[]{
    {.kind = K::LoadMonster, .value = 0x34, .address = 0x5148ab},
    {.kind = K::Create, .value = 0x186, .extra = 0x0, .address = 0x5148ba},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x514906},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x224, .address = 0x51490d},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x9, .address = 0x514916},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe20, .address = 0x51491d},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x9, .address = 0x514926},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x51492d},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514934},
};

// 0x514977: el tipo 75.
constexpr MonsterStep Case514977_2[]{
    {.kind = K::LoadMonster, .value = 0x36, .address = 0x514979},
    {.kind = K::Create, .value = 0x188, .extra = 0x0, .address = 0x514988},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x514998},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x5149a4},
    {.kind = K::MeshFlag, .offset = 0, .value = 1, .address = 0x5149ce},
    {.kind = K::MeshFlag, .offset = 1, .value = 0, .address = 0x5149eb},
    {.kind = K::MeshFlag, .offset = 2, .value = 0, .address = 0x514a09},
    {.kind = K::MeshFlag, .offset = 3, .value = 1, .address = 0x514a27},
    {.kind = K::MeshFlag, .offset = 4, .value = 1, .address = 0x514a45},
};

// 0x516e2a: los tipos que el switch no lista (y los del default que no son el 0, el 4 ni el 8).
constexpr MonsterStep DefaultSteps[]{
    {.kind = K::LoadMonster, .value = 0x0, .address = 0x516e2c},
    {.kind = K::Create, .value = 0x152, .extra = 0x0, .address = 0x516e3b},
};

// 0x514a51: el tipo 77.
constexpr MonsterStep Case514a51[]{
    {.kind = K::LoadMonster, .value = 0x37, .address = 0x514a53},
    {.kind = K::LoadMonster, .value = 0x38, .address = 0x514a5a},
    {.kind = K::Create, .value = 0x189, .extra = 0x0, .address = 0x514a69},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x514a73},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x514a7d},
    {.kind = K::ModelByte, .offset = 0x88, .value = 0, .extra = 0x189, .address = 0x514a8a},
};

// 0x5146f9: el tipo 78.
constexpr MonsterStep Case5146f9[]{
    {.kind = K::LoadMonster, .value = 0x13, .address = 0x5146fb},
    {.kind = K::Create, .value = 0x165, .extra = 0x0, .address = 0x514710},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x412, .address = 0x51471a},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x9, .address = 0x514723},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x5156f3},
    {.kind = K::Name, .extra = 0x6c8af0, .address = 0x515719, .text = "\xb0\xed\xba\xed\xb8\xb0"},
};

// 0x51472f: el tipo 80.
constexpr MonsterStep Case51472f[]{
    {.kind = K::LoadMonster, .value = 0x24, .address = 0x514731},
    {.kind = K::Create, .value = 0x176, .extra = 0x0, .address = 0x514740},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x51474a},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc19, .address = 0x514751},
    {.kind = K::Write, .size = 1, .offset = 0x2bb, .value = 0x3f, .address = 0x51475a},
};

// 0x514766: el tipo 82.
constexpr MonsterStep Case514766[]{
    {.kind = K::LoadMonster, .value = 0x2a, .address = 0x514768},
    {.kind = K::Create, .value = 0x17c, .extra = 0x0, .address = 0x514777},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x2, .address = 0x51477e},
    {.kind = K::Write, .size = 4, .offset = 0x68, .value = 0x3f800000, .address = 0x514785},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x51478c},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x222, .address = 0x514793},
    {.kind = K::Write, .size = 1, .offset = 0x2bb, .value = 0x3f, .address = 0x5147e4},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x5147eb},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x51480b},
};

// 0x51479e: el tipo 83.
constexpr MonsterStep Case51479e[]{
    {.kind = K::LoadMonster, .value = 0x29, .address = 0x5147a0},
    {.kind = K::Create, .value = 0x17b, .extra = 0x0, .address = 0x5147af},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x5147b6},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xa20, .address = 0x5147bd},
    {.kind = K::Write, .size = 1, .offset = 0x2bb, .value = 0x3f, .address = 0x5147e4},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x5147eb},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x51480b},
};

// 0x5145a6: los tipos 84, 90, 96, 113, 119, 125, 138.
constexpr MonsterStep Case5145a6[]{
    {.kind = K::LoadMonster, .value = 0x2f, .address = 0x5145a8},
    {.kind = K::Create, .value = 0x181, .extra = 0x0, .address = 0x5145b7},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x5145c1},
};

// 0x5145cd: los tipos 85, 91, 97, 114, 120, 126, 139.
constexpr MonsterStep Case5145cd[]{
    {.kind = K::LoadMonster, .value = 0x2e, .address = 0x5145cf},
    {.kind = K::Create, .value = 0x180, .extra = 0x0, .address = 0x5145de},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x5145e8},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa15, .address = 0x5145ef},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x1, .address = 0x5145f8},
};

// 0x514604: los tipos 86, 92, 98, 115, 121, 127, 140.
constexpr MonsterStep Case514604[]{
    {.kind = K::LoadMonster, .value = 0x3b, .address = 0x514606},
    {.kind = K::Create, .value = 0x18d, .extra = 0x0, .address = 0x51461b},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x41a, .address = 0x51462f},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x514636},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x41a, .address = 0x51463d},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x514644},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x51464b},
    {.kind = K::Name, .extra = 0x6c8be4, .address = 0x515719, .text = "\xc8\xe6\xc7\xd8\xb0\xf1\xc0\xfc\xbb\xe7"},
};

// 0x51465d: los tipos 87, 93, 99, 116, 122, 128, 141.
constexpr MonsterStep Case51465d[]{
    {.kind = K::LoadMonster, .value = 0x3a, .address = 0x51465f},
    {.kind = K::Create, .value = 0x18c, .extra = 0x0, .address = 0x514674},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x514683},
    {.kind = K::Name,
     .extra = 0x6c8bd4,
     .address = 0x515719,
     .text = "\xc0\xda\xc0\xcc\xbe\xf0\xc6\xae\xbf\xc0\xbf\xec\xb0\xc5"},
};

// 0x514695: los tipos 88, 94, 111, 117, 123, 129, 142.
constexpr MonsterStep Case514695[]{
    {.kind = K::LoadMonster, .value = 0x39, .address = 0x514697},
    {.kind = K::Create, .value = 0x18b, .extra = 0x0, .address = 0x5146a6},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x618, .address = 0x5146b5},
    {.kind = K::Write,
     .when = MonsterStepWhen::OutsideWorlds,
     .size = 1,
     .low = 8,
     .high = 12,
     .offset = 0x2ba,
     .value = 0x0,
     .address = 0x5146db},
    {.kind = K::Write,
     .when = MonsterStepWhen::InWorlds,
     .size = 1,
     .low = 8,
     .high = 12,
     .offset = 0x2ba,
     .value = 0x8,
     .address = 0x5146d2},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f9851ec, .address = 0x5146e2},
    {.kind = K::Name,
     .extra = 0x6c8bc4,
     .address = 0x515719,
     .text = "\xba\xd3\xc0\xba\xc7\xd8\xb0\xf1\xb1\xe2\xbb\xe7"},
};

// 0x5144cc: los tipos 89, 95, 112, 118, 124, 130, 143.
constexpr MonsterStep Case5144cc[]{
    {.kind = K::LoadMonster, .value = 0x3e, .address = 0x5144ce},
    {.kind = K::Create, .value = 0x190, .extra = 0x0, .address = 0x5144e3},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc12, .address = 0x5144f2},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0xb, .address = 0x5144fb},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x514502},
    {.kind = K::Name, .extra = 0x6c8c04, .address = 0x515719, .text = "\xb8\xb6\xb9\xfd\xc7\xd8\xb0\xf1"},
};

// 0x516105: el tipo 100.
constexpr MonsterStep Case516105[]{
    {.kind = K::Create, .value = 0x27, .extra = 0x0, .address = 0x51610c},
};

// 0x51611b: el tipo 101.
constexpr MonsterStep Case51611b[]{
    {.kind = K::Create, .value = 0x28, .extra = 0x0, .address = 0x516122},
};

// 0x516131: el tipo 102.
constexpr MonsterStep Case516131[]{
    {.kind = K::Create, .value = 0x33, .extra = 0x0, .address = 0x516138},
};

// 0x516147: el tipo 103.
constexpr MonsterStep Case516147[]{
    {.kind = K::Create, .value = 0x19, .extra = 0x0, .address = 0x51614e},
};

// 0x42d0ab: el tipo 104.
constexpr MonsterStep Event42d070Case42d0ab[]{
    {.kind = K::Create, .value = 0xb, .extra = 0x0, .address = 0x42d0bb},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d0c5},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d0cf},
};

// 0x514514: el tipo 131.
constexpr MonsterStep Case514514[]{
    {.kind = K::LoadMonster, .value = 0x3d, .address = 0x514516},
    {.kind = K::Create, .value = 0x18f, .extra = 0x0, .address = 0x51452b},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x51453a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x514544},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x51454b},
    {.kind = K::Name, .extra = 0x6c8bfc, .address = 0x515719, .text = "\xbc\xba\xb9\xae"},
};

// 0x51455d: los tipos 132, 133, 134.
constexpr MonsterStep Case51455d[]{
    {.kind = K::LoadMonster, .value = 0x3c, .address = 0x51455f},
    {.kind = K::Create, .value = 0x18e, .extra = 0x0, .address = 0x514574},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x514583},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x51458d},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x514594},
    {.kind = K::Name, .extra = 0x6c8bf0, .address = 0x515719, .text = "\xbc\xba\xc0\xda\xc0\xc7\xbc\xae\xb0\xfc"},
};

// 0x514ad4: el tipo 137.
constexpr MonsterStep Case514ad4[]{
    {.kind = K::LoadMonster, .value = 0x2e, .address = 0x514ad6},
    {.kind = K::Create, .value = 0x180, .extra = 0x0, .address = 0x514ae5},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x514aef},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa15, .address = 0x514af6},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x5, .address = 0x514aff},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0x1, .address = 0x514b06},
};

// 0x439e1e: los tipos 144, 174, 182, 190, 260, 268, 334.
constexpr MonsterStep Event439df0Case439e1e[]{
    {.kind = K::LoadMonster, .value = 0x3f, .address = 0x439e20},
    {.kind = K::Create, .value = 0x191, .extra = 0x0, .address = 0x439e37},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x439e49},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x439e4f},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x439e56},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x439e5d},
    {.kind = K::Name, .extra = 0x6c74cc, .address = 0x43a0e4, .text = "\xc0\xe5\xbc\xf6\xb0\xc5\xba\xcf"},
};

// 0x439e6f: los tipos 145, 175, 183, 191, 261, 269, 336.
constexpr MonsterStep Event439df0Case439e6f[]{
    {.kind = K::LoadMonster, .value = 0x43, .address = 0x439e71},
    {.kind = K::Create, .value = 0x195, .extra = 0x0, .address = 0x439e88},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x0, .address = 0x439e97},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x81c, .address = 0x439e9a},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x7, .address = 0x439ea3},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x439eaa},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x9, .address = 0x439eb1},
    {.kind = K::Name,
     .extra = 0x6c74bc,
     .address = 0x43a0e4,
     .text = "\xb4\xeb\xc7\xfc \xba\xed\xb7\xe7\xb3\xaa\xc0\xcc\xc6\xae"},
};

// 0x439ec3: los tipos 146, 176, 184, 192, 262, 270, 333.
constexpr MonsterStep Event439df0Case439ec3[]{
    {.kind = K::LoadMonster, .value = 0x41, .address = 0x439ec5},
    {.kind = K::Create, .value = 0x193, .extra = 0x0, .address = 0x439edc},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x439eee},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x439ef4},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x439efb},
    {.kind = K::Name, .extra = 0x6c74b4, .address = 0x43a0e4, .text = "\xb6\xf8\xbd\xba\xc5\xcd"},
};

// 0x439f0d: los tipos 147, 177, 185, 193, 263, 271, 331.
constexpr MonsterStep Event439df0Case439f0d[]{
    {.kind = K::LoadMonster, .value = 0x42, .address = 0x439f0f},
    {.kind = K::Create, .value = 0x194, .extra = 0x0, .address = 0x439f26},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x439f38},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x439f3e},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x439f45},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x439f4c},
    {.kind = K::Name, .extra = 0x6c74ac, .address = 0x43a0e4, .text = "\xb0\xa1\xbf\xc0\xb8\xae"},
};

// 0x439f5e: los tipos 148, 178, 186, 194, 264, 272, 332.
constexpr MonsterStep Event439df0Case439f5e[]{
    {.kind = K::LoadMonster, .value = 0x43, .address = 0x439f60},
    {.kind = K::Create, .value = 0x195, .extra = 0x0, .address = 0x439f77},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x0, .address = 0x439f86},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x81c, .address = 0x439f89},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x7, .address = 0x439f92},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x439f99},
    {.kind = K::Name, .extra = 0x6c74a0, .address = 0x43a0e4, .text = "\xba\xed\xb7\xe7\xb3\xaa\xc0\xcc\xc6\xae"},
};

// 0x439fab: los tipos 149, 179, 187, 195, 265, 273, 335.
constexpr MonsterStep Event439df0Case439fab[]{
    {.kind = K::LoadMonster, .value = 0x44, .address = 0x439fad},
    {.kind = K::Create, .value = 0x196, .extra = 0x0, .address = 0x439fc4},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x439fd6},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x7, .address = 0x439fdd},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x439fe4},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x3, .address = 0x439feb},
    {.kind = K::Name, .extra = 0x6c7494, .address = 0x43a0e4, .text = "\xb8\xb6\xb8\xb0\xba\xb8\xc0\xcc"},
};

// 0x514f51: el tipo 150.
constexpr MonsterStep Case514f51[]{
    {.kind = K::LoadMonster, .value = 0x20, .address = 0x514f53},
    {.kind = K::Create, .value = 0x172, .extra = 0x0, .address = 0x514f68},
    {.kind = K::Name, .extra = 0x6c8bbc, .address = 0x514f91, .text = "\xb9\xdf\xb8\xae"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3df5c28f, .address = 0x514f9a},
};

// 0x515fc9: el tipo 151.
constexpr MonsterStep Case515fc9_2[]{
    {.kind = K::LoadMonster, .value = 0x28, .address = 0x515fcb},
    {.kind = K::Create, .value = 0x17a, .extra = 0x0, .address = 0x515fe0},
    {.kind = K::Name, .extra = 0x6c8a10, .address = 0x516009, .text = "\xbc\xd6\xc1\xae"},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa20, .address = 0x516018},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x515173},
};

// 0x514390: los tipos 152, 153, 154, 155, 156, 157, 158.
constexpr MonsterStep Case514390[]{
    {.kind = K::Create, .value = 0xc5, .extra = 0x0, .address = 0x5143a0},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x5143b2},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x5143bc},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x5143c3},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x5143ca},
    {.kind = K::Write, .size = 1, .offset = 0x3e4, .value = 0x0, .address = 0x5143d1},
    {.kind = K::Write, .size = 4, .offset = 0x118, .value = 0x41200000, .address = 0x5143d8},
    {.kind = K::Write, .size = 4, .offset = 0x114, .value = 0x41200000, .address = 0x5143de},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0xffffffff, .address = 0x5143e4},
    {.kind = K::Name, .extra = 0x6e57a4, .address = 0x516a97, .text = ""},
};

// 0x439ffd: los tipos 160, 180, 188, 196, 266, 274, 337.
constexpr MonsterStep Event439df0Case439ffd[]{
    {.kind = K::LoadMonster, .value = 0x45, .address = 0x439fff},
    {.kind = K::Create, .value = 0x197, .extra = 0x0, .address = 0x43a016},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x21f, .address = 0x43a02a},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x21f, .address = 0x43a031},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x43a03d},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x43a043},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x43a049},
    {.kind = K::Name, .extra = 0x6c7488, .address = 0x43a065, .text = "\xc4\xef\xb5\xd0\xc8\xc4\xba\xb8"},
};

// 0x43a075: los tipos 161, 181, 189, 197, 267, 338.
constexpr MonsterStep Event439df0Case43a075[]{
    {.kind = K::LoadMonster, .value = 0x45, .address = 0x43a077},
    {.kind = K::Create, .value = 0x197, .extra = 0x0, .address = 0x43a08e},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x43a0a2},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x43a0a8},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x21f, .address = 0x43a0ae},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x21f, .address = 0x43a0b5},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x9, .address = 0x43a0bc},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x43a0c3},
    {.kind = K::Name, .extra = 0x6c7488, .address = 0x43a0e4, .text = "\xc4\xef\xb5\xd0\xc8\xc4\xba\xb8"},
};

// 0x5143f7: los tipos 162, 164, 166, 168, 170, 172.
constexpr MonsterStep Case5143f7[]{
    {.kind = K::LoadMonster, .value = 0x46, .address = 0x5143f9},
    {.kind = K::Create, .value = 0x198, .extra = 0x0, .address = 0x514408},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f666666, .address = 0x514417},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x222, .address = 0x51441e},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x514425},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0x222, .address = 0x51442c},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x514433},
};

// 0x51443f: los tipos 163, 165, 167, 169, 171, 173.
constexpr MonsterStep Case51443f[]{
    {.kind = K::Random, .address = 0x51443f},
    {.kind = K::LoadMonster, .plusRandom = true, .value = 0x47, .address = 0x514457},
    {.kind = K::Create, .plusRandom = true, .value = 0x199, .extra = 0x0, .address = 0x51446e},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f666666, .address = 0x51447d},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x514484},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x0, .address = 0x51448b},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x514492},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x514499},
    {.kind = K::Write,
     .when = MonsterStepWhen::RandomZero,
     .size = 2,
     .offset = 0x2b8,
     .value = 0xa25,
     .address = 0x5144a2},
    {.kind = K::Write,
     .when = MonsterStepWhen::RandomZero,
     .size = 1,
     .offset = 0x2ba,
     .value = 0x0,
     .address = 0x5144ab},
    {.kind = K::Write,
     .when = MonsterStepWhen::RandomOne,
     .size = 2,
     .offset = 0x2b8,
     .value = 0xc17,
     .address = 0x5144b7},
    {.kind = K::Write,
     .when = MonsterStepWhen::RandomOne,
     .size = 1,
     .offset = 0x2ba,
     .value = 0x0,
     .address = 0x5144c0},
};

// 0x51615d: el tipo 200.
constexpr MonsterStep Case51615d[]{
    {.kind = K::Create, .value = 0xf1, .extra = 0x0, .address = 0x516167},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x2, .address = 0x516171},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x516178},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x1, .address = 0x51617f},
};

// 0x516b13: el tipo 204.
constexpr MonsterStep Case516b13[]{
    {.kind = K::LoadNpc, .value = 0x203, .address = 0x516b18},
    {.kind = K::Create, .value = 0x203, .extra = 0x0, .address = 0x516b2d},
    {.kind = K::Name, .extra = 0x6c8918, .address = 0x516b56, .text = "\xbc\xae\xbb\xf3"},
    {.kind = K::Write, .size = 1, .offset = 0x0, .value = 0x0, .address = 0x516b5f},
};

// 0x516b67: el tipo 205.
constexpr MonsterStep Case516b67[]{
    {.kind = K::LoadNpc, .value = 0x204, .address = 0x516b6c},
    {.kind = K::Create, .value = 0x204, .extra = 0x0, .address = 0x516b81},
    {.kind = K::Name,
     .extra = 0x6c8910,
     .address = 0x516baa,
     .text = "\xc1\xa6\xb4\xdc"
             "1"},
    {.kind = K::Lower, .value = 0x41200000, .address = 0x516bbc},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x516bbf},
    {.kind = K::Write, .size = 1, .offset = 0x16c, .value = 0x0, .address = 0x516bc6},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x516bcd},
};

// 0x516bd9: el tipo 206.
constexpr MonsterStep Case516bd9[]{
    {.kind = K::LoadNpc, .value = 0x205, .address = 0x516bde},
    {.kind = K::Create, .value = 0x205, .extra = 0x0, .address = 0x516bf3},
    {.kind = K::Name,
     .extra = 0x6c8908,
     .address = 0x516c1c,
     .text = "\xc1\xa6\xb4\xdc"
             "2"},
    {.kind = K::Lower, .value = 0x41200000, .address = 0x516c2e},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x516c31},
    {.kind = K::Write, .size = 1, .offset = 0x16c, .value = 0x0, .address = 0x516c38},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x516c3f},
};

// 0x516c4b: el tipo 207.
constexpr MonsterStep Case516c4b[]{
    {.kind = K::LoadNpc, .value = 0x206, .address = 0x516c50},
    {.kind = K::Create, .value = 0x206, .extra = 0x0, .address = 0x516c65},
    {.kind = K::Name,
     .extra = 0x6c8900,
     .address = 0x516c8e,
     .text = "\xc1\xa6\xb4\xdc"
             "3"},
    {.kind = K::Lower, .value = 0x41200000, .address = 0x516ca0},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x516ca3},
    {.kind = K::Write, .size = 1, .offset = 0x16c, .value = 0x0, .address = 0x516caa},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x516cb1},
};

// 0x516cbd: el tipo 208.
constexpr MonsterStep Case516cbd[]{
    {.kind = K::LoadNpc, .value = 0x207, .address = 0x516cc2},
    {.kind = K::Create, .value = 0x207, .extra = 0x0, .address = 0x516cd7},
    {.kind = K::Name,
     .extra = 0x6c88f8,
     .address = 0x516d00,
     .text = "\xc1\xa6\xb4\xdc"
             "4"},
    {.kind = K::Lower, .value = 0x41200000, .address = 0x516d12},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x516d15},
    {.kind = K::Write, .size = 1, .offset = 0x16c, .value = 0x0, .address = 0x516d1c},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x516d23},
};

// 0x516d2f: el tipo 209.
constexpr MonsterStep Case516d2f[]{
    {.kind = K::LoadNpc, .value = 0x208, .address = 0x516d34},
    {.kind = K::Create, .value = 0x208, .extra = 0x0, .address = 0x516d49},
    {.kind = K::Name,
     .extra = 0x6c88f0,
     .address = 0x516d72,
     .text = "\xc1\xa6\xb4\xdc"
             "5"},
    {.kind = K::Lower, .value = 0x41200000, .address = 0x516d84},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x516d87},
    {.kind = K::Write, .size = 1, .offset = 0x16c, .value = 0x0, .address = 0x516d8e},
    {.kind = K::Write, .size = 1, .offset = 0x98, .value = 0x0, .address = 0x516d95},
};

// 0x42d0db: el tipo 215.
constexpr MonsterStep Event42d070Case42d0db[]{
    {.kind = K::LoadNpc, .value = 0x1ff, .address = 0x42d0e0},
    {.kind = K::Create, .value = 0x1ff, .extra = 0x0, .address = 0x42d0f8},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d102},
    {.kind = K::Write, .size = 1, .offset = 0x42c, .value = 0x0, .address = 0x42d10c},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d113},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc28f5c, .address = 0x42d11a},
    {.kind = K::Write, .size = 4, .offset = 0x60, .value = 0x0, .address = 0x42d121},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d131},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d138},
    {.kind = K::Name, .extra = 0x6c737c, .address = 0x42d450, .text = "\xbf\xd5\xb0\xfc"},
};

// 0x42d149: el tipo 216.
constexpr MonsterStep Event42d070Case42d149[]{
    {.kind = K::LoadNpc, .value = 0x1fc, .address = 0x42d14e},
    {.kind = K::Create, .value = 0x1fc, .extra = 0x0, .address = 0x42d166},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d170},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d17a},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d131},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d138},
    {.kind = K::Name, .extra = 0x6c737c, .address = 0x42d450, .text = "\xbf\xd5\xb0\xfc"},
};

// 0x42d183: el tipo 217.
constexpr MonsterStep Event42d070Case42d183[]{
    {.kind = K::LoadNpc, .value = 0x1fd, .address = 0x42d188},
    {.kind = K::Create, .value = 0x1fd, .extra = 0x0, .address = 0x42d1a0},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d1b2},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d1bc},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d1c3},
    {.kind = K::Name,
     .extra = 0x6c7370,
     .address = 0x42d232,
     .text = "\xbf\xd5\xb0\xfc \xb9\xdf\xc6\xc7"
             "2"},
    {.kind = K::CopyHeight, .offset = 0xd8, .address = 0x42d23e},
    {.kind = K::Siege, .address = 0x42d244},
    {.kind = K::Lower, .value = 0x42c80000, .address = 0x42d25d},
};

// 0x42d1d2: el tipo 218.
constexpr MonsterStep Event42d070Case42d1d2[]{
    {.kind = K::LoadNpc, .value = 0x1fd, .address = 0x42d1d7},
    {.kind = K::Create, .value = 0x1fd, .extra = 0x0, .address = 0x42d1ef},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d201},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d20b},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d212},
    {.kind = K::Name,
     .extra = 0x6c7364,
     .address = 0x42d232,
     .text = "\xbf\xd5\xb0\xfc \xb9\xdf\xc6\xc7"
             "1"},
    {.kind = K::CopyHeight, .offset = 0xd8, .address = 0x42d23e},
    {.kind = K::Siege, .address = 0x42d244},
    {.kind = K::Lower, .value = 0x42c80000, .address = 0x42d25d},
};

// 0x42d264: el tipo 219.
constexpr MonsterStep Event42d070Case42d264[]{
    {.kind = K::LoadNpc, .value = 0x1fb, .address = 0x42d269},
    {.kind = K::Create, .value = 0x1fb, .extra = 0x0, .address = 0x42d281},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d293},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x42d29d},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d2a4},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d2ab},
    {.kind = K::Name, .extra = 0x6c7358, .address = 0x42d450, .text = "\xbc\xba\xb9\xae \xbd\xba\xc0\xa7\xc4\xa1"},
};

// 0x42d2bd: el tipo 220.
constexpr MonsterStep Event42d070Case42d2bd[]{
    {.kind = K::LoadNpc, .value = 0x4d, .address = 0x42d2bf},
    {.kind = K::Create, .value = 0x19f, .extra = 0x0, .address = 0x42d2d7},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d2e9},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x42d2f3},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d2fa},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d301},
    {.kind = K::Name,
     .extra = 0x6c7348,
     .address = 0x42d450,
     .text = "\xbb\xe7\xb3\xc9\xc5\xcd \xb9\xae\xc1\xf6\xb1\xe2"},
};

// 0x42d313: el tipo 221.
constexpr MonsterStep Event42d070Case42d313[]{
    {.kind = K::LoadNpc, .value = 0x1f8, .address = 0x42d318},
    {.kind = K::Create, .value = 0x1f8, .extra = 0x0, .address = 0x42d330},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d33a},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d344},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x42d34b},
    {.kind = K::Write, .size = 4, .offset = 0x94, .value = 0x3f83d70a, .address = 0x42d352},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d64d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d654},
};

// 0x42d361: el tipo 222.
constexpr MonsterStep Event42d070Case42d361[]{
    {.kind = K::LoadNpc, .value = 0x1f9, .address = 0x42d366},
    {.kind = K::Create, .value = 0x1f9, .extra = 0x0, .address = 0x42d330},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d33a},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d344},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f4ccccd, .address = 0x42d34b},
    {.kind = K::Write, .size = 4, .offset = 0x94, .value = 0x3f83d70a, .address = 0x42d352},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d64d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d654},
};

// 0x42d37c: el tipo 223.
constexpr MonsterStep Event42d070Case42d37c[]{
    {.kind = K::LoadNpc, .value = 0x1fa, .address = 0x42d381},
    {.kind = K::Create, .value = 0x1fa, .extra = 0x0, .address = 0x42d399},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d3ab},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x42d3b5},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d3bc},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d3c3},
    {.kind = K::Name, .extra = 0x6c7340, .address = 0x42d450, .text = "\xbf\xf8\xb7\xce\xbf\xf8"},
};

// 0x514323: el tipo 224.
constexpr MonsterStep Case514323[]{
    {.kind = K::LoadNpc, .value = 0x1fe, .address = 0x514328},
    {.kind = K::Create, .value = 0x1fe, .extra = 0x0, .address = 0x51433d},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x514347},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x514351},
    {.kind = K::Random, .address = 0x514358},
    {.kind = K::Write, .size = 4, .plusRandom = true, .offset = 0x4, .value = 0xa, .address = 0x51436f},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x514372},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x514379},
    {.kind = K::Name, .extra = 0x6c7338, .address = 0x516a90, .text = "\xb1\xd9\xc0\xa7\xba\xb4"},
};

// 0x42d3d2: el tipo 224.
constexpr MonsterStep Event42d070Case42d3d2[]{
    {.kind = K::LoadNpc, .value = 0x1fe, .address = 0x42d3d7},
    {.kind = K::Create, .value = 0x1fe, .extra = 0x0, .address = 0x42d3ef},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d3f9},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d403},
    {.kind = K::Random, .address = 0x42d40a},
    {.kind = K::Write, .size = 4, .plusRandom = true, .offset = 0x4, .value = 0xa, .address = 0x42d421},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d424},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d42b},
    {.kind = K::Name, .extra = 0x6c7338, .address = 0x42d450, .text = "\xb1\xd9\xc0\xa7\xba\xb4"},
};

// 0x51618d: el tipo 226.
constexpr MonsterStep Case51618d[]{
    {.kind = K::LoadNpc, .value = 0x1f6, .address = 0x516192},
    {.kind = K::Create, .value = 0x1f6, .extra = 0x0, .address = 0x5161a7},
    {.kind = K::Name, .extra = 0x6c89e8, .address = 0x5161d0, .text = "\xc1\xb6\xb7\xc3\xbb\xe7 NPC"},
};

// 0x5161de: el tipo 229.
constexpr MonsterStep Case5161de[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x5161ee},
    {.kind = K::Name, .extra = 0x6c89e0, .address = 0x516215, .text = "\xb8\xbb\xb7\xd0"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x101b, .address = 0x516220},
    {.kind = K::Write, .size = 1, .offset = 0x21a, .value = 0x7, .address = 0x516229},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x121b, .address = 0x51622f},
    {.kind = K::Write, .size = 1, .offset = 0x23a, .value = 0x7, .address = 0x516238},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x141b, .address = 0x51623e},
    {.kind = K::Write, .size = 1, .offset = 0x25a, .value = 0x7, .address = 0x516247},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x161b, .address = 0x51624d},
    {.kind = K::Write, .size = 1, .offset = 0x27a, .value = 0x7, .address = 0x516256},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x181b, .address = 0x51625c},
    {.kind = K::Write, .size = 1, .offset = 0x29a, .value = 0x7, .address = 0x516265},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x819, .address = 0x51626b},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x8, .address = 0x516274},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x51627b},
    {.kind = K::Dress, .address = 0x516284},
};

// 0x516291: el tipo 230.
constexpr MonsterStep Case516291[]{
    {.kind = K::LoadNpc, .value = 0x1cc, .address = 0x516296},
    {.kind = K::Create, .value = 0x1cc, .extra = 0x0, .address = 0x5162ab},
    {.kind = K::Name,
     .extra = 0x6c89d0,
     .address = 0x5162d4,
     .text = "\xb7\xce\xb7\xa3\xc3\xdf\xb0\xa1\xbb\xf3\xc0\xce"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1e4, .address = 0x5162dd},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1e7, .address = 0x5162e6},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x1e9, .address = 0x5162ef},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x1ea, .address = 0x5162f8},
};

// 0x516306: el tipo 231.
constexpr MonsterStep Case516306[]{
    {.kind = K::LoadNpc, .value = 0x1f5, .address = 0x51630b},
    {.kind = K::Create, .value = 0x1f5, .extra = 0x0, .address = 0x516320},
    {.kind = K::Name,
     .extra = 0x6c89c0,
     .address = 0x516349,
     .text = "\xb5\xa5\xba\xf1\xc3\xdf\xb0\xa1\xbb\xf3\xc0\xce"},
};

// 0x516357: el tipo 232.
constexpr MonsterStep Case516357[]{
    {.kind = K::LoadNpc, .value = 0x1f3, .address = 0x51635c},
    {.kind = K::Create, .value = 0x1f3, .extra = 0x0, .address = 0x5163db},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x5163e5},
    {.kind = K::Write, .size = 1, .offset = 0x88, .value = 0x4, .address = 0x5163ec},
};

// 0x51636c: el tipo 233.
constexpr MonsterStep Case51636c[]{
    {.kind = K::LoadNpc, .value = 0x1f4, .address = 0x516371},
    {.kind = K::Create, .value = 0x1f4, .extra = 0x0, .address = 0x5163db},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x5163e5},
    {.kind = K::Write, .size = 1, .offset = 0x88, .value = 0x4, .address = 0x5163ec},
};

// 0x516381: el tipo 234.
constexpr MonsterStep Case516381[]{
    {.kind = K::LoadMonster, .value = 0x13, .address = 0x516383},
    {.kind = K::Create, .value = 0x165, .extra = 0x0, .address = 0x516392},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xc12, .address = 0x51639e},
    {.kind = K::Write, .size = 1, .offset = 0x2ba, .value = 0x4, .address = 0x5163a7},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x5163ad},
    {.kind = K::Write, .size = 1, .offset = 0x88, .value = 0x4, .address = 0x5163b4},
    {.kind = K::Action, .value = 0x0, .address = 0x5163ba},
};

// 0x5163c7: el tipo 235.
constexpr MonsterStep Case5163c7[]{
    {.kind = K::LoadNpc, .value = 0x1f2, .address = 0x5163cc},
    {.kind = K::Create, .value = 0x1f2, .extra = 0x0, .address = 0x5163db},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x5163e5},
    {.kind = K::Write, .size = 1, .offset = 0x88, .value = 0x4, .address = 0x5163ec},
};

// 0x5163f8: el tipo 236.
constexpr MonsterStep Case5163f8[]{
    {.kind = K::LoadNpc, .value = 0x20a, .address = 0x5163fd},
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x51640c},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0xd2, .address = 0x516416},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x51641d},
    {.kind = K::Write, .size = 1, .offset = 0x88, .value = 0x4, .address = 0x516424},
    {.kind = K::Write, .size = 2, .offset = 0x1d6, .value = 0x8, .address = 0x51642b},
};

// 0x516439: el tipo 237.
constexpr MonsterStep Case516439[]{
    {.kind = K::LoadNpc, .value = 0x1d9, .address = 0x51643e},
    {.kind = K::Create, .value = 0x1d9, .extra = 0x0, .address = 0x51644d},
};

// 0x51645c: el tipo 238.
constexpr MonsterStep Case51645c[]{
    {.kind = K::LoadNpc, .value = 0x1d8, .address = 0x516461},
    {.kind = K::Create, .value = 0x1d8, .extra = 0x0, .address = 0x516470},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x51647a},
};

// 0x516486: el tipo 239.
constexpr MonsterStep Case516486[]{
    {.kind = K::LoadNpc, .value = 0x1d7, .address = 0x51648b},
    {.kind = K::Create, .value = 0x1d7, .extra = 0x0, .address = 0x51649a},
};

// 0x5164a9: el tipo 240.
constexpr MonsterStep Case5164a9[]{
    {.kind = K::LoadNpc, .value = 0x1d6, .address = 0x5164ae},
    {.kind = K::Create, .value = 0x1d6, .extra = 0x0, .address = 0x5164bd},
};

// 0x5164cc: el tipo 241.
constexpr MonsterStep Case5164cc[]{
    {.kind = K::LoadNpc, .value = 0x1d5, .address = 0x5164d1},
    {.kind = K::Create, .value = 0x1d5, .extra = 0x0, .address = 0x5164e6},
    {.kind = K::Name, .extra = 0x6c89b8, .address = 0x51650f, .text = "\xb8\xb6\xbd\xba\xc5\xcd"},
};

// 0x5165de: el tipo 242.
constexpr MonsterStep Case5165de[]{
    {.kind = K::LoadNpc, .value = 0x1d3, .address = 0x5165e3},
    {.kind = K::Create, .value = 0x1d3, .extra = 0x0, .address = 0x5165f8},
    {.kind = K::Name, .extra = 0x6c8998, .address = 0x51661e, .text = "\xb6\xf3\xb6\xf3 \xbf\xe4\xc1\xa4"},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x1, .address = 0x51662f},
    {.kind = K::Ground, .value = 0x430c0000, .address = 0x516636},
};

// 0x51664c: el tipo 243.
constexpr MonsterStep Case51664c[]{
    {.kind = K::LoadNpc, .value = 0x1d4, .address = 0x516651},
    {.kind = K::Create, .value = 0x1d4, .extra = 0x0, .address = 0x516666},
    {.kind = K::Name, .extra = 0x6c8990, .address = 0x51668f, .text = "\xc0\xe5\xc0\xce"},
};

// 0x51669d: el tipo 244.
constexpr MonsterStep Case51669d[]{
    {.kind = K::LoadNpc, .value = 0x1d0, .address = 0x5166a2},
    {.kind = K::Create, .value = 0x1d0, .extra = 0x0, .address = 0x5166b7},
    {.kind = K::Name, .extra = 0x6c8984, .address = 0x5166e0, .text = "\xbc\xfa\xc1\xfd\xb8\xb6\xb4\xe3"},
};

// 0x5166ee: el tipo 245.
constexpr MonsterStep Case5166ee[]{
    {.kind = K::LoadNpc, .value = 0x1d2, .address = 0x5166f3},
    {.kind = K::Create, .value = 0x1d2, .extra = 0x0, .address = 0x516708},
    {.kind = K::Name, .extra = 0x6c897c, .address = 0x516731, .text = "\xb8\xb6\xb9\xfd\xbb\xe7"},
};

// 0x51673f: el tipo 246.
constexpr MonsterStep Case51673f[]{
    {.kind = K::LoadNpc, .value = 0x1d1, .address = 0x516744},
    {.kind = K::Create, .value = 0x1d1, .extra = 0x0, .address = 0x516759},
    {.kind = K::Name, .extra = 0x6c8970, .address = 0x516782, .text = "\xb9\xab\xb1\xe2\xbb\xf3\xc0\xce"},
};

// 0x516790: el tipo 247.
constexpr MonsterStep Case516790[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x5167a0},
    {.kind = K::Name, .extra = 0x6c8968, .address = 0x5167c7, .text = "\xb0\xe6\xba\xf1\xba\xb4"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x101b, .address = 0x5167d0},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x121b, .address = 0x5167d9},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x141b, .address = 0x5167e2},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x161b, .address = 0x5167eb},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x181b, .address = 0x5167f4},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xa1d, .address = 0x5167fd},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xa19, .address = 0x516806},
    {.kind = K::Dress, .address = 0x51680f},
};

// 0x51681c: el tipo 248.
constexpr MonsterStep Case51681c[]{
    {.kind = K::LoadNpc, .value = 0x1cc, .address = 0x516821},
    {.kind = K::Create, .value = 0x1cc, .extra = 0x0, .address = 0x516836},
    {.kind = K::Name, .extra = 0x6c895c, .address = 0x51685f, .text = "\xb6\xb0\xb5\xb9\xc0\xcc \xbb\xf3\xc0\xce"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1e5, .address = 0x516868},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1e7, .address = 0x516871},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x1e9, .address = 0x51687a},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x1eb, .address = 0x516883},
};

// 0x516891: el tipo 249.
constexpr MonsterStep Case516891[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x5168a1},
    {.kind = K::Name, .extra = 0x6c8968, .address = 0x5168c8, .text = "\xb0\xe6\xba\xf1\xba\xb4"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x101b, .address = 0x5168d1},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x121b, .address = 0x5168da},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x141b, .address = 0x5168e3},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x161b, .address = 0x5168ec},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x181b, .address = 0x5168f5},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x819, .address = 0x5168fe},
    {.kind = K::Dress, .address = 0x516907},
};

// 0x516914: el tipo 250.
constexpr MonsterStep Case516914[]{
    {.kind = K::LoadNpc, .value = 0x1cc, .address = 0x516919},
    {.kind = K::Create, .value = 0x1cc, .extra = 0x0, .address = 0x51692e},
    {.kind = K::Name, .extra = 0x6c895c, .address = 0x516957, .text = "\xb6\xb0\xb5\xb9\xc0\xcc \xbb\xf3\xc0\xce"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1e4, .address = 0x516960},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1e6, .address = 0x516969},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x1e8, .address = 0x516972},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x1ea, .address = 0x51697b},
};

// 0x516989: el tipo 251.
constexpr MonsterStep Case516989[]{
    {.kind = K::LoadNpc, .value = 0x1ce, .address = 0x51698e},
    {.kind = K::Create, .value = 0x1ce, .extra = 0x0, .address = 0x5169a3},
    {.kind = K::Name,
     .extra = 0x6c894c,
     .address = 0x5169cc,
     .text = "\xb4\xeb\xc0\xe5\xc0\xe5\xc0\xcc \xc7\xd1\xbd\xba"},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f733333, .address = 0x5169d5},
};

// 0x5169e1: el tipo 253.
constexpr MonsterStep Case5169e1[]{
    {.kind = K::LoadNpc, .value = 0x1cd, .address = 0x5169e6},
    {.kind = K::Create, .value = 0x1cd, .extra = 0x0, .address = 0x5169fb},
    {.kind = K::Name,
     .extra = 0x6c893c,
     .address = 0x516a24,
     .text = "\xb9\xb0\xbe\xe0\xc6\xc4\xb4\xc2 \xbc\xd2\xb3\xe0"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1ec, .address = 0x516a2d},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1ee, .address = 0x516a36},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x1f0, .address = 0x516a3f},
};

// 0x516a4d: el tipo 254.
constexpr MonsterStep Case516a4d[]{
    {.kind = K::LoadNpc, .value = 0x1cf, .address = 0x516a52},
    {.kind = K::Create, .value = 0x1cf, .extra = 0x0, .address = 0x516a67},
    {.kind = K::Name, .extra = 0x6c8930, .address = 0x516a90, .text = "\xb8\xb6\xb9\xfd\xbb\xe7 \xc6\xc4\xbd\xc3"},
};

// 0x516a9e: el tipo 255.
constexpr MonsterStep Case516a9e[]{
    {.kind = K::LoadNpc, .value = 0x1cb, .address = 0x516aa3},
    {.kind = K::Create, .value = 0x1cb, .extra = 0x0, .address = 0x516ab8},
    {.kind = K::Name,
     .extra = 0x6c8920,
     .address = 0x516ae1,
     .text = "\xbc\xfa\xc1\xfd\xb8\xb6\xb4\xe3 \xb8\xae\xbe\xc6\xb8\xd5"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1db, .address = 0x516aea},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1dd, .address = 0x516af3},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x1df, .address = 0x516afc},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x1e3, .address = 0x516b05},
};

// 0x51651d: el tipo 256.
constexpr MonsterStep Case51651d[]{
    {.kind = K::LoadNpc, .value = 0x200, .address = 0x516522},
    {.kind = K::Create, .value = 0x200, .extra = 0x0, .address = 0x516537},
    {.kind = K::Name, .extra = 0x6c89ac, .address = 0x516560, .text = "\xbc\xbc\xb8\xa3\xba\xf1\xbd\xba"},
};

// 0x51656e: el tipo 257.
constexpr MonsterStep Case51656e[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x51657e},
    {.kind = K::Box, .address = 0x516586},
    {.kind = K::Name, .extra = 0x6c89a4, .address = 0x5165ae, .text = "\xc6\xe4\xc0\xcc\xbe\xc6"},
    {.kind = K::Joint, .offset = 42, .value = 0x60f, .extra = 0x41700000, .address = 0x5165d1},
};

// 0x516da1: el tipo 258.
constexpr MonsterStep Case516da1[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x516db1},
    {.kind = K::Name, .extra = 0x6c8968, .address = 0x516dd8, .text = "\xb0\xe6\xba\xf1\xba\xb4"},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x101b, .address = 0x516de1},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x121b, .address = 0x516dea},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x141b, .address = 0x516df3},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x161b, .address = 0x516dfc},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x181b, .address = 0x516e05},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x516e0e},
    {.kind = K::Dress, .address = 0x516e17},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x102, .address = 0x516e22},
};

// 0x43a0f4: el tipo 275.
constexpr MonsterStep Event439df0Case43a0f4[]{
    {.kind = K::LoadMonster, .value = 0x40, .address = 0x43a0f6},
    {.kind = K::Create, .value = 0x192, .extra = 0x0, .address = 0x43a10d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xc1d, .address = 0x43a121},
    {.kind = K::Write, .size = 1, .offset = 0x2da, .value = 0x0, .address = 0x43a12a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x40000000, .address = 0x43a130},
    {.kind = K::Name, .extra = 0x6c747c, .address = 0x43a14c, .text = "\xc1\xf8\xc2\xa5\xc4\xef\xb5\xd0"},
    {.kind = K::Write, .size = 4, .offset = 0x60, .value = 0x64, .address = 0x43a155},
};

// 0x42d460: el tipo 277.
constexpr MonsterStep Event42d070Case42d460[]{
    {.kind = K::LoadMonster, .value = 0x49, .address = 0x42d462},
    {.kind = K::Create, .value = 0x19b, .extra = 0x0, .address = 0x42d47a},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d484},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d48e},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d495},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d64d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d654},
};

// 0x42d4a1: el tipo 278.
constexpr MonsterStep Event42d070Case42d4a1[]{
    {.kind = K::LoadMonster, .value = 0x56, .address = 0x42d4a3},
    {.kind = K::Create, .value = 0x1a8, .extra = 0x0, .address = 0x42d4bb},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d4e6},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d4f0},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d4f7},
    {.kind = K::Write, .size = 4, .offset = 0x64, .value = 0x3, .address = 0x42d4fe},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d505},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d50c},
    {.kind = K::Arrival, .offset = 0, .value = 0x134, .address = 0x42d513},
};

// 0x42d522: el tipo 283.
constexpr MonsterStep Event42d070Case42d522[]{
    {.kind = K::LoadMonster, .value = 0x4a, .address = 0x42d524},
    {.kind = K::Create, .value = 0x19c, .extra = 0x0, .address = 0x42d53c},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d549},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d553},
    {.kind = K::Write, .size = 4, .offset = 0x94, .value = 0x3f83d70a, .address = 0x42d55a},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d564},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d56b},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d572},
    {.kind = K::Siege, .address = 0x42d579},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x42d587},
    {.kind = K::Arrival, .offset = 0, .value = 0x134, .address = 0x42d5af},
};

// 0x42d5be: el tipo 285.
constexpr MonsterStep Event42d070Case42d5be[]{
    {.kind = K::LoadMonster, .value = 0x4a, .address = 0x42d5c0},
    {.kind = K::Create, .value = 0x19c, .extra = 0x0, .address = 0x42d47a},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d484},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d48e},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d495},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d64d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d654},
};

// 0x42d5d9: el tipo 286.
constexpr MonsterStep Event42d070Case42d5d9[]{
    {.kind = K::LoadMonster, .value = 0x4c, .address = 0x42d5db},
    {.kind = K::Create, .value = 0x19e, .extra = 0x0, .address = 0x42d5f3},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d5fd},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d607},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d64d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d654},
};

// 0x42d610: el tipo 287.
constexpr MonsterStep Event42d070Case42d610[]{
    {.kind = K::LoadMonster, .value = 0x4d, .address = 0x42d612},
    {.kind = K::Create, .value = 0x19f, .extra = 0x0, .address = 0x42d62a},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d634},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d63e},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x1e, .address = 0x42d645},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d64d},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d654},
};

// 0x42d660: el tipo 288.
constexpr MonsterStep Event42d070Case42d660[]{
    {.kind = K::LoadMonster, .value = 0x4f, .address = 0x42d662},
    {.kind = K::Create, .value = 0x1a1, .extra = 0x0, .address = 0x42d67a},
    {.kind = K::Write, .size = 4, .offset = 0x394, .value = 0x1, .address = 0x42d687},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x42d691},
    {.kind = K::Write, .size = 4, .offset = 0x94, .value = 0x3f851eb8, .address = 0x42d698},
    {.kind = K::Write, .size = 1, .offset = 0x92, .value = 0x0, .address = 0x42d6a2},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42d6a9},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42d6b0},
    {.kind = K::Siege, .address = 0x42d6b7},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xfffffffe, .address = 0x42d6cd},
    {.kind = K::Write, .size = 4, .offset = 0x58, .value = 0xffffffff, .address = 0x42d6d4},
};

// 0x44056b: el tipo 290.
constexpr MonsterStep Event440530Case44056b[]{
    {.kind = K::LoadMonster, .value = 0x51, .address = 0x44056d},
    {.kind = K::Create, .value = 0x1a3, .extra = 0x0, .address = 0x440585},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x440595},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x44059c},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4405a3},
    {.kind = K::BoneName, .value = 29, .address = 0x44061f, .text = "Monster81_EyeRight"},
    {.kind = K::BoneName, .value = 30, .address = 0x4406af, .text = "Monster81_EyeLeft"},
};

// 0x4406da: el tipo 291.
constexpr MonsterStep Event440530Case4406da[]{
    {.kind = K::LoadMonster, .value = 0x52, .address = 0x4406dc},
    {.kind = K::Create, .value = 0x1a4, .extra = 0x0, .address = 0x4406f4},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x440704},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x44070b},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x440712},
    {.kind = K::BoneName, .value = 45, .address = 0x44077c, .text = "Monster82_RHand"},
    {.kind = K::BoneName, .value = 34, .address = 0x440809, .text = "Monster82_LHand"},
    {.kind = K::BoneName, .value = 48, .address = 0x440899, .text = "Monster82_Eye"},
    {.kind = K::BoneName, .value = 46, .address = 0x440917, .text = "Monster82_Back"},
};

// 0x44093f: el tipo 292.
constexpr MonsterStep Event440530Case44093f[]{
    {.kind = K::LoadMonster, .value = 0x53, .address = 0x440941},
    {.kind = K::Create, .value = 0x1a5, .extra = 0x0, .address = 0x440959},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x440969},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x440970},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x440977},
    {.kind = K::BoneName, .value = 65, .address = 0x4409f3, .text = "Monster83_Belly"},
    {.kind = K::BoneName, .value = 62, .address = 0x440a71, .text = "Monster83_Tail"},
};

// 0x440a99: los tipos 293, 303.
constexpr MonsterStep Event440530Case440a99[]{
    {.kind = K::LoadMonster, .value = 0x54, .address = 0x440a9b},
    {.kind = K::Create, .value = 0x1a6, .extra = 0x0, .address = 0x440ab3},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x440ac3},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x440aca},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x440ad1},
    {.kind = K::BoneName, .value = 53, .address = 0x440b3b, .text = "Monster84_PoisonTop"},
    {.kind = K::BoneName, .value = 54, .address = 0x440bc8, .text = "Monster84_PoisonRight"},
    {.kind = K::BoneName, .value = 55, .address = 0x440c58, .text = "Monster84_PoisonLeft"},
    {.kind = K::BoneName, .value = 37, .address = 0x440ce8, .text = "Monster84_LeftHand"},
    {.kind = K::BoneName, .value = 50, .address = 0x440d78, .text = "Monster84_RightHand"},
};

// 0x440da3: los tipos 294, 302.
constexpr MonsterStep Event440530Case440da3[]{
    {.kind = K::LoadMonster, .value = 0x55, .address = 0x440da5},
    {.kind = K::Create, .value = 0x1a7, .extra = 0x0, .address = 0x440dbd},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f333333, .address = 0x440dd5},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x440ddc},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x440de3},
    {.kind = K::BoneName, .value = 18, .address = 0x440e57, .text = "Monster85_LeftEye"},
    {.kind = K::BoneName, .value = 19, .address = 0x440ee7, .text = "Monster85_RightEye"},
};

// 0x440f12: el tipo 295.
constexpr MonsterStep Event440530Case440f12[]{
    {.kind = K::LoadMonster, .value = 0x57, .address = 0x440f14},
    {.kind = K::Create, .value = 0x1a9, .extra = 0x0, .address = 0x440f2c},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x40000000, .address = 0x440f41},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x440f48},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x440f4f},
    {.kind = K::BoneName, .value = 8, .address = 0x440fb3, .text = "Monster87_LeftEye"},
    {.kind = K::BoneName, .value = 9, .address = 0x44102f, .text = "Monster87_RightEye"},
    {.kind = K::BoneName, .value = 16, .address = 0x4410cd, .text = "Monster87_LeftHand"},
    {.kind = K::Sound, .value = 0x20a, .address = 0x4410eb},
};

// 0x441106: el tipo 297.
constexpr MonsterStep Event440530Case441106[]{
    {.kind = K::Create, .value = 0x20a, .extra = 0x0, .address = 0x441119},
    {.kind = K::Name,
     .extra = 0x6c74d8,
     .address = 0x441140,
     .text = "\xc0\xfa\xc1\xd6\xb9\xde\xc0\xba \xb1\xe2\xbb\xe7"},
    {.kind = K::Write, .size = 1, .offset = 0x1d5, .value = 0x1, .address = 0x44114b},
    {.kind = K::Write, .size = 2, .offset = 0x218, .value = 0x1013, .address = 0x441152},
    {.kind = K::Write, .size = 2, .offset = 0x238, .value = 0x1213, .address = 0x44115b},
    {.kind = K::Write, .size = 2, .offset = 0x258, .value = 0x1413, .address = 0x441164},
    {.kind = K::Write, .size = 2, .offset = 0x278, .value = 0x1613, .address = 0x44116d},
    {.kind = K::Write, .size = 2, .offset = 0x298, .value = 0x1813, .address = 0x441176},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0x222, .address = 0x44117f},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xe1f, .address = 0x441188},
    {.kind = K::Write, .size = 1, .offset = 0x21a, .value = 0x9, .address = 0x441191},
    {.kind = K::Write, .size = 1, .offset = 0x23a, .value = 0x9, .address = 0x441197},
    {.kind = K::Write, .size = 1, .offset = 0x25a, .value = 0x9, .address = 0x44119d},
    {.kind = K::Write, .size = 1, .offset = 0x27a, .value = 0x9, .address = 0x4411a3},
    {.kind = K::Write, .size = 1, .offset = 0x29a, .value = 0x9, .address = 0x4411a9},
    {.kind = K::Write, .size = 1, .offset = 0x375, .value = 0x6, .address = 0x4411af},
    {.kind = K::Dress, .address = 0x4411b6},
};

// 0x425deb: el tipo 304.
constexpr MonsterStep Event425db0Case425deb[]{
    {.kind = K::LoadMonster, .value = 0x64, .address = 0x425ded},
    {.kind = K::Create, .value = 0x1b6, .extra = 0x0, .address = 0x425e05},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fb33333, .address = 0x425e15},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x425e1c},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x425e23},
    {.kind = K::BoneName, .value = 0, .address = 0x425e6d, .text = "Monster100_Footstepst"},
    {.kind = K::BoneName, .value = 76, .address = 0x425ecb, .text = "Monster100_L_Hand"},
    {.kind = K::BoneName, .value = 94, .address = 0x425f29, .text = "Monster100_R_Hand"},
    {.kind = K::BoneName, .value = 2, .address = 0x425f87, .text = "Monster100_Pelvis"},
    {.kind = K::BoneName, .value = 57, .address = 0x425fe5, .text = "Monster100_Head"},
    {.kind = K::BoneName, .value = 107, .address = 0x426043, .text = "Monster100_z02"},
    {.kind = K::BoneName, .value = 108, .address = 0x4260a1, .text = "Monster100_z03"},
    {.kind = K::BoneName, .value = 109, .address = 0x4260ff, .text = "Monster100_z04"},
    {.kind = K::BoneName, .value = 110, .address = 0x42615d, .text = "Monster100_z05"},
};

// 0x426188: el tipo 305.
constexpr MonsterStep Event425db0Case426188[]{
    {.kind = K::LoadMonster, .value = 0x65, .address = 0x42618a},
    {.kind = K::Create, .value = 0x1b7, .extra = 0x0, .address = 0x4261a2},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3faccccd, .address = 0x4261b2},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4261b9},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4261c0},
    {.kind = K::BoneName, .value = 12, .address = 0x42620a, .text = "Monster101_L_Arm"},
    {.kind = K::BoneName, .value = 20, .address = 0x426268, .text = "Monster101_R_Arm"},
    {.kind = K::BoneName, .value = 6, .address = 0x4262f8, .text = "Monster101_Head"},
};

// 0x426323: el tipo 306.
constexpr MonsterStep Event425db0Case426323[]{
    {.kind = K::LoadMonster, .value = 0x66, .address = 0x426325},
    {.kind = K::Create, .value = 0x1b8, .extra = 0x0, .address = 0x42633d},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f8ccccd, .address = 0x42634d},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x426354},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42635b},
    {.kind = K::BoneName, .value = 0, .address = 0x4263c5, .text = "Monster102_Footstepst"},
    {.kind = K::BoneName, .value = 6, .address = 0x426452, .text = "Monster102_Head"},
};

// 0x42647d: el tipo 307.
constexpr MonsterStep Event425db0Case42647d[]{
    {.kind = K::LoadMonster, .value = 0x67, .address = 0x42647f},
    {.kind = K::Create, .value = 0x1b9, .extra = 0x0, .address = 0x426497},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4264a4},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4264ab},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f933333, .address = 0x4264b3},
};

// 0x4264cc: el tipo 308.
constexpr MonsterStep Event425db0Case4264cc[]{
    {.kind = K::LoadMonster, .value = 0x68, .address = 0x4264ce},
    {.kind = K::Create, .value = 0x1ba, .extra = 0x0, .address = 0x4264e6},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x4264f6},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4264fd},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x426504},
    {.kind = K::BoneName, .value = 37, .address = 0x42656e, .text = "Monster104_Horn0"},
    {.kind = K::BoneName, .value = 38, .address = 0x4265fb, .text = "Monster104_Horn1"},
    {.kind = K::BoneName, .value = 39, .address = 0x426679, .text = "Monster104_Horn2"},
    {.kind = K::BoneName, .value = 40, .address = 0x426706, .text = "Monster104_Horn3"},
    {.kind = K::BoneName, .value = 44, .address = 0x426796, .text = "Monster104_Horn4"},
    {.kind = K::BoneName, .value = 45, .address = 0x426826, .text = "Monster104_Horn5"},
    {.kind = K::BoneName, .value = 1, .address = 0x4268b6, .text = "Monster104_Footsteps"},
};

// 0x4268e1: el tipo 309.
constexpr MonsterStep Event425db0Case4268e1[]{
    {.kind = K::LoadMonster, .value = 0x69, .address = 0x4268e3},
    {.kind = K::Create, .value = 0x1bb, .extra = 0x0, .address = 0x4268fb},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fe66666, .address = 0x426913},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42691a},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x426921},
    {.kind = K::BoneName, .value = 9, .address = 0x426995, .text = "Monster105_R_Eye"},
    {.kind = K::BoneName, .value = 10, .address = 0x426a25, .text = "Monster105_L_Eye"},
    {.kind = K::BoneName, .value = 33, .address = 0x426ab5, .text = "Monster105_L_Arm00"},
    {.kind = K::BoneName, .value = 34, .address = 0x426b33, .text = "Monster105_L_Arm01"},
    {.kind = K::BoneName, .value = 35, .address = 0x426bae, .text = "Monster105_L_Arm02"},
    {.kind = K::BoneName, .value = 20, .address = 0x426c29, .text = "Monster105_L_Hand"},
    {.kind = K::BoneName, .value = 39, .address = 0x426c77, .text = "Monster105_R_Hand"},
};

// 0x430cee: el tipo 310.
constexpr MonsterStep Event430c90Case430cee[]{
    {.kind = K::LoadMonster, .value = 0x62, .address = 0x430cf0},
    {.kind = K::Create, .value = 0x1b4, .extra = 0x0, .address = 0x430d07},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x430d11},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4310db},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4310e2},
};

// 0x42f4d7: el tipo 310.
constexpr MonsterStep Event42f190Case42f4d7[]{
    {.kind = K::LoadMonster, .value = 0x5b, .address = 0x42f4d9},
    {.kind = K::Create, .value = 0x1ad, .extra = 0x0, .address = 0x42f4f0},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa00000, .address = 0x42f4fa},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42f504},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42f50b},
};

// 0x430d1d: el tipo 311.
constexpr MonsterStep Event430c90Case430d1d[]{
    {.kind = K::LoadMonster, .value = 0x61, .address = 0x430d1f},
    {.kind = K::Create, .value = 0x1b3, .extra = 0x0, .address = 0x430d07},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x430d11},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4310db},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4310e2},
};

// 0x430d34: el tipo 312.
constexpr MonsterStep Event430c90Case430d34[]{
    {.kind = K::LoadMonster, .value = 0x60, .address = 0x430d36},
    {.kind = K::Create, .value = 0x1b2, .extra = 0x0, .address = 0x430d4d},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x430d61},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x430d68},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x430d6f},
    {.kind = K::BoneName, .value = 27, .address = 0x430da2, .text = "Monster96_Top"},
    {.kind = K::BoneName, .value = 28, .address = 0x430e18, .text = "Monster96_Center"},
    {.kind = K::BoneName, .value = 29, .address = 0x430e8e, .text = "Monster96_Bottom"},
};

// 0x430ece: el tipo 313.
constexpr MonsterStep Event430c90Case430ece[]{
    {.kind = K::LoadMonster, .value = 0x5f, .address = 0x430ed0},
    {.kind = K::Create, .value = 0x1b1, .extra = 0x0, .address = 0x430ee7},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa00000, .address = 0x430efb},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x0, .address = 0x430f02},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x430f05},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x430f0c},
    {.kind = K::BoneName, .value = 6, .address = 0x430f70, .text = "Monster95_Head"},
};

// 0x430f80: el tipo 314.
constexpr MonsterStep Event430c90Case430f80[]{
    {.kind = K::LoadMonster, .value = 0x62, .address = 0x430f82},
    {.kind = K::Create, .value = 0x1b4, .extra = 0x0, .address = 0x430f99},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fcccccd, .address = 0x430fa3},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4310db},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4310e2},
};

// 0x430faf: el tipo 315.
constexpr MonsterStep Event430c90Case430faf[]{
    {.kind = K::LoadMonster, .value = 0x5f, .address = 0x430fb1},
    {.kind = K::Create, .value = 0x1b1, .extra = 0x0, .address = 0x430fc8},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x430fd8},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x430fdf},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fd33333, .address = 0x430fe9},
    {.kind = K::Write, .size = 4, .offset = 0x4, .value = 0x1, .address = 0x430ff0},
    {.kind = K::BoneName, .value = 6, .address = 0x431055, .text = "Monster95_Head"},
};

// 0x42f1e3: el tipo 315.
constexpr MonsterStep Event42f190Case42f1e3[]{
    {.kind = K::LoadMonster, .value = 0x5f, .address = 0x42f1e5},
    {.kind = K::Create, .value = 0x1b1, .extra = 0x0, .address = 0x42f1fc},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa00000, .address = 0x42f20c},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42f213},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42f21a},
    {.kind = K::BoneName, .value = 6, .address = 0x42f280, .text = "Monster95_Head"},
};

// 0x431081: los tipos 316, 344.
constexpr MonsterStep Event430c90Case431081[]{
    {.kind = K::LoadMonster, .value = 0x5b, .address = 0x431083},
    {.kind = K::Create, .value = 0x1ad, .extra = 0x0, .address = 0x43109a},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa00000, .address = 0x4310a4},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4310db},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4310e2},
};

// 0x42f2ac: el tipo 316.
constexpr MonsterStep Event42f190Case42f2ac[]{
    {.kind = K::LoadMonster, .value = 0x60, .address = 0x42f2ae},
    {.kind = K::Create, .value = 0x1b2, .extra = 0x0, .address = 0x42f2c5},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42f2d5},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42f2dc},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x42f2e6},
    {.kind = K::BoneName, .value = 27, .address = 0x42f34d, .text = "Monster96_Top"},
    {.kind = K::BoneName, .value = 28, .address = 0x42f3a0, .text = "Monster96_Center"},
    {.kind = K::BoneName, .value = 29, .address = 0x42f440, .text = "Monster96_Bottom"},
};

// 0x4310ad: los tipos 317, 341.
constexpr MonsterStep Event430c90Case4310ad[]{
    {.kind = K::LoadMonster, .value = 0x5e, .address = 0x4310af},
    {.kind = K::Create, .value = 0x1b0, .extra = 0x0, .address = 0x4310c6},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa66666, .address = 0x4310d0},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4310db},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4310e2},
};

// 0x42f494: el tipo 317.
constexpr MonsterStep Event42f190Case42f494[]{
    {.kind = K::LoadMonster, .value = 0x61, .address = 0x42f496},
    {.kind = K::Create, .value = 0x1b3, .extra = 0x0, .address = 0x42f4c4},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x42f4ce},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42f504},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42f50b},
};

// 0x42f4ab: el tipo 318.
constexpr MonsterStep Event42f190Case42f4ab[]{
    {.kind = K::LoadMonster, .value = 0x62, .address = 0x42f4ad},
    {.kind = K::Create, .value = 0x1b4, .extra = 0x0, .address = 0x42f4c4},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f99999a, .address = 0x42f4ce},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x42f504},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x42f50b},
};

// 0x4312d7: el tipo 340.
constexpr MonsterStep Event430c90Case4312d7[]{
    {.kind = K::LoadMonster, .value = 0x5c, .address = 0x4312d9},
    {.kind = K::Create, .value = 0x1ae, .extra = 0x0, .address = 0x4312f0},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fc00000, .address = 0x431300},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x431307},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x43130e},
    {.kind = K::BoneName, .value = 17, .address = 0x431376, .text = "Left_Hand"},
};

// 0x431189: el tipo 345.
constexpr MonsterStep Event430c90Case431189[]{
    {.kind = K::LoadMonster, .value = 0x5d, .address = 0x43118b},
    {.kind = K::Create, .value = 0x1af, .extra = 0x0, .address = 0x4311a2},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3fa00000, .address = 0x4311b4},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4311bb},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4311c2},
    {.kind = K::BoneName, .value = 27, .address = 0x4311fb, .text = "Monster94_zx"},
    {.kind = K::BoneName, .value = 28, .address = 0x431280, .text = "Monster94_zx01"},
};

// 0x4313a2: el tipo 348.
constexpr MonsterStep Event430c90Case4313a2[]{
    {.kind = K::LoadMonster, .value = 0x63, .address = 0x4313a4},
    {.kind = K::Create, .value = 0x1b5, .extra = 0x43340000, .address = 0x4313bf},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x3f800000, .address = 0x4313cf},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x4313d6},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x4313dd},
    {.kind = K::Write,
     .when = MonsterStepWhen::AtEitherX,
     .size = 4,
     .low = 62,
     .high = 183,
     .offset = 0x24,
     .value = 0x42b40000,
     .address = 0x4313ee},
};

// 0x4310fb: el tipo 349.
constexpr MonsterStep Event430c90Case4310fb[]{
    {.kind = K::LoadMonster, .value = 0x59, .address = 0x4310fd},
    {.kind = K::Create, .value = 0x1ab, .extra = 0x0, .address = 0x431114},
    {.kind = K::Write, .size = 4, .offset = 0xc, .value = 0x40000000, .address = 0x431138},
    {.kind = K::Write, .size = 2, .offset = 0x2b8, .value = 0xffff, .address = 0x43113f},
    {.kind = K::Write, .size = 2, .offset = 0x2d8, .value = 0xffff, .address = 0x431146},
    {.kind = K::Joint, .offset = 2, .value = 0x618, .extra = 0x41f00000, .address = 0x43114d},
    {.kind = K::Joint, .offset = 3, .value = 0x618, .extra = 0x41f00000, .address = 0x43116e},
};

// 0x51430e: el switch de 0x5141e0 por tipo − 1 (de 1 a 301), con el default (0x516e2a) en el 0, el 4 y el 8
// y en los tipos que el switch no lista.
constexpr std::array<MonsterKind, 197> MainKinds{{
    {0, 0x516e2a, Case516e2a},    {1, 0x515be6, Case515be6},     {2, 0x515cb0, Case515cb0},
    {3, 0x515a1f, Case515a1f},    {4, 0x516e2a, Case516e2a_2},   {5, 0x515be6, Case515be6_2},
    {6, 0x515d6c, Case515d6c},    {7, 0x515e0c, Case515e0c},     {8, 0x516e2a, Case516e2a_3},
    {9, 0x515d6c, Case515d6c_2},  {10, 0x515d05, Case515d05},    {11, 0x515acb, Case515acb},
    {12, 0x515b33, Case515b33},   {13, 0x515b88, Case515b88},    {14, 0x515e74, Case515e74},
    {15, 0x516033, Case516033},   {16, 0x51608e, Case51608e},    {17, 0x515a74, Case515a74},
    {18, 0x5159b3, Case5159b3},   {19, 0x51595e, Case51595e},    {20, 0x515909, Case515909},
    {21, 0x5158b4, Case5158b4},   {22, 0x515858, Case515858},    {23, 0x5157f1, Case5157f1},
    {24, 0x5157a3, Case5157a3},   {25, 0x515727, Case515727},    {26, 0x5156c9, Case5156c9},
    {27, 0x515674, Case515674},   {28, 0x51560f, Case51560f},    {29, 0x5155b1, Case5155b1},
    {30, 0x51555c, Case51555c},   {31, 0x5154f4, Case5154f4},    {32, 0x5154a6, Case5154a6},
    {33, 0x515436, Case515436},   {34, 0x51536b, Case51536b},    {35, 0x5152ec, Case5152ec},
    {36, 0x515297, Case515297},   {37, 0x515242, Case515242},    {38, 0x5151dd, Case5151dd},
    {39, 0x51517f, Case51517f},   {40, 0x515121, Case515121},    {41, 0x5150c3, Case5150c3},
    {42, 0x515050, Case515050},   {43, 0x514ffb, Case514ffb},    {44, 0x514fa6, Case514fa6},
    {45, 0x514f2a, Case514f2a},   {46, 0x514f03, Case514f03},    {47, 0x514ec5, Case514ec5},
    {48, 0x514e95, Case514e95},   {49, 0x514e60, Case514e60},    {50, 0x514e39, Case514e39},
    {51, 0x514e09, Case514e09},   {52, 0x514dd9, Case514dd9},    {53, 0x515f23, Case515f23},
    {54, 0x515fc9, Case515fc9},   {55, 0x515e74, Case515e74_2},  {56, 0x515e74, Case515e74_3},
    {57, 0x514d66, Case514d66},   {58, 0x514cc4, Case514cc4},    {59, 0x514cc4, Case514cc4_2},
    {60, 0x514c5a, Case514c5a},   {61, 0x514bd1, Case514bd1},    {62, 0x514b67, Case514b67},
    {63, 0x514bd1, Case514bd1_2}, {64, 0x514a96, Case514a96},    {65, 0x514b12, Case514b12},
    {66, 0x514b40, Case514b40},   {67, 0x5151dd, Case5151dd},    {68, 0x514818, Case514818},
    {69, 0x51483f, Case51483f},   {70, 0x51486d, Case51486d},    {71, 0x5148a9, Case5148a9},
    {72, 0x514940, Case514940},   {73, 0x514977, Case514977},    {74, 0x5148a9, Case5148a9_2},
    {75, 0x514977, Case514977_2}, {77, 0x514a51, Case514a51},    {78, 0x5146f9, Case5146f9},
    {79, 0x514fa6, Case514fa6},   {80, 0x51472f, Case51472f},    {81, 0x514f03, Case514f03},
    {82, 0x514766, Case514766},   {83, 0x51479e, Case51479e},    {84, 0x5145a6, Case5145a6},
    {85, 0x5145cd, Case5145cd},   {86, 0x514604, Case514604},    {87, 0x51465d, Case51465d},
    {88, 0x514695, Case514695},   {89, 0x5144cc, Case5144cc},    {90, 0x5145a6, Case5145a6},
    {91, 0x5145cd, Case5145cd},   {92, 0x514604, Case514604},    {93, 0x51465d, Case51465d},
    {94, 0x514695, Case514695},   {95, 0x5144cc, Case5144cc},    {96, 0x5145a6, Case5145a6},
    {97, 0x5145cd, Case5145cd},   {98, 0x514604, Case514604},    {99, 0x51465d, Case51465d},
    {100, 0x516105, Case516105},  {101, 0x51611b, Case51611b},   {102, 0x516131, Case516131},
    {103, 0x516147, Case516147},  {111, 0x514695, Case514695},   {112, 0x5144cc, Case5144cc},
    {113, 0x5145a6, Case5145a6},  {114, 0x5145cd, Case5145cd},   {115, 0x514604, Case514604},
    {116, 0x51465d, Case51465d},  {117, 0x514695, Case514695},   {118, 0x5144cc, Case5144cc},
    {119, 0x5145a6, Case5145a6},  {120, 0x5145cd, Case5145cd},   {121, 0x514604, Case514604},
    {122, 0x51465d, Case51465d},  {123, 0x514695, Case514695},   {124, 0x5144cc, Case5144cc},
    {125, 0x5145a6, Case5145a6},  {126, 0x5145cd, Case5145cd},   {127, 0x514604, Case514604},
    {128, 0x51465d, Case51465d},  {129, 0x514695, Case514695},   {130, 0x5144cc, Case5144cc},
    {131, 0x514514, Case514514},  {132, 0x51455d, Case51455d},   {133, 0x51455d, Case51455d},
    {134, 0x51455d, Case51455d},  {135, 0x514b40, Case514b40},   {136, 0x514b12, Case514b12},
    {137, 0x514ad4, Case514ad4},  {138, 0x5145a6, Case5145a6},   {139, 0x5145cd, Case5145cd},
    {140, 0x514604, Case514604},  {141, 0x51465d, Case51465d},   {142, 0x514695, Case514695},
    {143, 0x5144cc, Case5144cc},  {150, 0x514f51, Case514f51},   {151, 0x515fc9, Case515fc9_2},
    {152, 0x514390, Case514390},  {153, 0x514390, Case514390},   {154, 0x514390, Case514390},
    {155, 0x514390, Case514390},  {156, 0x514390, Case514390},   {157, 0x514390, Case514390},
    {158, 0x514390, Case514390},  {162, 0x5143f7, Case5143f7},   {163, 0x51443f, Case51443f},
    {164, 0x5143f7, Case5143f7},  {165, 0x51443f, Case51443f},   {166, 0x5143f7, Case5143f7},
    {167, 0x51443f, Case51443f},  {168, 0x5143f7, Case5143f7},   {169, 0x51443f, Case51443f},
    {170, 0x5143f7, Case5143f7},  {171, 0x51443f, Case51443f},   {172, 0x5143f7, Case5143f7},
    {173, 0x51443f, Case51443f},  {200, 0x51615d, Case51615d},   {204, 0x516b13, Case516b13},
    {205, 0x516b67, Case516b67},  {206, 0x516bd9, Case516bd9},   {207, 0x516c4b, Case516c4b},
    {208, 0x516cbd, Case516cbd},  {209, 0x516d2f, Case516d2f},   {224, 0x514323, Case514323},
    {226, 0x51618d, Case51618d},  {229, 0x5161de, Case5161de},   {230, 0x516291, Case516291},
    {231, 0x516306, Case516306},  {232, 0x516357, Case516357},   {233, 0x51636c, Case51636c},
    {234, 0x516381, Case516381},  {235, 0x5163c7, Case5163c7},   {236, 0x5163f8, Case5163f8},
    {237, 0x516439, Case516439},  {238, 0x51645c, Case51645c},   {239, 0x516486, Case516486},
    {240, 0x5164a9, Case5164a9},  {241, 0x5164cc, Case5164cc},   {242, 0x5165de, Case5165de},
    {243, 0x51664c, Case51664c},  {244, 0x51669d, Case51669d},   {245, 0x5166ee, Case5166ee},
    {246, 0x51673f, Case51673f},  {247, 0x516790, Case516790},   {248, 0x51681c, Case51681c},
    {249, 0x516891, Case516891},  {250, 0x516914, Case516914},   {251, 0x516989, Case516989},
    {253, 0x5169e1, Case5169e1},  {254, 0x516a4d, Case516a4d},   {255, 0x516a9e, Case516a9e},
    {256, 0x51651d, Case51651d},  {257, 0x51656e, Case51656e},   {258, 0x516da1, Case516da1},
    {300, 0x514b67, Case514b67},  {301, 0x5148a9, Case5148a9_2},
}};
constexpr MonsterKind DefaultKind{-1, 0x516e2a, DefaultSteps};

// Los seis creadores de evento que 0x5141e0 prueba antes del switch, en su orden.
constexpr std::array<EventMonsterKind, 108> EventKinds{{
    {0x439df0, -1, {144, 0x439e1e, Event439df0Case439e1e}}, {0x439df0, -1, {145, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {146, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {147, 0x439f0d, Event439df0Case439f0d}},
    {0x439df0, -1, {148, 0x439f5e, Event439df0Case439f5e}}, {0x439df0, -1, {149, 0x439fab, Event439df0Case439fab}},
    {0x439df0, -1, {160, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {161, 0x43a075, Event439df0Case43a075}},
    {0x439df0, -1, {174, 0x439e1e, Event439df0Case439e1e}}, {0x439df0, -1, {175, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {176, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {177, 0x439f0d, Event439df0Case439f0d}},
    {0x439df0, -1, {178, 0x439f5e, Event439df0Case439f5e}}, {0x439df0, -1, {179, 0x439fab, Event439df0Case439fab}},
    {0x439df0, -1, {180, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {181, 0x43a075, Event439df0Case43a075}},
    {0x439df0, -1, {182, 0x439e1e, Event439df0Case439e1e}}, {0x439df0, -1, {183, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {184, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {185, 0x439f0d, Event439df0Case439f0d}},
    {0x439df0, -1, {186, 0x439f5e, Event439df0Case439f5e}}, {0x439df0, -1, {187, 0x439fab, Event439df0Case439fab}},
    {0x439df0, -1, {188, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {189, 0x43a075, Event439df0Case43a075}},
    {0x439df0, -1, {190, 0x439e1e, Event439df0Case439e1e}}, {0x439df0, -1, {191, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {192, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {193, 0x439f0d, Event439df0Case439f0d}},
    {0x439df0, -1, {194, 0x439f5e, Event439df0Case439f5e}}, {0x439df0, -1, {195, 0x439fab, Event439df0Case439fab}},
    {0x439df0, -1, {196, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {197, 0x43a075, Event439df0Case43a075}},
    {0x439df0, -1, {260, 0x439e1e, Event439df0Case439e1e}}, {0x439df0, -1, {261, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {262, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {263, 0x439f0d, Event439df0Case439f0d}},
    {0x439df0, -1, {264, 0x439f5e, Event439df0Case439f5e}}, {0x439df0, -1, {265, 0x439fab, Event439df0Case439fab}},
    {0x439df0, -1, {266, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {267, 0x43a075, Event439df0Case43a075}},
    {0x439df0, -1, {268, 0x439e1e, Event439df0Case439e1e}}, {0x439df0, -1, {269, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {270, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {271, 0x439f0d, Event439df0Case439f0d}},
    {0x439df0, -1, {272, 0x439f5e, Event439df0Case439f5e}}, {0x439df0, -1, {273, 0x439fab, Event439df0Case439fab}},
    {0x439df0, -1, {274, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {275, 0x43a0f4, Event439df0Case43a0f4}},
    {0x439df0, -1, {331, 0x439f0d, Event439df0Case439f0d}}, {0x439df0, -1, {332, 0x439f5e, Event439df0Case439f5e}},
    {0x439df0, -1, {333, 0x439ec3, Event439df0Case439ec3}}, {0x439df0, -1, {334, 0x439e1e, Event439df0Case439e1e}},
    {0x439df0, -1, {335, 0x439fab, Event439df0Case439fab}}, {0x439df0, -1, {336, 0x439e6f, Event439df0Case439e6f}},
    {0x439df0, -1, {337, 0x439ffd, Event439df0Case439ffd}}, {0x439df0, -1, {338, 0x43a075, Event439df0Case43a075}},
    {0x42d070, 30, {104, 0x42d0ab, Event42d070Case42d0ab}}, {0x42d070, 30, {215, 0x42d0db, Event42d070Case42d0db}},
    {0x42d070, 30, {216, 0x42d149, Event42d070Case42d149}}, {0x42d070, 30, {217, 0x42d183, Event42d070Case42d183}},
    {0x42d070, 30, {218, 0x42d1d2, Event42d070Case42d1d2}}, {0x42d070, 30, {219, 0x42d264, Event42d070Case42d264}},
    {0x42d070, 30, {220, 0x42d2bd, Event42d070Case42d2bd}}, {0x42d070, 30, {221, 0x42d313, Event42d070Case42d313}},
    {0x42d070, 30, {222, 0x42d361, Event42d070Case42d361}}, {0x42d070, 30, {223, 0x42d37c, Event42d070Case42d37c}},
    {0x42d070, 30, {224, 0x42d3d2, Event42d070Case42d3d2}}, {0x42d070, 30, {277, 0x42d460, Event42d070Case42d460}},
    {0x42d070, 30, {278, 0x42d4a1, Event42d070Case42d4a1}}, {0x42d070, 30, {283, 0x42d522, Event42d070Case42d522}},
    {0x42d070, 30, {285, 0x42d5be, Event42d070Case42d5be}}, {0x42d070, 30, {286, 0x42d5d9, Event42d070Case42d5d9}},
    {0x42d070, 30, {287, 0x42d610, Event42d070Case42d610}}, {0x42d070, 30, {288, 0x42d660, Event42d070Case42d660}},
    {0x440530, -1, {290, 0x44056b, Event440530Case44056b}}, {0x440530, -1, {291, 0x4406da, Event440530Case4406da}},
    {0x440530, -1, {292, 0x44093f, Event440530Case44093f}}, {0x440530, -1, {293, 0x440a99, Event440530Case440a99}},
    {0x440530, -1, {294, 0x440da3, Event440530Case440da3}}, {0x440530, -1, {295, 0x440f12, Event440530Case440f12}},
    {0x440530, -1, {297, 0x441106, Event440530Case441106}}, {0x440530, -1, {302, 0x440da3, Event440530Case440da3}},
    {0x440530, -1, {303, 0x440a99, Event440530Case440a99}}, {0x42f190, 35, {310, 0x42f4d7, Event42f190Case42f4d7}},
    {0x42f190, 35, {315, 0x42f1e3, Event42f190Case42f1e3}}, {0x42f190, 35, {316, 0x42f2ac, Event42f190Case42f2ac}},
    {0x42f190, 35, {317, 0x42f494, Event42f190Case42f494}}, {0x42f190, 35, {318, 0x42f4ab, Event42f190Case42f4ab}},
    {0x430c90, 34, {310, 0x430cee, Event430c90Case430cee}}, {0x430c90, 34, {311, 0x430d1d, Event430c90Case430d1d}},
    {0x430c90, 34, {312, 0x430d34, Event430c90Case430d34}}, {0x430c90, 34, {313, 0x430ece, Event430c90Case430ece}},
    {0x430c90, 34, {314, 0x430f80, Event430c90Case430f80}}, {0x430c90, 34, {315, 0x430faf, Event430c90Case430faf}},
    {0x430c90, 34, {316, 0x431081, Event430c90Case431081}}, {0x430c90, 34, {317, 0x4310ad, Event430c90Case4310ad}},
    {0x430c90, 34, {340, 0x4312d7, Event430c90Case4312d7}}, {0x430c90, 34, {341, 0x4310ad, Event430c90Case4310ad}},
    {0x430c90, 34, {344, 0x431081, Event430c90Case431081}}, {0x430c90, 34, {345, 0x431189, Event430c90Case431189}},
    {0x430c90, 34, {348, 0x4313a2, Event430c90Case4313a2}}, {0x430c90, 34, {349, 0x4310fb, Event430c90Case4310fb}},
    {0x425db0, -1, {304, 0x425deb, Event425db0Case425deb}}, {0x425db0, -1, {305, 0x426188, Event425db0Case426188}},
    {0x425db0, -1, {306, 0x426323, Event425db0Case426323}}, {0x425db0, -1, {307, 0x42647d, Event425db0Case42647d}},
    {0x425db0, -1, {308, 0x4264cc, Event425db0Case4264cc}}, {0x425db0, -1, {309, 0x4268e1, Event425db0Case4268e1}},
}};
} // namespace

std::span<const MonsterKind> MainMonsterKinds() noexcept
{
    return MainKinds;
}

const MonsterKind& DefaultMonsterKind() noexcept
{
    return DefaultKind;
}

std::span<const EventMonsterKind> EventMonsterKinds() noexcept
{
    return EventKinds;
}
} // namespace Client::Scenes
