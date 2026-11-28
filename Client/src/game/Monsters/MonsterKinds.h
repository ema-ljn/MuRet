#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

// Creating a monster or an NPC by type: 0x5141e0(type, x, y, key) and its tail 0x5140c0 (04, "Los monstruos y los NPC
// que entran"; 06, "El alta de un monstruo"). 0x5141e0 first tries six event creators (0x439df0, 0x42d070, 0x440530,
// 0x42f190, 0x430c90 and 0x425db0, in that order; the first that returns a character wins), then its switch on type − 1
// (1 to 301, byte table 0x517138 over jump table 0x516f44) with default 0x516e2a. Each case is its steps in ASM order:
// what it loads, the 0x512870 with the model, what it writes into the character and what it calls (*static*).
namespace Client::Monsters
{
enum class MonsterStepKind : std::uint8_t
{
    LoadMonster, // 0x612d90(value): Monster\Monster{value + 1}.bmd into model 0x152 + value
    LoadNpc,     // 0x612270(value): NPC model value
    Create,      // 0x512870(key, value, x, y, extra): extra holds the angle's bits
    Write,       // [character + offset] = value, size bytes (a float goes as its bits)
    Name,        // strcpy of text to +0x1d9 (extra: the text's address in .data)
    Dress,       // 0x512930(character): dresses it with what +0x218..+0x2d8 hold
    Box,         // 0x5133b0(character)
    Action,      // 0x4ea030(character, value)
    Ground,      // +0x18 = 0x5f93c0(+0x10, +0x14) + value (the terrain height plus a float)
    Lower,       // +0x18 -= value (float)
    CopyHeight,  // [character + offset] = +0x18
    MeshFlag,    // byte +0 of mesh offset of the character's model (+2) = value
    ModelByte,   // byte offset of model extra (0x5769ad8 + extra · 0xe0) = value
    Random,      // r = rand() % 2 (0x69fd70 with AND 0x80000001)
    Siege,       // 0x4294b0: with the siege on it takes another branch (CL9.5)
    Joint,       // 0x530860(value, position, position, angles, offset, character, extra, −1, 0, 0, −1) (CL11)
    Arrival,     // 0x518cf0(value, position, angles, light, offset, character) (CL11)
    Sound,       // 0x41ed10(value, 0, 0) (CL10)
    BoneName,    // 0x401000(character, text, value): the model's named bone (0x6e5640, CL11)
};

enum class MonsterStepWhen : std::uint8_t
{
    Always,
    RandomZero,    // the Random step's rand() is even
    RandomOne,     // the Random step's rand() is odd
    InWorlds,      // the map is in low..high
    OutsideWorlds, // the map is outside low..high
    AtEitherX,     // the cell's X is low or high
    SiegeOn,       // 0x4294b0 nonzero
    SiegeOff,      // 0x4294b0 at 0
};

struct MonsterStep
{
    MonsterStepKind kind = MonsterStepKind::Write;
    MonsterStepWhen when = MonsterStepWhen::Always;
    std::uint8_t size = 0;
    bool plusRandom = false; // value + r (the r of the last Random step)
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    std::uint16_t offset = 0;
    std::uint32_t value = 0;
    std::uint32_t extra = 0;
    std::uint32_t address = 0; // where it is in the ASM
    std::string_view text{};
};

struct MonsterKind
{
    std::int32_t type = 0;
    std::uint32_t address = 0;
    std::span<const MonsterStep> steps;
};

// A type of an event creator, which handles it only in `world` (−1: any map).
struct EventMonsterKind
{
    std::uint32_t creator = 0;
    std::int32_t world = -1;
    MonsterKind kind;
};

// The tables (MonsterKindTable.cpp): the switch types that do not go to the default, the default and the event
// creators' types, in the order 0x5141e0 tries them.
[[nodiscard]] std::span<const MonsterKind> MainMonsterKinds() noexcept;
[[nodiscard]] const MonsterKind& DefaultMonsterKind() noexcept;
[[nodiscard]] std::span<const EventMonsterKind> EventMonsterKinds() noexcept;

// Who handles a type on a map: `creator` is 0x5141e0 with the switch or the event creator's address.
struct MonsterCreation
{
    std::uint32_t creator = 0;
    MonsterKind kind;
};

[[nodiscard]] MonsterCreation MonsterCreationOf(std::int32_t type, std::int32_t world) noexcept;

// What step resolution reads: the map (0x6c9dd0), the cell's X and the siege byte (0x7239ec, Q-C330).
struct MonsterCreationContext
{
    std::int32_t world = 0;
    std::int32_t x = 0;
    bool siegeActive = false;
};

// Step-by-step resolution, to draw each rand() in its place among the creation's (0x511290 and its cells): the r of the
// last Random and the context. `Next` returns the step as it runs (the Random with its remainder in value, plusRandom
// with r added) or nullopt if its condition fails; it selects the siege-state branches.
class MonsterStepResolver
{
public:
    explicit MonsterStepResolver(const MonsterCreationContext& context) noexcept : m_context(context) {}

    [[nodiscard]] Core::Expected<std::optional<MonsterStep>, std::string> Next(MonsterStep step, std::int32_t type,
                                                                               Crt::CrtRandom& random);

private:
    MonsterCreationContext m_context;
    std::int32_t m_r = 0;
};

// The steps that run with this context, in order, with each rand() drawn in its place: each Random draws one (and stays
// in the list with its remainder in value) and the plusRandom steps already carry r. With the siege on it cuts at the
// Siege step (0x4294b0, CL9.5).
[[nodiscard]] Core::Expected<std::vector<MonsterStep>, std::string>
ResolveMonsterSteps(const MonsterCreation& creation, const MonsterCreationContext& context, Crt::CrtRandom& random);

// 0x5140c0, every creation's tail (MonsterCreation copies the name from the 0x7681eb8 table): on maps 10 to 17, types
// 0x54 to 0x8f get level (+0x1d6) 0 and scale (+0xc) plus (map − 9) / 3 · 0.05 (0x6b8a48); then +0x376 = type, +8 = 0,
// +0x3a4 = the word at 0x57678b0 and the object class (+0x88): 4 for types 201 to 259, 8 for 100 to 110 and 2 for the
// rest.
struct MonsterTail
{
    bool clearsLevel = false;
    std::int32_t scaleSteps = 0; // (map − 9) / 3, the integer that multiplies 0.05
    std::uint8_t objectClass = 2;
};

[[nodiscard]] MonsterTail MonsterTailOf(std::int32_t type, std::int32_t world) noexcept;

// 0x514103..0x51410f: FILD of the steps, FMUL by 0.05f (0x6b8a48), FADD of the scale and FSTP to float.
[[nodiscard]] float AddTailScale(float scale, std::int32_t steps) noexcept;
} // namespace Client::Monsters
