#include "Data/Models/ModelTextureCalls.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace Client::Data::Models
{
namespace
{
constexpr std::string_view PlayerFolder = "Player\\";
constexpr std::string_view ItemFolder = "Item\\";
constexpr std::string_view SkillFolder = "Skill\\";
constexpr std::string_view MonsterFolder = "Monster\\";
constexpr std::string_view NpcFolder = "Npc\\";
constexpr std::string_view BowFolder = "Item\\Bow\\";

// Las llamadas de un cargador, armadas en compilación con los lazos del decompilado: una de más no
// compila, y una de menos la ve el static_assert de la cuenta.
template <std::size_t Size> struct CallList
{
    std::array<ModelTextureCall, Size> calls{};
    std::size_t count = 0;

    constexpr void Add(int model, std::string_view folder)
    {
        calls[count] = ModelTextureCall{static_cast<std::uint16_t>(model), folder};
        ++count;
    }

    constexpr void Add(std::initializer_list<int> models, std::string_view folder)
    {
        for (const int model : models)
        {
            Add(model, folder);
        }
    }

    // De `first` a `last`, los dos incluidos.
    constexpr void AddRange(int first, int last, std::string_view folder)
    {
        for (int model = first; model <= last; ++model)
        {
            Add(model, folder);
        }
    }

    // Un lazo con varias llamadas por vuelta: para cada base de `first` a `last`, la base más cada
    // corrimiento, en su orden.
    constexpr void AddEach(int first, int last, std::initializer_list<int> offsets, std::string_view folder)
    {
        for (int base = first; base <= last; ++base)
        {
            for (const int offset : offsets)
            {
                Add(base + offset, folder);
            }
        }
    }
};

// Las partes de un ítem del cuerpo desde su pantalón: el casco (0x200 antes), la armadura, el
// pantalón, los guantes y las botas (grupos 7 a 11, ItemModel). Las de las clases van de a 8.
constexpr std::initializer_list<int> ItemParts = {-0x200, 0, 0x200, 0x400, 0x600};
constexpr std::initializer_list<int> ClassParts = {-8, 0, 8, 0x10, 0x18};

constexpr CallList<214> MakePlayerCalls()
{
    CallList<214> list;
    list.AddEach(0x221a, 0x221e, ClassParts, PlayerFolder); // 0x60f97f: las clases 1 a 5
    list.AddEach(0x1212, 0x1222, ItemParts, PlayerFolder);  // los ítems 0 a 16
    for (int base = 0x1227; base <= 0x122a; ++base)         // los 21 a 24, sin el casco 23
    {
        if (base != 0x1229)
        {
            list.Add(base - 0x200, PlayerFolder);
        }
        list.Add({base, base + 0x200, base + 0x400, base + 0x600}, PlayerFolder);
    }
    list.AddEach(0x122b, 0x122e, ItemParts, PlayerFolder);  // los 25 a 28
    list.AddEach(0x221f, 0x2221, ClassParts, PlayerFolder); // las clases 6 a 8
    list.AddEach(0x1223, 0x1226, ItemParts, PlayerFolder);  // los 17 a 20
    list.Add({0x429d, 0x42a2, 0x42a3, 0x42a5, 0x42a6, 0x20b}, PlayerFolder);
    list.AddEach(0x142f, 0x1433, {-0x200, 0, 0x200, 0x400}, PlayerFolder); // los 29 a 33, sin casco
    list.Add({0x102f, 0x1030, 0x1031, 0x1033}, PlayerFolder);              // y cuatro de sus cascos
    return list;
}

constexpr CallList<291> MakeItemCalls()
{
    CallList<291> list;
    list.AddRange(0x1c22, 0x1c24, ItemFolder); // 0x611886: i < 3 da 0x1c22 + i, y si no 0x2242 + i
    list.Add(0x2245, ItemFolder);
    list.AddRange(0x2246, 0x2249, ItemFolder);
    list.AddRange(0x1a15, 0x1a18, ItemFolder);
    list.Add(0x1c16, SkillFolder);
    list.Add(0x1c31, ItemFolder);
    list.Add(0x1c17, SkillFolder);
    list.Add({0x1e28, 0x1e31, 0x1e19, 0x1c19, 0x1c1d}, ItemFolder);
    list.Add(0x224c, MonsterFolder);
    list.AddRange(0x1c20, 0x1c21, ItemFolder);
    list.Add({0x223, 0x224, 0xc1b, 0xa23}, ItemFolder);
    list.Add(0x1c15, SkillFolder);
    list.AddRange(0x1e29, 0x1e2f, ItemFolder); // tres lazos seguidos: 0x1e29 a 0x1e2c, 0x1e2d y los dos que siguen
    list.Add(0x1c2f, NpcFolder);
    list.Add({0x225, 0xc1c, 0xa24, 0xa25, 0x231, 0xe21, 0xe22, 0x81c, 0x619}, ItemFolder);
    // 0x611c85: los ítems 0 a 16 de los grupos 0, 1, 2, 3, 5, 6, 4, 13, 12, 14 y 15, en ese orden. 0x1c12 a 0x1c14 son
    // los Helper de Player\, y 0x1c15 vuelve a pedirse con Item\.
    list.AddEach(0x412, 0x422, {-0x200, 0, 0x200, 0x400, 0x800, 0xa00, 0x600, 0x1800, 0x1600, 0x1a00, 0x1c00},
                 ItemFolder);
    list.AddRange(0x1c27, 0x1c2e, ItemFolder);
    list.AddRange(0x61a, 0x61e, ItemFolder);
    list.AddRange(0x226, 0x227, ItemFolder);
    list.Add(0xa26, ItemFolder);
    list.AddRange(0x1a23, 0x1a2a, ItemFolder); // dos lazos seguidos
    list.AddRange(0x2023, 0x2024, ItemFolder);
    list.Add(0xdb, BowFolder);
    list.AddRange(0x223e, 0x2244, ItemFolder);
    list.Add({0x224a, 0x1c30}, ItemFolder);
    list.AddRange(0x1e23, 0x1e27, ItemFolder); // dos lazos seguidos
    list.AddRange(0x223a, 0x223b, ItemFolder);
    list.Add({0x1a30, 0x1a31, 0x228, 0x229, 0x620, 0xa27, 0xc1e, 0x1e32, 0x1e33, 0x1e34}, ItemFolder);
    list.AddRange(0x1c32, 0x1c37, ItemFolder);
    return list;
}

constexpr CallList<214> PlayerCalls = MakePlayerCalls();
constexpr CallList<291> ItemCalls = MakeItemCalls();
static_assert(PlayerCalls.count == PlayerCalls.calls.size(), "las llamadas de 0x60f7d0");
static_assert(ItemCalls.count == ItemCalls.calls.size(), "las llamadas de 0x611850");

// 0x2600 y 0x2900 en las trece.
constexpr Textures::Sampling FixedSampling{Textures::Filter::Nearest, Textures::Wrap::Clamp};
constexpr std::array<Textures::TextureSetEntry, 13> FixedTextures{{
    {"Player\\skin_barbarian_01.jpg", 0x12e, FixedSampling},
    {"Player\\level_man022.jpg", 0x12f, FixedSampling},
    {"Player\\skin_wizard_01.jpg", 0x130, FixedSampling},
    {"Player\\level_man01.jpg", 0x131, FixedSampling},
    {"Player\\skin_archer_01.jpg", 0x132, FixedSampling},
    {"Player\\level_man033.jpg", 0x133, FixedSampling},
    {"Player\\skin_special_01.jpg", 0x134, FixedSampling},
    {"Player\\level_man02.jpg", 0x136, FixedSampling},
    {"Player\\hair_r.jpg", 0x137, FixedSampling},
    {"Player\\Robe01.jpg", 0x250, FixedSampling},
    {"Player\\Robe02.jpg", 0x251, FixedSampling},
    {"Player\\Robe03.tga", 0x252, FixedSampling},
    {"Player\\DarklordRobe.tga", 0x257, FixedSampling},
}};
} // namespace

std::span<const ModelTextureCall> PlayerTextureCalls()
{
    return PlayerCalls.calls;
}

std::span<const ModelTextureCall> ItemTextureCalls()
{
    return ItemCalls.calls;
}

std::span<const Textures::TextureSetEntry> PlayerFixedTextures()
{
    return FixedTextures;
}
} // namespace Client::Data::Models
