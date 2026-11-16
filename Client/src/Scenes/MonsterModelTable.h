#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Lo que 0x612d90(n) y 0x612270(modelo) hacen con un modelo de monstruo o de NPC después de leerlo (CL9 hito 2, 08
// P-C176): las tablas, leídas del listado caso por caso (*estático*). Quien carga es FaunaModels (LoadMonster y
// LoadNpc); acá están solo los datos y las reglas sin estado. Contrato: 06, "Los modelos de los monstruos y los NPC".
namespace Client::Scenes
{
// 0x612d90(n) lee Monster\Monster{n + 1}.bmd en el modelo 0x152 + n.
inline constexpr std::uint16_t FirstMonsterModel = 0x152;

// Un pedido de 0x41eac0(id, ruta, búferes, banderas): el .wav que la CL10 carga (MuRet todavía no reproduce audio).
struct ModelSoundFile
{
    std::int32_t id = 0;
    std::string_view path{};
    std::int32_t buffers = 2;
    std::int32_t flags = 1;
};

// Lo que hace cada caso del tercer switch de 0x612d90 (0x613130, la tabla de saltos 0x615340), en su orden.
enum class MonsterModelOpKind : std::uint8_t
{
    Sound,       // 0x41eac0(value, path, 2, 1)
    SoundTable,  // 0x612c80(model, sounds…): los diez WORD de los sonidos de las acciones del modelo
    Speed,       // la velocidad (+4) de la acción index: los bits de value
    HeadBone,    // +0x54 del modelo `model` = value
    ModelByte,   // el byte index del modelo `model` = value
    JpegTexture, // 0x63b870(path, value, filter, wrap, 0, 1): una textura en el slot fijo value
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

// Una escritura de la velocidad (+4) de una acción: los bits del float.
struct ActionSpeedStore
{
    std::uint8_t action = 0;
    std::uint32_t bits = 0;
};

// 0x612e5a..0x612e9f: las siete velocidades que pone a todo monstruo (0,25; 0,2; 0,34; 0,33; 0,33; 0,5 y 0,55) antes
// del byte de la acción 6 (+0x60 en 1, no se repite).
[[nodiscard]] std::span<const ActionSpeedStore> CommonMonsterSpeeds() noexcept;

// 0x612ea3..0x612efb: el factor de las acciones 0 a 5 de algunos monstruos (1,2 el 3; 0,7 el 5 y el 0x19; 0,4 el 0x25
// y el 0x2a), multiplicado en el x87 y guardado en float.
[[nodiscard]] std::optional<float> MonsterSpeedFactorOf(std::int32_t number) noexcept;

// El primer switch (0x612f16, las tablas 0x6152b4 y 0x615240, de n = 2 a 0x69): velocidades de acciones sueltas.
[[nodiscard]] std::span<const ActionSpeedStore> EarlyMonsterSpeedsOf(std::int32_t number) noexcept;

// El segundo switch (0x613119, las tablas 0x615324 y 0x61531c): el 0xe, el 0x1f, el 0x27 y el 0x28 ponen +0x10 en 1, la
// acción 1 no se repite (0x613120).
[[nodiscard]] bool MonsterFirstActionStops(std::int32_t number) noexcept;

// El tercer switch, con su dirección (0 si va al default, que no hace nada).
struct MonsterModelCase
{
    std::int32_t number = 0;
    std::uint32_t address = 0;
    std::span<const MonsterModelOp> ops{};
};
[[nodiscard]] MonsterModelCase MonsterModelCaseOf(std::int32_t number) noexcept;

// 0x612df4..0x612e4e: la carpeta de las texturas de 0x60d5f0: Npc\ del 0x46 al 0x48 en los mapas 18 a 23 (Chaos
// Castle), Object31\ el 0x4a en el 30 y Monster\ los demás.
[[nodiscard]] std::string_view MonsterTextureFolderOf(std::int32_t number, std::int32_t map) noexcept;

// Lo que hace 0x612270(modelo) en el caso del modelo (la tabla de bytes 0x612c2c sobre la de saltos 0x612b9c, con
// modelo − 0x1cb sin signo hasta 0x3d), en su orden.
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
// El caso del modelo; sin caso (el default 0x612b4f), ninguna operación.
[[nodiscard]] NpcModelCase NpcModelCaseOf(std::uint16_t model) noexcept;

// 0x612b4f..0x612b8b: lo que sigue a cualquier caso: la velocidad de todas las acciones del modelo en 0,25
// (0x3e800000) y, con mallas, 0x60d5f0(modelo, "Npc\", …) otra vez.
inline constexpr float NpcActionSpeed = 0.25f;
inline constexpr std::string_view NpcTextureFolder = "Npc\\";
} // namespace Client::Scenes
