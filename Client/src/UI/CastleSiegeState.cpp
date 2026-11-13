#include "UI/CastleSiegeState.h"

#include "UI/OldWindows.h"

#include <cstddef>
#include <cstdint>

namespace Client::UI
{
namespace
{
std::size_t Offset(std::uint32_t address)
{
    return static_cast<std::size_t>(address - CastleDataBase);
}

// 0x45ad80 y su vtable + 8 (0x45a9f0): el id del contador y el tamaño. El texto (0x45b150 y 0x45b170) es una copia
// de Text.bmd que solo lee el dibujo; el padre queda en 0.
OldButton MakeTheButton(std::int32_t id, std::int32_t width, std::int32_t height)
{
    OldButton button;
    button.base.id = id;
    button.base.width = width;
    button.base.height = height;
    return button;
}
} // namespace

bool CastleData::Holds(std::uint32_t address, std::uint32_t size)
{
    return address >= CastleDataBase && address - CastleDataBase <= CastleDataSize &&
           size <= CastleDataSize - (address - CastleDataBase);
}

std::uint8_t CastleData::U8(std::uint32_t address) const
{
    return bytes.at(Offset(address));
}

std::uint32_t CastleData::U32(std::uint32_t address) const
{
    std::uint32_t value = 0;
    for (std::size_t i = 4; i-- > 0;)
        value = (value << 8u) | bytes.at(Offset(address) + i);
    return value;
}

std::int32_t CastleData::S32(std::uint32_t address) const
{
    return static_cast<std::int32_t>(U32(address));
}

std::uint64_t CastleData::U64(std::uint32_t address) const
{
    std::uint64_t value = 0;
    for (std::size_t i = 8; i-- > 0;)
        value = (value << 8u) | bytes.at(Offset(address) + i);
    return value;
}

void CastleData::SetU8(std::uint32_t address, std::uint8_t value)
{
    bytes.at(Offset(address)) = value;
}

void CastleData::SetU32(std::uint32_t address, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        bytes.at(Offset(address) + i) = static_cast<std::uint8_t>(value >> (8u * i));
}

void CastleData::SetU64(std::uint32_t address, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i)
        bytes.at(Offset(address) + i) = static_cast<std::uint8_t>(value >> (8u * i));
}

// 0x64aca0: el botón (0x32 × 0x12), las dos listas (0x46f320 y 0x4706f0, que se ubican en (0x1d2, 0x168) con
// 0x9d × 0xeb), la pestaña en 0, el estado en −1 y +0xa8..+0xbb en 0.
SiegeRegistrationWindow::SiegeRegistrationWindow() : button(MakeTheButton(RegistrationButtonId, 0x32, 0x12)) {}

// 0x661090: los siete botones de 0x31 × 0x10, +4, +0x18 y +0x3d8 en 0 y +0x3d4 en 1. También pone 0x7d5c640 y
// 0x7d5c644 en 0, que en MuRet arrancan en 0 con CastleData.
CastleSeniorWindow::CastleSeniorWindow()
{
    for (std::size_t i = 0; i < buttons.size(); ++i)
        buttons[i] = MakeTheButton(FirstSeniorButtonId + static_cast<std::int32_t>(i), 0x31, 0x10);
}

// 0x6485f0: +0x2c y +0x1c4 de 0x32 × 0x12 (los textos 1619 y 1593), +0xb4 y +0x13c de 0x10 × 0x10, y lo demás en 0.
HuntZoneWindow::HuntZoneWindow()
    : buttons{MakeTheButton(FirstHuntZoneButtonId, 0x32, 0x12), MakeTheButton(FirstHuntZoneButtonId + 1, 0x10, 0x10),
              MakeTheButton(FirstHuntZoneButtonId + 2, 0x10, 0x10),
              MakeTheButton(FirstHuntZoneButtonId + 3, 0x32, 0x12)}
{
}
} // namespace Client::UI
