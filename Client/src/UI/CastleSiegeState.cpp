#include "UI/CastleSiegeState.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::UI
{
namespace
{
std::size_t Offset(std::uint32_t address)
{
    return static_cast<std::size_t>(address - CastleDataBase);
}
} // namespace

bool CastleData::Holds(std::uint32_t address, std::uint32_t size)
{
    return address >= CastleDataBase && address - CastleDataBase <= CastleDataSize &&
           size <= CastleDataSize - (address - CastleDataBase);
}

std::uint32_t CastleData::U32(std::uint32_t address) const
{
    std::uint32_t value = 0;
    for (std::size_t i = 4; i-- > 0;)
    {
        value = (value << 8u) | bytes.at(Offset(address) + i);
    }
    return value;
}

std::uint64_t CastleData::U64(std::uint32_t address) const
{
    std::uint64_t value = 0;
    for (std::size_t i = 8; i-- > 0;)
    {
        value = (value << 8u) | bytes.at(Offset(address) + i);
    }
    return value;
}

void CastleData::SetU32(std::uint32_t address, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        bytes.at(Offset(address) + i) = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

void CastleData::SetU64(std::uint32_t address, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i)
    {
        bytes.at(Offset(address) + i) = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

void CastleData::SetBytes(std::uint32_t address, std::span<const std::uint8_t> values)
{
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        bytes.at(Offset(address) + i) = values[i];
    }
}

std::int32_t LifeLevelOf(std::uint32_t type, std::int32_t maxLife)
{
    if (type == GateType)
    {
        if (maxLife <= 500000)
        {
            return 0;
        }
        if (maxLife <= 800000)
        {
            return 1;
        }
        return maxLife == 1500000 ? 2 : 3;
    }
    if (type == StatueType)
    {
        if (maxLife <= 400000)
        {
            return 0;
        }
        if (maxLife <= 750000)
        {
            return 1;
        }
        return maxLife <= 1300000 ? 2 : 3;
    }
    return -1;
}

// SUB 2, NEG, SBB y AND: el nivel 2 da la base y cualquier otro, la base más el salto.
std::int32_t MaxLifeOfLevel(std::uint32_t type, std::int32_t level)
{
    if (type == GateType)
    {
        if (level == 0)
        {
            return 500000;
        }
        if (level == 1)
        {
            return 800000;
        }
        return level == 2 ? 1500000 : 3000000;
    }
    if (type == StatueType)
    {
        if (level == 0)
        {
            return 400000;
        }
        if (level == 1)
        {
            return 750000;
        }
        return level == 2 ? 1300000 : 2500000;
    }
    return -1;
}

// LEA [index × 8], SUB index, LEA [× 4 + base]: base + index × 0x1c, que da la vuelta en 32 bits.
std::uint32_t NpcRecordAddress(std::uint32_t type, std::uint32_t index)
{
    const std::uint32_t base = type == GateType ? GateRecordsAt : StatueRecordsAt;
    return base + index * NpcRecordBytes;
}
} // namespace Client::UI
