#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Render::Models
{
class CrtRandom;
} // namespace Client::Render::Models

// El alta de un monstruo o un NPC por tipo: 0x5141e0(tipo, x, y, clave) y su cola 0x5140c0 (04, "Los monstruos y
// los NPC que entran"; 06, "El alta de un monstruo"). 0x5141e0 prueba primero seis creadores de evento (0x439df0,
// 0x42d070, 0x440530, 0x42f190, 0x430c90 y 0x425db0, en ese orden: el primero que devuelve un personaje gana) y
// después su switch por tipo − 1 (de 1 a 301, la tabla de bytes 0x517138 sobre la de saltos 0x516f44), con el
// default 0x516e2a. Cada caso es una lista de pasos en el orden del ASM: lo que carga, el 0x512870 con el modelo, lo
// que escribe en el personaje y lo que llama. Las tablas están leídas del listado caso por caso (*estático*).
namespace Client::Scenes
{
enum class MonsterStepKind : std::uint8_t
{
    LoadMonster, // 0x612d90(value): Monster\Monster{value + 1}.bmd en el modelo 0x152 + value
    LoadNpc,     // 0x612270(value): el modelo value de los NPC
    Create,      // 0x512870(clave, value, x, y, extra): extra son los bits del ángulo
    Write,       // [personaje + offset] = value, de size bytes (un float va con sus bits)
    Name,        // el strcpy de text a +0x1d9 (extra: la dirección del texto en la .data)
    Dress,       // 0x512930(personaje): viste con lo que dejaron +0x218..+0x2d8
    Box,         // 0x5133b0(personaje)
    Action,      // 0x4ea030(personaje, value)
    Ground,      // +0x18 = 0x5f93c0(+0x10, +0x14) + value (la altura del terreno más un float)
    Lower,       // +0x18 -= value (float)
    CopyHeight,  // [personaje + offset] = +0x18
    MeshFlag,    // el byte +0 de la malla offset del modelo del personaje (+2) = value
    ModelByte,   // el byte offset del modelo extra (0x5769ad8 + extra · 0xe0) = value
    Random,      // r = rand() % 2 (0x69fd70 con AND 0x80000001)
    Siege,       // 0x4294b0: con el asedio prendido sigue otra rama (CL9.5)
    Joint,       // 0x530860(value, posición, posición, ángulos, offset, personaje, extra, −1, 0, 0, −1) (CL11)
    Arrival,     // 0x518cf0(value, posición, ángulos, luz, offset, personaje) (CL11)
    Sound,       // 0x41ed10(value, 0, 0) (CL10)
    BoneName,    // 0x401000(personaje, text, value): el hueso con nombre del modelo (0x6e5640, CL11)
};

enum class MonsterStepWhen : std::uint8_t
{
    Always,
    RandomZero,    // con el rand() del paso Random par
    RandomOne,     // con el rand() del paso Random impar
    InWorlds,      // con el mapa de low a high
    OutsideWorlds, // con el mapa fuera de low..high
    AtEitherX,     // con la X de la celda en low o en high
};

struct MonsterStep
{
    MonsterStepKind kind = MonsterStepKind::Write;
    MonsterStepWhen when = MonsterStepWhen::Always;
    std::uint8_t size = 0;
    bool plusRandom = false; // value + r (el r del último paso Random)
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    std::uint16_t offset = 0;
    std::uint32_t value = 0;
    std::uint32_t extra = 0;
    std::uint32_t address = 0; // dónde está en el ASM
    std::string_view text{};
};

// Un tipo con su caso: el lugar del caso en el ASM y sus pasos.
struct MonsterKind
{
    std::int32_t type = 0;
    std::uint32_t address = 0;
    std::span<const MonsterStep> steps;
};

// Un tipo de un creador de evento, que lo atiende solo en `world` (−1: en cualquier mapa).
struct EventMonsterKind
{
    std::uint32_t creator = 0;
    std::int32_t world = -1;
    MonsterKind kind;
};

// Las tablas (MonsterKindTable.cpp): los tipos del switch que no van al default, el default y los de los creadores
// de evento, en el orden en que 0x5141e0 los prueba.
[[nodiscard]] std::span<const MonsterKind> MainMonsterKinds() noexcept;
[[nodiscard]] const MonsterKind& DefaultMonsterKind() noexcept;
[[nodiscard]] std::span<const EventMonsterKind> EventMonsterKinds() noexcept;

// Quién atiende un tipo en un mapa: `creator` es 0x5141e0 con el switch o la dirección del creador de evento.
struct MonsterCreation
{
    std::uint32_t creator = 0;
    MonsterKind kind;
};

[[nodiscard]] MonsterCreation MonsterCreationOf(std::int32_t type, std::int32_t world) noexcept;

// Lo que lee la resolución de los pasos: el mapa (0x6c9dd0), la X de la celda y el byte del asedio (0x7239ec,
// Q-C330).
struct MonsterCreationContext
{
    std::int32_t world = 0;
    std::int32_t x = 0;
    bool siegeActive = false;
};

// Los pasos que corren con este contexto, en orden, con los rand() sacados en su lugar: cada Random saca uno (y queda
// en la lista con su resto en value) y los pasos con plusRandom ya llevan el r sumado. Con el asedio prendido corta en
// el paso Siege (0x4294b0, CL9.5).
[[nodiscard]] Core::Expected<std::vector<MonsterStep>, std::string>
ResolveMonsterSteps(const MonsterCreation& creation, const MonsterCreationContext& context,
                    Render::Models::CrtRandom& random);

// 0x5140c0, la cola de cada alta, sin el nombre de la tabla de 0x7681eb8: en los mapas 10 a 17 los tipos 0x54 a
// 0x8f van con el nivel (+0x1d6) en 0 y la escala (+0xc) más (mapa − 9) / 3 · 0,05 (0x6b8a48); después +0x376 = el
// tipo, +8 = 0, +0x3a4 = el word de 0x57678b0 y la clase del objeto (+0x88): 4 con los tipos 201 a 259, 8 con los
// 100 a 110 y 2 con el resto.
struct MonsterTail
{
    bool clearsLevel = false;
    std::int32_t scaleSteps = 0; // (mapa − 9) / 3, el entero que multiplica a 0,05
    std::uint8_t objectClass = 2;
};

[[nodiscard]] MonsterTail MonsterTailOf(std::int32_t type, std::int32_t world) noexcept;

// 0x514103..0x51410f: FILD de los escalones, FMUL por 0,05f (0x6b8a48), FADD de la escala y FSTP a float.
[[nodiscard]] float AddTailScale(float scale, std::int32_t steps) noexcept;
} // namespace Client::Scenes
