#include "Player/OwnTimers.h"

#include "Core/Expected.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Models/LogicalAccess.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Player/OwnStatHelpers.h"
#include "Tables/ItemTable.h"

#include <cstdint>
#include <string>

namespace Client::Player
{
namespace
{
// 0x4fc57e, 0x4fc5b0 and 0x4fc606: the bit of +0x3c that 0x4fc450 turns off with each WORD at 0.
constexpr std::uint8_t Word3eBit = 0x01;
constexpr std::uint8_t Word40Bit = 0x02;
constexpr std::uint8_t Word42Bit = 0x08;

void ClearBit(std::uint8_t& bits, std::uint8_t bit)
{
    bits = static_cast<std::uint8_t>(bits & ~bit);
}
} // namespace

Core::Expected<void, std::string> StepTheOwnTimers(Kernel::WorldState& world, const Tables::ItemTable* items,
                                                   Crt::CrtRandom& random)
{
    Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    ItemSetOptionList& setOptions = world.ItemSets().list;
    // The cursor's item, with whatever is in its globals (HeldItemOf).
    const OwnEquipmentOverride held = HeldItemOf(runtime);
    // 0x4fc46a..0x4fc556: opens the own record inline; 0x4fc617..0x4fc767 closes it inline, with the
    // 3,848 rand() of 0x40b4d0 if the count stays at 0.
    const Models::LogicalAccess access(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    // 0x4fc55b..0x4fc58a: +0x3e drops if positive and, at 0, turns off bit 0x01 of +0x3c and calls 0x54fe10.
    if (own.word3e > 0)
    {
        --own.word3e;
    }
    if (own.word3e == 0)
    {
        ClearBit(own.speedAdjustBits, Word3eBit);
        if (auto updated = UpdateOwnAttackAndMagicSpeeds(own, held, runtime.ownRecordDepth, random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    // 0x4fc594..0x4fc5e0: +0x40, bit 0x02, the list's 16 marks ([0x6eed3c] + 0x937c) at 0, 0x54efe0
    // and 0x54f9b0.
    if (own.word40 > 0)
    {
        --own.word40;
    }
    if (own.word40 == 0)
    {
        ClearBit(own.speedAdjustBits, Word40Bit);
        setOptions.used = {};
        if (auto updated = UpdateOwnWords50To56(own, held, items, &setOptions); !updated.HasValue())
        {
            return updated;
        }
        if (auto updated = UpdateOwnWords5aAnd5c(own, held, items, &setOptions); !updated.HasValue())
        {
            return updated;
        }
    }
    // 0x4fc5ea..0x4fc612: +0x42, bit 0x08 and 0x54fe10 again.
    if (own.word42 > 0)
    {
        --own.word42;
    }
    if (own.word42 == 0)
    {
        ClearBit(own.speedAdjustBits, Word42Bit);
        if (auto updated = UpdateOwnAttackAndMagicSpeeds(own, held, runtime.ownRecordDepth, random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    return {};
}
} // namespace Client::Player
