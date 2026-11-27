#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// What 0x612d90(n) and 0x612270(model) do with a monster or NPC model after reading it (08 P-C176): the tables, read
// from the listing case by case (*static*). FaunaModels loads (LoadMonster and LoadNpc); here are only the data and
// the stateless rules. Contract: 06, "Los modelos de los monstruos y los NPC".
namespace Client::Assets
{
// 0x612d90(n) reads Monster\Monster{n + 1}.bmd into model 0x152 + n.
inline constexpr std::uint16_t FirstMonsterModel = 0x152;

// A 0x41eac0(id, path, buffers, flags) request: the .wav to load.
struct ModelSoundFile
{
    std::int32_t id = 0;
    std::string_view path{};
    std::int32_t buffers = 2;
    std::int32_t flags = 1;
};

// What each case of the third switch of 0x612d90 does (0x613130, jump table 0x615340), in its order.
enum class MonsterModelOpKind : std::uint8_t
{
    Sound,       // 0x41eac0(value, path, 2, 1)
    SoundTable,  // 0x612c80(model, sounds…): the model's ten action sound WORDs
    Speed,       // the speed (+4) of action index: the bits of value
    HeadBone,    // +0x54 of model `model` = value
    ModelByte,   // byte index of model `model` = value
    JpegTexture, // 0x63b870(path, value, filter, wrap, 0, 1): a texture in fixed slot value
    TgaTexture,  // 0x63bd00(path, value, filter, wrap, 0, 1)
};

struct MonsterModelOp
{
    MonsterModelOpKind kind = MonsterModelOpKind::Speed;
    std::uint16_t model = 0;
    std::uint8_t index = 0;
    std::uint32_t value = 0;
    std::array<std::int16_t, 10> sounds{};
    std::string_view path{};
    std::uint32_t filter = 0;
    std::uint32_t wrap = 0;
};

// A write of an action's speed (+4): the float's bits.
struct ActionSpeedStore
{
    std::uint8_t action = 0;
    std::uint32_t bits = 0;
};

// 0x612e5a..0x612e9f: the seven speeds it sets on every monster (0.25, 0.2, 0.34, 0.33, 0.33, 0.5 and 0.55) before
// the byte of action 6 (+0x60 at 1, it does not repeat).
[[nodiscard]] std::span<const ActionSpeedStore> CommonMonsterSpeeds() noexcept;

// 0x612ea3..0x612efb: the factor of actions 0 to 5 of some monsters (1.2 for 3; 0.7 for 5 and 0x19; 0.4 for 0x25 and
// 0x2a), multiplied in the x87 and stored as float.
[[nodiscard]] std::optional<float> MonsterSpeedFactorOf(std::int32_t number) noexcept;

// The first switch (0x612f16, tables 0x6152b4 and 0x615240, n = 2 to 0x69): speeds of single actions.
[[nodiscard]] std::span<const ActionSpeedStore> EarlyMonsterSpeedsOf(std::int32_t number) noexcept;

// The second switch (0x613119, tables 0x615324 and 0x61531c): 0xe, 0x1f, 0x27 and 0x28 set +0x10 to 1, so action 1
// does not repeat (0x613120).
[[nodiscard]] bool MonsterFirstActionStops(std::int32_t number) noexcept;

// The third switch, with its address (0 if it goes to the default, which does nothing).
struct MonsterModelCase
{
    std::int32_t number = 0;
    std::uint32_t address = 0;
    std::span<const MonsterModelOp> ops{};
};
[[nodiscard]] MonsterModelCase MonsterModelCaseOf(std::int32_t number) noexcept;

// 0x612df4..0x612e4e: the texture folder of 0x60d5f0: Npc\ for 0x46 to 0x48 on maps 18 to 23 (Chaos Castle),
// Object31\ for 0x4a on map 30 and Monster\ for the rest.
[[nodiscard]] std::string_view MonsterTextureFolderOf(std::int32_t number, std::int32_t map) noexcept;

// What 0x612270(model) does in the model's case (byte table 0x612c2c over jump table 0x612b9c, with model − 0x1cb
// unsigned up to 0x3d), in its order.
enum class NpcModelOpKind : std::uint8_t
{
    Load,     // 0x60da30(model, "Data\" + folder, name, number)
    Textures, // 0x60d5f0(model, folder, GL_NEAREST, GL_REPEAT, 1)
    Sound,    // 0x41eac0(sound)
};

struct NpcModelOp
{
    NpcModelOpKind kind = NpcModelOpKind::Load;
    std::uint16_t model = 0;
    std::string_view folder{};
    std::string_view name{};
    int number = -1;
    ModelSoundFile sound{};
};

struct NpcModelCase
{
    std::uint32_t address = 0;
    std::span<const NpcModelOp> ops{};
};
// The model's case; without a case (the default 0x612b4f), no operation.
[[nodiscard]] NpcModelCase NpcModelCaseOf(std::uint16_t model) noexcept;

// 0x612b4f..0x612b8b: what follows any case: the speed of all the model's actions at 0.25 (0x3e800000) and, with
// meshes, 0x60d5f0(model, "Npc\", …) again.
inline constexpr float NpcActionSpeed = 0.25f;
inline constexpr std::string_view NpcTextureFolder = "Npc\\";
} // namespace Client::Assets
