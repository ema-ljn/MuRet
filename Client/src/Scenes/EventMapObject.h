#pragma once

#include <array>
#include <cstdint>

// El objeto de evento de 0x447d70 (DAT_00723c60, operator new(0x134)): Devil Square, Blood Castle o Chaos
// Castle. Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Los objetos de Devil Square y de Blood
// Castle".
namespace Client::Scenes
{
// La clase, por la vtable que 0x447d70 (o 0x404d60) escribe en +0x00. Sus métodos los llaman el HUD
// (0x5a7320, CL8) y los paquetes (0x4e2b10, 0x4da730, 0x4da750, 0x4dac70, 0x4e7360, CL9.5).
enum class EventMapKind : std::uint8_t
{
    DevilSquare, // 0x6b8c1c
    BloodCastle, // 0x6b8c30
    ChaosCastle, // 0x6b8794, por 0x404d60
};

struct EventMapObject
{
    EventMapKind kind = EventMapKind::DevilSquare;
    // +0x04 y +0x08: ni 0x447d70 ni 0x404d60 los escriben, y operator new no los inicia (Q-C320). MuRet
    // los deja en 0.
    std::int32_t field04 = 0;
    std::int32_t field08 = 0;
    std::int32_t field0c = 0;
    std::uint8_t field10 = 0;
    // +0x14..+0x20 en −1; 0x40ba20 los vuelve a poner así, con +0x10 en 0.
    std::array<std::int32_t, 4> field14{-1, -1, -1, -1};
    std::int32_t field24 = 0;
    std::int32_t field28 = 0;
    // +0x2c..+0x133: rep stos de 0x42 dwords en 0.
    std::array<std::int32_t, 0x42> field2c{};
};
} // namespace Client::Scenes
