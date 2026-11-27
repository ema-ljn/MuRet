#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::Kernel
{
struct CharacterAppearance;
}

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct ActorState;
struct OwnCharacterRecord;
class WorldState;
} // namespace Client::Kernel

// 0x505820, a character's full +9 set, and what it leaves in [0x743a178] and [0x743a17c] for
// 0x550990 and for the characters' drawing. Contract: Client/docs/research/06-render-escenas-y-ui.md,
// "El conjunto completo de 0x505820".
namespace Client::Actors
{

// [0x743a178] and [0x743a17c], in the .bss: 0 at startup. Only 0x505820 writes them, both on every
// call; 0x550990 (0x550b1e..0x550b2a) and the characters' drawing (0x50ff2a..0x510313) read them.
struct Scan505820
{
    std::int32_t dword743a178 = 0;
    std::uint8_t byte743a17c = 0;
};

// What 0x505820 reads from a body part: the model (+0, signed) and the level (+2, & 0xf).
struct ScannedPart
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
};

inline constexpr std::size_t ScannedPartCount = 5;

// What it reads from the character: the class (+0x1d4, & 7) and the five body parts, from +0x218 to +0x298
// 0x20 apart, in the order of Kernel::BodyPart (from the helmet to the boots).
struct ScannedCharacter
{
    std::uint8_t characterClass = 0;
    std::array<ScannedPart, ScannedPartCount> body{};
};

[[nodiscard]] ScannedCharacter ScannedCharacterOf(const Kernel::ActorState& actor);
// A character of the list or the login's: what 0x517270 and 0x5139f0 leave in those parts.
[[nodiscard]] ScannedCharacter ScannedCharacterOf(const Kernel::CharacterAppearance& appearance);

// What 0x505820 leaves: what it returns in al and the two globals.
struct FullSetScan
{
    bool complete = false;
    Scan505820 globals;
};

// 0x505820(character), cdecl. With some part at 0xffff, or with one of level below 9 or of another index
// ((model − 0x212) with the signed remainder of 512) than the boots, there is no set: [0x743a178] at 0. Otherwise,
// [0x743a178] is the smallest level. Class 3 does not look at the helmet. [0x743a17c] stays at 1, except with
// class 3 and the set: it opens the own record (Models::LogicalAccess over
// `ownRecordAccesses`), sets it to 0 if the record's armor (+0xc44) is not 0x100f, 0x1014, 0x1017
// or 0x1020, and closes it, with 3,848 rand() of `random` if it leaves the count at 0.
[[nodiscard]] FullSetScan ScanFullSet(const ScannedCharacter& character, const Kernel::OwnCharacterRecord& own,
                                      std::uint8_t& ownRecordAccesses, Crt::CrtRandom& random);

// 0x50c904..0x50c926: what 0x50ba30 does with a 0x20a with +0x3e4 at 0, on each frame of the original that
// draws it, `frames` times: 0x505820 with the own record and the access count of `world`, and the
// globals in WorldRuntime::fullSetScan. Returns the last one's, which the full set of the effect queue reads
// (0x50fddb). With `frames` at 0 (a drawing between frames), the same over copies of the rand() and of the
// count, without publishing anything.
[[nodiscard]] FullSetScan ScanTheDrawnCharacter(Kernel::WorldState& world, const ScannedCharacter& character,
                                                Crt::CrtRandom& random, int frames);
} // namespace Client::Actors
